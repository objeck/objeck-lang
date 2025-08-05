//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"
#include "DlSystem/DlError.hpp"
#include "DlSystem/StringList.hpp"

#include "DlSystem/UserMemoryMap.h"

namespace DlSystem {

class UserMemoryMap :
public Wrapper<UserMemoryMap, Snpe_UserMemoryMap_Handle_t>
{
  friend BaseType;
// Use this to get free move Ctor and move assignment operator, provided this class does not specify
// as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_UserMemoryMap_Delete};
public:

/**
 * @brief Creates a new empty UserMemory map
 */
  UserMemoryMap()
    : BaseType(Snpe_UserMemoryMap_Create())
  {  }

/**
 * @brief Copy constructor to create an object
 * @param[in] other object to copy from
 */
  UserMemoryMap(const UserMemoryMap& other)
    : BaseType(Snpe_UserMemoryMap_Copy(other.handle()))
  {  }

/**
 * @brief Move constructor to create an object
 * @param[in] other is reference to source object
 */
  UserMemoryMap(UserMemoryMap&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Assignment operator to create an object
 * @param[in] other is reference to source object
 */
  UserMemoryMap& operator=(const UserMemoryMap& other){
    if(this != &other){
      Snpe_UserMemoryMap_Assign(handle(), other.handle());
    }
    return *this;
  }

/**
 * @brief Adds a name and the corresponding buffer address
 *        to the map
 *
 * @param[in] name The name of the UserMemory
 * @param[in] address The pointer to the Buffer Memory. The address is assumed
 *                    to be DSP Fast RPC allocated memory (libcdsprpc.so/dll)
 *
 * @note If a UserBuffer with the same name already exists, the new
 *       address would be added to the map along with the existing entries.
 */
  DlSystem::ErrorCode add(const char* name, void* address) noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_UserMemoryMap_Add(handle(), name, address));
  }

/**
 * @brief Adds a name and the corresponding buffer address
 *        to the map
 *
 * @param[in] name The name of the UserMemory
 * @param[in] address The pointer to the buffer memory
 * @param[in] totalAllocatedSize Total size of the allocated buffer in bytes
 * @param[in] fd The file descriptor to the buffer memory. Passing -1 would lead SNPE to assume
 *            the address to be DSP Fast RPC allocated memory (libcdsprpc.so/dll)
 * @param[in] offset The byte offset to the Buffer Memory. This allows a single large block
 *            of memory to be allocated for multiple tensors and individual tensors are
 *            identified by a common address/fd and an unique byte offset to the real address
 *
 * @note totalAllocatedSize is the total size of the allocation even if all of it is not used or
 *       only a portion of it is used via offsets. This will not always be equal to tensor size
 *
 * @note If a UserBuffer with the same name already exists, the new
 *       address/fd/offset would be added to the map along with the existing entries
 */
  DlSystem::ErrorCode add(const char* name, void* address, size_t totalAllocatedSize, int32_t fd,
                                                                       uint64_t offset) noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_UserMemoryMap_AddFdOffset(handle(), name, address,
                                            totalAllocatedSize, fd, offset));
  }

/**
 * @brief Removes a mapping of one Buffer address and its name by its name
 *
 * @param[in] name The name of Memory address to be removed
 *
 * @note If no UserBuffer with the specified name is found, nothing
 *       is done.
 */
  DlSystem::ErrorCode remove(const char* name){
    return static_cast<DlSystem::ErrorCode>(Snpe_UserMemoryMap_Remove(handle(), name));
  }

/**
 * @brief Returns the number of User Memory addresses in the map
 */
  size_t size() const noexcept{
    return Snpe_UserMemoryMap_Size(handle());
  }

/**
 * @brief Removes all User Memory from the map
 */
  DlSystem::ErrorCode clear() noexcept{
    return static_cast<DlSystem::ErrorCode>(Snpe_UserMemoryMap_Clear(handle()));
  }

/**
 * @brief Returns the names of all User Memory
 *
 * @return A list of Buffer names.
 */
  StringList getUserBufferNames() const{
    return moveHandle(Snpe_UserMemoryMap_GetUserBufferNames(handle()));
  }

/**
 * @brief Returns the no of UserMemory addresses mapped to the buffer
 *
 * @param[in] name The name of the UserMemory
 */
  size_t getUserMemoryAddressCount(const char* name) const noexcept{
    return Snpe_UserMemoryMap_GetUserMemoryAddressCount(handle(), name);
  }

/**
 * @brief Returns address at a specified index corresponding to a UserMemory buffer name
 *
 * @param[in] name The name of the buffer
 * @param[in] index The index in the list of addresses
 */
  void* getUserMemoryAddressAtIndex(const char* name, uint32_t index) const noexcept{
    return Snpe_UserMemoryMap_GetUserMemoryAddressAtIndex(handle(), name, index);
  }

/**
 * @brief Returns file descriptor at a specified index corresponding to a UserMemory buffer name
 *
 * @param[in] name The name of the buffer
 * @param[in] index The index in the list of addresses
 */
  int32_t getUserMemoryFdAtIndex(const char* name, uint32_t index) const noexcept{
    return Snpe_UserMemoryMap_GetUserMemoryFdAtIndex(handle(), name, index);
  }

/**
 * @brief Returns offset at a specified offset corresponding to a UserMemory buffer name
 *
 * @param[in] name The name of the buffer
 * @param[in] index The index in the list of addresses
 */
  uint64_t getUserMemoryOffsetAtIndex(const char* name, uint32_t index) const noexcept{
    return Snpe_UserMemoryMap_GetUserMemoryOffsetAtIndex(handle(), name, index);
  }

 /**
  * @brief Returns total allocated size at a specified index corresponding to a UserMemory buffer name
  *
  * @param[in] name The name of the buffer
  * @param[in] index The index in the list of addresses
  */
  size_t getUserMemoryTotalAllocatedSizeAtIndex(const char* name, uint32_t index) const noexcept{
    return Snpe_UserMemoryMap_GetUserMemoryTotalAllocatedSizeAtIndex(handle(), name, index);
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, UserMemoryMap)