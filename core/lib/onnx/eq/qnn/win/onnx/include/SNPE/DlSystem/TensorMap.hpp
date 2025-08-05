//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"
#include "DlSystem/ITensor.hpp"
#include "DlSystem/StringList.hpp"
#include "DlSystem/DlError.hpp"

#include "DlSystem/TensorMap.h"

namespace DlSystem {

class TensorMap :
        public Wrapper<TensorMap, Snpe_TensorMap_Handle_t, true>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_TensorMap_Delete};
public:

/**
 * @brief Constructs a TensorMap and returns a handle to it
 *
 * @return the handle to the created TensorMap
 */
  TensorMap()
    : BaseType(Snpe_TensorMap_Create())
  {  }

/**
 * @brief Copy-Constructs a TensorMap and returns a handle to it
 *
 * @param[in] other the other TensorMap to copy
 *
 * @return the handle to the created TensorMap
 */
  TensorMap(const TensorMap& other)
    : BaseType(Snpe_TensorMap_CreateCopy(other.handle()))
  {  }

  TensorMap(TensorMap&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Assigns the contents of this handle into other handle
 *
 * @param[in] other Destination TensorMap handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  TensorMap& operator=(const TensorMap& other){
    if(this != &other){
      Snpe_TensorMap_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Assigns the contents of this handle into other handle
 *
 * @param[in] other Destination TensorMap handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  TensorMap& operator=(TensorMap&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Adds a name and the corresponding tensor pointer
 *        to the map
 *
 * @param[in] name : The name of the tensor
 * @param[in] tensor : Handle to access ITensor
 *
 * @note If a tensor with the same name already exists, the
 *       tensor is replaced with the existing tensor.
 */
  DlSystem::ErrorCode add(const char* name, ITensor* tensor){
    if(!tensor) return DlSystem::ErrorCode::SNPE_CAPI_BAD_ARGUMENT;
    Snpe_TensorMap_Add(handle(), name, getHandle(*tensor));
    return DlSystem::ErrorCode::NONE;
  }

/**
 * @brief Removes a mapping of tensor and its name by its name
 *
 * @param[in] name : The name of tensor to be removed
 *
 * @note If no tensor with the specified name is found, nothing
 *       is done.
 */
  void remove(const char* name) noexcept{
    Snpe_TensorMap_Remove(handle(), name);
  }

/**
 * @brief Returns the number of tensors in the map
 *
 * @return Number of tensors in the map
 */
  size_t size() const noexcept{
    return Snpe_TensorMap_Size(handle());
  }

/**
 * @brief Removes all tensors from the map
 */
  void clear() noexcept{
    Snpe_TensorMap_Clear(handle());
  }

/**
 * @brief Returns the tensor given its name.
 *
 * @param[in] name : The name of the tensor to get.
 *
 * @return nullptr if no tensor with the specified name is
 *         found; otherwise, a valid pointer to the tensor.
 */
  ITensor* getTensor(const char* name) const noexcept{
    return makeReference<ITensor>(Snpe_TensorMap_GetTensor_Ref(handle(), name));
  }

/**
 * @brief Retrieve list of all tensor names
 *
 * @return A StringList of the names of all tensors
 */
  StringList getTensorNames() const{
    return moveHandle(Snpe_TensorMap_GetTensorNames(handle()));
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, TensorMap)