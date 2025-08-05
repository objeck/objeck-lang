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

#include "DlEnums.hpp"


#include "DlSystem/IOBufferDataTypeMap.h"

namespace DlSystem {

class IOBufferDataTypeMap :
public Wrapper<IOBufferDataTypeMap, Snpe_IOBufferDataTypeMap_Handle_t>
{
    friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_IOBufferDataTypeMap_Delete};
public:
/**
 * @brief Creates a new Buffer Data type map
 */
  IOBufferDataTypeMap()
    : BaseType(Snpe_IOBufferDataTypeMap_Create())
  {  }

/**
 * @brief Adds a name and the corresponding buffer data type
 *        to the map
 *
 * @param[in] name : The name of the buffer
 *
 * @param[in] bufferDataType : data type of the buffer
 *
 * @note If a buffer with the same name already exists, no new
 *       buffer is added.
 */
  void add(const char* name, IOBufferDataType_t bufferDataType){
    Snpe_IOBufferDataTypeMap_Add(handle(), name, static_cast<Snpe_IOBufferDataType_t>(bufferDataType));
  }

/**
 * @brief Removes a buffer name from the map
 *
 * @param[in] name : The name of the buffer
 */
  void remove(const char* name){
    Snpe_IOBufferDataTypeMap_Remove(handle(), name);
  }

/**
 * @brief Returns the type of the named buffer
 *
 * @param[in] name : The name of the buffer
 *
 * @return The type of the buffer, or UNSPECIFIED if the buffer does not exist
 */
  IOBufferDataType_t getBufferDataType(const char* name){
    return static_cast<IOBufferDataType_t>(Snpe_IOBufferDataTypeMap_GetBufferDataType(handle(), name));
  }

/**
 * @brief Returns the type of the first buffer
 *
 * @return The type of the first buffer, or SNPE_IO_BUFFER_DATATYPE_UNSPECIFIED if the map is empty.
 */
  IOBufferDataType_t getBufferDataType(){
    return static_cast<IOBufferDataType_t>(Snpe_IOBufferDataTypeMap_GetBufferDataTypeOfFirst(handle()));
  }

/**
 * @brief Returns the size of the buffer type map.
 *
 * @return The size of the map
 */
  size_t size() const{
    return Snpe_IOBufferDataTypeMap_Size(handle());
  }

/**
 * @brief Checks the existence of the named buffer in the map
 *
 * @param[in] name : The name of the buffer
 *
 * @return 1 if the named buffer exists, 0 otherwise.
 */
  bool find(const char* name) const{
    return Snpe_IOBufferDataTypeMap_Find(handle(), name);
  }

/**
 * @brief Resets the map
 */
  void clear(){
    Snpe_IOBufferDataTypeMap_Clear(handle());
  }

/**
 * @brief Checks whether the map is empty
 *
 * @return 1 if the map is empty, 0 otherwise.
 */
  bool empty() const{
    return Snpe_IOBufferDataTypeMap_Empty(handle());
  }
};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, IOBufferDataTypeMap)