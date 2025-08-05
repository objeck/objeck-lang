//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"
#include "ITensor.hpp"

#include <istream>


#include "SNPE/SNPEUtil.h"

namespace DlSystem{
// NOTE: These factories use a different handle type because they are singletons
// Never copy this pattern unless you're also implementing a singleton
class ITensorFactory :
public Wrapper<ITensorFactory, ITensorFactory*, true>
{
  friend BaseType;

  using BaseType::BaseType;
  static constexpr DeleteFunctionType DeleteFunction{NoOpDeleter};
public:
  ITensorFactory()
    : BaseType(nullptr)
  {  }
/**
 * @brief Creates a new ITensor with uninitialized data.
 *
 * The strides for the tensor will match the tensor dimensions
 * (i.e., the tensor data is contiguous in memory).
 *
 * @param[in] shape The dimensions for the tensor in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @return A pointer to the created tensor or nullptr if creating failed.
 */
  std::unique_ptr<ITensor> createTensor(const TensorShape &shape) noexcept{
    return makeUnique<ITensor>(Snpe_Util_CreateITensor(getHandle(shape)));
  }

/**
 * @brief Creates a new ITensor by loading it from a file.
 *
 * @param[in] input The input stream from which to read the tensor
 *                  data.
 *
 * @return A pointer to the created tensor or nullptr if creating failed.
 *
 */
  std::unique_ptr<ITensor> createTensor(std::istream &input) noexcept = delete;

/**
 * @brief Create a new ITensor with specific data.
 * (i.e. the tensor data is contiguous in memory). This tensor is
 * primarily used to create a tensor where tensor size can't be
 * computed directly from dimension. One such example is
 * NV21-formatted image, or any YUV formatted image
 *
 * @param[in] shape The dimensions for the tensor in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @param[in] data The actual data with which the Tensor object is filled.
 *
 * @param[in] dataSize The size of data
 *
 * @return A pointer to the created tensor
 */
  std::unique_ptr<ITensor> createTensor(const TensorShape &shape,
                                        const unsigned char *data,
                                        size_t dataSize) noexcept{
    auto handle = Snpe_Util_CreateITensorDataSize(getHandle(shape), data, dataSize);
    return makeUnique<ITensor>(handle);
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, ITensorFactory)