/***************************************************************************
 * Optional CUDA backend for System.ML.Cnn: the C interface
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

// The interface of libobjk_ml_cuda, the OPTIONAL GPU library. It is built apart from
// the rest of Objeck (core/lib/matrix/cuda/build_cuda.cmd or .sh, needs the CUDA
// toolkit) and is NOT part of CI or a release. libobjk_ml never links it: cnn_api.h
// loads it on demand (LoadLibrary / dlopen) and looks the functions below up by name,
// so an installation without it, or a machine without a GPU, simply has no "cuda"
// device and every model stays on the CPU.
//
// This header includes nothing from CUDA, so both sides can use it. The list of
// functions is a macro that is expanded twice: the library declares them, the
// loader makes a table of pointers. Every function returns 1 for success and 0 for
// failure unless it says otherwise, and never throws or ends the process; the
// reason is in objk_cuda_net_error (or objk_cuda_last_error when there is no net yet).
// All sizes are element counts. Everything is float32.

#ifndef __OBJK_ML_CUDA_H__
#define __OBJK_ML_CUDA_H__

#include <stddef.h>

#define OBJK_CUDA_ABI 1

#ifdef _WIN32
#define OBJK_CUDA_EXPORT __declspec(dllexport)
#else
#define OBJK_CUDA_EXPORT __attribute__((visibility("default")))
#endif

// layer kinds of objk_cuda_net_add, the same numbers as ml_cnn_add
//   0 conv (a = filters, b = kernel, c = stride, d = 1 for 'same'), 1 maxpool (a = size, b = stride),
//   2 relu, 3 flatten, 4 dense (a = units), 5 dropout (rate, seed)
//
// objk_cuda_net_run modes: 0 train step (forward, loss, backward, optimizer), 1 loss only,
//   2 gradients only (left in the net), 3 forward only (y may be null)
// optimizers of objk_cuda_net_compile: 0 SGD, 1 Adam
//
// parameters (set_params / get_params / get_grads) are laid out as the CPU engine does, layer by
// layer, each weight tensor then its bias, so a vector can be moved between the two as it is.
#define OBJK_CUDA_FUNCTIONS(X) \
  X(int, objk_cuda_abi, (void)) \
  X(int, objk_cuda_device_count, (void)) \
  X(int, objk_cuda_device_info, (int dev, char* name, int name_len, size_t* total_bytes, size_t* free_bytes, int* major, int* minor)) \
  X(const char*, objk_cuda_last_error, (void)) \
  X(void*, objk_cuda_net_create, (int dev, int channels, int height, int width, int tf32)) \
  X(void, objk_cuda_net_destroy, (void* net)) \
  X(const char*, objk_cuda_net_error, (void* net)) \
  X(int, objk_cuda_net_add, (void* net, int kind, int a, int b, int c, int d, double rate, unsigned long long seed)) \
  X(int, objk_cuda_net_compile, (void* net, int optimizer, double lr, double beta1, double beta2, double eps, double momentum)) \
  X(int, objk_cuda_net_set_lr, (void* net, double lr)) \
  X(int, objk_cuda_net_reset_rng, (void* net)) \
  X(int, objk_cuda_net_set_params, (void* net, const float* values, size_t count)) \
  X(int, objk_cuda_net_get_params, (void* net, float* values, size_t count)) \
  X(int, objk_cuda_net_get_grads, (void* net, float* values, size_t count)) \
  X(int, objk_cuda_net_run, (void* net, int mode, const float* x, const float* y, int n, int training, int want_input_grad, double* loss, int* correct, float* logits)) \
  X(size_t, objk_cuda_net_activation_size, (void* net, int layer)) \
  X(int, objk_cuda_net_get_activation, (void* net, int layer, float* values, size_t count)) \
  X(size_t, objk_cuda_net_input_grad_size, (void* net)) \
  X(int, objk_cuda_net_get_input_grad, (void* net, float* values, size_t count)) \
  X(size_t, objk_cuda_net_device_bytes, (void* net))

#endif
