/***************************************************************************
 * Convolutional neural network layers (System.ML.Cnn), CPU backend
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

// The engine only: no VM types, so it can be exercised by itself, and so a
// different backend (CUDA) can implement the same Layer interface. Every layer
// reads and writes contiguous tensors in NCHW layout, batch first. Standard
// C++14 plus Eigen; no intrinsics, no OpenMP -- the batch is spread over a small
// std::thread pool, one sample per task, with the matrix multiply inside a task
// done by Eigen on a single thread.
//
// Everything is a template on the scalar, so the same code is the float32 model
// users train and a float64 model the gradient checks use: central differences
// in float32 cannot resolve a gradient better than about 1e-3, in float64 they
// agree with backprop to about 1e-9, which is what a correctness check needs.

#ifndef __OBJK_CNN_H__
#define __OBJK_CNN_H__

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifdef OBJK_CNN_PROFILE
#include <chrono>
#endif

#ifdef _WIN32
#ifdef _MSYS2
#include <Eigen/Dense>
#else
#include <eigen-3.4.0/Eigen/Dense>
#endif
#else
#include <Eigen/Dense>
#endif

namespace objk_cnn {

//
// Deterministic generator (splitmix64). Used instead of <random> because the
// standard distributions are implementation-defined: the same seed must give the
// same weights, masks, augmentations and shuffles on every platform.
//
struct Rng {
  uint64_t state;
  explicit Rng(uint64_t seed = 0) : state(seed) {}
  uint64_t Next() {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  // uniform in [0, 1)
  double Uniform() { return (double)(Next() >> 11) * (1.0 / 9007199254740992.0); }
  double Uniform(double lo, double hi) { return lo + (hi - lo) * Uniform(); }
};

// stateless mix of one 64 bit value, the same finalizer
static inline uint64_t Mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

//
// A small persistent thread pool. For(n, f) runs f(i, tid) for i in [0, n), the
// calling thread taking part as tid 0, and returns when every call is done.
//
class Pool {
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable work_cv_;
  std::condition_variable done_cv_;
  std::mutex call_mutex_;
  const std::function<void(long, int)>* job_;
  long total_;
  unsigned long generation_;
  int finished_;
  bool stop_;
  int threads_;

  // Static partition: thread `tid` runs a fixed contiguous block of the indexes. Dynamic
  // scheduling would balance better, but which thread runs which sample would change from run
  // to run, and with it the order float32 partial sums are added in (the per-thread gradient
  // accumulators), so training would not repeat bit for bit. Samples cost the same, so little
  // is lost.
  void Work(int tid) {
    const long threads = threads_;
    const long lo = total_ * tid / threads, hi = total_ * (tid + 1) / threads;
    for(long i = lo; i < hi; ++i) {
      (*job_)(i, tid);
    }
  }

  // `seen` is the generation when the thread was created: a thread made between
  // calls must not run the previous call's (finished) job
  void Loop(int tid, unsigned long seen) {
    for(;;) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        work_cv_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if(stop_) {
          return;
        }
        seen = generation_;
      }
      Work(tid);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_++;
      }
      done_cv_.notify_all();
    }
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    work_cv_.notify_all();
    for(size_t i = 0; i < workers_.size(); ++i) {
      workers_[i].join();
    }
    workers_.clear();
    stop_ = false;
  }

 public:
  Pool() : job_(nullptr), total_(0), generation_(0), finished_(0), stop_(false), threads_(1) {
    unsigned hw = std::thread::hardware_concurrency();
    SetThreads((int)std::min(8u, hw ? hw : 1u));
  }

  // The pool lives for the whole process and is never destroyed (see Instance):
  // joining threads from a static destructor while a DLL unloads can deadlock.
  ~Pool() {}

  int Threads() const { return threads_; }

  // Joins the workers. The VM calls unload_lib and then unloads the library, and a thread
  // still waiting inside it would run unmapped code, so the library stops them first.
  // Afterwards For runs everything on the calling thread.
  void Shutdown() {
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    Stop();
    threads_ = 1;
  }

  void SetThreads(int count) {
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    if(count < 1) {
      count = 1;
    }
    if(count > 64) {
      count = 64;
    }
    if(count == threads_ && (int)workers_.size() == count - 1) {
      return;
    }
    Stop();
    threads_ = count;
    for(int t = 1; t < count; ++t) {
      workers_.push_back(std::thread(&Pool::Loop, this, t, generation_));
    }
  }

  void For(long n, const std::function<void(long, int)>& f) {
    if(n <= 0) {
      return;
    }
    if(threads_ <= 1 || n == 1) {
      for(long i = 0; i < n; ++i) {
        f(i, 0);
      }
      return;
    }

    std::lock_guard<std::mutex> call_lock(call_mutex_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      job_ = &f;
      total_ = n;
      finished_ = 0;
      generation_++;
    }
    work_cv_.notify_all();
    Work(0);
    {
      std::unique_lock<std::mutex> lock(mutex_);
      done_cv_.wait(lock, [&] { return finished_ == (int)workers_.size(); });
    }
  }

  static Pool& Instance() {
    static Pool* pool = new Pool();
    return *pool;
  }
};

struct Shape {
  int c, h, w;
  Shape() : c(0), h(0), w(0) {}
  Shape(int c_, int h_, int w_) : c(c_), h(h_), w(w_) {}
  size_t Size() const { return (size_t)c * h * w; }
  std::string Str() const { return std::to_string(c) + "x" + std::to_string(h) + "x" + std::to_string(w); }
};

template<class T>
struct ParamRef {
  T* value;
  T* grad;
  size_t size;
};

enum InitMode { INIT_GLOROT = 0, INIT_KAIMING = 1 };

//
// The interface a backend implements. Backward OVERWRITES the layer's parameter
// gradients and gin (it never accumulates), and gin may be null when the caller
// does not need the gradient with respect to the layer's input.
//
template<class T>
class Layer {
 public:
  virtual ~Layer() {}
  virtual std::string Describe() const = 0;
  virtual bool Setup(const Shape& in, Shape& out, std::string& err) = 0;
  virtual void Init(Rng& rng, int mode) { (void)rng; (void)mode; }
  virtual void Forward(const T* in, T* out, int n, bool training, Pool& pool) = 0;
  virtual void Backward(const T* in, const T* out, const T* gout, T* gin, int n, Pool& pool) = 0;
  virtual void Params(std::vector<ParamRef<T>>& list) { (void)list; }
  virtual void ResetRng() {}
  virtual size_t ParamCount() { return 0; }
};

typedef long long i64;

template<class T>
struct Mat {
  typedef Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> RM;
  typedef Eigen::Map<RM> Map;
  typedef Eigen::Map<const RM> CMap;
  typedef Eigen::Map<const Eigen::Matrix<T, 1, Eigen::Dynamic>> CRow;
};

// weight initializer: bound of the uniform distribution
static inline double InitLimit(int mode, double fan_in, double fan_out) {
  if(mode == INIT_KAIMING) {
    // He uniform: sqrt(6 / fan_in)
    return std::sqrt(6.0 / fan_in);
  }
  // Glorot (Xavier) uniform, the Keras default: sqrt(6 / (fan_in + fan_out))
  return std::sqrt(6.0 / (fan_in + fan_out));
}

//
// 2D convolution, cross-correlation as in PyTorch and Keras. Weights are laid out
// [filters][channels][kh][kw] (PyTorch's layout), followed by one bias per filter.
// Done as im2col and a matrix multiply per sample: the column buffer is a
// per-thread scratch that is reused, so nothing allocates inside the batch loop.
//
// Padding 'valid' is none; 'same' keeps ceil(size / stride) and, as TensorFlow
// does, puts the odd extra pixel of an even kernel at the bottom/right.
//
template<class T>
class Conv2D : public Layer<T> {
  typedef Mat<T> M;
  int filters_, k_, stride_;
  bool same_;
  int c_, h_, w_, oh_, ow_, pad_top_, pad_left_;
  std::vector<T> w_data_, b_data_, gw_, gb_;
  std::vector<std::vector<T>> col_, tgw_, tgb_, gcol_;

  size_t ColSize() const { return (size_t)c_ * k_ * k_ * oh_ * ow_; }

  void EnsureScratch(int threads, bool grads) {
    if((int)col_.size() < threads) {
      col_.resize(threads);
      tgw_.resize(threads);
      tgb_.resize(threads);
    }
    for(int t = 0; t < threads; ++t) {
      if(col_[t].size() < ColSize()) {
        col_[t].resize(ColSize());
      }
      if(grads) {
        tgw_[t].assign(w_data_.size(), (T)0);
        tgb_[t].assign(b_data_.size(), (T)0);
      }
    }
  }

  void Im2Col(const T* x, T* col) const {
    const int P = oh_ * ow_;
    for(int c = 0; c < c_; ++c) {
      for(int kh = 0; kh < k_; ++kh) {
        for(int kw = 0; kw < k_; ++kw) {
          T* row = col + ((size_t)(c * k_ + kh) * k_ + kw) * P;
          for(int oh = 0; oh < oh_; ++oh) {
            const int ih = oh * stride_ - pad_top_ + kh;
            T* r = row + (size_t)oh * ow_;
            if(ih < 0 || ih >= h_) {
              std::fill(r, r + ow_, (T)0);
              continue;
            }
            const T* xr = x + ((size_t)c * h_ + ih) * w_;
            for(int ow = 0; ow < ow_; ++ow) {
              const int iw = ow * stride_ - pad_left_ + kw;
              r[ow] = (iw < 0 || iw >= w_) ? (T)0 : xr[iw];
            }
          }
        }
      }
    }
  }

  // adds the column gradient back into the image gradient
  void Col2Im(const T* col, T* gx) const {
    const int P = oh_ * ow_;
    for(int c = 0; c < c_; ++c) {
      for(int kh = 0; kh < k_; ++kh) {
        for(int kw = 0; kw < k_; ++kw) {
          const T* row = col + ((size_t)(c * k_ + kh) * k_ + kw) * P;
          for(int oh = 0; oh < oh_; ++oh) {
            const int ih = oh * stride_ - pad_top_ + kh;
            if(ih < 0 || ih >= h_) {
              continue;
            }
            T* xr = gx + ((size_t)c * h_ + ih) * w_;
            const T* r = row + (size_t)oh * ow_;
            for(int ow = 0; ow < ow_; ++ow) {
              const int iw = ow * stride_ - pad_left_ + kw;
              if(iw >= 0 && iw < w_) {
                xr[iw] += r[ow];
              }
            }
          }
        }
      }
    }
  }

 public:
  Conv2D(int filters, int kernel, int stride, bool same) :
    filters_(filters), k_(kernel), stride_(stride), same_(same), c_(0), h_(0), w_(0), oh_(0), ow_(0),
    pad_top_(0), pad_left_(0) {}

  std::string Describe() const {
    return "Conv2D(" + std::to_string(filters_) + "," + std::to_string(k_) + "," + std::to_string(stride_) +
           (same_ ? ",same)" : ",valid)");
  }

  bool Setup(const Shape& in, Shape& out, std::string& err) {
    if(filters_ < 1 || k_ < 1 || stride_ < 1) {
      err = "Conv2D needs filters, kernel and stride of at least 1";
      return false;
    }
    c_ = in.c; h_ = in.h; w_ = in.w;
    if(same_) {
      oh_ = (h_ + stride_ - 1) / stride_;
      ow_ = (w_ + stride_ - 1) / stride_;
      const int total_h = std::max((oh_ - 1) * stride_ + k_ - h_, 0);
      const int total_w = std::max((ow_ - 1) * stride_ + k_ - w_, 0);
      pad_top_ = total_h / 2;
      pad_left_ = total_w / 2;
    } else {
      if(h_ < k_ || w_ < k_) {
        err = "Conv2D kernel " + std::to_string(k_) + " does not fit the input " + in.Str();
        return false;
      }
      oh_ = (h_ - k_) / stride_ + 1;
      ow_ = (w_ - k_) / stride_ + 1;
      pad_top_ = pad_left_ = 0;
    }
    const i64 wsize = (i64)filters_ * c_ * k_ * k_;
    if(wsize > 2000000000LL || (i64)filters_ * oh_ * ow_ > 2000000000LL) {
      err = "Conv2D is too large";
      return false;
    }
    w_data_.assign((size_t)wsize, (T)0);
    b_data_.assign((size_t)filters_, (T)0);
    gw_.assign(w_data_.size(), (T)0);
    gb_.assign(b_data_.size(), (T)0);
    out = Shape(filters_, oh_, ow_);
    return true;
  }

  void Init(Rng& rng, int mode) {
    const double limit = InitLimit(mode, (double)c_ * k_ * k_, (double)filters_ * k_ * k_);
    for(size_t i = 0; i < w_data_.size(); ++i) {
      w_data_[i] = (T)rng.Uniform(-limit, limit);
    }
    std::fill(b_data_.begin(), b_data_.end(), (T)0);
  }

  void Forward(const T* in, T* out, int n, bool, Pool& pool) {
    const int P = oh_ * ow_;
    const int CKK = c_ * k_ * k_;
    EnsureScratch(pool.Threads(), false);
    const size_t in_size = (size_t)c_ * h_ * w_, out_size = (size_t)filters_ * P;
    pool.For(n, [&](long i, int tid) {
      T* col = col_[tid].data();
      Im2Col(in + i * in_size, col);
      typename M::Map o(out + i * out_size, filters_, P);
      o.noalias() = typename M::CMap(w_data_.data(), filters_, CKK) * typename M::CMap(col, CKK, P);
      for(int f = 0; f < filters_; ++f) {
        T* r = out + i * out_size + (size_t)f * P;
        const T b = b_data_[f];
        for(int p = 0; p < P; ++p) {
          r[p] += b;
        }
      }
    });
  }

  void Backward(const T* in, const T*, const T* gout, T* gin, int n, Pool& pool) {
    const int P = oh_ * ow_;
    const int CKK = c_ * k_ * k_;
    EnsureScratch(pool.Threads(), true);
    // the gradient columns need a second per-thread buffer, kept between calls
    if(gin && (int)gcol_.size() < pool.Threads()) {
      gcol_.resize(pool.Threads());
    }
    const size_t in_size = (size_t)c_ * h_ * w_, out_size = (size_t)filters_ * P;
    pool.For(n, [&](long i, int tid) {
      T* col = col_[tid].data();
      Im2Col(in + i * in_size, col);
      typename M::CMap go(gout + i * out_size, filters_, P);
      typename M::Map gw(tgw_[tid].data(), filters_, CKK);
      gw.noalias() += go * typename M::CMap(col, CKK, P).transpose();
      T* gb = tgb_[tid].data();
      for(int f = 0; f < filters_; ++f) {
        const T* r = gout + i * out_size + (size_t)f * P;
        T s = 0;
        for(int p = 0; p < P; ++p) {
          s += r[p];
        }
        gb[f] += s;
      }
      if(gin) {
        std::vector<T>& gc = gcol_[tid];
        if(gc.size() < (size_t)CKK * P) {
          gc.resize((size_t)CKK * P);
        }
        typename M::Map gcm(gc.data(), CKK, P);
        gcm.noalias() = typename M::CMap(w_data_.data(), filters_, CKK).transpose() * go;
        T* gx = gin + i * in_size;
        std::fill(gx, gx + in_size, (T)0);
        Col2Im(gc.data(), gx);
      }
    });
    std::fill(gw_.begin(), gw_.end(), (T)0);
    std::fill(gb_.begin(), gb_.end(), (T)0);
    for(int t = 0; t < pool.Threads(); ++t) {
      for(size_t j = 0; j < gw_.size(); ++j) {
        gw_[j] += tgw_[t][j];
      }
      for(size_t j = 0; j < gb_.size(); ++j) {
        gb_[j] += tgb_[t][j];
      }
    }
  }

  void Params(std::vector<ParamRef<T>>& list) {
    ParamRef<T> w = { w_data_.data(), gw_.data(), w_data_.size() };
    ParamRef<T> b = { b_data_.data(), gb_.data(), b_data_.size() };
    list.push_back(w);
    list.push_back(b);
  }

  size_t ParamCount() { return w_data_.size() + b_data_.size(); }
};

//
// Max pooling over size x size windows, no padding, output floor((in - size) / stride) + 1.
// A tie goes to the first maximum in scan order, as in PyTorch.
//
template<class T>
class MaxPool2D : public Layer<T> {
  int size_, stride_;
  int c_, h_, w_, oh_, ow_;
  std::vector<int> index_;

 public:
  MaxPool2D(int size, int stride) : size_(size), stride_(stride > 0 ? stride : size), c_(0), h_(0), w_(0), oh_(0), ow_(0) {}

  std::string Describe() const {
    return "MaxPool2D(" + std::to_string(size_) + "," + std::to_string(stride_) + ")";
  }

  bool Setup(const Shape& in, Shape& out, std::string& err) {
    if(size_ < 1 || stride_ < 1) {
      err = "MaxPool2D needs a size and stride of at least 1";
      return false;
    }
    c_ = in.c; h_ = in.h; w_ = in.w;
    if(h_ < size_ || w_ < size_) {
      err = "MaxPool2D window " + std::to_string(size_) + " does not fit the input " + in.Str();
      return false;
    }
    oh_ = (h_ - size_) / stride_ + 1;
    ow_ = (w_ - size_) / stride_ + 1;
    out = Shape(c_, oh_, ow_);
    return true;
  }

  void Forward(const T* in, T* out, int n, bool, Pool& pool) {
    const size_t out_size = (size_t)c_ * oh_ * ow_, in_size = (size_t)c_ * h_ * w_;
    if(index_.size() < (size_t)n * out_size) {
      index_.resize((size_t)n * out_size);
    }
    pool.For((long)n * c_, [&](long job, int) {
      const long i = job / c_, c = job % c_;
      const T* plane = in + i * in_size + (size_t)c * h_ * w_;
      T* o = out + i * out_size + (size_t)c * oh_ * ow_;
      int* idx = index_.data() + i * out_size + (size_t)c * oh_ * ow_;
      for(int oh = 0; oh < oh_; ++oh) {
        for(int ow = 0; ow < ow_; ++ow) {
          int best = (oh * stride_) * w_ + ow * stride_;
          T best_v = plane[best];
          for(int kh = 0; kh < size_; ++kh) {
            for(int kw = 0; kw < size_; ++kw) {
              const int at = (oh * stride_ + kh) * w_ + ow * stride_ + kw;
              if(plane[at] > best_v) {
                best_v = plane[at];
                best = at;
              }
            }
          }
          o[oh * ow_ + ow] = best_v;
          idx[oh * ow_ + ow] = best;
        }
      }
    });
  }

  void Backward(const T*, const T*, const T* gout, T* gin, int n, Pool& pool) {
    if(!gin) {
      return;
    }
    const size_t out_size = (size_t)c_ * oh_ * ow_, in_size = (size_t)c_ * h_ * w_;
    pool.For((long)n * c_, [&](long job, int) {
      const long i = job / c_, c = job % c_;
      T* g = gin + i * in_size + (size_t)c * h_ * w_;
      std::fill(g, g + (size_t)h_ * w_, (T)0);
      const T* go = gout + i * out_size + (size_t)c * oh_ * ow_;
      const int* idx = index_.data() + i * out_size + (size_t)c * oh_ * ow_;
      for(int p = 0; p < oh_ * ow_; ++p) {
        g[idx[p]] += go[p];
      }
    });
  }
};

template<class T>
class ReLU : public Layer<T> {
  size_t size_;

 public:
  ReLU() : size_(0) {}
  std::string Describe() const { return "ReLU"; }
  bool Setup(const Shape& in, Shape& out, std::string&) { size_ = in.Size(); out = in; return true; }

  void Forward(const T* in, T* out, int n, bool, Pool& pool) {
    pool.For(n, [&](long i, int) {
      const T* x = in + i * size_;
      T* y = out + i * size_;
      for(size_t j = 0; j < size_; ++j) {
        y[j] = x[j] > (T)0 ? x[j] : (T)0;
      }
    });
  }

  void Backward(const T* in, const T*, const T* gout, T* gin, int n, Pool& pool) {
    if(!gin) {
      return;
    }
    pool.For(n, [&](long i, int) {
      const T* x = in + i * size_;
      const T* go = gout + i * size_;
      T* gi = gin + i * size_;
      for(size_t j = 0; j < size_; ++j) {
        gi[j] = x[j] > (T)0 ? go[j] : (T)0;
      }
    });
  }
};

template<class T>
class Flatten : public Layer<T> {
  size_t size_;

 public:
  Flatten() : size_(0) {}
  std::string Describe() const { return "Flatten"; }
  bool Setup(const Shape& in, Shape& out, std::string&) { size_ = in.Size(); out = Shape((int)size_, 1, 1); return true; }

  void Forward(const T* in, T* out, int n, bool, Pool&) {
    std::memcpy(out, in, sizeof(T) * size_ * n);
  }

  void Backward(const T*, const T*, const T* gout, T* gin, int n, Pool&) {
    if(gin) {
      std::memcpy(gin, gout, sizeof(T) * size_ * n);
    }
  }
};

//
// Fully connected layer, y = x W^T + b with W laid out [units][inputs] (PyTorch's
// layout). The input may be any shape; it is read as a flat vector per sample.
//
template<class T>
class Dense : public Layer<T> {
  typedef Mat<T> M;
  int units_, inputs_;
  std::vector<T> w_data_, b_data_, gw_, gb_;

 public:
  explicit Dense(int units) : units_(units), inputs_(0) {}
  std::string Describe() const { return "Dense(" + std::to_string(units_) + ")"; }

  bool Setup(const Shape& in, Shape& out, std::string& err) {
    if(units_ < 1) {
      err = "Dense needs at least 1 unit";
      return false;
    }
    if(in.Size() < 1 || (i64)in.Size() * units_ > 2000000000LL) {
      err = "Dense is too large";
      return false;
    }
    inputs_ = (int)in.Size();
    w_data_.assign((size_t)units_ * inputs_, (T)0);
    b_data_.assign((size_t)units_, (T)0);
    gw_.assign(w_data_.size(), (T)0);
    gb_.assign(b_data_.size(), (T)0);
    out = Shape(units_, 1, 1);
    return true;
  }

  void Init(Rng& rng, int mode) {
    const double limit = InitLimit(mode, (double)inputs_, (double)units_);
    for(size_t i = 0; i < w_data_.size(); ++i) {
      w_data_[i] = (T)rng.Uniform(-limit, limit);
    }
    std::fill(b_data_.begin(), b_data_.end(), (T)0);
  }

  void Forward(const T* in, T* out, int n, bool, Pool& pool) {
    // split the units across the pool: each task owns a block of output columns
    const int tasks = std::min(pool.Threads(), units_);
    const int block = (units_ + tasks - 1) / tasks;
    pool.For(tasks, [&](long t, int) {
      const int u0 = (int)t * block, u1 = std::min(units_, u0 + block);
      if(u0 >= u1) {
        return;
      }
      typename M::CMap x(in, n, inputs_);
      typename M::CMap w(w_data_.data() + (size_t)u0 * inputs_, u1 - u0, inputs_);
      Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> y = x * w.transpose();
      for(int i = 0; i < n; ++i) {
        for(int u = u0; u < u1; ++u) {
          out[(size_t)i * units_ + u] = y(i, u - u0) + b_data_[u];
        }
      }
    });
  }

  void Backward(const T* in, const T*, const T* gout, T* gin, int n, Pool& pool) {
    const int tasks = std::min(pool.Threads(), units_);
    const int block = (units_ + tasks - 1) / tasks;
    typename M::CMap go(gout, n, units_);
    pool.For(tasks, [&](long t, int) {
      const int u0 = (int)t * block, u1 = std::min(units_, u0 + block);
      if(u0 >= u1) {
        return;
      }
      // gW rows u0..u1: gout[:, u0..u1]^T * x
      Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> gb_cols = go.middleCols(u0, u1 - u0);
      typename M::Map gw(gw_.data() + (size_t)u0 * inputs_, u1 - u0, inputs_);
      gw.noalias() = gb_cols.transpose() * typename M::CMap(in, n, inputs_);
      for(int u = u0; u < u1; ++u) {
        T s = 0;
        for(int i = 0; i < n; ++i) {
          s += gout[(size_t)i * units_ + u];
        }
        gb_[u] = s;
      }
    });
    if(gin) {
      // gin = gout * W, split over the batch
      const int rtasks = std::min(pool.Threads(), n);
      const int rblock = (n + rtasks - 1) / rtasks;
      pool.For(rtasks, [&](long t, int) {
        const int r0 = (int)t * rblock, r1 = std::min(n, r0 + rblock);
        if(r0 >= r1) {
          return;
        }
        typename M::Map g(gin + (size_t)r0 * inputs_, r1 - r0, inputs_);
        g.noalias() = go.middleRows(r0, r1 - r0) * typename M::CMap(w_data_.data(), units_, inputs_);
      });
    }
  }

  void Params(std::vector<ParamRef<T>>& list) {
    ParamRef<T> w = { w_data_.data(), gw_.data(), w_data_.size() };
    ParamRef<T> b = { b_data_.data(), gb_.data(), b_data_.size() };
    list.push_back(w);
    list.push_back(b);
  }

  size_t ParamCount() { return w_data_.size() + b_data_.size(); }
};

//
// Inverted dropout: in training each activation is zeroed with probability `rate`
// and the survivors are scaled by 1 / (1 - rate), so the expected activation is
// unchanged and evaluation is the identity. The mask is a pure function of the
// seed, the forward-call counter and the element index (not of thread timing), so
// a run is repeatable and ResetRng() replays the same masks.
//
template<class T>
class Dropout : public Layer<T> {
  double rate_;
  uint64_t seed_, step_;
  size_t size_;
  std::vector<T> mask_;

 public:
  Dropout(double rate, uint64_t seed) : rate_(rate), seed_(seed), step_(0), size_(0) {}
  std::string Describe() const { return "Dropout(" + std::to_string(rate_) + ")"; }

  bool Setup(const Shape& in, Shape& out, std::string& err) {
    if(!(rate_ >= 0.0 && rate_ < 1.0)) {
      err = "Dropout rate must be in [0, 1)";
      return false;
    }
    size_ = in.Size();
    out = in;
    return true;
  }

  void ResetRng() { step_ = 0; }

  void Forward(const T* in, T* out, int n, bool training, Pool& pool) {
    const size_t total = size_ * n;
    if(!training || rate_ == 0.0) {
      if(in != out) {
        std::memcpy(out, in, sizeof(T) * total);
      }
      mask_.clear();
      return;
    }
    if(mask_.size() < total) {
      mask_.resize(total);
    }
    const T scale = (T)(1.0 / (1.0 - rate_));
    const uint64_t key = Mix64(seed_ ^ Mix64(step_ + 0x51ED270B));
    step_++;
    const long chunk = 4096;
    const long chunks = (long)((total + chunk - 1) / chunk);
    pool.For(chunks, [&](long c, int) {
      const size_t lo = (size_t)c * chunk, hi = std::min(total, lo + chunk);
      for(size_t j = lo; j < hi; ++j) {
        const double u = (double)(Mix64(key + j) >> 11) * (1.0 / 9007199254740992.0);
        const T m = u >= rate_ ? scale : (T)0;
        mask_[j] = m;
        out[j] = in[j] * m;
      }
    });
  }

  void Backward(const T*, const T*, const T* gout, T* gin, int n, Pool&) {
    if(!gin) {
      return;
    }
    const size_t total = size_ * n;
    if(mask_.empty()) {
      std::memcpy(gin, gout, sizeof(T) * total);
      return;
    }
    for(size_t j = 0; j < total; ++j) {
      gin[j] = gout[j] * mask_[j];
    }
  }
};

enum OptimizerKind { OPT_SGD = 0, OPT_ADAM = 1 };

//
// A sequential model that ends in one logit per sample and is trained with the
// numerically stable binary cross-entropy on logits,
//   loss = mean( max(z, 0) - z * y + log(1 + exp(-|z|)) ),   dloss/dz = (sigmoid(z) - y) / n
// so a sigmoid layer is never needed to train; Predict applies it.
//
template<class T>
class Sequential {
  Shape input_;
  std::vector<std::unique_ptr<Layer<T>>> layers_;
  std::vector<Shape> shapes_;  // output shape of each layer
  std::vector<ParamRef<T>> params_;
  std::vector<std::vector<T>> acts_, grads_;
  std::vector<T> input_grad_;
  std::vector<T> staging_;
  int staging_n_;
  Rng rng_;
  uint64_t seed_;
  bool compiled_, training_, want_input_grad_;
  int opt_kind_;
  double lr_, beta1_, beta2_, eps_, momentum_;
  uint64_t step_;
  std::vector<T> m_, v_;
  int last_n_;
  int last_correct_;
  std::string error_;
#ifdef OBJK_CNN_PROFILE
 public:
  std::vector<double> prof_fwd_ = std::vector<double>(64, 0.0), prof_bwd_ = std::vector<double>(64, 0.0);
  static double ProfNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void ProfDump() {
    for(size_t i = 0; i < layers_.size(); ++i) {
      std::printf("  layer %2zu %-22s fwd %8.4f s  bwd %8.4f s\n", i, layers_[i]->Describe().c_str(), prof_fwd_[i], prof_bwd_[i]);
    }
  }
 private:
#endif

  Layer<T>* Add(Layer<T>* layer) {
    std::unique_ptr<Layer<T>> own(layer);
    if(compiled_) {
      error_ = "the model is already compiled; add layers before Compile";
      return nullptr;
    }
    Shape out;
    if(!layer->Setup(Current(), out, error_)) {
      return nullptr;
    }
    shapes_.push_back(out);
    layers_.push_back(std::move(own));
    return layers_.back().get();
  }

  // computed in double whatever T is, so a probability is exact to float32 at worst
  static double Sigmoid(double z) {
    if(z >= 0.0) {
      return 1.0 / (1.0 + std::exp(-z));
    }
    const double e = std::exp(z);
    return e / (1.0 + e);
  }

  void Ensure(int n) {
    const size_t layers = layers_.size();
    acts_.resize(layers);
    grads_.resize(layers);
    for(size_t i = 0; i < layers; ++i) {
      const size_t need = (size_t)n * shapes_[i].Size();
      if(acts_[i].size() < need) {
        acts_[i].resize(need);
      }
      if(grads_[i].size() < need) {
        grads_[i].resize(need);
      }
    }
  }

 public:
  Sequential(const Shape& input, uint64_t seed) :
    input_(input), staging_n_(0), rng_(seed), seed_(seed), compiled_(false), training_(false), want_input_grad_(false),
    opt_kind_(OPT_ADAM), lr_(1e-3), beta1_(0.9), beta2_(0.999), eps_(1e-8), momentum_(0.0), step_(0), last_n_(0),
    last_correct_(0) {}

  const std::string& Error() const { return error_; }
  void SetError(const std::string& e) { error_ = e; }
  const Shape& Input() const { return input_; }
  Shape Current() const { return shapes_.empty() ? input_ : shapes_.back(); }
  size_t Layers() const { return layers_.size(); }
  bool Compiled() const { return compiled_; }
  int LastCorrect() const { return last_correct_; }

  bool AddConv(int filters, int kernel, int stride, bool same) { return Add(new Conv2D<T>(filters, kernel, stride, same)) != nullptr; }
  bool AddMaxPool(int size, int stride) { return Add(new MaxPool2D<T>(size, stride)) != nullptr; }
  bool AddReLU() { return Add(new ReLU<T>()) != nullptr; }
  bool AddFlatten() { return Add(new Flatten<T>()) != nullptr; }
  bool AddDense(int units) { return Add(new Dense<T>(units)) != nullptr; }
  bool AddDropout(double rate) {
    // each dropout layer draws its own stream from the model seed
    return Add(new Dropout<T>(rate, Mix64(seed_ ^ (0xD20D0000ULL + layers_.size())))) != nullptr;
  }

  bool Compile(int optimizer, double lr, double beta1, double beta2, double eps, double momentum, int init_mode) {
    if(compiled_) {
      error_ = "the model is already compiled";
      return false;
    }
    if(layers_.empty()) {
      error_ = "the model has no layers";
      return false;
    }
    if(Current().Size() != 1) {
      error_ = "the last layer must produce one logit per sample, it produces " + Current().Str();
      return false;
    }
    if(!(lr > 0.0) || (optimizer != OPT_SGD && optimizer != OPT_ADAM) || !(eps > 0.0) ||
       !(beta1 >= 0.0 && beta1 < 1.0) || !(beta2 >= 0.0 && beta2 < 1.0) || !(momentum >= 0.0 && momentum < 1.0)) {
      error_ = "bad optimizer settings";
      return false;
    }
    opt_kind_ = optimizer;
    lr_ = lr; beta1_ = beta1; beta2_ = beta2; eps_ = eps; momentum_ = momentum;
    rng_ = Rng(seed_);
    for(size_t i = 0; i < layers_.size(); ++i) {
      layers_[i]->Init(rng_, init_mode);
    }
    params_.clear();
    size_t total = 0;
    for(size_t i = 0; i < layers_.size(); ++i) {
      layers_[i]->Params(params_);
    }
    for(size_t i = 0; i < params_.size(); ++i) {
      total += params_[i].size;
    }
    m_.assign(opt_kind_ == OPT_ADAM || momentum_ > 0.0 ? total : 0, (T)0);
    v_.assign(opt_kind_ == OPT_ADAM ? total : 0, (T)0);
    step_ = 0;
    compiled_ = true;
    return true;
  }

  void SetLearningRate(double lr) { if(lr > 0.0) { lr_ = lr; } }
  void SetTraining(bool training) { training_ = training; }
  bool Training() const { return training_; }
  void ResetRng() { for(size_t i = 0; i < layers_.size(); ++i) { layers_[i]->ResetRng(); } }

  size_t ParamCount() const {
    size_t total = 0;
    for(size_t i = 0; i < layers_.size(); ++i) {
      total += layers_[i]->ParamCount();
    }
    return total;
  }

  // a stable digest of the architecture; a saved file only loads into the same one
  uint64_t Signature() const {
    uint64_t h = 1469598103934665603ULL;
    std::string s = "in:" + input_.Str();
    for(size_t i = 0; i < layers_.size(); ++i) {
      s += "|" + layers_[i]->Describe();
    }
    for(size_t i = 0; i < s.size(); ++i) {
      h = (h ^ (uint64_t)(unsigned char)s[i]) * 1099511628211ULL;
    }
    return h;
  }

  std::string Summary() {
    std::string s = "Input " + input_.Str() + "\n";
    for(size_t i = 0; i < layers_.size(); ++i) {
      s += std::to_string(i) + ": " + layers_[i]->Describe() + " -> " + shapes_[i].Str() + "  params " +
           std::to_string(layers_[i]->ParamCount()) + "\n";
    }
    s += "Total parameters: " + std::to_string(ParamCount()) + "\n";
    return s;
  }

  // forward pass; returns the last layer's output (n logits)
  const T* Forward(const T* x, int n, bool training) {
    Ensure(n);
    Pool& pool = Pool::Instance();
    const T* cur = x;
    for(size_t i = 0; i < layers_.size(); ++i) {
#ifdef OBJK_CNN_PROFILE
      const double t0 = ProfNow();
#endif
      layers_[i]->Forward(cur, acts_[i].data(), n, training, pool);
#ifdef OBJK_CNN_PROFILE
      prof_fwd_[i] += ProfNow() - t0;
#endif
      cur = acts_[i].data();
    }
    last_n_ = n;
    return cur;
  }

  // backward pass from dloss/dlogit already stored in grads_.back()
  void Backward(const T* x, int n) {
    Pool& pool = Pool::Instance();
    for(int i = (int)layers_.size() - 1; i >= 0; --i) {
      const T* in = i == 0 ? x : acts_[i - 1].data();
      T* gin = nullptr;
      if(i > 0) {
        gin = grads_[i - 1].data();
      } else if(want_input_grad_) {
        const size_t need = (size_t)n * input_.Size();
        if(input_grad_.size() < need) {
          input_grad_.resize(need);
        }
        gin = input_grad_.data();
      }
#ifdef OBJK_CNN_PROFILE
      const double t0 = ProfNow();
#endif
      layers_[i]->Backward(in, acts_[i].data(), grads_[i].data(), gin, n, pool);
#ifdef OBJK_CNN_PROFILE
      prof_bwd_[i] += ProfNow() - t0;
#endif
    }
  }

  // loss and dloss/dlogit; counts correct predictions at the 0.5 threshold
  double Loss(const T* logits, const T* y, int n, bool want_grad) {
    double sum = 0.0;
    int correct = 0;
    T* g = want_grad ? grads_.back().data() : nullptr;
    for(int i = 0; i < n; ++i) {
      const double z = (double)logits[i], t = (double)y[i];
      sum += std::max(z, 0.0) - z * t + std::log1p(std::exp(-std::fabs(z)));
      if((z > 0.0) == (t > 0.5)) {
        correct++;
      }
      if(g) {
        g[i] = (T)((Sigmoid((double)logits[i]) - t) / n);
      }
    }
    last_correct_ = correct;
    return sum / n;
  }

  bool CheckBatch(const T* x, const T* y, int n) {
    if(!compiled_) {
      error_ = "the model is not compiled";
      return false;
    }
    if(n < 1 || !x || (y == nullptr)) {
      error_ = "an empty batch, or a batch with no images or labels";
      return false;
    }
    return true;
  }

  // forward + backward (no weight update); gradients are left in the parameters
  double ComputeGradients(const T* x, const T* y, int n, bool training, bool input_grad) {
    if(!CheckBatch(x, y, n)) {
      return -1.0;
    }
    want_input_grad_ = input_grad;
    const T* logits = Forward(x, n, training);
    const double loss = Loss(logits, y, n, true);
    Backward(x, n);
    return loss;
  }

  void Step() {
    ++step_;
    const double b1c = 1.0 - std::pow(beta1_, (double)step_);
    const double b2c = 1.0 - std::pow(beta2_, (double)step_);
    size_t off = 0;
    for(size_t p = 0; p < params_.size(); ++p) {
      T* w = params_[p].value;
      const T* g = params_[p].grad;
      const size_t size = params_[p].size;
      if(opt_kind_ == OPT_ADAM) {
        // PyTorch's Adam: p -= lr/(1-b1^t) * m / (sqrt(v)/sqrt(1-b2^t) + eps)
        const T b1 = (T)beta1_, b2 = (T)beta2_, ob1 = (T)(1.0 - beta1_), ob2 = (T)(1.0 - beta2_);
        const T step_size = (T)(lr_ / b1c), bc2_sqrt = (T)std::sqrt(b2c), eps = (T)eps_;
        T* m = m_.data() + off;
        T* v = v_.data() + off;
        for(size_t j = 0; j < size; ++j) {
          m[j] = b1 * m[j] + ob1 * g[j];
          v[j] = b2 * v[j] + ob2 * g[j] * g[j];
          const T denom = (T)(std::sqrt(v[j]) / bc2_sqrt) + eps;
          w[j] -= step_size * m[j] / denom;
        }
      } else if(momentum_ > 0.0) {
        T* m = m_.data() + off;
        const T mu = (T)momentum_, lr = (T)lr_;
        for(size_t j = 0; j < size; ++j) {
          m[j] = mu * m[j] + g[j];
          w[j] -= lr * m[j];
        }
      } else {
        const T lr = (T)lr_;
        for(size_t j = 0; j < size; ++j) {
          w[j] -= lr * g[j];
        }
      }
      off += size;
    }
  }

  // one optimizer step on a batch; returns the loss, -1 on a refused batch
  double TrainBatch(const T* x, const T* y, int n) {
    const double loss = ComputeGradients(x, y, n, true, false);
    if(loss < 0.0) {
      return loss;
    }
    Step();
    return loss;
  }

  double EvalBatch(const T* x, const T* y, int n) {
    if(!CheckBatch(x, y, n)) {
      return -1.0;
    }
    const T* logits = Forward(x, n, false);
    Ensure(n);
    return Loss(logits, y, n, false);
  }

  void Probabilities(const T* logits, int n, std::vector<double>& out) {
    out.resize(n);
    for(int i = 0; i < n; ++i) {
      out[i] = Sigmoid((double)logits[i]);
    }
  }

  // staging area: images written one at a time, then trained or predicted on
  T* Staging(int n) {
    const size_t need = (size_t)n * input_.Size();
    if(staging_.size() < need) {
      staging_.resize(need);
    }
    staging_n_ = n;
    return staging_.data();
  }
  T* StagingData() { return staging_.data(); }
  int StagingCount() const { return staging_n_; }

  bool GetWeights(std::vector<double>& out) const {
    out.clear();
    for(size_t p = 0; p < params_.size(); ++p) {
      for(size_t j = 0; j < params_[p].size; ++j) {
        out.push_back((double)params_[p].value[j]);
      }
    }
    return true;
  }

  bool GetGrads(std::vector<double>& out) const {
    out.clear();
    for(size_t p = 0; p < params_.size(); ++p) {
      for(size_t j = 0; j < params_[p].size; ++j) {
        out.push_back((double)params_[p].grad[j]);
      }
    }
    return true;
  }

  bool SetWeights(const double* in, size_t count) {
    if(!compiled_ || count != ParamCount()) {
      error_ = "weight count " + std::to_string(count) + " does not match the model's " + std::to_string(ParamCount());
      return false;
    }
    size_t k = 0;
    for(size_t p = 0; p < params_.size(); ++p) {
      for(size_t j = 0; j < params_[p].size; ++j) {
        params_[p].value[j] = (T)in[k++];
      }
    }
    return true;
  }

  // activation of layer `index` for the last forward pass
  bool Activation(int index, std::vector<double>& out) const {
    if(index < 0 || index >= (int)layers_.size() || last_n_ < 1) {
      return false;
    }
    const size_t count = (size_t)last_n_ * shapes_[index].Size();
    out.resize(count);
    for(size_t j = 0; j < count; ++j) {
      out[j] = (double)acts_[index][j];
    }
    return true;
  }

  bool InputGrad(std::vector<double>& out) const {
    const size_t count = (size_t)last_n_ * input_.Size();
    if(!want_input_grad_ || input_grad_.size() < count || last_n_ < 1) {
      return false;
    }
    out.resize(count);
    for(size_t j = 0; j < count; ++j) {
      out[j] = (double)input_grad_[j];
    }
    return true;
  }

  // Weights only, in a small binary file: magic, scalar size, parameter count,
  // architecture signature, then the raw values. Exact: no text conversion.
  // The caller opens the file (the API layer needs a wide-character open on
  // Windows) and these close it.
  bool Save(FILE* file, const std::string& path) {
    if(!compiled_) {
      error_ = "the model is not compiled";
      if(file) { std::fclose(file); }
      return false;
    }
    if(!file) {
      error_ = "cannot open " + path + " for writing";
      return false;
    }
    const char magic[8] = { 'O', 'B', 'J', 'K', 'C', 'N', 'N', '1' };
    const uint64_t scalar = sizeof(T), count = ParamCount(), sig = Signature();
    bool ok = std::fwrite(magic, 1, 8, file) == 8 && std::fwrite(&scalar, 8, 1, file) == 1 &&
              std::fwrite(&count, 8, 1, file) == 1 && std::fwrite(&sig, 8, 1, file) == 1;
    for(size_t p = 0; ok && p < params_.size(); ++p) {
      ok = std::fwrite(params_[p].value, sizeof(T), params_[p].size, file) == params_[p].size;
    }
    ok = (std::fclose(file) == 0) && ok;
    if(!ok) {
      error_ = "write to " + path + " failed";
    }
    return ok;
  }

  bool Load(FILE* file, const std::string& path) {
    if(!compiled_) {
      error_ = "compile the model before loading weights into it";
      if(file) { std::fclose(file); }
      return false;
    }
    if(!file) {
      error_ = "cannot open " + path;
      return false;
    }
    char magic[8];
    uint64_t scalar = 0, count = 0, sig = 0;
    bool ok = std::fread(magic, 1, 8, file) == 8 && std::memcmp(magic, "OBJKCNN1", 8) == 0 &&
              std::fread(&scalar, 8, 1, file) == 1 && std::fread(&count, 8, 1, file) == 1 &&
              std::fread(&sig, 8, 1, file) == 1;
    if(!ok) {
      std::fclose(file);
      error_ = path + " is not a CNN weights file";
      return false;
    }
    if(scalar != sizeof(T) || count != ParamCount() || sig != Signature()) {
      std::fclose(file);
      error_ = "the weights in " + path + " were saved from a different architecture";
      return false;
    }
    // read everything first so a truncated file leaves the model untouched
    std::vector<T> values((size_t)count);
    ok = std::fread(values.data(), sizeof(T), (size_t)count, file) == (size_t)count;
    std::fclose(file);
    if(!ok) {
      error_ = path + " is truncated";
      return false;
    }
    size_t k = 0;
    for(size_t p = 0; p < params_.size(); ++p) {
      std::memcpy(params_[p].value, values.data() + k, sizeof(T) * params_[p].size);
      k += params_[p].size;
    }
    return true;
  }
};

//
// Turns one decoded 8-bit image (rows x cols x channels, interleaved, BGR order
// as OpenCV stores it) into one float sample, CHW, RGB, scaled to [0, 1], in
// `out` (out_c x rows x cols), then applies the augmentations that fired:
//   rotation by a random angle in [-max_angle, max_angle] about the centre (bilinear, zeros outside),
//   horizontal flip, brightness by a random factor in [bmin, bmax] (clipped to [0, 1]).
// Whether each fires, and its value, are drawn from (seed, key), so a given
// (seed, epoch, image) always gets the same augmentation.
//
struct AugmentConfig {
  bool enabled;
  double p_flip, p_rotate, max_angle, p_bright, bmin, bmax;
  uint64_t seed, key;
  AugmentConfig() : enabled(false), p_flip(0), p_rotate(0), max_angle(0), p_bright(0), bmin(1), bmax(1), seed(0), key(0) {}
};

template<class T>
static bool StageImage(const unsigned char* src, int rows, int cols, int src_c, int out_c, T* out, const AugmentConfig& aug) {
  if(!src || rows < 1 || cols < 1 || (src_c != 1 && src_c != 3 && src_c != 4) || (out_c != 1 && out_c != 3)) {
    return false;
  }
  const size_t plane = (size_t)rows * cols;
  for(size_t p = 0; p < plane; ++p) {
    const unsigned char* px = src + p * src_c;
    float r, g, b;
    if(src_c == 1) {
      r = g = b = px[0] * (1.0f / 255.0f);
    } else {
      b = px[0] * (1.0f / 255.0f);
      g = px[1] * (1.0f / 255.0f);
      r = px[2] * (1.0f / 255.0f);
    }
    if(out_c == 3) {
      out[p] = (T)r;
      out[plane + p] = (T)g;
      out[2 * plane + p] = (T)b;
    } else {
      out[p] = (T)(0.299f * r + 0.587f * g + 0.114f * b);
    }
  }
  if(!aug.enabled) {
    return true;
  }

  Rng rng(Mix64(aug.seed) ^ Mix64(aug.key + 0xA5A5A5A5ULL));
  const bool rotate = rng.Uniform() < aug.p_rotate;
  const double angle = rng.Uniform(-aug.max_angle, aug.max_angle);
  const bool flip = rng.Uniform() < aug.p_flip;
  const bool bright = rng.Uniform() < aug.p_bright;
  const double factor = rng.Uniform(aug.bmin, aug.bmax);

  if(rotate && angle != 0.0) {
    const double rad = angle * 3.14159265358979323846 / 180.0;
    const double ca = std::cos(rad), sa = std::sin(rad);
    const double cx = (cols - 1) * 0.5, cy = (rows - 1) * 0.5;
    std::vector<T> tmp((size_t)out_c * plane);
    for(int c = 0; c < out_c; ++c) {
      const T* in = out + c * plane;
      T* dst = tmp.data() + c * plane;
      for(int y = 0; y < rows; ++y) {
        for(int x = 0; x < cols; ++x) {
          // inverse mapping: where in the source does this output pixel come from
          const double dx = x - cx, dy = y - cy;
          const double sx = ca * dx + sa * dy + cx, sy = -sa * dx + ca * dy + cy;
          const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
          const double fx = sx - x0, fy = sy - y0;
          double v = 0.0;
          for(int j = 0; j < 2; ++j) {
            for(int i = 0; i < 2; ++i) {
              const int xx = x0 + i, yy = y0 + j;
              if(xx >= 0 && xx < cols && yy >= 0 && yy < rows) {
                v += (i ? fx : 1.0 - fx) * (j ? fy : 1.0 - fy) * (double)in[(size_t)yy * cols + xx];
              }
            }
          }
          dst[(size_t)y * cols + x] = (T)v;
        }
      }
    }
    std::copy(tmp.begin(), tmp.end(), out);
  }
  if(flip) {
    for(int c = 0; c < out_c; ++c) {
      for(int y = 0; y < rows; ++y) {
        T* row = out + c * plane + (size_t)y * cols;
        std::reverse(row, row + cols);
      }
    }
  }
  if(bright) {
    for(size_t j = 0; j < (size_t)out_c * plane; ++j) {
      const double v = (double)out[j] * factor;
      out[j] = (T)(v > 1.0 ? 1.0 : (v < 0.0 ? 0.0 : v));
    }
  }
  return true;
}

// a seeded Fisher-Yates shuffle of 0..n-1, the same on every platform
static inline std::vector<long long> Permutation(long long n, uint64_t seed) {
  std::vector<long long> p((size_t)(n > 0 ? n : 0));
  for(long long i = 0; i < (long long)p.size(); ++i) {
    p[(size_t)i] = i;
  }
  Rng rng(seed);
  for(long long i = (long long)p.size() - 1; i > 0; --i) {
    const long long j = (long long)(rng.Next() % (uint64_t)(i + 1));
    std::swap(p[(size_t)i], p[(size_t)j]);
  }
  return p;
}

}  // namespace objk_cnn

#endif
