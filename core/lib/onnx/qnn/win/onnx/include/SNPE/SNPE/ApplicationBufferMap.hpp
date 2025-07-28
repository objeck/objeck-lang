//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

#include "Wrapper.hpp"
#include "DlSystem/StringList.hpp"

#include "SNPE/ApplicationBufferMap.h"

namespace PSNPE {
/** @addtogroup c_plus_plus_apis C++
@{ */

/**
 * @brief A class representing the UserBufferMap of Input and Output asynchronous mode.
 */
class ApplicationBufferMap :
public Wrapper<ApplicationBufferMap, Snpe_ApplicationBufferMap_Handle_t>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_ApplicationBufferMap_Delete};
public:
/**
 * @brief Constructs a ApplicationBufferMap and returns a handle to it
 *
 * @return the handle to the created Application Buffer Map
 */
  ApplicationBufferMap()
  : BaseType(Snpe_ApplicationBufferMap_Create()){}

/**
 * @brief Constructs a ApplicationBufferMap initializes it with the given map
 *
 * @param[in] buffer Unordered map to be initialized in the Application Buffer Map
 *
 * @return the handle to the created Application Buffer
 */
  explicit ApplicationBufferMap(const std::unordered_map<std::string, std::vector<uint8_t>> &buffer)
  : ApplicationBufferMap(){
    for(const auto &kv: buffer){
      add(kv.first.c_str(), kv.second);
    }
  }

/**
 * @brief Adds a name and the corresponding buffer
 *        to the map
 *
 * @param[in] name The name of the UserBuffer
 * @param[in] buffer The vector of the uint8_t data
 *
 * @note If a UserBuffer with the same name already exists, the new
 *       UserBuffer pointer would be updated.
 */
  void add(const char *name, const std::vector<uint8_t> &buff){
    Snpe_ApplicationBufferMap_Add(handle(), name, buff.data(), buff.size());
  }

/**
 * @brief Adds an entry of (name,buff) to the Application buffer map
 *
 * @param[in] name Name of the buffer
 * @param[in] buff Vector of floats
 */
  void add(const char *name, const std::vector<float> &buff){
    Snpe_ApplicationBufferMap_Add(handle(), name, reinterpret_cast<const uint8_t *>(buff.data()), buff.size()*sizeof(float));
  }

/**
 * @brief Removes a mapping of one UserBuffer and its name by its name
 *
 * @param[in] name The name of UserBuffer to be removed
 *
 * @note If no UserBuffer with the specified name is found, nothing
 *       is done.
 */
  void remove(const char *name) noexcept{
    Snpe_ApplicationBufferMap_Remove(handle(), name);
  }

/**
 * @brief Returns the size of the Application buffer map
 *
 * @return The size of the application buffer map
 */
  size_t size() const noexcept{
    return Snpe_ApplicationBufferMap_Size(handle());
  }

/**
 * @brief Clears the Application buffer map
 */
  void clear() noexcept{
    Snpe_ApplicationBufferMap_Clear(handle());
  }

/**
 * @brief Returns the UserBuffer given its name.
 *
 * @param[in] name The name of the UserBuffer to get.
 *
 * @return nullptr if no UserBuffer with the specified name is
 *         found; otherwise, a valid pointer to the UserBuffer.
 */
  std::vector<uint8_t> getUserBuffer(const char *name) const{
    size_t size{};
    const uint8_t *data{};
    Snpe_ApplicationBufferMap_GetUserBuffer(handle(), name, &size, &data);

    return std::vector<uint8_t>(data, data + size);
  }

/**
 * @brief Gets a user buffer from Application buffer map
 *
 * @param[in] name Name of the buffer to be retrieved
 *
 * @return A vector containing the contents of the user buffer
 */
  std::vector<uint8_t> operator[](const char *name) const{
    return getUserBuffer(name);
  }

/**
 * @brief Returns the list of names of user Buffer in Application Buffer Map
 *
 * @return A StringList of User buffer names
 */
  DlSystem::StringList getUserBufferNames() const{
    return moveHandle(Snpe_ApplicationBufferMap_GetUserBufferNames(handle()));
  }

/**
 * @brief Gets all the user buffers from Application buffer map
 *
 * @return A map of all user buffers (name, buffer) present in the Application Buffer
 */
  std::unordered_map<std::string, std::vector<uint8_t>> getUserBuffer() const{
    std::unordered_map<std::string, std::vector<uint8_t>> toret;
    for(auto name: getUserBufferNames()){
      toret.emplace(name, getUserBuffer(name));
    }

    return toret;
  }

};

} // ns PSNPE

ALIAS_IN_ZDL_NAMESPACE(PSNPE, ApplicationBufferMap)