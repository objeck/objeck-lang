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
#include <map>
#include <memory>
#include <mutex>

namespace objk_cnn_api {

using namespace objk_cnn;

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
};

template<class T>
class ModelImpl : public ModelBase {
  Sequential<T> net_;
  std::vector<T> x_, y_;

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

 public:
  ModelImpl(const Shape& in, uint64_t seed) : net_(in, seed) { input = in; }
  bool AddConv(int f, int k, int s, bool same) { return Check(net_.AddConv(f, k, s, same)); }
  bool AddMaxPool(int size, int stride) { return Check(net_.AddMaxPool(size, stride)); }
  bool AddReLU() { return Check(net_.AddReLU()); }
  bool AddFlatten() { return Check(net_.AddFlatten()); }
  bool AddDense(int units) { return Check(net_.AddDense(units)); }
  bool AddDropout(double rate) { return Check(net_.AddDropout(rate)); }
  bool Compile(int opt, double lr, double b1, double b2, double eps, double momentum, int init) {
    return Check(net_.Compile(opt, lr, b1, b2, eps, momentum, init));
  }
  void SetLearningRate(double lr) { net_.SetLearningRate(lr); }
  void SetTraining(bool t) { net_.SetTraining(t); }
  void ResetRng() { net_.ResetRng(); }
  size_t ParamCount() { return net_.ParamCount(); }
  std::string Summary() { return net_.Summary(); }

  double Train(const double* x, const double* y, int n) {
    if(!Prepare(x, y, n)) {
      return -1.0;
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

  bool GetWeights(std::vector<double>& out) { return net_.GetWeights(out); }
  bool SetWeights(const double* in, size_t count) { return Check(net_.SetWeights(in, count)); }
  bool GetGrads(std::vector<double>& out) { return net_.GetGrads(out); }
  bool Activation(int layer, std::vector<double>& out) { return net_.Activation(layer, out); }
  bool InputGrad(std::vector<double>& out) { return net_.InputGrad(out); }
  bool Save(FILE* file, const std::string& name) { return Check(net_.Save(file, name)); }
  bool Load(FILE* file, const std::string& name) { return Check(net_.Load(file, name)); }
  int LastCorrect() { return net_.LastCorrect(); }
  bool Compiled() { return net_.Compiled(); }
  bool IsDouble() { return sizeof(T) == sizeof(double); }
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

// called from unload_lib (matrix.cpp): stops the worker threads before the library is unmapped
void objk_cnn_shutdown() {
  objk_cnn::Pool::Instance().Shutdown();
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
    g_models[id] = model;
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
  default: break;
  }
  APITools_SetIntValue(context, 0, value);
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
