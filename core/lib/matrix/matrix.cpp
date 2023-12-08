/***************************************************************************
 * Vector matrix math library
 *
 * Copyright (c) 2015-2019, Randy Hollines
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
#include <Eigen/Dense>

typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> EigenMatrix;
typedef Eigen::Vector<double, Eigen::Dynamic> EigenVector;

static Eigen::MatrixXd PtrToMatrix(size_t* matrix_data_ptr);
static Eigen::VectorXd PtrToVector(size_t* matrix_data_ptr);

static size_t* MatrixToPtr(Eigen::MatrixXd& matrix, size_t* matrix_data_ptr, VMContext& context);

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void load_lib(VMContext& context) {
  }

  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
    void unload_lib() {
  }

  //
  // matrix core
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_add_scalar_matrix(VMContext& context) {
    const double value = (double)APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatMatrixRef'
    
    // create matrix from 2d double array
    if(!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_data_ptr);

    // add value
    matrix.array() += value;
    
    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(matrix, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_add_vector_matrix(VMContext& context) {
    size_t* lhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    if(!lhs_matrix_obj || !(*lhs_matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* lhs_data_ptr = (size_t*)*lhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd lhs_matrix = PtrToVector(lhs_data_ptr);
    
    size_t* rhs_matrix_obj = APITools_GetObjectValue(context, 1); // pointer to 'FloatArrayRef'
    if (!rhs_matrix_obj || !(*rhs_matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* rhs_data_ptr = (size_t*)*rhs_matrix_obj; // pointer to double array
    Eigen::MatrixXd rhs_matrix = PtrToVector(rhs_data_ptr);

    // subract value
    Eigen::MatrixXd result = lhs_matrix + rhs_matrix;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(result, lhs_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void ml_matrix_sub_scalar_matrix(VMContext& context) {
    const double value = (double)APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2); // pointer to 'FloatMatrixRef'

    // create matrix from 2d double array
    if (!matrix_obj || !(*matrix_obj)) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    size_t* matrix_data_ptr = (size_t*)*matrix_obj; // pointer to 2d double array
    Eigen::MatrixXd matrix = PtrToMatrix(matrix_obj);

    // subract value
    matrix.array() -= value;

    // create and set results from matrix
    size_t* result_obj = MatrixToPtr(matrix, matrix_data_ptr, context);
    APITools_SetObjectValue(context, 0, result_obj);
  }
}

Eigen::VectorXd PtrToVector(size_t* vector_data_ptr)
{
  // ensure 2d matrix
  const size_t array_dim = vector_data_ptr[1];
  if(array_dim != 1) {
    return Eigen::VectorXd();
  }

  // copy input values
  const size_t array_size = vector_data_ptr[0];

  FLOAT_VALUE* input_values = (FLOAT_VALUE*)(vector_data_ptr + array_dim + 2);
  Eigen::VectorXd vector = Eigen::Map<EigenVector>(input_values, array_size);

  return vector;
}

Eigen::MatrixXd PtrToMatrix(size_t* matrix_data_ptr)
{
  // ensure 2d matrix
  const size_t array_dim = matrix_data_ptr[1];
  if(array_dim != 2) {
    return Eigen::MatrixXd();
  }

  // copy input values
  const size_t array_rows = matrix_data_ptr[2];
  const size_t array_cols = matrix_data_ptr[3];

  FLOAT_VALUE* input_values = (FLOAT_VALUE*)(matrix_data_ptr + array_dim + 2);
  Eigen::MatrixXd matrix = Eigen::Map<EigenMatrix>(input_values, array_rows, array_cols);

  return matrix;
}

size_t* MatrixToPtr(Eigen::MatrixXd& matrix, size_t* matrix_data_ptr, VMContext& context)
{
  const size_t array_size = matrix_data_ptr[0];
  const size_t array_dim = matrix_data_ptr[1];

  size_t* output_ptr = APITools_MakeFloatArray(context, array_size + array_dim + 2);
  FLOAT_VALUE* output_values = (FLOAT_VALUE*)(output_ptr + array_dim + 2);

  // copy results to output matrix
  Eigen::Map<EigenMatrix>(output_values, matrix.rows(), matrix.cols()) = matrix;
  size_t* result_obj = APITools_GetObjectValue(context, 0);
  result_obj[0] = (size_t)output_ptr;

  return result_obj;
}
