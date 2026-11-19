/***************************************************************************
 * Optional CUDA backend for System.ML.Cnn: layers, kernels and the C API
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

// The GPU twin of the CPU engine in ../cnn.h. The same layers (Conv2D, MaxPool2D, ReLU,
// Flatten, Dense, Dropout), the same binary cross-entropy on logits, the same Adam and
// SGD, on contiguous NCHW float32 tensors that stay on the device between layers: only the
// batch goes in, and the loss and the logits come out.
//
//   * convolution: im2col kernel + cublasSgemmStridedBatched (one GEMM per sample, launched
//     as a batch), col2im as a gather (no atomics); the weight gradient is a batched GEMM into
//     per-sample partials that a kernel then adds in a fixed order;
//   * dense: cublasSgemm;
//   * max pooling: the first maximum of a window wins, as on the CPU; the backward pass is a
//     gather, so no atomics and the result does not depend on thread timing;
//   * dropout: the mask is the CPU engine's counter-based one (splitmix64 of seed, step and
//     element index, compared in double), so a seed gives the same mask on both devices;
//   * everything is FP32. cuBLAS math mode is the default (no Tensor Cores) unless the model
//     asked for TF32; the kernels are compiled without --use_fast_math.
// There is no cuDNN. Nothing here uses atomics on floats, so a run repeats bit for bit on the
// same GPU and driver.

#include "objk_ml_cuda.h"

#include <cuda_runtime.h>
#include <cublas_v2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

typedef unsigned long long u64;

thread_local std::string g_error;

struct Shape {
  int c, h, w;
  Shape() : c(0), h(0), w(0) {}
  Shape(int c_, int h_, int w_) : c(c_), h(h_), w(w_) {}
  size_t Size() const { return (size_t)c * h * w; }
};

__host__ __device__ static inline u64 Mix64(u64 x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

#define GRID_LOOP(i, total) \
  for(size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < (size_t)(total); i += (size_t)gridDim.x * blockDim.x)

static int Blocks(size_t total) {
  size_t b = (total + 255) / 256;
  if(b > 1048576) {
    b = 1048576;
  }
  return b < 1 ? 1 : (int)b;
}

//
// kernels
//
__global__ void k_im2col(const float* x, float* col, int n, int C, int H, int W, int K, int S, int padt, int padl, int OH, int OW) {
  const size_t P = (size_t)OH * OW, CKK = (size_t)C * K * K;
  GRID_LOOP(idx, (size_t)n * CKK * P) {
    const size_t p = idx % P, r = idx / P;
    const size_t ckk = r % CKK, i = r / CKK;
    const int kw = (int)(ckk % K), kh = (int)((ckk / K) % K), c = (int)(ckk / ((size_t)K * K));
    const int oh = (int)(p / OW), ow = (int)(p % OW);
    const int ih = oh * S - padt + kh, iw = ow * S - padl + kw;
    float v = 0.0f;
    if(ih >= 0 && ih < H && iw >= 0 && iw < W) {
      v = x[((i * C + c) * H + ih) * W + iw];
    }
    col[idx] = v;
  }
}

// the image gradient from the column gradient, one thread per image pixel
__global__ void k_col2im(const float* gcol, float* gx, int n, int C, int H, int W, int K, int S, int padt, int padl, int OH, int OW) {
  const size_t P = (size_t)OH * OW, CKK = (size_t)C * K * K;
  GRID_LOOP(idx, (size_t)n * C * H * W) {
    const int iw = (int)(idx % W), ih = (int)((idx / W) % H);
    const int c = (int)((idx / ((size_t)W * H)) % C);
    const size_t i = idx / ((size_t)W * H * C);
    float s = 0.0f;
    for(int kh = 0; kh < K; ++kh) {
      const int t = ih + padt - kh;
      if(t < 0 || t % S != 0) {
        continue;
      }
      const int oh = t / S;
      if(oh >= OH) {
        continue;
      }
      for(int kw = 0; kw < K; ++kw) {
        const int u = iw + padl - kw;
        if(u < 0 || u % S != 0) {
          continue;
        }
        const int ow = u / S;
        if(ow >= OW) {
          continue;
        }
        s += gcol[i * CKK * P + ((size_t)(c * K + kh) * K + kw) * P + (size_t)oh * OW + ow];
      }
    }
    gx[idx] = s;
  }
}

// out[i][f][p] += b[f]
__global__ void k_add_bias(float* out, const float* b, int F, int P, size_t total) {
  GRID_LOOP(idx, total) {
    out[idx] += b[(idx / P) % F];
  }
}

// gb[f] = sum over the batch and the positions of gout[i][f][p], in two steps with a fixed order, so that a
// layer with few filters still keeps the whole GPU busy: blocks (b, f) each add one slice of the n * P values
// of filter f, then one thread per filter adds the slices
#define BIAS_SLICES 64

__global__ void k_bias_partial(const float* go, float* part, int F, int P, int n) {
  __shared__ float sh[256];
  const int f = blockIdx.y, b = blockIdx.x;
  const size_t total = (size_t)n * P;
  const size_t per = (total + gridDim.x - 1) / gridDim.x;
  const size_t lo = (size_t)b * per, hi = lo + per < total ? lo + per : total;
  float s = 0.0f;
  for(size_t j = lo + threadIdx.x; j < hi; j += blockDim.x) {
    const size_t i = j / P, p = j % P;
    s += go[(i * F + f) * P + p];
  }
  sh[threadIdx.x] = s;
  __syncthreads();
  for(int stride = 128; stride > 0; stride >>= 1) {
    if((int)threadIdx.x < stride) {
      sh[threadIdx.x] += sh[threadIdx.x + stride];
    }
    __syncthreads();
  }
  if(threadIdx.x == 0) {
    part[(size_t)f * gridDim.x + b] = sh[0];
  }
}

__global__ void k_bias_final(const float* part, float* gb, int F, int slices) {
  const int f = blockIdx.x * blockDim.x + threadIdx.x;
  if(f < F) {
    float s = 0.0f;
    for(int b = 0; b < slices; ++b) {
      s += part[(size_t)f * slices + b];
    }
    gb[f] = s;
  }
}

// gw[j] (+)= sum over the chunk's samples of partial[i][j]
__global__ void k_sum_partials(const float* partial, float* gw, size_t count, int chunk, int accumulate) {
  GRID_LOOP(j, count) {
    float s = accumulate ? gw[j] : 0.0f;
    for(int i = 0; i < chunk; ++i) {
      s += partial[(size_t)i * count + j];
    }
    gw[j] = s;
  }
}

__global__ void k_maxpool_fwd(const float* x, float* out, int* ind, int n, int C, int H, int W, int size, int S, int OH, int OW) {
  GRID_LOOP(idx, (size_t)n * C * OH * OW) {
    const int ow = (int)(idx % OW), oh = (int)((idx / OW) % OH);
    const size_t plane = idx / ((size_t)OW * OH);  // i * C + c
    const float* pl = x + plane * H * W;
    int best = (oh * S) * W + ow * S;
    float bv = pl[best];
    for(int kh = 0; kh < size; ++kh) {
      for(int kw = 0; kw < size; ++kw) {
        const int at = (oh * S + kh) * W + ow * S + kw;
        if(pl[at] > bv) {
          bv = pl[at];
          best = at;
        }
      }
    }
    out[idx] = bv;
    ind[idx] = best;
  }
}

// gather: an input pixel collects the gradient of every window in which it was the first maximum
__global__ void k_maxpool_bwd(const float* go, const int* ind, float* gx, int n, int C, int H, int W, int size, int S, int OH, int OW) {
  GRID_LOOP(idx, (size_t)n * C * H * W) {
    const int iw = (int)(idx % W), ih = (int)((idx / W) % H);
    const size_t plane = idx / ((size_t)W * H);
    const int here = ih * W + iw;
    const int lo_h = ih - size + 1 <= 0 ? 0 : (ih - size + 1 + S - 1) / S, hi_h = min(ih / S, OH - 1);
    const int lo_w = iw - size + 1 <= 0 ? 0 : (iw - size + 1 + S - 1) / S, hi_w = min(iw / S, OW - 1);
    float s = 0.0f;
    for(int oh = lo_h; oh <= hi_h; ++oh) {
      for(int ow = lo_w; ow <= hi_w; ++ow) {
        const size_t at = (plane * OH + oh) * OW + ow;
        if(ind[at] == here) {
          s += go[at];
        }
      }
    }
    gx[idx] = s;
  }
}

__global__ void k_relu_fwd(const float* x, float* y, size_t total) {
  GRID_LOOP(j, total) {
    y[j] = x[j] > 0.0f ? x[j] : 0.0f;
  }
}

__global__ void k_relu_bwd(const float* x, const float* go, float* gi, size_t total) {
  GRID_LOOP(j, total) {
    gi[j] = x[j] > 0.0f ? go[j] : 0.0f;
  }
}

__device__ static inline float DropMask(u64 key, size_t j, double rate, float scale) {
  const double u = (double)(Mix64(key + (u64)j) >> 11) * (1.0 / 9007199254740992.0);
  return u >= rate ? scale : 0.0f;
}

__global__ void k_dropout_fwd(const float* x, float* y, size_t total, u64 key, double rate, float scale) {
  GRID_LOOP(j, total) {
    y[j] = x[j] * DropMask(key, j, rate, scale);
  }
}

__global__ void k_dropout_bwd(const float* go, float* gi, size_t total, u64 key, double rate, float scale) {
  GRID_LOOP(j, total) {
    gi[j] = go[j] * DropMask(key, j, rate, scale);
  }
}

// the stable BCE on logits, in double like the CPU engine: per-sample loss, hit flag and dloss/dlogit
__global__ void k_loss(const float* logits, const float* y, double* loss, int* hit, float* grad, int n, int want_grad) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if(i >= n) {
    return;
  }
  const double z = (double)logits[i], t = (double)y[i];
  loss[i] = fmax(z, 0.0) - z * t + log1p(exp(-fabs(z)));
  hit[i] = ((z > 0.0) == (t > 0.5)) ? 1 : 0;
  if(want_grad) {
    double sig;
    if(z >= 0.0) {
      sig = 1.0 / (1.0 + exp(-z));
    } else {
      const double e = exp(z);
      sig = e / (1.0 + e);
    }
    grad[i] = (float)((sig - t) / n);
  }
}

__global__ void k_adam(float* w, const float* g, float* m, float* v, size_t total, float b1, float b2, float ob1, float ob2,
                       float step_size, float bc2_sqrt, float eps) {
  GRID_LOOP(j, total) {
    const float gj = g[j];
    const float mj = b1 * m[j] + ob1 * gj;
    const float vj = b2 * v[j] + ob2 * gj * gj;
    m[j] = mj;
    v[j] = vj;
    const float denom = sqrtf(vj) / bc2_sqrt + eps;
    w[j] -= step_size * mj / denom;
  }
}

__global__ void k_sgd_momentum(float* w, const float* g, float* m, size_t total, float mu, float lr) {
  GRID_LOOP(j, total) {
    const float mj = mu * m[j] + g[j];
    m[j] = mj;
    w[j] -= lr * mj;
  }
}

__global__ void k_sgd(float* w, const float* g, size_t total, float lr) {
  GRID_LOOP(j, total) {
    w[j] -= lr * g[j];
  }
}

//
// the network
//
struct Layer {
  int kind;
  Shape in, out;
  // conv / dense (F: filters or units)
  int F, K, S, padt, padl, OH, OW;
  // pool
  int size;
  // dropout
  double rate;
  u64 seed, step, key;
  bool active;
  // parameters: offsets into the parameter buffer
  size_t w_off, w_size, b_off, b_size;
  Layer() : kind(0), F(0), K(0), S(0), padt(0), padl(0), OH(0), OW(0), size(0), rate(0), seed(0), step(0), key(0), active(false),
            w_off(0), w_size(0), b_off(0), b_size(0) {}
};

#ifdef OBJK_CUDA_PROFILE
// development aid, off in the normal build: -DOBJK_CUDA_PROFILE times every layer's forward and backward with CUDA events and
// prints the totals when the model is destroyed
#define PROF_BEGIN(slot) cudaEventRecord(prof_ev[(slot) * 2])
#define PROF_END(slot) cudaEventRecord(prof_ev[(slot) * 2 + 1])
#else
#define PROF_BEGIN(slot)
#define PROF_END(slot)
#endif

static const size_t PARTIAL_CAP = 64u * 1024u * 1024u;  // floats (256 MB) of per-sample weight-gradient partials per chunk

struct Net {
  int dev;
  cublasHandle_t blas;
  bool blas_ok;
  Shape input;
  std::vector<Layer> layers;
  std::string err;
  bool compiled;
  int opt;
  double lr, beta1, beta2, eps, momentum;
  u64 steps;
  size_t total;
  float *params, *grads, *m, *v;
  // per batch
  int cap_n, last_n;
  std::vector<float*> acts, gacts;
  std::vector<int*> inds;
  float *d_x, *d_y, *d_ginput, *scratch, *partial, *bias_part;
  double* d_loss;
  int* d_hit;
  std::vector<std::pair<void*, size_t>> batch_allocs, fixed_allocs;
  bool want_input_grad;
  size_t device_bytes;
#ifdef OBJK_CUDA_PROFILE
  cudaEvent_t prof_ev[256];
  std::vector<double> prof_ms;
  int prof_runs;
#endif

  Net() : dev(0), blas(nullptr), blas_ok(false), compiled(false), opt(1), lr(1e-3), beta1(0.9), beta2(0.999), eps(1e-8), momentum(0.0),
          steps(0), total(0), params(nullptr), grads(nullptr), m(nullptr), v(nullptr), cap_n(0), last_n(0), d_x(nullptr), d_y(nullptr),
          d_ginput(nullptr), scratch(nullptr), partial(nullptr), bias_part(nullptr), d_loss(nullptr), d_hit(nullptr), want_input_grad(false), device_bytes(0) {}

  Shape Current() const { return layers.empty() ? input : layers.back().out; }

  bool Fail(const std::string& what) {
    err = what;
    return false;
  }

  bool Cuda(cudaError_t e, const char* what) {
    if(e == cudaSuccess) {
      return true;
    }
    cudaGetLastError();  // clear a non-sticky error (out of memory) so the next call can succeed
    err = std::string(what) + ": " + cudaGetErrorString(e);
    return false;
  }

  bool Blas(cublasStatus_t s, const char* what) {
    if(s == CUBLAS_STATUS_SUCCESS) {
      return true;
    }
    err = std::string(what) + ": cuBLAS status " + std::to_string((int)s);
    return false;
  }

  bool Alloc(float** p, size_t floats, bool fixed, const char* what) {
    return AllocBytes((void**)p, floats * sizeof(float), fixed, what);
  }

  bool AllocBytes(void** p, size_t bytes, bool fixed, const char* what) {
    *p = nullptr;
    if(bytes == 0) {
      bytes = 4;
    }
    void* mem = nullptr;
    const cudaError_t e = cudaMalloc(&mem, bytes);
    if(e != cudaSuccess) {
      cudaGetLastError();
      char buf[200];
      std::snprintf(buf, sizeof(buf), "out of GPU memory allocating %.1f MB for %s", bytes / 1048576.0, what);
      err = buf;
      return false;
    }
    (fixed ? fixed_allocs : batch_allocs).push_back(std::make_pair(mem, bytes));
    device_bytes += bytes;
    *p = mem;
    return true;
  }

  void FreeList(std::vector<std::pair<void*, size_t> >& list) {
    for(size_t i = 0; i < list.size(); ++i) {
      cudaFree(list[i].first);
      device_bytes -= list[i].second;
    }
    list.clear();
  }

  void FreeBatch() {
    FreeList(batch_allocs);
    acts.clear();
    gacts.clear();
    inds.clear();
    d_x = d_y = d_ginput = scratch = partial = bias_part = nullptr;
    d_loss = nullptr;
    d_hit = nullptr;
    cap_n = 0;
  }

  void Destroy() {
    cudaSetDevice(dev);
#ifdef OBJK_CUDA_PROFILE
    for(size_t i = 0; i < layers.size(); ++i) {
      std::printf("  layer %2zu kind %d fwd %8.3f ms  bwd %8.3f ms (per run, %d runs)\n", i, layers[i].kind, prof_ms[i] / std::max(prof_runs, 1), prof_ms[64 + i] / std::max(prof_runs, 1), prof_runs);
    }
#endif
    FreeBatch();
    FreeList(fixed_allocs);
    if(blas_ok) {
      cublasDestroy(blas);
      blas_ok = false;
    }
  }

  bool Create(int device, int c, int h, int w, int tf32) {
    dev = device;
    input = Shape(c, h, w);
    if(!Cuda(cudaSetDevice(dev), "cudaSetDevice")) {
      return false;
    }
    if(!Blas(cublasCreate(&blas), "cublasCreate")) {
      return false;
    }
    blas_ok = true;
#ifdef OBJK_CUDA_PROFILE
    for(int e = 0; e < 256; ++e) {
      cudaEventCreate(&prof_ev[e]);
    }
    prof_ms.assign(256, 0.0);
    prof_runs = 0;
#endif
    return Blas(cublasSetMathMode(blas, tf32 ? CUBLAS_TF32_TENSOR_OP_MATH : CUBLAS_DEFAULT_MATH), "cublasSetMathMode");
  }

  bool Add(int kind, int a, int b, int c, int d, double rate, u64 seed) {
    if(compiled) {
      return Fail("the model is already compiled; add layers before Compile");
    }
    Layer l;
    l.kind = kind;
    l.in = Current();
    const Shape in = l.in;
    switch(kind) {
    case 0: {
      l.F = a; l.K = b; l.S = c;
      if(l.F < 1 || l.K < 1 || l.S < 1) {
        return Fail("Conv2D needs filters, kernel and stride of at least 1");
      }
      if(d) {
        l.OH = (in.h + l.S - 1) / l.S;
        l.OW = (in.w + l.S - 1) / l.S;
        const int th = std::max((l.OH - 1) * l.S + l.K - in.h, 0), tw = std::max((l.OW - 1) * l.S + l.K - in.w, 0);
        l.padt = th / 2;
        l.padl = tw / 2;
      } else {
        if(in.h < l.K || in.w < l.K) {
          return Fail("Conv2D kernel does not fit the input");
        }
        l.OH = (in.h - l.K) / l.S + 1;
        l.OW = (in.w - l.K) / l.S + 1;
      }
      l.out = Shape(l.F, l.OH, l.OW);
      l.w_size = (size_t)l.F * in.c * l.K * l.K;
      l.b_size = (size_t)l.F;
      break;
    }
    case 1:
      l.size = a;
      l.S = b > 0 ? b : a;
      if(l.size < 1 || l.S < 1 || in.h < l.size || in.w < l.size) {
        return Fail("MaxPool2D window does not fit the input");
      }
      l.OH = (in.h - l.size) / l.S + 1;
      l.OW = (in.w - l.size) / l.S + 1;
      l.out = Shape(in.c, l.OH, l.OW);
      break;
    case 2:
      l.out = in;
      break;
    case 3:
      l.out = Shape((int)in.Size(), 1, 1);
      break;
    case 4:
      if(a < 1) {
        return Fail("Dense needs at least 1 unit");
      }
      l.F = a;
      l.out = Shape(a, 1, 1);
      l.w_size = (size_t)a * in.Size();
      l.b_size = (size_t)a;
      break;
    case 5:
      if(!(rate >= 0.0 && rate < 1.0)) {
        return Fail("Dropout rate must be in [0, 1)");
      }
      l.rate = rate;
      l.seed = seed;
      l.out = in;
      break;
    default:
      return Fail("unknown layer kind");
    }
    layers.push_back(l);
    return true;
  }

  bool Compile(int optimizer, double lr_, double b1, double b2, double eps_, double mom) {
    if(compiled) {
      return Fail("the model is already compiled");
    }
    if(layers.empty() || Current().Size() != 1) {
      return Fail("the last layer must produce one logit per sample");
    }
    opt = optimizer; lr = lr_; beta1 = b1; beta2 = b2; eps = eps_; momentum = mom;
    if(!Cuda(cudaSetDevice(dev), "cudaSetDevice")) {
      return false;
    }
    total = 0;
    for(size_t i = 0; i < layers.size(); ++i) {
      Layer& l = layers[i];
      l.w_off = total;
      total += l.w_size;
      l.b_off = total;
      total += l.b_size;
    }
    const bool need_m = opt == 1 || momentum > 0.0, need_v = opt == 1;
    if(!Alloc(&params, total, true, "the parameters") || !Alloc(&grads, total, true, "the gradients") ||
       (need_m && !Alloc(&m, total, true, "the optimizer state")) || (need_v && !Alloc(&v, total, true, "the optimizer state"))) {
      FreeList(fixed_allocs);
      params = grads = m = v = nullptr;
      return false;
    }
    if(!Cuda(cudaMemset(params, 0, total * 4), "cudaMemset") || !Cuda(cudaMemset(grads, 0, total * 4), "cudaMemset") ||
       (m && !Cuda(cudaMemset(m, 0, total * 4), "cudaMemset")) || (v && !Cuda(cudaMemset(v, 0, total * 4), "cudaMemset"))) {
      return false;
    }
    steps = 0;
    compiled = true;
    return true;
  }

  // sizes the per-batch buffers for n samples, reusing them when they are already large enough
  bool EnsureBatch(int n) {
    if(n <= cap_n) {
      return true;
    }
    FreeBatch();
    const size_t un = (size_t)n;
    size_t need = (un * input.Size() + un) * 4 + un * 12;
    size_t scratch_need = 0, partial_need = 0;
    for(size_t i = 0; i < layers.size(); ++i) {
      const Layer& l = layers[i];
      need += 2 * un * l.out.Size() * 4;
      if(l.kind == 1) {
        need += un * l.out.Size() * 4;
      }
      if(l.kind == 0) {
        const size_t ckk = (size_t)l.in.c * l.K * l.K, p = (size_t)l.OH * l.OW;
        scratch_need = std::max(scratch_need, un * ckk * p);
        const size_t per = (size_t)l.F * ckk;
        const size_t chunk = std::max<size_t>(1, std::min<size_t>(un, PARTIAL_CAP / per));
        partial_need = std::max(partial_need, chunk * per);
      }
    }
    size_t bias_need = 1;
    for(size_t i = 0; i < layers.size(); ++i) {
      if(layers[i].kind == 0 || layers[i].kind == 4) {
        bias_need = std::max(bias_need, (size_t)layers[i].F * BIAS_SLICES);
      }
    }
    need += (scratch_need + partial_need + bias_need) * 4;
    size_t free_b = 0, total_b = 0;
    if(!Cuda(cudaMemGetInfo(&free_b, &total_b), "cudaMemGetInfo")) {
      return false;
    }
    if(need > free_b) {
      char buf[240];
      std::snprintf(buf, sizeof(buf), "out of GPU memory: a batch of %d needs about %.0f MB and %.0f MB are free; use a smaller batch", n,
                    need / 1048576.0, free_b / 1048576.0);
      return Fail(buf);
    }
    bool ok = Alloc(&d_x, un * input.Size(), false, "the batch") && Alloc(&d_y, un, false, "the labels") &&
              AllocBytes((void**)&d_loss, un * 8, false, "the loss") && AllocBytes((void**)&d_hit, un * 4, false, "the hits");
    acts.assign(layers.size(), nullptr);
    gacts.assign(layers.size(), nullptr);
    inds.assign(layers.size(), nullptr);
    for(size_t i = 0; ok && i < layers.size(); ++i) {
      ok = Alloc(&acts[i], un * layers[i].out.Size(), false, "an activation") && Alloc(&gacts[i], un * layers[i].out.Size(), false, "a gradient");
      if(ok && layers[i].kind == 1) {
        ok = AllocBytes((void**)&inds[i], un * layers[i].out.Size() * 4, false, "the pooling indexes");
      }
    }
    ok = ok && Alloc(&scratch, scratch_need, false, "the im2col buffer") && Alloc(&partial, partial_need, false, "the gradient partials") &&
         Alloc(&bias_part, bias_need, false, "the bias gradient partials");
    if(!ok) {
      FreeBatch();
      return false;
    }
    cap_n = n;
    return true;
  }

  bool EnsureInputGrad() {
    if(d_ginput) {
      return true;
    }
    return Alloc(&d_ginput, (size_t)cap_n * input.Size(), false, "the input gradient");
  }

  void BiasGrad(const float* go, float* gb, int F, int P, int n) {
    const size_t total = (size_t)n * P;
    int slices = (int)std::min<size_t>(BIAS_SLICES, std::max<size_t>(1, total / 1024));
    k_bias_partial<<<dim3(slices, F), 256>>>(go, bias_part, F, P, n);
    k_bias_final<<<(F + 127) / 128, 128>>>(bias_part, gb, F, slices);
  }

  bool Forward(int n, bool training) {
    for(size_t i = 0; i < layers.size(); ++i) {
      Layer& l = layers[i];
      const float* in = i == 0 ? d_x : acts[i - 1];
      float* out = acts[i];
      const size_t out_total = (size_t)n * l.out.Size();
      PROF_BEGIN(i);
      switch(l.kind) {
      case 0: {
        const int P = l.OH * l.OW, CKK = l.in.c * l.K * l.K;
        k_im2col<<<Blocks((size_t)n * CKK * P), 256>>>(in, scratch, n, l.in.c, l.in.h, l.in.w, l.K, l.S, l.padt, l.padl, l.OH, l.OW);
        const float one = 1.0f, zero = 0.0f;
        if(!Blas(cublasSgemmStridedBatched(blas, CUBLAS_OP_N, CUBLAS_OP_N, P, l.F, CKK, &one, scratch, P, (long long)CKK * P,
                                           params + l.w_off, CKK, 0, &zero, out, P, (long long)l.F * P, n), "conv forward")) {
          return false;
        }
        k_add_bias<<<Blocks(out_total), 256>>>(out, params + l.b_off, l.F, P, out_total);
        break;
      }
      case 1:
        k_maxpool_fwd<<<Blocks(out_total), 256>>>(in, out, inds[i], n, l.in.c, l.in.h, l.in.w, l.size, l.S, l.OH, l.OW);
        break;
      case 2:
        k_relu_fwd<<<Blocks(out_total), 256>>>(in, out, out_total);
        break;
      case 3:
        if(!Cuda(cudaMemcpyAsync(out, in, out_total * 4, cudaMemcpyDeviceToDevice), "flatten")) {
          return false;
        }
        break;
      case 4: {
        const int U = l.F, I = (int)l.in.Size();
        const float one = 1.0f, zero = 0.0f;
        if(!Blas(cublasSgemm(blas, CUBLAS_OP_T, CUBLAS_OP_N, U, n, I, &one, params + l.w_off, I, in, I, &zero, out, U), "dense forward")) {
          return false;
        }
        k_add_bias<<<Blocks(out_total), 256>>>(out, params + l.b_off, U, 1, out_total);
        break;
      }
      case 5:
        l.active = training && l.rate != 0.0;
        if(l.active) {
          l.key = Mix64(l.seed ^ Mix64(l.step + 0x51ED270B));
          l.step++;
          k_dropout_fwd<<<Blocks(out_total), 256>>>(in, out, out_total, l.key, l.rate, (float)(1.0 / (1.0 - l.rate)));
        } else if(!Cuda(cudaMemcpyAsync(out, in, out_total * 4, cudaMemcpyDeviceToDevice), "dropout")) {
          return false;
        }
        break;
      }
      PROF_END(i);
    }
    return true;
  }

  bool Backward(int n) {
    for(int i = (int)layers.size() - 1; i >= 0; --i) {
      Layer& l = layers[(size_t)i];
      const float* in = i == 0 ? d_x : acts[(size_t)i - 1];
      const float* go = gacts[(size_t)i];
      float* gin = i > 0 ? gacts[(size_t)i - 1] : (want_input_grad ? d_ginput : nullptr);
      const size_t in_total = (size_t)n * l.in.Size(), out_total = (size_t)n * l.out.Size();
      PROF_BEGIN(64 + i);
      switch(l.kind) {
      case 0: {
        const int P = l.OH * l.OW, CKK = l.in.c * l.K * l.K;
        k_im2col<<<Blocks((size_t)n * CKK * P), 256>>>(in, scratch, n, l.in.c, l.in.h, l.in.w, l.K, l.S, l.padt, l.padl, l.OH, l.OW);
        const float one = 1.0f, zero = 0.0f;
        const size_t per = (size_t)l.F * CKK;
        const int chunk_max = (int)std::max<size_t>(1, std::min<size_t>((size_t)n, PARTIAL_CAP / per));
        for(int s0 = 0; s0 < n; s0 += chunk_max) {
          const int cb = std::min(chunk_max, n - s0);
          // partial[i] (F x CKK) = gout[i] (F x P) * col[i]^T
          if(!Blas(cublasSgemmStridedBatched(blas, CUBLAS_OP_T, CUBLAS_OP_N, CKK, l.F, P, &one, scratch + (size_t)s0 * CKK * P, P,
                                             (long long)CKK * P, go + (size_t)s0 * l.F * P, P, (long long)l.F * P, &zero, partial, CKK,
                                             (long long)per, cb), "conv weight gradient")) {
            return false;
          }
          k_sum_partials<<<Blocks(per), 256>>>(partial, grads + l.w_off, per, cb, s0 > 0 ? 1 : 0);
        }
        BiasGrad(go, grads + l.b_off, l.F, P, n);
        if(gin) {
          // gcol[i] (CKK x P) = W^T * gout[i], reusing the column buffer, then gathered back into the image
          if(!Blas(cublasSgemmStridedBatched(blas, CUBLAS_OP_N, CUBLAS_OP_T, P, CKK, l.F, &one, go, P, (long long)l.F * P,
                                             params + l.w_off, CKK, 0, &zero, scratch, P, (long long)CKK * P, n), "conv input gradient")) {
            return false;
          }
          k_col2im<<<Blocks(in_total), 256>>>(scratch, gin, n, l.in.c, l.in.h, l.in.w, l.K, l.S, l.padt, l.padl, l.OH, l.OW);
        }
        break;
      }
      case 1:
        if(gin) {
          k_maxpool_bwd<<<Blocks(in_total), 256>>>(go, inds[(size_t)i], gin, n, l.in.c, l.in.h, l.in.w, l.size, l.S, l.OH, l.OW);
        }
        break;
      case 2:
        if(gin) {
          k_relu_bwd<<<Blocks(in_total), 256>>>(in, go, gin, in_total);
        }
        break;
      case 3:
        if(gin && !Cuda(cudaMemcpyAsync(gin, go, in_total * 4, cudaMemcpyDeviceToDevice), "flatten backward")) {
          return false;
        }
        break;
      case 4: {
        const int U = l.F, I = (int)l.in.Size();
        const float one = 1.0f, zero = 0.0f;
        // gW (U x I) = gout^T * x
        if(!Blas(cublasSgemm(blas, CUBLAS_OP_N, CUBLAS_OP_T, I, U, n, &one, in, I, go, U, &zero, grads + l.w_off, I), "dense weight gradient")) {
          return false;
        }
        BiasGrad(go, grads + l.b_off, U, 1, n);
        if(gin && !Blas(cublasSgemm(blas, CUBLAS_OP_N, CUBLAS_OP_N, I, n, U, &one, params + l.w_off, I, go, U, &zero, gin, I), "dense input gradient")) {
          return false;
        }
        break;
      }
      case 5:
        if(gin) {
          if(l.active) {
            k_dropout_bwd<<<Blocks(out_total), 256>>>(go, gin, out_total, l.key, l.rate, (float)(1.0 / (1.0 - l.rate)));
          } else if(!Cuda(cudaMemcpyAsync(gin, go, out_total * 4, cudaMemcpyDeviceToDevice), "dropout backward")) {
            return false;
          }
        }
        break;
      }
      PROF_END(64 + i);
    }
    return true;
  }

  void Step() {
    ++steps;
    if(opt == 1) {
      const double b1c = 1.0 - std::pow(beta1, (double)steps), b2c = 1.0 - std::pow(beta2, (double)steps);
      k_adam<<<Blocks(total), 256>>>(params, grads, m, v, total, (float)beta1, (float)beta2, (float)(1.0 - beta1), (float)(1.0 - beta2),
                                     (float)(lr / b1c), (float)std::sqrt(b2c), (float)eps);
    } else if(momentum > 0.0) {
      k_sgd_momentum<<<Blocks(total), 256>>>(params, grads, m, total, (float)momentum, (float)lr);
    } else {
      k_sgd<<<Blocks(total), 256>>>(params, grads, total, (float)lr);
    }
  }

  bool Run(int mode, const float* x, const float* y, int n, int training, int input_grad, double* loss_out, int* correct_out, float* logits_out) {
    if(!compiled) {
      return Fail("the model is not compiled");
    }
    if(n < 1 || !x || (mode != 3 && !y)) {
      return Fail("an empty batch, or a batch with no images or labels");
    }
    if(!Cuda(cudaSetDevice(dev), "cudaSetDevice") || !EnsureBatch(n)) {
      return false;
    }
    want_input_grad = input_grad != 0 && (mode == 0 || mode == 2);
    if(want_input_grad && !EnsureInputGrad()) {
      return false;
    }
    last_n = 0;
    if(!Cuda(cudaMemcpy(d_x, x, (size_t)n * input.Size() * 4, cudaMemcpyHostToDevice), "uploading the batch")) {
      return false;
    }
    if(y && !Cuda(cudaMemcpy(d_y, y, (size_t)n * 4, cudaMemcpyHostToDevice), "uploading the labels")) {
      return false;
    }
    if(!Forward(n, training != 0)) {
      return false;
    }
    const bool grads_wanted = mode == 0 || mode == 2;
    if(mode != 3) {
      k_loss<<<(n + 255) / 256, 256>>>(acts.back(), d_y, d_loss, d_hit, gacts.back(), n, grads_wanted ? 1 : 0);
    }
    if(grads_wanted) {
      if(!Backward(n)) {
        return false;
      }
      if(mode == 0) {
        Step();
      }
    }
    // results: the copies wait for everything queued above
    if(logits_out && !Cuda(cudaMemcpy(logits_out, acts.back(), (size_t)n * 4, cudaMemcpyDeviceToHost), "reading the logits")) {
      return false;
    }
    if(mode != 3) {
      std::vector<double> l((size_t)n);
      std::vector<int> h((size_t)n);
      if(!Cuda(cudaMemcpy(l.data(), d_loss, (size_t)n * 8, cudaMemcpyDeviceToHost), "reading the loss") ||
         !Cuda(cudaMemcpy(h.data(), d_hit, (size_t)n * 4, cudaMemcpyDeviceToHost), "reading the hits")) {
        return false;
      }
      double sum = 0.0;
      int correct = 0;
      for(int i = 0; i < n; ++i) {
        sum += l[(size_t)i];
        correct += h[(size_t)i];
      }
      if(loss_out) {
        *loss_out = sum / n;
      }
      if(correct_out) {
        *correct_out = correct;
      }
    }
    if(!Cuda(cudaDeviceSynchronize(), "the GPU pass") || !Cuda(cudaGetLastError(), "a kernel launch")) {
      return false;
    }
    last_n = n;
#ifdef OBJK_CUDA_PROFILE
    if(mode == 0) {
      for(size_t i = 0; i < layers.size(); ++i) {
        float ms = 0.0f;
        if(cudaEventElapsedTime(&ms, prof_ev[i * 2], prof_ev[i * 2 + 1]) == cudaSuccess) { prof_ms[i] += ms; }
        ms = 0.0f;
        if(cudaEventElapsedTime(&ms, prof_ev[(64 + i) * 2], prof_ev[(64 + i) * 2 + 1]) == cudaSuccess) { prof_ms[64 + i] += ms; }
      }
      prof_runs++;
    }
#endif
    return true;
  }

  // copies between the host and a device buffer; false (with the reason) on any CUDA error
  bool Copy(void* dst, const void* src, size_t bytes, cudaMemcpyKind kind, const char* what) {
    return Cuda(cudaSetDevice(dev), "cudaSetDevice") && Cuda(cudaMemcpy(dst, src, bytes, kind), what);
  }
};

static Net* AsNet(void* p) { return static_cast<Net*>(p); }

}  // namespace

extern "C" {

OBJK_CUDA_EXPORT int objk_cuda_abi(void) { return OBJK_CUDA_ABI; }

OBJK_CUDA_EXPORT int objk_cuda_device_count(void) {
  int count = 0;
  const cudaError_t e = cudaGetDeviceCount(&count);
  if(e != cudaSuccess) {
    cudaGetLastError();
    g_error = std::string("no CUDA device: ") + cudaGetErrorString(e);
    return 0;
  }
  if(count < 1) {
    g_error = "no CUDA device";
  }
  return count;
}

OBJK_CUDA_EXPORT int objk_cuda_device_info(int dev, char* name, int name_len, size_t* total_bytes, size_t* free_bytes, int* major, int* minor) {
  cudaDeviceProp prop;
  const cudaError_t e = cudaGetDeviceProperties(&prop, dev);
  if(e != cudaSuccess) {
    cudaGetLastError();
    g_error = std::string("cannot query device ") + std::to_string(dev) + ": " + cudaGetErrorString(e);
    return 0;
  }
  if(name && name_len > 0) {
    std::snprintf(name, (size_t)name_len, "%s", prop.name);
  }
  if(major) { *major = prop.major; }
  if(minor) { *minor = prop.minor; }
  size_t free_b = 0, total_b = 0;
  int previous = 0;
  cudaGetDevice(&previous);
  if(cudaSetDevice(dev) == cudaSuccess && cudaMemGetInfo(&free_b, &total_b) == cudaSuccess) {
    if(total_bytes) { *total_bytes = total_b; }
    if(free_bytes) { *free_bytes = free_b; }
  } else {
    cudaGetLastError();
    if(total_bytes) { *total_bytes = prop.totalGlobalMem; }
    if(free_bytes) { *free_bytes = 0; }
  }
  cudaSetDevice(previous);
  return 1;
}

OBJK_CUDA_EXPORT const char* objk_cuda_last_error(void) { return g_error.c_str(); }

OBJK_CUDA_EXPORT void* objk_cuda_net_create(int dev, int channels, int height, int width, int tf32) {
  int count = 0;
  if(cudaGetDeviceCount(&count) != cudaSuccess || dev < 0 || dev >= count) {
    cudaGetLastError();
    g_error = "no CUDA device " + std::to_string(dev);
    return nullptr;
  }
  Net* net = new (std::nothrow) Net();
  if(!net) {
    g_error = "out of memory";
    return nullptr;
  }
  if(!net->Create(dev, channels, height, width, tf32)) {
    g_error = net->err;
    net->Destroy();
    delete net;
    return nullptr;
  }
  return net;
}

OBJK_CUDA_EXPORT void objk_cuda_net_destroy(void* net) {
  if(net) {
    AsNet(net)->Destroy();
    delete AsNet(net);
  }
}

OBJK_CUDA_EXPORT const char* objk_cuda_net_error(void* net) { return net ? AsNet(net)->err.c_str() : g_error.c_str(); }

OBJK_CUDA_EXPORT int objk_cuda_net_add(void* net, int kind, int a, int b, int c, int d, double rate, unsigned long long seed) {
  return net && AsNet(net)->Add(kind, a, b, c, d, rate, seed) ? 1 : 0;
}

OBJK_CUDA_EXPORT int objk_cuda_net_compile(void* net, int optimizer, double lr, double beta1, double beta2, double eps, double momentum) {
  return net && AsNet(net)->Compile(optimizer, lr, beta1, beta2, eps, momentum) ? 1 : 0;
}

OBJK_CUDA_EXPORT int objk_cuda_net_set_lr(void* net, double lr) {
  if(!net || !(lr > 0.0)) {
    return 0;
  }
  AsNet(net)->lr = lr;
  return 1;
}

OBJK_CUDA_EXPORT int objk_cuda_net_reset_rng(void* net) {
  if(!net) {
    return 0;
  }
  for(size_t i = 0; i < AsNet(net)->layers.size(); ++i) {
    AsNet(net)->layers[i].step = 0;
  }
  return 1;
}

OBJK_CUDA_EXPORT int objk_cuda_net_set_params(void* p, const float* values, size_t count) {
  Net* net = AsNet(p);
  if(!net) {
    return 0;
  }
  if(!net->compiled || !values || count != net->total) {
    net->Fail("the parameter count does not match the model");
    return 0;
  }
  return net->Copy(net->params, values, count * 4, cudaMemcpyHostToDevice, "uploading the parameters") ? 1 : 0;
}

OBJK_CUDA_EXPORT int objk_cuda_net_get_params(void* p, float* values, size_t count) {
  Net* net = AsNet(p);
  if(!net || !net->compiled || !values || count != net->total) {
    return 0;
  }
  return net->Copy(values, net->params, count * 4, cudaMemcpyDeviceToHost, "reading the parameters") ? 1 : 0;
}

OBJK_CUDA_EXPORT int objk_cuda_net_get_grads(void* p, float* values, size_t count) {
  Net* net = AsNet(p);
  if(!net || !net->compiled || !values || count != net->total) {
    return 0;
  }
  return net->Copy(values, net->grads, count * 4, cudaMemcpyDeviceToHost, "reading the gradients") ? 1 : 0;
}

OBJK_CUDA_EXPORT int objk_cuda_net_run(void* net, int mode, const float* x, const float* y, int n, int training, int want_input_grad,
                                       double* loss, int* correct, float* logits) {
  if(!net) {
    return 0;
  }
  return AsNet(net)->Run(mode, x, y, n, training, want_input_grad, loss, correct, logits) ? 1 : 0;
}

OBJK_CUDA_EXPORT size_t objk_cuda_net_activation_size(void* p, int layer) {
  Net* net = AsNet(p);
  if(!net || layer < 0 || layer >= (int)net->layers.size() || net->last_n < 1) {
    return 0;
  }
  return (size_t)net->last_n * net->layers[(size_t)layer].out.Size();
}

OBJK_CUDA_EXPORT int objk_cuda_net_get_activation(void* p, int layer, float* values, size_t count) {
  Net* net = AsNet(p);
  if(!net || !values || count == 0 || count != objk_cuda_net_activation_size(p, layer)) {
    return 0;
  }
  return net->Copy(values, net->acts[(size_t)layer], count * 4, cudaMemcpyDeviceToHost, "reading an activation") ? 1 : 0;
}

OBJK_CUDA_EXPORT size_t objk_cuda_net_input_grad_size(void* p) {
  Net* net = AsNet(p);
  if(!net || !net->want_input_grad || !net->d_ginput || net->last_n < 1) {
    return 0;
  }
  return (size_t)net->last_n * net->input.Size();
}

OBJK_CUDA_EXPORT int objk_cuda_net_get_input_grad(void* p, float* values, size_t count) {
  Net* net = AsNet(p);
  if(!net || !values || count == 0 || count != objk_cuda_net_input_grad_size(p)) {
    return 0;
  }
  return net->Copy(values, net->d_ginput, count * 4, cudaMemcpyDeviceToHost, "reading the input gradient") ? 1 : 0;
}

OBJK_CUDA_EXPORT size_t objk_cuda_net_device_bytes(void* net) { return net ? AsNet(net)->device_bytes : 0; }

}  // extern "C"
