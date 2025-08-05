//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"
#include "StringList.hpp"
#include "DlEnums.hpp"
#include "DlSystem/RuntimeList.h"

namespace DlSystem {

class RuntimeList :
public Wrapper<RuntimeList, Snpe_RuntimeList_Handle_t>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_RuntimeList_Delete};

  static Runtime_t GetRuntime(HandleType handle, size_t idx){
    return static_cast<Runtime_t>(Snpe_RuntimeList_GetRuntime(handle, int(idx)));
  }
  static Snpe_ErrorCode_t SetRuntime(HandleType handle, size_t idx, Runtime_t runtime){
    return Snpe_RuntimeList_SetRuntime(handle, idx, static_cast<Snpe_Runtime_t>(runtime));
  }

private:
  using RuntimeReference = WrapperDetail::MemberIndexedReference<RuntimeList, Snpe_RuntimeList_Handle_t, Runtime_t, size_t, GetRuntime, SetRuntime>;
  friend RuntimeReference;
public:
/**
 * @brief Creates a new runtime list
 */
  RuntimeList()
    : BaseType(Snpe_RuntimeList_Create())
  {  }

/**
 * @brief Copy-Constructs a RuntimeList and returns a handle to it
 *
 * @param[in] other Source RuntimeList
 *
 * @return the handle to the created RuntimeList
 */
  RuntimeList(const RuntimeList& other)
    : BaseType(Snpe_RuntimeList_CreateCopy(other.handle()))
  {  }
  RuntimeList(RuntimeList&& other) noexcept
    : BaseType(std::move(other))
  {  }

/**
 * @brief Creates a new runtime list
 */
  RuntimeList(const Runtime_t& runtime)
    : BaseType(Snpe_RuntimeList_Create())
  {
    Snpe_RuntimeList_Add(handle(), static_cast<Snpe_Runtime_t>(runtime));
  }

/**
 * @brief Copy-assigns the contents of srcHandle into dstHandle
 *
 * @param[in] other Source RuntimeList
 *
 * @return Reference to runtime list object post assignment
 */
  RuntimeList& operator=(const RuntimeList& other){
    if(this != &other){
      Snpe_RuntimeList_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Move-assigns the contents of srcHandle into dstHandle
 *
 * @param[in] other Source RuntimeList
 *
 * @return  Reference to runtime list object post assignment
 */
  RuntimeList& operator=(RuntimeList&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Retrieves element at a particular index
 *
 * @param[in] idx is index of element
 *
 * @return  Runtime list element at a particular index
 */
  Runtime_t operator[](size_t idx) const{
    return GetRuntime(handle(), idx);
  }

/**
 * @brief Retrieves element at a particular index
 *
 * @param[in] idx is index of element
 *
 * @return  Runtime list element at a particular index
 */
  RuntimeReference operator[](size_t idx) noexcept{
    return {*this, idx};
  }

/**
 * @brief Adds runtime to the end of the runtime list
 *        order of precedence is former followed by latter entry
 *
 * @param[in] runtime to add
 *
 * @return Error code. Ruturns SNPE_SUCCESS If the runtime added successfully
 */
  bool add(const Runtime_t& runtime){
    return SNPE_SUCCESS == Snpe_RuntimeList_Add(handle(), static_cast<Snpe_Runtime_t>(runtime));
  }

/**
 * @brief Removes the runtime from the list
 *
 * @param[in] runtime to be removed
 *
 * @return Error code. Ruturns SNPE_SUCCESS If the runtime removed successfully
 */
  void remove(Runtime_t runtime) noexcept{
    Snpe_RuntimeList_Remove(handle(), static_cast<Snpe_Runtime_t>(runtime));
  }

/**
 * @brief Returns the number of runtimes in the list
 *
 * @return number of entries in the runtimeList.
 */
  size_t size() const noexcept{
    return Snpe_RuntimeList_Size(handle());
  }

/**
 * @brief Returns 1 if the list is empty
 *
 * @return 1 if list empty, 0 otherwise.
 */
  bool empty() const noexcept{
    return Snpe_RuntimeList_Empty(handle());
  }

/**
 * @brief Removes all runtime from the list
 *
 * @return Error code. Returns SNPE_SUCCESS if runtime list is cleared successfully.
 */
  void clear() noexcept{
    Snpe_RuntimeList_Clear(handle());
  }

/**
 * @brief Get a StringList of names from the runtime list in order of precedence
 *
 * @return Handle to a StringList
 */
  StringList getRuntimeListNames() const{
    return moveHandle(Snpe_RuntimeList_GetRuntimeListNames(handle()));
  }

/**
 * @brief Converts the input string to corresponding runtime
 *
 * @param[in] runtime const char*
 * Returns a Runtime enum corresponding to the in param string
 */
  static Runtime_t stringToRuntime(const char* runtimeStr){
    return static_cast<Runtime_t>(Snpe_RuntimeList_StringToRuntime(runtimeStr));
  }

/**
 * @brief Converts the runtime enum to string
 *
 * @param[in] runtime
 * Returns a const char* corresponding to the in param runtime enum
 *
 */
  static const char* runtimeToString(Runtime_t runtime){
    return Snpe_RuntimeList_RuntimeToString(static_cast<Snpe_Runtime_t>(runtime));
  }

};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, RuntimeList)