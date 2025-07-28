//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"
#include <cstddef>
#include "TensorShape.hpp"

#include "DlSystem/IUserBuffer.h"


namespace DlSystem {


class UserBufferEncoding:
public Wrapper<UserBufferEncoding, Snpe_UserBufferEncoding_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_UserBufferEncoding_Delete};
protected:
  UserBufferEncoding(HandleType handle)
    : BaseType(handle)
  {  }
public:

  virtual ~UserBufferEncoding() = default;

  UserBufferEncoding(UserBufferEncoding&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief .
 *
 * An enum class of all supported element types in a IUserBuffer
 */
//enum class Snpe_UserBufferEncoding_ElementType_t
  enum class ElementType_t
  {
    /// Unknown element type.
    UNKNOWN         = 0,

    /// Each element is presented by 32-bit float.
    FLOAT           = 1,

    /// Each element is presented by an unsigned int.
    UNSIGNED8BIT    = 2,

    /// Each element is presented by 16-bit float.
    FLOAT16         = 3,

    /// Each element is presented by an 8-bit quantized value.
    TF8             = 10,

    /// Each element is presented by an 16-bit quantized value.
    TF16            = 11,

    /// Each element is presented by Int32
    INT32           = 12,

    /// Each element is presented by UInt32
    UINT32          = 13,

    /// Each element is presented by Int8
    INT8            = 14,

    /// Each element is presented by UInt8
    UINT8           = 15,

    /// Each element is presented by Int16
    INT16           = 16,

    /// Each element is presented by UInt16
    UINT16          = 17,

    // Each element is presented by Bool8
    BOOL8           = 18,

    // Each element is presented by Int64
    INT64           = 19,

    // Each element is presented by UInt64
    UINT64           = 20
  };

/**
 * @brief Retrieves the element type
 *
 * @return Element type
 */
  ElementType_t getElementType() const noexcept{
    return static_cast<ElementType_t>(Snpe_UserBufferEncoding_GetElementType(handle()));
  }

/**
 * @brief Retrieves the size of the element, in bytes.
 *
 * @return Size of the element, in bytes.
 */
  size_t getElementSize() const noexcept{
    return Snpe_UserBufferEncoding_GetElementSize(handle());
  }
};

class UserBufferSource:
public Wrapper<UserBufferSource, Snpe_UserBufferSource_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_UserBufferSource_Delete};
public:
  enum class SourceType_t
  {
    /// Unknown buffer source type.
    UNKNOWN = 0,

    /// The network inputs are from CPU buffer.
    CPU = 1,

    /// The network inputs are from OpenGL buffer.
    GLBUFFER = 2
  };
protected:
  UserBufferSource(HandleType handle)
    : BaseType(handle)
  {  }
public:
/**
 * @brief Retrieves the source type
 *
 * @return Source type
 */
  SourceType_t getSourceType() const noexcept{
    return static_cast<SourceType_t>(Snpe_UserBufferSource_GetSourceType(handle()));
  }

};

class UserBufferSourceGLBuffer : public UserBufferSource{
public:
/**
 * @brief .
 *
 * An source type where input data is delivered from OpenGL buffer
 */
  UserBufferSourceGLBuffer()
    : UserBufferSource(Snpe_UserBufferSourceGLBuffer_Create())
  {  }
};

class UserBufferEncodingUnsigned8Bit : public UserBufferEncoding{
public:
  using UserBufferEncoding::UserBufferEncoding;

/**
 * @brief Copy Constructor for UserBufferEncodingUnsigned8Bit
 *
 * An encoding type where each element is represented by an unsigned int.
 *
 * Userbuffer size assumes uint8 encoding for each element.
 * (i.e., a tensor with dimensions (2,3) will be represented by (2 * 3) * 1 = 6 bytes in memory).
 *
 * @return a handle to the UserBufferEncodingUnsigned8Bit
 */
  UserBufferEncodingUnsigned8Bit()
    : UserBufferEncoding(Snpe_UserBufferEncodingUnsigned8Bit_Create())
  {  }
};

class UserBufferEncodingFloatN : public UserBufferEncoding{
public:
  using UserBufferEncoding::UserBufferEncoding;

/**
 * @brief An encoding type where each element is represented by a float N
 *
 * @param[in] bWidth is bitWidth N.
 *
 * Userbuffer size assumes float N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 16 will be represented by (2 * 3) * 2 = 12 bytes in memory).
 */
  UserBufferEncodingFloatN(uint8_t bWidth=32)
    : UserBufferEncoding(Snpe_UserBufferEncodingFloatN_Create(bWidth))
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingFloatN
 *
 * An encoding type where each element is represented by a float N
 *
 * Userbuffer size assumes float N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 16 will be represented by (2 * 3) * 2 = 12 bytes in memory).
 *
 * @param[in] other : Source UserBufferEncodingFloatN to copy from
 *
 * @return a handle to the constructed UserBufferEncodingFloatN
 */
  UserBufferEncodingFloatN(const UserBufferEncodingFloatN& other)
    : UserBufferEncoding(Snpe_UserBufferEncodingFloatN_CreateCopy(other.handle()))
  {  }

  static ElementType_t getTypeFromWidth(uint8_t width){
    return static_cast<ElementType_t>(Snpe_UserBufferEncodingFloatN_GetTypeFromWidth(width));
  }
};

class UserBufferEncodingFloat : public UserBufferEncoding{
public:
  using UserBufferEncoding::UserBufferEncoding;
// Encoding Float
/**
 * @brief An encoding type where each element is represented by a float.
 * Userbuffer size assumes float encoding for each element.
 * (i.e., a tensor with dimensions (2,3) will be represented by (2 * 3) * 4 = 24 bytes in memory).
 */
  UserBufferEncodingFloat()
    : UserBufferEncoding(Snpe_UserBufferEncodingFloat_Create())
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingFloat
 *
 * An encoding type where each element is represented by a float.
 *
 * Userbuffer size assumes float encoding for each element.
 * (i.e., a tensor with dimensions (2,3) will be represented by (2 * 3) * 4 = 24 bytes in memory).
 *
 * @param[in] otherHandle : a handle to another UserBufferEncodingFloat to copy
 *
 * @return a handle to the constructed UserBufferEncodingFloat
 */
  UserBufferEncodingFloat(const UserBufferEncodingFloat& other)
    :  UserBufferEncoding(Snpe_UserBufferEncodingFloat_CreateCopy(other.handle()))
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingFloat
 *
 * An encoding type where each element is represented by a float.
 *
 * Userbuffer size assumes float encoding for each element.
 * (i.e., a tensor with dimensions (2,3) will be represented by (2 * 3) * 4 = 24 bytes in memory).
 *
 * @param[in] otherHandle : a handle to another UserBufferEncodingFloat to copy
 *
 * @return a handle to the constructed UserBufferEncodingFloat
 */
  UserBufferEncodingFloat(UserBufferEncodingFloat&& other) noexcept
    : UserBufferEncoding(std::move(other))
  {  }
};


class UserBufferEncodingTfN : public UserBufferEncoding{
public:

  using UserBufferEncoding::UserBufferEncoding;

/**
 * @brief An encoding type where each element is represented by tfN, which is an
 * N-bit quantized value, which has an exact representation of 0.0
 *
 * Userbuffer size assumes tf N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 16 will be represented by (2 * 3) * 2 = 12 bytes in memory).
 */
  template<typename T, typename U,
    typename std::enable_if<std::is_integral<T>::value && std::is_floating_point<U>::value, int>::type = 0>
  UserBufferEncodingTfN(T stepFor0, U stepSize, uint8_t bWidth=8)
    : UserBufferEncoding(Snpe_UserBufferEncodingTfN_Create(stepFor0, stepSize, bWidth))
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingTfN
 *
 * An encoding type where each element is represented by tfN, which is an
 * N-bit quantized value, which has an exact representation of 0.0
 *
 * Userbuffer size assumes tf N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 16 will be represented by (2 * 3) * 2 = 12 bytes in memory).
 * @param otherHandle the UserBufferEncodingTfN to copy
 * @return a handle to a newly constructed UserBufferEncodingTfN
 */
  UserBufferEncodingTfN(const UserBufferEncoding& ubEncoding)
    : UserBufferEncoding(Snpe_UserBufferEncodingTfN_CreateCopy(getHandle(ubEncoding)))
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingTfN
 *
 * An encoding type where each element is represented by tfN, which is an
 * N-bit quantized value, which has an exact representation of 0.0
 *
 * Userbuffer size assumes tf N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 16 will be represented by (2 * 3) * 2 = 12 bytes in memory).
 * @param otherHandle the UserBufferEncodingTfN to copy
 * @return a handle to a newly constructed UserBufferEncodingTfN
 */
  UserBufferEncodingTfN(const UserBufferEncodingTfN& ubEncoding)
    : UserBufferEncoding(Snpe_UserBufferEncodingTfN_CreateCopy(getHandle(ubEncoding)))
  {  }

/**
 * @brief Sets the step value that represents 0
 *
 * @param[in] userBufferEncodingHandle : Handle to access the encoding
 *
 * @param[in] stepExactly0 : The step value that represents 0
 */
  void setStepExactly0(uint64_t stepExactly0){
    Snpe_UserBufferEncodingTfN_SetStepExactly0(handle(), stepExactly0);
  }

/**
 * @brief Sets the float value that each step represents
 *
 * @param[in] userBufferEncodingHandle : Handle to access the encoding
 *
 * @param[in] quantizedStepSize : The float value of each step size
 */
  void setQuantizedStepSize(const float quantizedStepSize){
    Snpe_UserBufferEncodingTfN_SetQuantizedStepSize(handle(), quantizedStepSize);
  }

/**
 * @brief Retrieves the step that represents 0.0
 *
 * @param[in] userBufferEncodingHandle : Handle to access the encoding
 *
 * @return Step value
 */
  uint64_t getStepExactly0() const{
    return Snpe_UserBufferEncodingTfN_GetStepExactly0(handle());
  }

/**
 * @brief Calculates the minimum floating point value that
 * can be represented with this encoding.
 *
 * @param[in] userBufferEncodingHandle : Handle to access the encoding
 *
 * @return Minimum representable floating point value
 */
  float getMin() const{
    return Snpe_UserBufferEncodingTfN_GetMin(handle());
  }

/**
 * @brief Calculates the maximum floating point value that
 * can be represented with this encoding.
 *
 * @param[in] userBufferEncodingHandle : Handle to access the encoding
 *
 * @return Maximum representable floating point value
 */
  float getMax() const{
    return Snpe_UserBufferEncodingTfN_GetMax(handle());
  }

/**
 * @brief Retrieves the step size
 *
 * @param[in] userBufferEncodingHandle : Handle to access the encoding
 *
 * @return Step size
 */
  float getQuantizedStepSize() const{
    return Snpe_UserBufferEncodingTfN_GetQuantizedStepSize(handle());
  }

/**
 * @brief Get the tfN type corresponding to a given bitwidth
 *
 * @param width bitwidth of tfN type
 *
 * @return ElementType corresponding to a tfN of width bits
 */
  static ElementType_t getTypeFromWidth(uint8_t width){
    return static_cast<ElementType_t>(Snpe_UserBufferEncodingTfN_GetTypeFromWidth(width));
  }
};

class UserBufferEncodingIntN : public UserBufferEncoding{
public:
// Encoding Int N
/**
 * @brief An encoding type where each element is represented by a Int
 *
 * Userbuffer size assumes int N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 32 will be represented by (2 * 3) * 4 = 24 bytes in memory).
 */
  UserBufferEncodingIntN(uint8_t bWidth=32)
    : UserBufferEncoding(Snpe_UserBufferEncodingIntN_Create(bWidth))
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingIntN
 *
 * An encoding type where each element is represented by a Int
 *
 * Userbuffer size assumes int N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 32 will be represented by (2 * 3) * 4 = 24 bytes in memory).
 * @param otherHandle the UserBufferEncodingIntN to copy
 * @return a handle to a newly constructed UserBufferEncodingIntN
 */
  UserBufferEncodingIntN(const UserBufferEncoding& ubEncoding)
    : UserBufferEncoding(Snpe_UserBufferEncodingIntN_CreateCopy(getHandle(ubEncoding)))
  {  }

/**
 * @brief Get the int type corresponding to a given bitwidth
 *
 * @param width bitwidth of int type
 *
 * @return ElementType corresponding to a int of width bits
 */
  static ElementType_t getTypeFromWidth(uint8_t width){
    return static_cast<ElementType_t>(Snpe_UserBufferEncodingIntN_GetTypeFromWidth(width));
  }
};

class UserBufferEncodingUintN : public UserBufferEncoding{
public:
// Encoding Uint N
/**
 * @brief An encoding type where each element is represented by a Uint
 *
 * Userbuffer size assumes uint N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 32 will be represented by (2 * 3) * 4 = 24 bytes in memory).
 */
  UserBufferEncodingUintN(uint8_t bWidth=32)
    : UserBufferEncoding(Snpe_UserBufferEncodingUintN_Create(bWidth))
  {  }

/**
 * @brief Copy Constructor for UserBufferEncodingUintN
 *
 * An encoding type where each element is represented by a Uint
 *
 * Userbuffer size assumes uint N encoding for each element.
 * (i.e., a tensor with dimensions (2,3) with a provided bitwidth of 32 will be represented by (2 * 3) * 4 = 24 bytes in memory).
 * @param otherHandle the UserBufferEncodingUintN to copy
 * @return a handle to a newly constructed UserBufferEncodingUintN
 */
  UserBufferEncodingUintN(const UserBufferEncoding& ubEncoding)
    : UserBufferEncoding(Snpe_UserBufferEncodingUintN_CreateCopy(getHandle(ubEncoding)))
  {  }

/**
 * @brief Get the uint type corresponding to a given bitwidth
 *
 * @param width bitwidth of uint type
 *
 * @return ElementType corresponding to a uint of width bits
 */
  static ElementType_t getTypeFromWidth(uint8_t width){
    return static_cast<ElementType_t>(Snpe_UserBufferEncodingUintN_GetTypeFromWidth(width));
  }
};


class UserBufferEncodingTf8 : public UserBufferEncodingTfN{
public:
  using UserBufferEncodingTfN::UserBufferEncodingTfN;
  UserBufferEncodingTf8() = delete;

  template<typename T, typename U,
    typename std::enable_if<std::is_integral<T>::value && std::is_floating_point<U>::value, int>::type = 0>
  UserBufferEncodingTf8(T stepFor0, U stepSize)
    : UserBufferEncodingTfN(stepFor0, stepSize, 8)
  {  }

  UserBufferEncodingTf8(const UserBufferEncoding& ubEncoding)
    : UserBufferEncodingTfN(ubEncoding)
  {  }

};

class UserBufferEncodingBool : public UserBufferEncoding{
public:
    UserBufferEncodingBool(uint8_t bWidth=8)
      : UserBufferEncoding(Snpe_UserBufferEncodingBool_Create(bWidth))
    {  }

    UserBufferEncodingBool(const UserBufferEncoding& ubEncoding)
      : UserBufferEncoding(Snpe_UserBufferEncodingBool_CreateCopy(getHandle(ubEncoding)))
    {  }
};

class IUserBuffer: public Wrapper<IUserBuffer, Snpe_IUserBuffer_Handle_t, true> {
  friend BaseType;
  using BaseType::BaseType;
  static constexpr DeleteFunctionType DeleteFunction{Snpe_IUserBuffer_Delete};

public:
/**
 * @brief Retrieves the total number of bytes between elements in each dimension if
 * the buffer were to be interpreted as a multi-dimensional array.
 *
 * @warning Do not modify the TensorShape returned by reference. Treat it as a const reference.
 *
 * @return A const reference to the number of bytes between elements in each dimension.
 * e.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would
 * return strides of [24, 8, 4].
 */
  const TensorShape& getStrides() const{
    return *makeReference<TensorShape>(Snpe_IUserBuffer_GetStrides_Ref(handle()));
  }

/**
 * @brief Retrieves the size of the buffer, in bytes.
 *
 * @return Size of the underlying buffer, in bytes.
 */
  size_t getSize() const{
    return Snpe_IUserBuffer_GetSize(handle());
  }

/**
 * @brief Retrieves the size of the inference data in the buffer, in bytes.
 *
 * The inference results from a dynamic-sized model may not be exactly the same size
 * as the UserBuffer provided to SNPE. This function can be used to get the amount
 * of output inference data, which may be less or greater than the size of the UserBuffer.
 *
 * If the inference results fit in the UserBuffer, getOutputSize() would be less than
 * or equal to getSize(). But if the inference results were more than the capacity of
 * the provided UserBuffer, the results would be truncated to fit the UserBuffer. But,
 * getOutputSize() would be greater than getSize(), which indicates a bigger buffer
 * needs to be provided to SNPE to hold all of the inference results.
 *
 * @return Size required for the buffer to hold all inference results, which can be less
 * or more than the size of the buffer, in bytes.
 */
  size_t getOutputSize() const{
    return Snpe_IUserBuffer_GetOutputSize(handle());
  }

/**
 * @brief Retrieves the byte offset to the starting address of the buffer.
 *
 * @return byte offset of the buffer from total allocated memory address.
 */
  uint64_t getAddrOffset() const{
    return Snpe_IUserBuffer_GetAddressOffset(handle());
  }

/**
 * @brief Changes the underlying memory that backs the UserBuffer.
 *
 * This can be used to avoid creating multiple UserBuffer objects
 * when the only thing that differs is the memory location.
 *
 * @param[in] buffer : Pointer to the memory location
 *
 * @return Whether the set succeeds.
 */
  bool setBufferAddress(void* buffer) noexcept{
    return Snpe_IUserBuffer_SetBufferAddress(handle(), buffer);
  }

/**
 * @brief Updates the address offset of the UserBuffer.
 *
 * @param[in] offset : byte offset to the starting address of the UserBuffer.
 *
 * @return Whether the set succeeds.
 */
  bool setBufferAddressOffset(uint64_t offset) noexcept{
    return Snpe_IUserBuffer_SetBufferAddressOffset(handle(), offset);
  }

/**
 * @brief Gets a reference to the data encoding object of
 *        the underlying buffer.
 *
 * This is necessary when the UserBuffer is re-used, and the encoding
 * parameters can change.  For example, each input can be quantized with
 * different step sizes.
 *
 * @return Data encoding meta-data
 */
  const UserBufferEncoding& getEncoding() const noexcept{
    auto h = Snpe_IUserBuffer_GetEncoding_Ref(handle());
    switch(Snpe_UserBufferEncoding_GetElementType(h)){
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT:
        return *makeReference<UserBufferEncodingFloat>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNSIGNED8BIT:
        return *makeReference<UserBufferEncodingUnsigned8Bit>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT8:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT16:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT32:
        return *makeReference<UserBufferEncodingUintN>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT8:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT16:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT32:
        return *makeReference<UserBufferEncodingIntN>(h);


      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT16:
        return *makeReference<UserBufferEncodingFloatN>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF8:
        return *makeReference<UserBufferEncodingTf8>(h);
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF16:
        return *makeReference<UserBufferEncodingTfN>(h);
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_BOOL8:
        return *makeReference<UserBufferEncodingBool>(h);

      default:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNKNOWN:
        return *makeReference<UserBufferEncoding>(h);
    }
  }

/**
 * @brief Gets a reference to the data encoding object of
 *        the underlying buffer.
 *
 * This is necessary when the UserBuffer is re-used, and the encoding
 * parameters can change.  For example, each input can be quantized with
 * different step sizes.
 *
 * @return Data encoding meta-data
 */
  UserBufferEncoding& getEncoding() noexcept{
    auto h = Snpe_IUserBuffer_GetEncoding_Ref(handle());
    switch(Snpe_UserBufferEncoding_GetElementType(h)){
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT:
        return *makeReference<UserBufferEncodingFloat>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNSIGNED8BIT:
        return *makeReference<UserBufferEncodingUnsigned8Bit>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT8:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT16:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UINT32:
        return *makeReference<UserBufferEncodingUintN>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT8:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT16:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_INT32:
        return *makeReference<UserBufferEncodingIntN>(h);


      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_FLOAT16:
        return *makeReference<UserBufferEncodingFloatN>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF8:
        return *makeReference<UserBufferEncodingTf8>(h);
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_TF16:
        return *makeReference<UserBufferEncodingTfN>(h);

      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_BOOL8:
        return *makeReference<UserBufferEncodingBool>(h);

      default:
      case SNPE_USERBUFFERENCODING_ELEMENTTYPE_UNKNOWN:
        return *makeReference<UserBufferEncoding>(h);
    }
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncoding)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferSource)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferSourceGLBuffer)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingUnsigned8Bit)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingFloatN)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingFloat)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingTfN)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingIntN)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingUintN)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferEncodingTf8)

ALIAS_IN_ZDL_NAMESPACE(DlSystem, IUserBuffer)
