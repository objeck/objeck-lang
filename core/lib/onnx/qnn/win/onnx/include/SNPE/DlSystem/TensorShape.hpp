//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include <vector>
#include <initializer_list>
#include <cstddef>

#include "Wrapper.hpp"

#include "DlSystem/TensorShape.h"

namespace DlSystem {

using Dimension = size_t;

class TensorShape :
public Wrapper<TensorShape, Snpe_TensorShape_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;
protected:
  static constexpr DeleteFunctionType DeleteFunction{Snpe_TensorShape_Delete};
private:
  using DimensionReference = WrapperDetail::MemberIndexedReference<TensorShape, Snpe_TensorShape_Handle_t, size_t, size_t, Snpe_TensorShape_At, Snpe_TensorShape_Set>;
  friend DimensionReference;
public:

/**
 * @brief Constructs a TensorShape and returns a handle to it
 *
 * @return the handle to the created TensorShape
 */
  TensorShape()
    : BaseType(Snpe_TensorShape_Create())
  {  }

/**
 * @brief Copy constructor.
 * @param[in] other object to copy.
 *
 * @return the handle to the created TensorShape.
 */
  TensorShape(const TensorShape& other)
    : BaseType(Snpe_TensorShape_CreateCopy(other.handle()))
  {  }

  TensorShape(TensorShape&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Creates a new shape with a list of dims specified in array
 *
 * @param[in] dims The dimensions are specified in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @return the handle to the created TensorShape
 */
  TensorShape(std::initializer_list<Dimension> dims)
    : BaseType(Snpe_TensorShape_CreateDimsSize(dims.begin(), dims.size()))
  {  }

/**
 * @brief Assigns the contents of this handle into other handle
 *
 * @param other Destination TensorShape handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  TensorShape& operator=(const TensorShape& other) noexcept{
    if(this != &other){
      Snpe_TensorShape_Assign(other.handle(), handle());
    }
    return *this;
  }

  TensorShape& operator=(TensorShape&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Creates a new shape with a list of dims specified in array
 *
 * @param[in] dims The dimensions are specified in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @param[in] size Size of the array.
 *
 * @return the handle to the created TensorShape
 */
  TensorShape(const size_t *dims, size_t size)
    : BaseType(Snpe_TensorShape_CreateDimsSize(dims, size))
  {  }

  TensorShape(const std::vector<size_t>& dims)
    : TensorShape(dims.data(), dims.size())
  {  }

/**
 * @brief Concatenates additional dimensions specified in
 * the array to the existing dimensions.
 *
 * @param[in] dims The dimensions are specified in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @param[in] size Size of the array.
 */
  void concatenate(const size_t *dims, size_t size){
    Snpe_TensorShape_Concatenate(handle(), dims, size);
  }

/**
 * @brief Concatenates additional dimensions specified in
 * the array to the existing dimensions.
 *
 * @param[in] dims The dimensions are specified in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @param[in] size Size of the array.
 */
  void concatenate(const size_t &dim){
    return concatenate(&dim, 1);
  }

/**
 * @brief Gets dimension at index idx.
 *
 * @param[in] idx : Position in the dimension array.
 *
 * @return The dimension value in tensor shape
 */
  size_t operator[](size_t idx) const{
    return Snpe_TensorShape_At(handle(), idx);
  }

/**
 * @brief Gets dimension at index idx.
 *
 * @param[in] idx : Position in the dimension array.
 *
 * @return The dimension value in tensor shape
 */
  DimensionReference operator[](size_t idx){
    return {*this, idx};
  }

/**
 * @brief Retrieves the rank i.e. number of dimensions.
 *
 * @return The rank
 */
  size_t rank() const{
    return Snpe_TensorShape_Rank(handle());
  }

/**
 * @brief Retrieves a pointer to the first dimension of shape
 *
 * @return nullptr if no dimension exists; otherwise, points to
 * the first dimension.
 */
  const size_t* getDimensions() const{
    return Snpe_TensorShape_GetDimensions(handle());
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, Dimension)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, TensorShape)
