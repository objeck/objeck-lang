//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include <cstddef>

#include "Wrapper.hpp"
#include "DlSystem/DlError.hpp"
#include "DlSystem/StringList.hpp"
#include "DlSystem/IUserBuffer.hpp"

#include "DlSystem/UserBufferMap.h"

namespace DlSystem {

class UserBufferMap :
public Wrapper<UserBufferMap, Snpe_UserBufferMap_Handle_t, true>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_UserBufferMap_Delete};
public:

/**
 * @brief Creates a new empty UserBuffer map
 */
  UserBufferMap()
    : BaseType(Snpe_UserBufferMap_Create())
  {  }

/**
 * @brief Copy constructor.
 * @param[in] other : Handle to the other userBufferMap to be copied from.
 */
  UserBufferMap(const UserBufferMap& other)
    : BaseType(Snpe_UserBufferMap_CreateCopy(other.handle()))
  {  }
  UserBufferMap(UserBufferMap&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Assigns the contents of this handle into other handle
 *
 * @param other Destination UserBufferMap handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  UserBufferMap& operator=(const UserBufferMap& other){
    if(this != &other){
      Snpe_UserBufferMap_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Assigns the contents of this handle into other handle
 *
 * @param other Destination UserBufferMap handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  UserBufferMap& operator=(UserBufferMap&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Adds a name and the corresponding UserBuffer pointer
 *        to the map
 *
 * @param[in] name : The name of the UserBuffer
 * @param[in] buffer : Pointer to the UserBuffer
 *
 * @note If a UserBuffer with the same name already exists, the new
 *       UserBuffer pointer would be updated.
 */
  DlSystem::ErrorCode add(const char* name, IUserBuffer* buffer){
    if(!buffer) return ErrorCode::SNPE_CAPI_BAD_ARGUMENT;
    return static_cast<DlSystem::ErrorCode>(Snpe_UserBufferMap_Add(handle(), name, getHandle(*buffer)));
  }

/**
 * @brief Removes a mapping of one UserBuffer and its name by its name
 *
 * @param[in] name : The name of UserBuffer to be removed
 *
 * @note If no UserBuffer with the specified name is found, nothing
 *       is done.
 */
  DlSystem::ErrorCode remove(const char* name) noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_UserBufferMap_Remove(handle(), name));
  }

/**
 * @brief Returns the number of UserBuffers in the map
 */
  size_t size() const noexcept{
    return Snpe_UserBufferMap_Size(handle());
  }

/**
 * @brief Removes all UserBuffers from the map
 */
  DlSystem::ErrorCode clear() noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_UserBufferMap_Clear(handle()));
  }

/**
 * @brief Returns the UserBuffer given its name.
 *
 * @param[in] name : The name of the UserBuffer to get.
 *
 * @return nullptr if no UserBuffer with the specified name is
 *         found; otherwise, a valid pointer to the UserBuffer.
 */
  IUserBuffer* getUserBuffer(const char* name) const noexcept{
    return makeReference<IUserBuffer>(Snpe_UserBufferMap_GetUserBuffer_Ref(handle(), name));
  }

/**
 * @brief Returns the names of all UserBuffers
 *
 * @return A list of UserBuffer names.
 */
  StringList getUserBufferNames() const{
    return moveHandle(Snpe_UserBufferMap_GetUserBufferNames(handle()));
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserBufferMap)