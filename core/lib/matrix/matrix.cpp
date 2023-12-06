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
    const double c = (double)APITools_GetFloatValue(context, 1);
    size_t* m_obj = APITools_GetObjectValue(context, 2);
    m_obj = (size_t*)*m_obj;

    std::wcout << c << std::endl;

    const size_t m_dims = m_obj[1];
    if (m_dims == 2) {
      const size_t m_dim_rows = m_obj[2];
      const size_t m_dim_cols = m_obj[3];

      Eigen::MatrixXd m(m_dim_rows, m_dim_cols);

      double x = m(1,1);
      double y = m(1, 2);
      

      std::wcout << x << L", " << y << std::endl;

      return;
    }

    APITools_SetIntValue(context, 0, 0);
  }
}
