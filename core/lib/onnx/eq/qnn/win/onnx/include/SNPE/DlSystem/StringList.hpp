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

#include "DlSystem/StringList.h"


namespace DlSystem {

class StringList :
public Wrapper<StringList, Snpe_StringList_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;
  static constexpr DeleteFunctionType DeleteFunction = Snpe_StringList_Delete;
public:
/**
 * @brief Constructs a StringList and returns a handle to it
 *
 * @return the handle to the created StringList
 */
  StringList()
    : BaseType(Snpe_StringList_Create())
  {  }

/**
 * @brief Constructs a StringList and returns a handle to it
 *
 * @param[in] length : length of list
 *
 * @return the handle to the created StringList
 */
  explicit StringList(size_t length)
    : BaseType(Snpe_StringList_CreateSize(length))
  {  }

/**
 * @brief Constructs a StringList and returns a handle to it
 *
 * @param[in] other : StringList handle to be copied from
 *
 * @return the handle to the created StringList
 */
  StringList(const StringList& other)
    : BaseType(Snpe_StringList_CreateCopy(other.handle()))
  {  }
  StringList(StringList&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Assigns the contents of this handle to other handle
 *
 * @param[in] other Destination StringList handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  StringList& operator=(const StringList& other){
    if(this != &other){
      Snpe_StringList_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Assigns the contents of this handle to other handle
 *
 * @param[in] other Destination StringList handle
 *
 * @return SNPE_SUCCESS on successful Assignment
 */
  StringList& operator=(StringList&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Append a string to the list.
 *
 * @param[in] str Null-terminated ASCII string to append to the list.
 *
 * @return SNPE_SUCCESS if Append operation successful.
 */
  DlSystem::ErrorCode append(const char* str){
    return static_cast<DlSystem::ErrorCode>(Snpe_StringList_Append(handle(), str));
  }

/**
 * @brief Returns the string at the indicated position,
 *  or an empty string if the positions is greater than the size
 *  of the list.
 *
 * @param[in] idx Position in the list of the desired string
 *
 * @return the string at the indicated position
 */
  const char* at(size_t idx) const noexcept{
    return Snpe_StringList_At(handle(), idx);
  }

/**
 * @brief Pointer to the first string in the list.
 *  Can be used to iterate through the list.
 *
 * @return Pointer to the first string in the list.
 */
  const char** begin() const noexcept{
    return Snpe_StringList_Begin(handle());
  }

/**
 * @brief Pointer to one after the last string in the list.
 *  Can be used to iterate through the list.
 *
 * @return Pointer to one after the last string in the list
 */
  const char** end() const noexcept{
    return Snpe_StringList_End(handle());
  }

/**
 * @brief Return the number of valid string pointers held by this list.
 *
 * @return The size of the StringList
 */
  size_t size() const noexcept{
    return Snpe_StringList_Size(handle());
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, StringList)