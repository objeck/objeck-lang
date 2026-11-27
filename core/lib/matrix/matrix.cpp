/***************************************************************************
 * Vector matrix math library
 *
 * Copyright (c) 2024, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
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

#include "../../vm/lib_api.h"
#include "../../shared/sys.h"

#include <mutex>

#ifdef _WIN32
// MSYS2
#ifdef _MSYS2 
#include <Eigen/Dense>
#else
// Windows
#include <eigen-3.4.0/Eigen/Dense>
#endif
// Linux
#else
#include <Eigen/Dense>
#endif

typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> ObjkMatrix;

void objk_cnn_shutdown(); // cnn_api.h

//
// Fixes the cache sizes Eigen cuts its matrix products into blocks by, so that a product gives
// the same bits in every process, whichever core it starts on.
//
// Eigen blocks a matrix product (GEBP) to fit the caches, and the block depth decides in which
// order the float products of one output element are added, so the low bits of the result. By
// default it asks the CPU for the sizes (cpuid), once per process, on whichever core happens to run
// the first product. On a CPU whose cores differ (Intel performance and efficiency cores: 48 KiB
// against 32 KiB of L1) that answer depends on where the scheduler put the thread, so a seeded CNN
// training run printed one of two loss curves, the rarer one mostly on a loaded machine. Constants
// remove the question. These are Eigen's own fallback for an x86 CPU that does not answer cpuid;
// measured on an i9-14900HX (cnn_speed.obs, 64 to 224 pixels) they train and predict as fast as
// the sizes cpuid reports there.
//
// Called from load_lib, so before any product. call_once: were the library loaded a second time,
// the sizes must not be written while a layer thread of the first load reads them.
//
static void FixEigenCacheSizes() {
  static std::once_flag once;
  std::call_once(once, [] {
    const std::ptrdiff_t l1 = 32 * 1024, l2 = 256 * 1024, l3 = 2 * 1024 * 1024;
    Eigen::setCpuCacheSizes(l1, l2, l3);
  });
}
static Eigen::MatrixXd PtrToMatrix(size_t* matrix_data_ptr);
static size_t* MatrixToPtr(Eigen::MatrixXd& matrix, size_t* matrix_data_ptr, VMContext& context);

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
    FixEigenCacheSizes();
  }

  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
    objk_cnn_shutdown();
  }

  //
  // Addition
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_add_scalar_matrix(VMContext& context) {
    const double value = APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatMatrixRef'

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // add value
    Eigen::MatrixXd result = value + matrix.array();

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_add_matrix_scalar(VMContext& context) {
    size_t* matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatMatrixRef'
    const double value = APITools_GetFloatValue(context, 2);

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // add value
    matrix.array() += value;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(matrix, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_add_matrix_matrix(VMContext& context) {
    size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd lhs_matrix = PtrToMatrix(lhs_data_ptr);
    if(!lhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
// std::cout << "lhs: " << lhs_matrix(0, 0) << ", " << lhs_matrix(1, 2) << std::endl;

    size_t* rhs_matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatArrayRef'
    size_t* rhs_data_ptr = (size_t*)*rhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd rhs_matrix = PtrToMatrix(rhs_data_ptr);
    if(!rhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
// std::cout << "rhs: " << rhs_matrix(0, 0) << ", " << rhs_matrix(1, 2) << std::endl;

    if(lhs_matrix.rows() != rhs_matrix.rows() || lhs_matrix.cols() != rhs_matrix.cols()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    Eigen::MatrixXd result = lhs_matrix + rhs_matrix;
// std::cout << "r: " << result(0, 0) << ", " << result(1, 2) << std::endl;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, lhs_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

  //
  // Subtraction
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_sub_scalar_matrix(VMContext& context) {
    const double value = APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatMatrixRef'

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // add value
    Eigen::MatrixXd result = value - matrix.array();

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_sub_matrix_scalar(VMContext& context) {
    size_t* matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatMatrixRef'
    const double value = APITools_GetFloatValue(context, 2);

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    matrix.array() -= value;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(matrix, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_sub_matrix_matrix(VMContext& context) {
    size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd lhs_matrix = PtrToMatrix(lhs_data_ptr);
    if(!lhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
    // std::cout << "lhs: " << lhs_matrix(0, 0) << ", " << lhs_matrix(1, 2) << std::endl;

    size_t* rhs_matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatArrayRef'
    size_t* rhs_data_ptr = (size_t*)*rhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd rhs_matrix = PtrToMatrix(rhs_data_ptr);
    if(!rhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
    // std::cout << "rhs: " << rhs_matrix(0, 0) << ", " << rhs_matrix(1, 2) << std::endl;

    if(lhs_matrix.cols() != rhs_matrix.cols() || lhs_matrix.cols() != rhs_matrix.cols()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    Eigen::MatrixXd result = lhs_matrix - rhs_matrix;
    // std::cout << "r: " << result(0, 0) << ", " << result(1, 2) << std::endl;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, lhs_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }
  
  //
  // Multiply
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_mul_scalar_matrix(VMContext& context) {
    const double value = APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatMatrixRef'

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // add value
    Eigen::MatrixXd result = value * matrix.array();

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_mul_matrix_scalar(VMContext& context) {
    size_t* matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatMatrixRef'
    const double value = APITools_GetFloatValue(context, 2);

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    matrix.array() *= value;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(matrix, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_dot_matrix_matrix(VMContext& context) {
    size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd lhs_matrix = PtrToMatrix(lhs_data_ptr);
    // std::cout << "lhs: " << lhs_matrix(0, 0) << ", " << lhs_matrix(2, 0) << std::endl;
    if(!lhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    size_t* rhs_matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatArrayRef'
    size_t* rhs_data_ptr = (size_t*)*rhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd rhs_matrix = PtrToMatrix(rhs_data_ptr);
    // std::cout << "rhs: " << rhs_matrix(0, 0) << ", " << rhs_matrix(2, 2) << std::endl;
    if(!rhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    if(lhs_matrix.cols() != rhs_matrix.rows()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    Eigen::MatrixXd result = lhs_matrix * rhs_matrix;
    // std::cout << "r: " << result(0, 0) << ", " << result(0, 1) << ", " << result(0, 2) << std::endl;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, lhs_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

  //
  // Division
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_div_scalar_matrix(VMContext& context) {
    const double value = APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatMatrixRef'

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // add value
    Eigen::MatrixXd result = value / matrix.array();

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_div_matrix_scalar(VMContext& context) {
    size_t* matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatMatrixRef'
    const double value = APITools_GetFloatValue(context, 2);

    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);
    if(!matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    matrix.array() /= value;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(matrix, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

  // 
  // Transpose
  //
#ifdef _WIN32
    __declspec(dllexport)
#endif
  void ml_matrix_transpose(VMContext& context) {
    try {
      size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
      size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
      Eigen::MatrixXd lhs_matrix = PtrToMatrix(lhs_data_ptr);
      // std::cout << "lhs: " << lhs_matrix(0, 0) << ", " << lhs_matrix(1, 0) << std::endl;
      if(!lhs_matrix.size()) {
        APITools_SetObjectValue(context, 0, 0);
        return;
      }

      // calculate value
      Eigen::MatrixXd result = lhs_matrix.transpose();
      // std::cout << "r: " << result(0, 0) << ", " << result(0, 1) << ", " << result(0, 2) << std::endl;
      if(!result.size()) {
        APITools_SetObjectValue(context, 0, 0);
        return;
      }

      // create and set results from matrix
      size_t* result_obj = MatrixToPtr(result, lhs_data_ptr, context);
      APITools_SetObjectValue(context, 0, result_obj);
    }
    catch(std::exception& e) {
#ifdef _WIN32      
      UNREFERENCED_PARAMETER(e);
#endif      
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
  }

  // 
  // Inverse
  //
#ifdef _WIN32
      __declspec(dllexport)
#endif
  void ml_matrix_inverse(VMContext& context) {
    size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    if(!lhs_matrix_obj || !(*lhs_matrix_obj)) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
    size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd lhs_matrix = PtrToMatrix(lhs_data_ptr);
 // std::cout << "lhs: " << lhs_matrix(0, 0) << ", " << lhs_matrix(1, 0) << std::endl;
    if(!lhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // before inverse(): Eigen asserts squareness only in debug builds, and a
    // release build given a non-square matrix never returned
    if(lhs_matrix.cols() != lhs_matrix.rows()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    // calculate value
    Eigen::MatrixXd result = lhs_matrix.inverse();
 // std::cout << "r: " << result(0, 0) << ", " << result(0, 1) << ", " << result(0, 2) << std::endl;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, lhs_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

  // 
  // Inverse
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_solve_matrix_matrix(VMContext& context)
  {
    size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    if(!lhs_matrix_obj || !(*lhs_matrix_obj)) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
    size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd lhs_matrix = PtrToMatrix(lhs_data_ptr);
    // std::cout << "lhs: " << lhs_matrix(0, 0) << ", " << lhs_matrix(2, 0) << std::endl;
    if(!lhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    size_t* rhs_matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatArrayRef'
    if(!rhs_matrix_obj || !(*rhs_matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* rhs_data_ptr = (size_t*)*rhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd rhs_matrix = PtrToMatrix(rhs_data_ptr);
    // std::cout << "rhs: " << rhs_matrix(0, 0) << ", " << rhs_matrix(2, 2) << std::endl;
    if(!rhs_matrix.size()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }

    if(lhs_matrix.rows() != rhs_matrix.rows()) {
      APITools_SetObjectValue(context, 0, 0);
      return;
    }
    
    // Eigen::VectorXd  coeffs = lhs_matrix.colPivHouseholderQr().solve(rhs_matrix);
    Eigen::BDCSVD<Eigen::MatrixXd> bdcSvd(lhs_matrix, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::VectorXd coeffs = bdcSvd.solve(rhs_matrix);
    
    size_t* rtrn_array = APITools_MakeFloatArray(context, coeffs.size());
    double* rtrn_ptr = (double*)(rtrn_array + 3);

    for(int i = 0; i < coeffs.size(); ++i) {
      rtrn_ptr[i] = coeffs(i);
    }

    size_t* rtrn_obj = APITools_GetObjectValue(context, 0); // pointer to 'FloatArrayRef'
    rtrn_obj[0] = (size_t)rtrn_array;
    APITools_SetObjectValue(context, 0, rtrn_obj);
  }
}

//
// Utilities
//
Eigen::MatrixXd PtrToMatrix(size_t* matrix_data_ptr)
{
  // a Nil matrix: callers reject the empty result
  if(!matrix_data_ptr) {
    return Eigen::MatrixXd();
  }

  // ensure 2d matrix
  const size_t array_dim = matrix_data_ptr[1];
  if(array_dim != 2) {
    return Eigen::MatrixXd();
  }

  // copy input values
  const size_t array_rows = matrix_data_ptr[3];
  const size_t array_cols = matrix_data_ptr[2];

  FLOAT_VALUE* input_values = (FLOAT_VALUE*)(matrix_data_ptr + array_dim + 2);
  Eigen::MatrixXd matrix = Eigen::Map<ObjkMatrix>(input_values, array_rows, array_cols);

  return matrix;
}

size_t* MatrixToPtr(Eigen::MatrixXd& matrix, size_t* matrix_data_ptr, VMContext& context)
{
  const size_t array_dim = matrix_data_ptr[1];

  size_t* output_ptr = APITools_MakeFloatMatrix(context, matrix.rows(), matrix.cols());
  FLOAT_VALUE* output_values = (FLOAT_VALUE*)(output_ptr + array_dim + 2);

  // copy results to output matrix
  Eigen::Map<ObjkMatrix>(output_values, matrix.rows(), matrix.cols()) = matrix;
  size_t* result_obj = APITools_GetObjectValue(context, 0);
  result_obj[0] = (size_t)output_ptr;

  return result_obj;
}

//
// Convolutional neural network layers (System.ML.Cnn). Header-only, so the
// library's existing build files compile it without new entries.
//
#include "cnn_api.h"
