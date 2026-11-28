/***************************************************************************
 * Convolutional neural network layers (System.ML.Cnn): the Objeck entry points
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

// Included once, at the end of matrix.cpp, so the CNN entry points ship in
// libobjk_ml and the existing build of that library (Visual Studio, Xcode,
// build_linux.sh) compiles them with no new project entries. The engine is in
// cnn.h and knows nothing of the VM.
//
// A model lives in native memory and is named by an integer handle from a
// table, never by an address: a stale or wrong handle is a lookup miss that
// answers "refused", not a wild pointer. Every entry point returns a failure
// value (0, -1.0, Nil) for bad input and records the reason for
// ml_cnn_last_error; none of them ends the program.

#ifndef __OBJK_CNN_API_H__
#define __OBJK_CNN_API_H__

#include "cnn.h"
#include "cuda/objk_ml_cuda.h"
#include <map>
#include <memory>
#include <mutex>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace objk_cnn_api {

using namespace objk_cnn;

//
// The optional GPU backend. libobjk_ml does not link CUDA: the first time a "cuda" device is asked
// for, the library libobjk_ml_cuda (built apart from core/lib/matrix/cuda, not shipped) is loaded
// with LoadLibrary / dlopen and its entry points are looked up by name. When it, or a GPU, is not
// there the caller gets a failure value and a reason, and the model stays on the CPU.
//
struct CudaLib {
  bool loaded;
  void* module;
#define OBJK_CUDA_DECLARE(ret, name, args) ret (*name) args = nullptr;
  OBJK_CUDA_FUNCTIONS(OBJK_CUDA_DECLARE)
#undef OBJK_CUDA_DECLARE
  CudaLib() : loaded(false), module(nullptr) {}
};

static CudaLib g_cuda;
static std::mutex g_cuda_mutex;
static bool g_cuda_down = false;  // the library is shutting down: nothing may call into the GPU library any more

static std::string EnvVar(const char* name) {
#ifdef _WIN32
  char buffer[4096];
  const DWORD n = GetEnvironmentVariableA(name, buffer, (DWORD)sizeof(buffer));
  return n > 0 && n < sizeof(buffer) ? std::string(buffer, n) : std::string();
#else
  const char* value = getenv(name);
  return value ? std::string(value) : std::string();
#endif
}

static void* CudaOpen(const std::string& path) {
#ifdef _WIN32
  const std::wstring wide(path.begin(), path.end());
  if(GetFileAttributesW(wide.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return nullptr;
  }
  // the CUDA runtime DLLs (cublas64_*.dll) live in the toolkit's bin folder, which need not be on PATH
  static bool dirs_added = false;
  if(!dirs_added) {
    dirs_added = true;
    const std::string base = EnvVar("CUDA_PATH");
    if(!base.empty()) {
      const char* sub[] = { "\\bin\\x64", "\\bin" };
      for(int i = 0; i < 2; ++i) {
        const std::string dir = base + sub[i];
        const std::wstring wdir(dir.begin(), dir.end());
        AddDllDirectory(wdir.c_str());
      }
    }
  }
  HMODULE module = LoadLibraryExW(wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS);
  if(!module) {
    module = LoadLibraryW(wide.c_str());  // the legacy search order, which includes PATH
  }
  return (void*)module;
#else
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

// the directory libobjk_ml itself was loaded from, with a trailing separator
static std::string OwnDirectory() {
#ifdef _WIN32
  HMODULE self = nullptr;
  if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&OwnDirectory, &self)) {
    wchar_t buffer[4096];
    const DWORD n = GetModuleFileNameW(self, buffer, 4096);
    if(n > 0 && n < 4096) {
      std::wstring path(buffer, n);
      const size_t slash = path.find_last_of(L"\\/");
      std::string narrow;
      for(size_t i = 0; slash != std::wstring::npos && i <= slash; ++i) {
        narrow += path[i] < 128 ? (char)path[i] : '?';
      }
      return narrow;
    }
  }
#else
  Dl_info info;
  if(dladdr((void*)&OwnDirectory, &info) && info.dli_fname) {
    const std::string path(info.dli_fname);
    const size_t slash = path.find_last_of('/');
    if(slash != std::string::npos) {
      return path.substr(0, slash + 1);
    }
  }
#endif
  return std::string();
}

// loads the GPU library once; on failure says why. Caller holds g_cuda_mutex.
static bool CudaLoad(std::string& why) {
  if(g_cuda.loaded) {
    return true;
  }
#ifdef _WIN32
  const char* name = "libobjk_ml_cuda.dll";
#elif defined(__APPLE__)
  const char* name = "libobjk_ml_cuda.dylib";
#else
  const char* name = "libobjk_ml_cuda.so";
#endif
  std::vector<std::string> candidates;
  const std::string env = EnvVar("OBJK_CUDA_LIB");
  if(!env.empty()) {
    candidates.push_back(env);
  }
  candidates.push_back(OwnDirectory() + name);
#ifndef _WIN32
  candidates.push_back(name);  // the loader's own search path
#endif
  void* module = nullptr;
  for(size_t i = 0; !module && i < candidates.size(); ++i) {
    module = CudaOpen(candidates[i]);
  }
  if(!module) {
    why = std::string("the CUDA library ") + name + " was not found or could not be loaded (it is built apart from Objeck, see docs/AI.md; "
          "set OBJK_CUDA_LIB to its path, and make sure the CUDA runtime libraries can be found)";
    return false;
  }
  CudaLib lib;
  lib.module = module;
  bool ok = true;
#ifdef _WIN32
#define OBJK_CUDA_LOOKUP(ret, fn, args) lib.fn = (ret (*) args)GetProcAddress((HMODULE)module, #fn); ok = ok && lib.fn != nullptr;
#else
#define OBJK_CUDA_LOOKUP(ret, fn, args) lib.fn = (ret (*) args)dlsym(module, #fn); ok = ok && lib.fn != nullptr;
#endif
  OBJK_CUDA_FUNCTIONS(OBJK_CUDA_LOOKUP)
#undef OBJK_CUDA_LOOKUP
  if(!ok || lib.objk_cuda_abi() != OBJK_CUDA_ABI) {
    why = "the CUDA library is not the version this libobjk_ml expects; rebuild it from core/lib/matrix/cuda";
    // left loaded: unloading a half-initialised CUDA library is not worth the risk
    // coverity[leaked_storage] the library handle is deliberately kept for the process lifetime
    return false;
  }
  lib.loaded = true;
  g_cuda = lib;
  return true;
}

// the loaded library, or null with the reason
static const CudaLib* CudaGet(std::string& why) {
  std::lock_guard<std::mutex> lock(g_cuda_mutex);
  if(g_cuda_down) {
    why = "the library is shutting down";
    return nullptr;
  }
  return CudaLoad(why) ? &g_cuda : nullptr;
}

struct LayerSpec {
  int kind, a, b, c, d;
  double rate;
  unsigned long long seed;
};

class ModelBase {
 public:
  virtual ~ModelBase() {}
  std::string error;
  Shape input;
  virtual bool AddConv(int f, int k, int s, bool same) = 0;
  virtual bool AddMaxPool(int size, int stride) = 0;
  virtual bool AddReLU() = 0;
  virtual bool AddFlatten() = 0;
  virtual bool AddDense(int units) = 0;
  virtual bool AddDropout(double rate) = 0;
  virtual bool Compile(int opt, double lr, double b1, double b2, double eps, double momentum, int init) = 0;
  virtual void SetLearningRate(double lr) = 0;
  virtual void SetTraining(bool t) = 0;
  virtual void ResetRng() = 0;
  virtual size_t ParamCount() = 0;
  virtual std::string Summary() = 0;
  virtual double Train(const double* x, const double* y, int n) = 0;
  virtual double Eval(const double* x, const double* y, int n) = 0;
  virtual double Gradients(const double* x, const double* y, int n, bool training, bool input_grad) = 0;
  virtual bool Predict(const double* x, int n, bool logits, std::vector<double>& out) = 0;
  virtual bool Stage(const unsigned char* src, int rows, int cols, int src_c, int slot, int total, const AugmentConfig& aug) = 0;
  virtual double TrainStaged(const double* y, int n) = 0;
  virtual double EvalStaged(const double* y, int n) = 0;
  virtual bool PredictStaged(int n, bool logits, std::vector<double>& out) = 0;
  virtual bool GetWeights(std::vector<double>& out) = 0;
  virtual bool SetWeights(const double* in, size_t count) = 0;
  virtual bool GetGrads(std::vector<double>& out) = 0;
  virtual bool Activation(int layer, std::vector<double>& out) = 0;
  virtual bool InputGrad(std::vector<double>& out) = 0;
  virtual bool Save(FILE* file, const std::string& name) = 0;
  virtual bool Load(FILE* file, const std::string& name) = 0;
  virtual int LastCorrect() = 0;
  virtual bool Compiled() = 0;
  virtual bool IsDouble() = 0;
  // the device the model runs on: 0 = CPU (always possible), 1 = CUDA (needs the optional GPU library)
  virtual bool SetDevice(int kind, int index, bool tf32) {
    (void)index; (void)tf32;
    return kind == 0;
  }
  virtual int Device() { return 0; }
  virtual int DeviceIndex() { return 0; }
  virtual size_t DeviceBytes() { return 0; }
  virtual void Shutdown() {}
};

template<class T>
class ModelImpl : public ModelBase {
  Sequential<T> net_;
  std::vector<T> x_, y_;

  // the optional GPU session (float models only). While it exists the weights live on the GPU and the
  // CPU copy in net_ is only a shadow: it holds the architecture, the initial weights, and the weights
  // as of the last time they were fetched (host_stale_ says the GPU has moved on).
  const CudaLib* cuda_;
  void* gpu_;
  int gpu_index_;
  bool tf32_, host_stale_;
  int gpu_correct_;
  uint64_t seed_;
  std::vector<LayerSpec> specs_;
  bool have_compile_;
  int c_opt_, c_init_;
  double c_lr_, c_b1_, c_b2_, c_eps_, c_mom_;

  void Convert(const double* src, size_t count, std::vector<T>& dst) {
    if(dst.size() < count) {
      dst.resize(count);
    }
    for(size_t i = 0; i < count; ++i) {
      dst[i] = (T)src[i];
    }
  }

  // x must hold exactly n whole images and y exactly n labels
  bool Prepare(const double* x, const double* y, int n) {
    if(n < 1) {
      error = "an empty batch";
      return false;
    }
    Convert(x, (size_t)n * input.Size(), x_);
    if(y) {
      Convert(y, (size_t)n, y_);
    }
    return true;
  }

  bool Check(bool ok) {
    if(!ok) {
      error = net_.Error();
    }
    return ok;
  }

  static double SigmoidD(double z) {
    if(z >= 0.0) {
      return 1.0 / (1.0 + std::exp(-z));
    }
    const double e = std::exp(z);
    return e / (1.0 + e);
  }

  //
  // GPU session
  //
  bool GpuFail() {
    error = std::string("CUDA: ") + (gpu_ ? cuda_->objk_cuda_net_error(gpu_) : cuda_->objk_cuda_last_error());
    return false;
  }

  void GpuDestroy() {
    if(gpu_ && cuda_) {
      std::lock_guard<std::mutex> lock(g_cuda_mutex);
      if(!g_cuda_down) {
        cuda_->objk_cuda_net_destroy(gpu_);
      }
    }
    gpu_ = nullptr;
    host_stale_ = false;
  }

  // the weights on the GPU, as float, into the CPU shadow
  bool SyncToHost() {
    if(!gpu_ || !host_stale_ || !net_.Compiled()) {
      return true;
    }
    const size_t count = net_.ParamCount();
    std::vector<float> w(count);
    if(!cuda_->objk_cuda_net_get_params(gpu_, w.data(), count)) {
      return GpuFail();
    }
    std::vector<double> d(count);
    for(size_t i = 0; i < count; ++i) {
      d[i] = (double)w[i];
    }
    if(!net_.SetWeights(d.data(), count)) {
      error = net_.Error();
      return false;
    }
    host_stale_ = false;
    return true;
  }

  // the CPU shadow's weights to the GPU
  bool UploadWeights() {
    if(!gpu_ || !net_.Compiled()) {
      return true;
    }
    std::vector<double> d;
    net_.GetWeights(d);
    std::vector<float> w(d.size());
    for(size_t i = 0; i < d.size(); ++i) {
      w[i] = (float)d[i];
    }
    if(!cuda_->objk_cuda_net_set_params(gpu_, w.data(), w.size())) {
      return GpuFail();
    }
    host_stale_ = false;
    return true;
  }

  bool GpuCompile() {
    if(!cuda_->objk_cuda_net_compile(gpu_, c_opt_, c_lr_, c_b1_, c_b2_, c_eps_, c_mom_)) {
      return GpuFail();
    }
    return UploadWeights();
  }

  // adds one layer to the CPU model and the GPU one; they validate alike, so a GPU refusal is a bug or
  // a device fault, and it drops the model back to the CPU rather than let the two disagree
  bool AddLayer(bool cpu_ok, const LayerSpec& spec) {
    if(!Check(cpu_ok)) {
      return false;
    }
    specs_.push_back(spec);
    if(gpu_ && !cuda_->objk_cuda_net_add(gpu_, spec.kind, spec.a, spec.b, spec.c, spec.d, spec.rate, spec.seed)) {
      GpuFail();
      GpuDestroy();
      error += " (the model is back on the CPU)";
      return false;
    }
    return true;
  }

  // a float pointer to the batch the GPU reads; only reached for float models
  static const float* AsFloat(const T* p) { return reinterpret_cast<const float*>(p); }

  // runs one pass on the GPU; the loss, or -1.0 with the reason in `error`
  double GpuRun(int mode, const T* x, const T* y, int n, bool training, bool input_grad, std::vector<float>* logits) {
    double loss = 0.0;
    int correct = 0;
    if(logits) {
      logits->resize((size_t)n);
    }
    if(!cuda_->objk_cuda_net_run(gpu_, mode, AsFloat(x), AsFloat(y), n, training ? 1 : 0, input_grad ? 1 : 0, &loss, &correct,
                                 logits ? logits->data() : nullptr)) {
      GpuFail();
      return -1.0;
    }
    if(mode == 0) {
      host_stale_ = true;
    }
    if(mode != 3) {
      gpu_correct_ = correct;
    }
    return loss;
  }

  bool GpuPredict(const T* x, int n, bool logits, std::vector<double>& out) {
    std::vector<float> z;
    if(GpuRun(3, x, nullptr, n, net_.Training(), false, &z) < 0.0) {
      return false;
    }
    out.resize((size_t)n);
    for(int i = 0; i < n; ++i) {
      out[(size_t)i] = logits ? (double)z[(size_t)i] : SigmoidD((double)z[(size_t)i]);
    }
    return true;
  }

 public:
  ModelImpl(const Shape& in, uint64_t seed) : net_(in, seed), cuda_(nullptr), gpu_(nullptr), gpu_index_(0), tf32_(false), host_stale_(false),
    gpu_correct_(0), seed_(seed), have_compile_(false), c_opt_(0), c_init_(0), c_lr_(0), c_b1_(0), c_b2_(0), c_eps_(0), c_mom_(0) {
    input = in;
  }

  ~ModelImpl() { GpuDestroy(); }

  bool AddConv(int f, int k, int s, bool same) {
    LayerSpec spec = { 0, f, k, s, same ? 1 : 0, 0.0, 0 };
    return AddLayer(net_.AddConv(f, k, s, same), spec);
  }
  bool AddMaxPool(int size, int stride) {
    LayerSpec spec = { 1, size, stride, 0, 0, 0.0, 0 };
    return AddLayer(net_.AddMaxPool(size, stride), spec);
  }
  bool AddReLU() {
    LayerSpec spec = { 2, 0, 0, 0, 0, 0.0, 0 };
    return AddLayer(net_.AddReLU(), spec);
  }
  bool AddFlatten() {
    LayerSpec spec = { 3, 0, 0, 0, 0, 0.0, 0 };
    return AddLayer(net_.AddFlatten(), spec);
  }
  bool AddDense(int units) {
    LayerSpec spec = { 4, units, 0, 0, 0, 0.0, 0 };
    return AddLayer(net_.AddDense(units), spec);
  }
  bool AddDropout(double rate) {
    // the same stream the CPU layer draws: Sequential::AddDropout derives it from the model seed and the layer's index
    const unsigned long long layer_seed = Mix64(seed_ ^ (0xD20D0000ULL + net_.Layers()));
    LayerSpec spec = { 5, 0, 0, 0, 0, rate, layer_seed };
    return AddLayer(net_.AddDropout(rate), spec);
  }
  bool Compile(int opt, double lr, double b1, double b2, double eps, double momentum, int init) {
    if(!Check(net_.Compile(opt, lr, b1, b2, eps, momentum, init))) {
      return false;
    }
    have_compile_ = true;
    c_opt_ = opt; c_init_ = init; c_lr_ = lr; c_b1_ = b1; c_b2_ = b2; c_eps_ = eps; c_mom_ = momentum;
    if(gpu_ && !GpuCompile()) {
      GpuDestroy();
      error += " (the model is back on the CPU)";
      return false;
    }
    return true;
  }
  void SetLearningRate(double lr) {
    net_.SetLearningRate(lr);
    if(lr > 0.0) {
      c_lr_ = lr;
      if(gpu_) {
        cuda_->objk_cuda_net_set_lr(gpu_, lr);
      }
    }
  }
  void SetTraining(bool t) { net_.SetTraining(t); }
  void ResetRng() {
    net_.ResetRng();
    if(gpu_) {
      cuda_->objk_cuda_net_reset_rng(gpu_);
    }
  }
  size_t ParamCount() { return net_.ParamCount(); }
  std::string Summary() { return net_.Summary(); }

  double Train(const double* x, const double* y, int n) {
    if(!Prepare(x, y, n)) {
      return -1.0;
    }
    if(gpu_) {
      return GpuRun(0, x_.data(), y_.data(), n, true, false, nullptr);
    }
    const double loss = net_.TrainBatch(x_.data(), y_.data(), n);
    if(loss < 0.0) {
      error = net_.Error();
    }
    return loss;
  }

  double Eval(const double* x, const double* y, int n) {
    if(!Prepare(x, y, n)) {
      return -1.0;
    }
    if(gpu_) {
      return GpuRun(1, x_.data(), y_.data(), n, false, false, nullptr);
    }
    const double loss = net_.EvalBatch(x_.data(), y_.data(), n);
    if(loss < 0.0) {
      error = net_.Error();
    }
    return loss;
  }

  double Gradients(const double* x, const double* y, int n, bool training, bool input_grad) {
    if(!Prepare(x, y, n)) {
      return -1.0;
    }
    if(gpu_) {
      return GpuRun(2, x_.data(), y_.data(), n, training, input_grad, nullptr);
    }
    const double loss = net_.ComputeGradients(x_.data(), y_.data(), n, training, input_grad);
    if(loss < 0.0) {
      error = net_.Error();
    }
    return loss;
  }

  bool Predict(const double* x, int n, bool logits, std::vector<double>& out) {
    if(!net_.Compiled()) {
      error = "the model is not compiled";
      return false;
    }
    if(!Prepare(x, nullptr, n)) {
      return false;
    }
    if(gpu_) {
      return GpuPredict(x_.data(), n, logits, out);
    }
    const T* z = net_.Forward(x_.data(), n, net_.Training());
    out.resize(n);
    if(logits) {
      for(int i = 0; i < n; ++i) {
        out[i] = (double)z[i];
      }
    } else {
      net_.Probabilities(z, n, out);
    }
    return true;
  }

  bool Stage(const unsigned char* src, int rows, int cols, int src_c, int slot, int total, const AugmentConfig& aug) {
    if(!net_.Compiled()) {
      error = "the model is not compiled";
      return false;
    }
    if(total < 1 || slot < 0 || slot >= total) {
      error = "image slot " + std::to_string(slot) + " is outside the batch of " + std::to_string(total);
      return false;
    }
    if(input.c != 1 && input.c != 3) {
      error = "images can be staged only for a model with 1 or 3 input channels";
      return false;
    }
    if(rows != input.h || cols != input.w) {
      error = "image is " + std::to_string(cols) + "x" + std::to_string(rows) + ", the model takes " +
              std::to_string(input.w) + "x" + std::to_string(input.h);
      return false;
    }
    if(net_.StagingCount() != total || slot == 0) {
      net_.Staging(total);
    }
    T* out = net_.StagingData() + (size_t)slot * input.Size();
    if(!StageImage<T>(src, rows, cols, src_c, input.c, out, aug)) {
      error = "unsupported image (8-bit with 1, 3 or 4 channels expected)";
      return false;
    }
    return true;
  }

  double TrainStaged(const double* y, int n) {
    if(n < 1 || n != net_.StagingCount()) {
      error = "the labels (" + std::to_string(n) + ") do not match the staged images (" + std::to_string(net_.StagingCount()) + ")";
      return -1.0;
    }
    Convert(y, (size_t)n, y_);
    if(gpu_) {
      return GpuRun(0, net_.StagingData(), y_.data(), n, true, false, nullptr);
    }
    const double loss = net_.TrainBatch(net_.StagingData(), y_.data(), n);
    if(loss < 0.0) {
      error = net_.Error();
    }
    return loss;
  }

  double EvalStaged(const double* y, int n) {
    if(n < 1 || n != net_.StagingCount()) {
      error = "the labels (" + std::to_string(n) + ") do not match the staged images (" + std::to_string(net_.StagingCount()) + ")";
      return -1.0;
    }
    Convert(y, (size_t)n, y_);
    if(gpu_) {
      return GpuRun(1, net_.StagingData(), y_.data(), n, false, false, nullptr);
    }
    const double loss = net_.EvalBatch(net_.StagingData(), y_.data(), n);
    if(loss < 0.0) {
      error = net_.Error();
    }
    return loss;
  }

  bool PredictStaged(int n, bool logits, std::vector<double>& out) {
    if(!net_.Compiled() || n < 1 || n != net_.StagingCount()) {
      error = "no staged images, or a count that does not match them";
      return false;
    }
    if(gpu_) {
      return GpuPredict(net_.StagingData(), n, logits, out);
    }
    const T* z = net_.Forward(net_.StagingData(), n, net_.Training());
    out.resize(n);
    if(logits) {
      for(int i = 0; i < n; ++i) {
        out[i] = (double)z[i];
      }
    } else {
      net_.Probabilities(z, n, out);
    }
    return true;
  }

  bool GetWeights(std::vector<double>& out) {
    if(!SyncToHost()) {
      return false;
    }
    return net_.GetWeights(out);
  }

  bool SetWeights(const double* in, size_t count) {
    if(!Check(net_.SetWeights(in, count))) {
      return false;
    }
    return !gpu_ || UploadWeights();
  }

  bool GetGrads(std::vector<double>& out) {
    if(!gpu_) {
      return net_.GetGrads(out);
    }
    const size_t count = net_.ParamCount();
    std::vector<float> g(count);
    if(!cuda_->objk_cuda_net_get_grads(gpu_, g.data(), count)) {
      return GpuFail();
    }
    out.assign(g.begin(), g.end());
    return true;
  }

  bool Activation(int layer, std::vector<double>& out) {
    if(!gpu_) {
      return net_.Activation(layer, out);
    }
    const size_t count = cuda_->objk_cuda_net_activation_size(gpu_, layer);
    if(count == 0) {
      return false;
    }
    std::vector<float> a(count);
    if(!cuda_->objk_cuda_net_get_activation(gpu_, layer, a.data(), count)) {
      return GpuFail();
    }
    out.assign(a.begin(), a.end());
    return true;
  }

  bool InputGrad(std::vector<double>& out) {
    if(!gpu_) {
      return net_.InputGrad(out);
    }
    const size_t count = cuda_->objk_cuda_net_input_grad_size(gpu_);
    if(count == 0) {
      return false;
    }
    std::vector<float> g(count);
    if(!cuda_->objk_cuda_net_get_input_grad(gpu_, g.data(), count)) {
      return GpuFail();
    }
    out.assign(g.begin(), g.end());
    return true;
  }

  // a weights file is the same bytes whichever device wrote it, so a GPU model's file loads on the CPU and back
  bool Save(FILE* file, const std::string& name) {
    if(!SyncToHost()) {
      if(file) { std::fclose(file); }
      return false;
    }
    return Check(net_.Save(file, name));
  }

  bool Load(FILE* file, const std::string& name) {
    if(!Check(net_.Load(file, name))) {
      return false;
    }
    return !gpu_ || UploadWeights();
  }

  int LastCorrect() { return gpu_ ? gpu_correct_ : net_.LastCorrect(); }
  bool Compiled() { return net_.Compiled(); }
  bool IsDouble() { return sizeof(T) == sizeof(double); }

  // 0 = cpu, 1 = cuda. Moving a trained model keeps its weights (not the Adam moments: they restart from zero).
  bool SetDevice(int kind, int index, bool tf32) {
    if(kind == 0) {
      if(gpu_ && !SyncToHost()) {
        return false;
      }
      GpuDestroy();
      return true;
    }
    if(sizeof(T) != sizeof(float)) {
      error = "the float64 reference model runs on the CPU only";
      return false;
    }
    if(gpu_ && gpu_index_ == index && tf32_ == tf32) {
      return true;
    }
    std::string why;
    const CudaLib* lib = CudaGet(why);
    if(!lib) {
      error = std::move(why);
      return false;
    }
    const int count = lib->objk_cuda_device_count();
    if(count < 1 || index < 0 || index >= count) {
      error = count < 1 ? std::string("no CUDA device: ") + lib->objk_cuda_last_error() : "no CUDA device " + std::to_string(index);
      return false;
    }
    if(gpu_ && !SyncToHost()) {
      return false;
    }
    void* net = lib->objk_cuda_net_create(index, input.c, input.h, input.w, tf32 ? 1 : 0);
    if(!net) {
      error = std::string("CUDA: ") + lib->objk_cuda_last_error();
      return false;
    }
    // the new session must be complete before the old one goes
    const CudaLib* old_lib = cuda_;
    void* old_gpu = gpu_;
    cuda_ = lib;
    gpu_ = net;
    bool ok = true;
    for(size_t i = 0; ok && i < specs_.size(); ++i) {
      const LayerSpec& s = specs_[i];
      ok = lib->objk_cuda_net_add(net, s.kind, s.a, s.b, s.c, s.d, s.rate, s.seed) != 0;
    }
    ok = ok && (!have_compile_ || GpuCompile());
    if(!ok) {
      GpuFail();
      const std::string reason = error;
      lib->objk_cuda_net_destroy(net);
      cuda_ = old_lib;
      gpu_ = old_gpu;
      error = reason;
      return false;
    }
    if(old_gpu && old_lib) {
      std::lock_guard<std::mutex> lock(g_cuda_mutex);
      if(!g_cuda_down) {
        old_lib->objk_cuda_net_destroy(old_gpu);
      }
    }
    gpu_index_ = index;
    tf32_ = tf32;
    host_stale_ = false;
    return true;
  }
  int Device() { return gpu_ ? 1 : 0; }
  int DeviceIndex() { return gpu_ ? gpu_index_ : 0; }
  size_t DeviceBytes() { return gpu_ ? cuda_->objk_cuda_net_device_bytes(gpu_) : 0; }
  // called when the library unloads: the GPU session has to go while its library is still there
  void Shutdown() { GpuDestroy(); }
};

static std::mutex g_mutex;
static std::map<size_t, std::shared_ptr<ModelBase>> g_models;
static size_t g_next_id = 1;
static std::string g_global_error;

static std::shared_ptr<ModelBase> Find(size_t id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::map<size_t, std::shared_ptr<ModelBase>>::iterator it = g_models.find(id);
  return it == g_models.end() ? std::shared_ptr<ModelBase>() : it->second;
}

// the argument as a 1-D array of Float (double) or Byte; false for Nil or another rank
static bool ArrayArg(VMContext& context, size_t index, size_t*& array, size_t& size) {
  size_t* holder = APITools_GetArray(context, index);
  if(!holder) {
    return false;
  }
  array = (size_t*)holder[0];
  if(!array || array[1] != 1) {
    return false;
  }
  size = array[0];
  return true;
}

static bool FloatArg(VMContext& context, size_t index, const double*& data, size_t& size) {
  size_t* array = nullptr;
  if(!ArrayArg(context, index, array, size)) {
    return false;
  }
  data = (const double*)(array + 3);
  return true;
}

static bool ByteArg(VMContext& context, size_t index, const unsigned char*& data, size_t& size) {
  size_t* array = nullptr;
  if(!ArrayArg(context, index, array, size)) {
    return false;
  }
  data = (const unsigned char*)(array + 3);
  return true;
}

static void ReturnFloats(VMContext& context, const std::vector<double>& values) {
  size_t* array = APITools_MakeFloatArray(context, values.size());
  double* out = (double*)(array + 3);
  for(size_t i = 0; i < values.size(); ++i) {
    out[i] = values[i];
  }
  size_t* holder = APITools_GetObjectValue(context, 0);
  if(holder) {
    holder[0] = (size_t)array;
    APITools_SetObjectValue(context, 0, holder);
  }
}

static int IntArg(VMContext& context, size_t index) {
  const long long v = (long long)APITools_GetIntValue(context, index);
  return v > 2000000000LL ? 2000000000 : (v < -2000000000LL ? -2000000000 : (int)v);
}

// "no result": the holder object the caller passed in (a FloatArrayRef or IntArrayRef)
// stays, its array is left unset, and Get() on it answers Nil
static void Nothing(VMContext& context) {
  size_t* holder = APITools_GetObjectValue(context, 0);
  if(holder) {
    holder[0] = 0;
  }
}

}  // namespace objk_cnn_api

// called from unload_lib (matrix.cpp): stops the worker threads before the library is unmapped, and
// gives back the GPU memory of any model that was not closed, while the GPU library is still loaded
void objk_cnn_shutdown() {
  objk_cnn::Pool::Instance().Shutdown();
  std::vector<std::shared_ptr<objk_cnn_api::ModelBase>> models;
  {
    std::lock_guard<std::mutex> lock(objk_cnn_api::g_mutex);
    for(std::map<size_t, std::shared_ptr<objk_cnn_api::ModelBase>>::iterator it = objk_cnn_api::g_models.begin(); it != objk_cnn_api::g_models.end(); ++it) {
      models.push_back(it->second);
    }
  }
  for(size_t i = 0; i < models.size(); ++i) {
    models[i]->Shutdown();
  }
  std::lock_guard<std::mutex> lock(objk_cnn_api::g_cuda_mutex);
  objk_cnn_api::g_cuda_down = true;
}

#ifdef _WIN32
#define OBJK_CNN_EXPORT __declspec(dllexport)
#else
#define OBJK_CNN_EXPORT
#endif

extern "C" {
using namespace objk_cnn_api;

// ml_cnn_create(ret Int handle; channels, height, width, seed, use_double) -> 0 when refused
OBJK_CNN_EXPORT void ml_cnn_create(VMContext& context) {
  const int c = IntArg(context, 1), h = IntArg(context, 2), w = IntArg(context, 3);
  const uint64_t seed = (uint64_t)APITools_GetIntValue(context, 4);
  const bool use_double = APITools_GetIntValue(context, 5) != 0;
  APITools_SetIntValue(context, 0, 0);
  if(c < 1 || c > 4096 || h < 1 || w < 1 || (long long)h * w * c > 100000000LL) {
    g_global_error = "the input must have 1 to 4096 channels, at least 1 x 1, and at most 100M values";
    return;
  }
  try {
    std::shared_ptr<ModelBase> model;
    if(use_double) {
      model.reset(new ModelImpl<double>(Shape(c, h, w), seed));
    } else {
      model.reset(new ModelImpl<float>(Shape(c, h, w), seed));
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const size_t id = g_next_id++;
    g_models[id] = std::move(model);
    APITools_SetIntValue(context, 0, id);
  } catch(...) {
    g_global_error = "out of memory";
  }
}

// ml_cnn_release(id)
OBJK_CNN_EXPORT void ml_cnn_release(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  std::shared_ptr<ModelBase> keep;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::map<size_t, std::shared_ptr<ModelBase>>::iterator it = g_models.find(id);
    if(it != g_models.end()) {
      keep = it->second;
      g_models.erase(it);
    }
  }
}

// ml_cnn_add(ret Int ok; id, kind, a, b, c, d)  kind: 0 conv(filters,kernel,stride,same) 1 maxpool(size,stride)
// 2 relu 3 flatten 4 dense(units) 5 dropout(rate as Float in arg 6)
OBJK_CNN_EXPORT void ml_cnn_add(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int kind = IntArg(context, 2), a = IntArg(context, 3), b = IntArg(context, 4), c = IntArg(context, 5), d = IntArg(context, 6);
  const double rate = APITools_GetFloatValue(context, 7);
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    g_global_error = "unknown or released model";
    return;
  }
  (void)d;
  bool ok = false;
  try {
    switch(kind) {
    case 0: ok = m->AddConv(a, b, c, d != 0); break;
    case 1: ok = m->AddMaxPool(a, b); break;
    case 2: ok = m->AddReLU(); break;
    case 3: ok = m->AddFlatten(); break;
    case 4: ok = m->AddDense(a); break;
    case 5: ok = m->AddDropout(rate); break;
    default: m->error = "unknown layer kind"; break;
    }
  } catch(...) {
    m->error = "out of memory";
  }
  APITools_SetIntValue(context, 0, ok ? 1 : 0);
}

// ml_cnn_compile(ret Int ok; id, optimizer, init, lr, beta1, beta2, eps, momentum)
OBJK_CNN_EXPORT void ml_cnn_compile(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int opt = IntArg(context, 2), init = IntArg(context, 3);
  const double lr = APITools_GetFloatValue(context, 4), b1 = APITools_GetFloatValue(context, 5),
               b2 = APITools_GetFloatValue(context, 6), eps = APITools_GetFloatValue(context, 7),
               momentum = APITools_GetFloatValue(context, 8);
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  bool ok = false;
  try {
    ok = m->Compile(opt, lr, b1, b2, eps, momentum, init);
  } catch(...) {
    m->error = "out of memory";
  }
  APITools_SetIntValue(context, 0, ok ? 1 : 0);
}

// ml_cnn_set(ret Int ok; id, what, value Float)  what: 0 learning rate, 1 training flag, 2 reset rng
OBJK_CNN_EXPORT void ml_cnn_set(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int what = IntArg(context, 2);
  const double value = APITools_GetFloatValue(context, 3);
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  if(what == 0) {
    if(!(value > 0.0)) {
      m->error = "the learning rate must be positive";
      return;
    }
    m->SetLearningRate(value);
  } else if(what == 1) {
    m->SetTraining(value != 0.0);
  } else if(what == 2) {
    m->ResetRng();
  } else {
    return;
  }
  APITools_SetIntValue(context, 0, 1);
}

// ml_cnn_info(ret Int; id, what)  what: 0 parameter count, 1 compiled, 2 last correct count, 3 is double
OBJK_CNN_EXPORT void ml_cnn_info(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int what = IntArg(context, 2);
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  size_t value = 0;
  switch(what) {
  case 0: value = m->ParamCount(); break;
  case 1: value = m->Compiled() ? 1 : 0; break;
  case 2: value = (size_t)m->LastCorrect(); break;
  case 3: value = m->IsDouble() ? 1 : 0; break;
  case 4: value = (size_t)m->Device(); break;
  case 5: value = (size_t)m->DeviceIndex(); break;
  case 6: value = m->DeviceBytes(); break;
  default: break;
  }
  APITools_SetIntValue(context, 0, value);
}

// ml_cnn_device(ret Int ok; id, kind, index, tf32)  kind: 0 cpu, 1 cuda. A refusal leaves the model where it was.
OBJK_CNN_EXPORT void ml_cnn_device(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int kind = IntArg(context, 2), index = IntArg(context, 3);
  const bool tf32 = APITools_GetIntValue(context, 4) != 0;
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    g_global_error = "unknown or released model";
    return;
  }
  if(kind != 0 && kind != 1) {
    m->error = "unknown device, use cpu, cuda or cuda:N";
    return;
  }
  bool ok = false;
  try {
    APITools_Parked parked(context);
    ok = m->SetDevice(kind, index, tf32);
  } catch(...) {
    m->error = "out of memory";
  }
  APITools_SetIntValue(context, 0, ok ? 1 : 0);
}

// ml_cnn_cuda(ret Int; what, index)  what: 0 device count (0 when the GPU library or a GPU is missing), 1 total MB,
// 2 free MB, 3 compute capability major, 4 minor
OBJK_CNN_EXPORT void ml_cnn_cuda(VMContext& context) {
  const int what = IntArg(context, 1), index = IntArg(context, 2);
  APITools_SetIntValue(context, 0, 0);
  std::string why;
  const CudaLib* lib = CudaGet(why);
  if(!lib) {
    return;
  }
  const int count = lib->objk_cuda_device_count();
  if(what == 0) {
    APITools_SetIntValue(context, 0, count < 0 ? 0 : (size_t)count);
    return;
  }
  size_t total = 0, free_b = 0;
  int major = 0, minor = 0;
  if(index < 0 || index >= count || !lib->objk_cuda_device_info(index, nullptr, 0, &total, &free_b, &major, &minor)) {
    return;
  }
  size_t value = 0;
  switch(what) {
  case 1: value = total / 1048576; break;
  case 2: value = free_b / 1048576; break;
  case 3: value = (size_t)major; break;
  case 4: value = (size_t)minor; break;
  default: break;
  }
  APITools_SetIntValue(context, 0, value);
}

// ml_cnn_cuda_text(ret String; what, index)  what: 0 why CUDA is unavailable (empty when it works), 1 device name
OBJK_CNN_EXPORT void ml_cnn_cuda_text(VMContext& context) {
  const int what = IntArg(context, 1), index = IntArg(context, 2);
  std::string text;
  std::string why;
  const CudaLib* lib = CudaGet(why);
  if(what == 0) {
    if(!lib) {
      text = std::move(why);
    } else if(lib->objk_cuda_device_count() < 1) {
      text = lib->objk_cuda_last_error();
      if(text.empty()) {
        text = "no CUDA device";
      }
    }
  } else if(lib && index >= 0 && index < lib->objk_cuda_device_count()) {
    char name[256] = { 0 };
    if(lib->objk_cuda_device_info(index, name, (int)sizeof(name), nullptr, nullptr, nullptr, nullptr)) {
      text = name;
    }
  }
  APITools_SetStringValue(context, 0, BytesToUnicode(text));
}

// ml_cnn_text(ret String; id, what)  what: 0 last error, 1 summary
OBJK_CNN_EXPORT void ml_cnn_text(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int what = IntArg(context, 2);
  std::shared_ptr<ModelBase> m = Find(id);
  std::string text;
  if(!m) {
    text = what == 0 ? (g_global_error.empty() ? std::string("unknown or released model") : g_global_error) : std::string();
  } else {
    text = what == 0 ? m->error : m->Summary();
  }
  APITools_SetStringValue(context, 0, BytesToUnicode(text));
}

// ml_cnn_train(ret Float loss; id, mode, Float[] x, Float[] y)
//   mode 0 train step, 1 evaluate (loss only), 2 gradients only (eval mode), 3 gradients only (training mode),
//   4 and 5 as 2 and 3 plus the gradient with respect to the input. Returns -1.0 when refused.
OBJK_CNN_EXPORT void ml_cnn_train(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int mode = IntArg(context, 2);
  const double *x = nullptr, *y = nullptr;
  size_t xn = 0, yn = 0;
  APITools_SetFloatValue(context, 0, -1.0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m || !FloatArg(context, 3, x, xn) || !FloatArg(context, 4, y, yn)) {
    if(m) { m->error = "Nil images or labels"; }
    return;
  }
  if(yn < 1 || yn > 1000000 || xn != yn * m->input.Size()) {
    m->error = "the batch holds " + std::to_string(xn) + " values for " + std::to_string(yn) + " labels, expected " +
               std::to_string(yn * m->input.Size());
    return;
  }
  if(!m->Compiled()) {
    m->error = "the model is not compiled";
    return;
  }
  double loss = -1.0;
  try {
    APITools_Parked parked(context);
    switch(mode) {
    case 0: loss = m->Train(x, y, (int)yn); break;
    case 1: loss = m->Eval(x, y, (int)yn); break;
    case 2: loss = m->Gradients(x, y, (int)yn, false, false); break;
    case 3: loss = m->Gradients(x, y, (int)yn, true, false); break;
    case 4: loss = m->Gradients(x, y, (int)yn, false, true); break;
    case 5: loss = m->Gradients(x, y, (int)yn, true, true); break;
    default: m->error = "unknown mode"; break;
    }
  } catch(...) {
    m->error = "out of memory";
    loss = -1.0;
  }
  APITools_SetFloatValue(context, 0, loss);
}

// ml_cnn_predict(ret Float[]; id, logits, Float[] x) -> n probabilities (or logits), Nil when refused
OBJK_CNN_EXPORT void ml_cnn_predict(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const bool logits = APITools_GetIntValue(context, 2) != 0;
  const double* x = nullptr;
  size_t xn = 0;
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m || !FloatArg(context, 3, x, xn)) {
    if(m) { m->error = "Nil images"; }
    Nothing(context);
    return;
  }
  const size_t per = m->input.Size();
  if(xn < per || xn % per != 0 || xn / per > 1000000) {
    m->error = "the batch holds " + std::to_string(xn) + " values, not a whole number of " + m->input.Str() + " images";
    Nothing(context);
    return;
  }
  std::vector<double> out;
  bool ok = false;
  try {
    APITools_Parked parked(context);
    ok = m->Predict(x, (int)(xn / per), logits, out);
  } catch(...) {
    m->error = "out of memory";
  }
  if(!ok) {
    Nothing(context);
    return;
  }
  ReturnFloats(context, out);
}

// ml_cnn_vector(ret Float[]; id, what, layer)  what: 0 weights, 1 gradients, 2 activation of `layer`, 3 input gradient
OBJK_CNN_EXPORT void ml_cnn_vector(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int what = IntArg(context, 2), layer = IntArg(context, 3);
  std::shared_ptr<ModelBase> m = Find(id);
  std::vector<double> out;
  bool ok = false;
  if(m) {
    try {
      switch(what) {
      case 0: ok = m->Compiled() && m->GetWeights(out); break;
      case 1: ok = m->Compiled() && m->GetGrads(out); break;
      case 2: ok = m->Activation(layer, out); break;
      case 3: ok = m->InputGrad(out); break;
      default: break;
      }
    } catch(...) {
      ok = false;
    }
    if(!ok) {
      m->error = "no such values (not compiled, or no pass has run yet)";
    }
  }
  if(!ok) {
    Nothing(context);
    return;
  }
  ReturnFloats(context, out);
}

// ml_cnn_set_weights(ret Int ok; id, Float[] weights)
OBJK_CNN_EXPORT void ml_cnn_set_weights(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const double* w = nullptr;
  size_t wn = 0;
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  if(!FloatArg(context, 2, w, wn)) {
    m->error = "Nil weights";
    return;
  }
  APITools_SetIntValue(context, 0, m->SetWeights(w, wn) ? 1 : 0);
}

// ml_cnn_file(ret Int ok; id, save, String path)
OBJK_CNN_EXPORT void ml_cnn_file(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const bool save = APITools_GetIntValue(context, 2) != 0;
  const wchar_t* path = APITools_GetStringValue(context, 3);
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  if(!path || !*path) {
    m->error = "a Nil or empty path";
    return;
  }
  const std::wstring wide(path);
  std::string narrow;
  UnicodeToBytes(wide, narrow);
#ifdef _WIN32
  FILE* file = nullptr;
  if(_wfopen_s(&file, wide.c_str(), save ? L"wb" : L"rb") != 0) {
    file = nullptr;
  }
#else
  FILE* file = fopen(narrow.c_str(), save ? "wb" : "rb");
#endif
  bool ok = false;
  try {
    ok = save ? m->Save(file, narrow) : m->Load(file, narrow);
  } catch(...) {
    m->error = "out of memory";
  }
  APITools_SetIntValue(context, 0, ok ? 1 : 0);
}

// ml_cnn_stage(ret Int ok; id, Byte[] pixels, rows, cols, slot, total, Float[] config)
//   config: enabled, p_flip, p_rotate, max_angle, p_bright, bright_min, bright_max, seed, key
OBJK_CNN_EXPORT void ml_cnn_stage(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const unsigned char* pixels = nullptr;
  size_t pn = 0;
  const int rows = IntArg(context, 3), cols = IntArg(context, 4), slot = IntArg(context, 5), total = IntArg(context, 6);
  const double* cfg = nullptr;
  size_t cn = 0;
  APITools_SetIntValue(context, 0, 0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  if(!ByteArg(context, 2, pixels, pn) || !FloatArg(context, 7, cfg, cn) || cn < 9 || rows < 1 || cols < 1) {
    m->error = "Nil or malformed image or configuration";
    return;
  }
  const size_t plane = (size_t)rows * cols;
  if(pn % plane != 0 || (pn / plane != 1 && pn / plane != 3 && pn / plane != 4)) {
    m->error = "image has " + std::to_string(pn) + " bytes, not rows x cols x (1, 3 or 4)";
    return;
  }
  AugmentConfig aug;
  aug.enabled = cfg[0] != 0.0;
  aug.p_flip = cfg[1]; aug.p_rotate = cfg[2]; aug.max_angle = cfg[3];
  aug.p_bright = cfg[4]; aug.bmin = cfg[5]; aug.bmax = cfg[6];
  aug.seed = (uint64_t)(long long)cfg[7]; aug.key = (uint64_t)(long long)cfg[8];
  bool ok = false;
  try {
    ok = m->Stage(pixels, rows, cols, (int)(pn / plane), slot, total, aug);
  } catch(...) {
    m->error = "out of memory";
  }
  APITools_SetIntValue(context, 0, ok ? 1 : 0);
}

// ml_cnn_staged(ret Float / Float[]; id, mode, Float[] labels, count)
//   mode 0 train step, 1 evaluate -> Float loss (-1.0 refused); mode 2 probabilities, 3 logits -> use ml_cnn_staged_predict
OBJK_CNN_EXPORT void ml_cnn_staged(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const int mode = IntArg(context, 2);
  const double* y = nullptr;
  size_t yn = 0;
  APITools_SetFloatValue(context, 0, -1.0);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    return;
  }
  if(!FloatArg(context, 3, y, yn) || yn < 1 || yn > 1000000) {
    m->error = "Nil or empty labels";
    return;
  }
  double loss = -1.0;
  try {
    APITools_Parked parked(context);
    loss = mode == 0 ? m->TrainStaged(y, (int)yn) : m->EvalStaged(y, (int)yn);
  } catch(...) {
    m->error = "out of memory";
  }
  APITools_SetFloatValue(context, 0, loss);
}

// ml_cnn_staged_predict(ret Float[]; id, logits, count)
OBJK_CNN_EXPORT void ml_cnn_staged_predict(VMContext& context) {
  const size_t id = APITools_GetIntValue(context, 1);
  const bool logits = APITools_GetIntValue(context, 2) != 0;
  const int count = IntArg(context, 3);
  std::shared_ptr<ModelBase> m = Find(id);
  if(!m) {
    Nothing(context);
    return;
  }
  std::vector<double> out;
  bool ok = false;
  try {
    APITools_Parked parked(context);
    ok = m->PredictStaged(count, logits, out);
  } catch(...) {
    m->error = "out of memory";
  }
  if(!ok) {
    Nothing(context);
    return;
  }
  ReturnFloats(context, out);
}

// ml_cnn_threads(ret Int; count) sets the worker count (0 = just report); returns the count in force
OBJK_CNN_EXPORT void ml_cnn_threads(VMContext& context) {
  const int count = IntArg(context, 1);
  if(count > 0) {
    Pool::Instance().SetThreads(count);
  }
  APITools_SetIntValue(context, 0, (size_t)Pool::Instance().Threads());
}

// ml_cnn_permutation(ret Int[]; n, seed) a seeded shuffle of 0..n-1
OBJK_CNN_EXPORT void ml_cnn_permutation(VMContext& context) {
  const long long n = (long long)APITools_GetIntValue(context, 1);
  const uint64_t seed = (uint64_t)APITools_GetIntValue(context, 2);
  if(n < 1 || n > 100000000LL) {
    Nothing(context);
    return;
  }
  const std::vector<long long> perm = Permutation(n, seed);
  size_t* array = APITools_MakeIntArray(context, (size_t)n);
  for(long long i = 0; i < n; ++i) {
    array[3 + i] = (size_t)perm[(size_t)i];
  }
  size_t* holder = APITools_GetObjectValue(context, 0);
  if(holder) {
    holder[0] = (size_t)array;
    APITools_SetObjectValue(context, 0, holder);
  }
}
}  // extern "C"

#endif
