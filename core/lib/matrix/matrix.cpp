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

typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> Matrix2D;

static Eigen::MatrixXd& CreateMatrix(size_t array_dim, size_t* data_ptr);
static bool CopyMatrixToPtr(Eigen::MatrixXd& matrix, size_t array_dim, size_t* array_ptr);

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
  void ml_matrix_add_sm(VMContext& context) {
    const double value = (double)APITools_GetFloatValue(context, 1);
    size_t* matrix_obj = APITools_GetObjectValue(context, 2);
    
    size_t* data_ptr = (size_t*)*matrix_obj;
    if(!data_ptr) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
      return;
    }
    
    const size_t array_dim = data_ptr[1];
    if(array_dim != 2) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    const size_t array_size = data_ptr[0];
    const size_t array_cols = data_ptr[2];
    const size_t array_rows = data_ptr[3];

    // copy input values
    FLOAT_VALUE* input_values = (FLOAT_VALUE*)(data_ptr + array_dim + 2);
    Eigen::MatrixXd matrix = Eigen::Map<Matrix2D>(input_values, array_rows, array_cols);

    // add value
    matrix.array() += value;
    
    // create and copy output buffer
    size_t* output_ptr = APITools_MakeFloatArray(context, array_size);
    FLOAT_VALUE* output_values = (FLOAT_VALUE*)(output_ptr + array_dim + 2);

    // TODO:
    Eigen::Map<Matrix2D>(output_values, matrix.rows(), matrix.cols()) = matrix;
    size_t* result_obj = APITools_GetObjectValue(context, 0);
    result_obj[0] = (size_t)output_ptr;

    APITools_SetObjectValue(context, 0, result_obj);
  }
}

Eigen::MatrixXd& CreateMatrix(size_t array_dim, size_t* data_ptr)
{
  const size_t array_size = data_ptr[0];
  const size_t array_rows = data_ptr[1];
  const size_t array_cols = data_ptr[2];

  Eigen::MatrixXd matrix(array_rows, array_cols);

  size_t k = 0;
  FLOAT_VALUE* values = (FLOAT_VALUE*)(data_ptr + array_dim + 2);
  for (size_t i = 0; i < array_rows; i++) {
    for (size_t j = 0; j < array_cols; j++) {
      matrix(i, j) = values[k];
      ++k;
    };
  }

  return matrix;
}

bool CopyMatrixToPtr(Eigen::MatrixXd& matrix, size_t array_dim, size_t* data_ptr)
{
  const size_t array_size = data_ptr[0];
  const size_t array_rows = data_ptr[1];
  const size_t array_cols = data_ptr[2];

  size_t k = 0;
  FLOAT_VALUE* values = (FLOAT_VALUE*)(data_ptr + array_dim + 2);
  for(size_t i = 0; i < array_rows; i++) {
    for(size_t j = 0; j < array_cols; j++) {
      values[k] = matrix(i, j);
      ++k;
    };
  }

  return false;
}