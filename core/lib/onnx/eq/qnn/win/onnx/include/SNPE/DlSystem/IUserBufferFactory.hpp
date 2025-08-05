//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include <memory>

#include "Wrapper.hpp"
#include "IUserBuffer.hpp"
#include "TensorShape.hpp"


#include "SNPE/SNPEUtil.h"

namespace DlSystem{


// NOTE: These factories use a different handle type because they are singletons
// Never copy this pattern unless you're also implementing a singleton
class IUserBufferFactory :
        public Wrapper<IUserBufferFactory, IUserBufferFactory*, true>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{NoOpDeleter};
public:
  IUserBufferFactory()
    : BaseType(nullptr)
  {  }

  /**
   * @brief Creates a UserBuffer
   *
   * @param[in] buffer Pointer to the buffer that the caller supplies
   *
   * @param[in] bufSize Buffer size, in bytes
   *
   * @param[in] strides Total number of bytes between elements in each dimension.
   *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
   *
   * @param[in] userBufferEncoding Reference to an UserBufferEncoding object
   *
   * @note Caller has to ensure that memory pointed to by buffer stays accessible
   *       for the lifetime of the object created
   */
  std::unique_ptr<IUserBuffer> createUserBuffer(void *buffer,
                                                size_t bufSize,
                                                const TensorShape &strides,
                                                UserBufferEncoding* userBufferEncoding) noexcept{
    if(!userBufferEncoding) return {};
    auto handle = Snpe_Util_CreateUserBuffer(buffer,
                                             bufSize,
                                             getHandle(strides),
                                             getHandle(userBufferEncoding));
    return makeUnique<IUserBuffer>(handle);
  }

  /**
   * @brief Creates a UserBuffer
   *
   * @param[in] buffer Pointer to the buffer that the caller supplies.
   *
   * @param[in] bufSize Buffer size, in bytes.
   *
   * @param[in] addrOffset byte offset for sharing a buffer amongst tensors.
   *
   * @param[in] strides Total number of bytes between elements in each dimension.
   *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
   *
   * @param[in] userBufferEncoding Reference to an UserBufferEncoding object
   *
   * @note Caller has to ensure that memory pointed to by buffer stays accessible
   *       for the lifetime of the object created
   */
  std::unique_ptr<IUserBuffer> createUserBufferShared(void *buffer,
                                                      size_t bufSize,
                                                      uint64_t addrOffset,
                                                      const TensorShape &strides,
                                                      UserBufferEncoding* userBufferEncoding) noexcept{
    if(!userBufferEncoding) return {};
    auto handle = Snpe_Util_CreateUserBufferShared(buffer,
                                                   bufSize,
                                                   addrOffset,
                                                   getHandle(strides),
                                                   getHandle(userBufferEncoding));
    return makeUnique<IUserBuffer>(handle);
  }

  /**
   * @brief Creates a UserBuffer
   *
   * @param[in] buffer Pointer to the buffer that the caller supplies
   *
   * @param[in] bufSize Buffer size, in bytes
   *
   * @param[in] strides Total number of bytes between elements in each dimension.
   *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
   *
   * @param[in] userBufferEncoding Reference to an UserBufferEncoding object
   *
   * @param[in] userBufferSource Reference to an UserBufferSource object
   *
   * @note Caller has to ensure that memory pointed to by buffer stays accessible
   *       for the lifetime of the object created
   */
  std::unique_ptr<IUserBuffer> createUserBuffer(void *buffer,
                                                size_t bufSize,
                                                const TensorShape &strides,
                                                UserBufferEncoding* userBufferEncoding,
                                                UserBufferSource* userBufferSource) noexcept{
    if(!userBufferEncoding || !userBufferSource) return {};
    auto handle = Snpe_Util_CreateUserBufferFromSource(buffer,
                                                       bufSize,
                                                       getHandle(strides),
                                                       getHandle(*userBufferEncoding),
                                                       getHandle(*userBufferSource));
    return makeUnique<IUserBuffer>(handle);
  }

 /**
  * @brief Converts/Quantizes a float32 buffer to an unsigned fixed point 8/16 bit buffer
  *
  * @param[in] inputBuffer Pointer to the float32 buffer that the caller supplies
  *
  * @param[in] inputBufferSizeBytes Input buffer size, in bytes. Must be at least 4 bytes
  *
  * @param[in] isDynamicEncoding When set to false, scale and offset provided (next 2 argumets) will be used
  *            to qunatize. When set to true, scale and offset provided will be ignored and min/max will be computed
  *            from the inputBuffer to derrive the scale and offset for quantization. The computed scale and offset
  *            will be populated back to the caller in the next two arguments
  *
  * @param[in/out] scale Qunatization encoding scale to be passed in by the caller when isDynamicEncoding is true.
  *                      When isDynamicEncoding is false this argument is ignored and is populated as an outparam.
  *
  * @param[in/out] offset Qunatization encoding offset to be passed in by the caller when isDynamicEncoding is true.
  *                       When isDynamicEncoding is false this argument is ignored and is populated as an outparam.
  *
  * @param[in] outputBuffer Pointer to the unsigned fixed point 8/16 bit buffer that the caller supplies
  *
  * @param[in] outputBufferSizeBytes Output buffer size, in bytes. Must be large enough for all the inputs
  *
  * @param[in] bitWidth Quantization bitwidth (8 or 16)
  *
  * @note This API can do in-place quantization i.e. when outputBuffer equals inputBuffer pointer. But the output buffer
  *       CANNOT start at an offset from input buffer which will lead to corrupting inputs at sbsequent indexes.
  */
  bool Float32ToTfN(const float* inputBuffer,
                    size_t inputBufferSizeBytes,
                    bool isDynamicEncoding,
                    float& scale,
                    uint64_t& offset,
                    void* outputBuffer,
                    size_t outputBufferSizeBytes,
                    unsigned bitWidth) noexcept{
     return Snpe_Util_Convert_Float32ToTfN(inputBuffer, inputBufferSizeBytes, isDynamicEncoding, &scale, &offset, outputBuffer,
                                           outputBufferSizeBytes, bitWidth);
  }

 /**
  * @brief Converts/De-Quantizes an unsigned fixed point 8/16 bit buffer to a float32 buffer
  *
  * @param[in] inputBuffer Pointer to the unsigned fixed point 8/16 bit buffer that the caller supplies
  *
  * @param[in] inputBufferSizeBytes Input buffer size, in bytes
  *
  * @param[in/out] scale Qunatization encoding scale to be passed in by the caller
  *
  * @param[in/out] offset Qunatization encoding offset to be passed in by the caller
  *
  * @param[in] outputBuffer Pointer to the float32 buffer that the caller supplies
  *
  * @param[in] outputBufferSizeBytes Output buffer size, in bytes. Must be large enough for all the inputs
  *
  * @param[in] bitWidth Quantization bitwidth (8 or 16)
  *
  * @note This API CANNOT in place de-quantization. The input and output buffers must have no overlap
  */
  bool TfNToFloat32(const void* inputBuffer,
                    size_t inputBufferSizeBytes,
                    float scale,
                    uint64_t offset,
                    float* outputBuffer,
                    size_t outputBufferSizeBytes,
                    unsigned bitWidth) noexcept{
     return Snpe_Util_Convert_TfNToFloat32(inputBuffer, inputBufferSizeBytes, scale, offset, outputBuffer, outputBufferSizeBytes, bitWidth);
  }
};


} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, IUserBufferFactory)
