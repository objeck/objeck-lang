//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"

#include "DlSystem/StringList.hpp"
#include "DlSystem/TensorShape.hpp"
#include "DlSystem/DlError.hpp"

#include "DlSystem/TensorShapeMap.h"

namespace DlSystem {

class TensorShapeMap :
public Wrapper<TensorShapeMap, Snpe_TensorShapeMap_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;
  static constexpr DeleteFunctionType DeleteFunction{Snpe_TensorShapeMap_Delete};
public:

/**
 * @brief Constructs a TensorShapeMap and returns a handle to it
 *
 * @return the handle to the created TensorShapeMap
 */
  TensorShapeMap()
    : BaseType(Snpe_TensorShapeMap_Create())
  {  }

/**
 * @brief Copy constructor.
 *
 * @param[in] other : Handle to the other object to copy.
 * @return the handle to the created TensorShapeMap
 */
  TensorShapeMap(const TensorShapeMap& other)
    : BaseType(Snpe_TensorShapeMap_CreateCopy(other.handle()))
  {  }
  TensorShapeMap(TensorShapeMap&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Assignment operator. Assigns from handle to other handle
 * @param[out] other : handle to destination Tensor Shape Map object
 *
 * @return Returns SNPE_SUCCESS if Assignment successful
 */
  TensorShapeMap& operator=(const TensorShapeMap& other){
    if(this != &other){
      Snpe_TensorShapeMap_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Assignment operator. Assigns from handle to other handle
 * @param[out] other : handle to destination Tensor Shape Map object
 *
 * @return Returns SNPE_SUCCESS if Assignment successful
 */
  TensorShapeMap& operator=(TensorShapeMap&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Adds a name and the corresponding tensor pointer
 *        to the map
 *
 * @param[in] name The name of the tensor
 * @param[in] tensorShape : Tensor Shape
 *
 * @return Returns SNPE_SUCCESS if Add operation successful
 * @note If a tensor with the same name already exists, no new
 *       tensor is added.
 */
  DlSystem::ErrorCode add(const char *name, const TensorShape& tensorShape){
    return static_cast<DlSystem::ErrorCode>(
      Snpe_TensorShapeMap_Add(handle(), name, getHandle(tensorShape))
    );
  }

/**
 * @brief Removes a mapping of tensor and its name by its name
 *
 * @param[in] name The name of tensor to be removed
 * @return Returns SNPE_SUCCESS if Remove operation successful
 *
 * @note If no tensor with the specified name is found, nothing
 *       is done.
 */
  DlSystem::ErrorCode remove(const char* name) noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_TensorShapeMap_Remove(handle(), name));
  }

/**
 * @brief Returns the number of tensors in the map
 *
 * @return Returns number entries in TensorShapeMap
 */
  size_t size() const noexcept{
    return Snpe_TensorShapeMap_Size(handle());
  }

/**
 * @brief Removes all tensors from the map
 *
 * @return Returns SNPE_SUCCESS if Clear operation successful
 */
  DlSystem::ErrorCode clear() noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_TensorShapeMap_Clear(handle()));
  }

/**
 * @brief Returns the tensor given its name.
 *
 * @param[in] name The name of the tensor to get.
 *
 * @return nullptr if no tensor with the specified name is
 *         found; otherwise, a valid Tensor Shape Handle.
 */
  TensorShape getTensorShape(const char* name) const noexcept{
    return moveHandle(Snpe_TensorShapeMap_GetTensorShape(handle(), name));
  }

/**
 * @brief Retrieves tensor names
 *
 * @return A stringList Handle to access names of all tensor shapes
 */
  StringList getTensorShapeNames() const{
    return moveHandle(Snpe_TensorShapeMap_GetTensorShapeNames(handle()));
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, TensorShapeMap)