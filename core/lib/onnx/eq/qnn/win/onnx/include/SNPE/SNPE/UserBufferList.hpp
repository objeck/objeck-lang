//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"
#include "DlSystem/UserBufferMap.hpp"

#include "SNPE/UserBufferList.h"


namespace PSNPE {

class UserBufferList :
public Wrapper<UserBufferList, Snpe_UserBufferList_Handle_t, true>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_UserBufferList_Delete};
public:

/**
 * @brief Creates an empty UserBufferList
 *
 * @return A handle of User buffer list
 */
  UserBufferList()
    : BaseType(Snpe_UserBufferList_Create())
  {  }

/**
 * @brief Creates a UserBufferList of a specific size
 *
 * @param[in] size Required size of the user buffer list
 *
 * @return A handle of User buffer list
 */
  explicit UserBufferList(size_t size)
    : BaseType(Snpe_UserBufferList_CreateSize(size))
  {  }

/**
 * @brief Copy Constructor for User buffer list
 *
 * @param[in] other Pointer to another UserBufferList
 *
 * @return A handle to a User buffer list initialized with the given UserBufferList
 */
  UserBufferList(const UserBufferList& other)
    : BaseType(Snpe_UserBufferList_CreateCopy(other.handle()))
  {  }

/**
 * @brief Move Constructor for User buffer list
 *
 * @param[in] other Pointer to another UserBufferList
 *
 * @return A handle to a User buffer list initialized with the given UserBufferList
 */
  UserBufferList(UserBufferList&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Assignment operator for User buffer list
 *
 * @param[in] other Pointer to another UserBufferList
 *
 * @return A handle to a User buffer list initialized with the given UserBufferList
 */
  UserBufferList& operator=(const UserBufferList& other){
    if(this != &other){
      Snpe_UserBufferList_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Assignment operator for User buffer list
 *
 * @param[in] other Pointer to another UserBufferList
 *
 * @return A handle to a User buffer list initialized with the given UserBufferList
 */
  UserBufferList& operator=(UserBufferList&& other){
    return moveAssign(std::move(other));
  }

/**
 * @brief Add a UserBufferMap at the end of UserBufferList
 *
 * @param[in] userBufferMap Pointer to the userBufferMap to be added
 */
  void push_back(const DlSystem::UserBufferMap&  userBufferMap){
    Snpe_UserBufferList_PushBack(handle(), getHandle(userBufferMap));
  }

/**
 * @brief Returns the user buffer map at a specific index
 *
 * @param[in] idx Specifc index, from where to return a user buffer map
 *
 * @return UserBufferMap at index idx in the User Buffer List
 */
  DlSystem::UserBufferMap& operator[](size_t idx){
    return *makeReference<DlSystem::UserBufferMap>(Snpe_UserBufferList_At_Ref(handle(), idx));
  }

/**
 * @brief Returns the size of the user buffer map
 *
 * @return size of the user buffer map
 */
  size_t size() const noexcept{
    return Snpe_UserBufferList_Size(handle());
  }

/**
 * @brief Returns the max size possible for the user buffer map
 *
 * @return max size of the user buffer map
 */
  size_t capacity() const noexcept{
    return Snpe_UserBufferList_Capacity(handle());
  }

/**
 * @brief Clears the given user buffer map
 */
  void clear() noexcept{
    Snpe_UserBufferList_Clear(handle());
  }
};

} // ns PSNPE

ALIAS_IN_ZDL_NAMESPACE(PSNPE, UserBufferList)