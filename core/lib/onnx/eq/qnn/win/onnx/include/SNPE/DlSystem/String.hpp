//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once


#include <string>


#include "Wrapper.hpp"

namespace DlSystem{


// Just a backwards compatible wrapper for std::string
class String{
public:
  String() = delete;

/**
 * @brief Constructor for the String class
 *
 * @param[in] str Const std::string variable which is used to initialize
 *
 * @return String initialized with given str
 */
  explicit String(const std::string& str)
    : m_String(str)
  {  }
  explicit String(std::string&& str) noexcept
    : m_String(std::move(str))
  {  }

  explicit String(const char* str)
    : m_String(str)
  {  }

/**
 * @brief Copy and Move Constructor for the String class
 *
 * @param[in] other Another String variable
 *
 * @return String initialized with given String
 */
  String(String&& other) noexcept = default;
  String(const String& other) = delete;

/**
 * @brief Move assigment operator for the String class
 *
 * @param[in] Other Another String variable
 *
 * @return String initialized with given String
 * @note Behaves as the default move assignemnt operator provided by compiler
 */
  String& operator=(String&& other) noexcept = default;

/**
 * @brief Deleted copy assigment operator for the String class
 *
 * @param[in] Other Another String variable
 *
 * @return String initialized with given String
 * @note This line explicitly deletes the copy assignment operator for the String class, preventing copying
 */
  String& operator=(const String& other) = delete;

/**
 * @brief Comparision operators for the String Class
 *
 * @param[in] rhs Another String variable
 *
 * @return True or False, depending on if String <operator> rhs is True or False  lexicographically
 */
  bool operator<(const String& rhs) const noexcept{ return m_String < rhs.m_String; }
  bool operator>(const String& rhs) const noexcept{ return m_String > rhs.m_String; }
  bool operator<=(const String& rhs) const noexcept{ return m_String <= rhs.m_String; }
  bool operator>=(const String& rhs) const noexcept{ return m_String >= rhs.m_String; }
  bool operator==(const String& rhs) const noexcept{ return m_String == rhs.m_String; }
  bool operator!=(const String& rhs) const noexcept{ return m_String != rhs.m_String; }

/**
 * @brief Comparision operators for the String Class
 *
 * @param[in] rhs std::string variable
 *
 * @return True or False, depending on if String <operator> rhs is True or False  lexicographically
 */
  bool operator<(const std::string& rhs) const noexcept{ return m_String < rhs; }
  bool operator>(const std::string& rhs) const noexcept{ return m_String > rhs; }
  bool operator<=(const std::string& rhs) const noexcept{ return m_String <= rhs; }
  bool operator>=(const std::string& rhs) const noexcept{ return m_String >= rhs; }
  bool operator==(const std::string& rhs) const noexcept{ return m_String == rhs; }
  bool operator!=(const std::string& rhs) const noexcept{ return m_String != rhs; }

/**
 * @brief Returns the C style string for this class
 *
 * @return C string for the current class
 */
  const char* c_str() const noexcept{ return m_String.c_str(); }

/**
 * @brief onversion operators for the String Class to convert to std::string class
 *
 * @return A variable of type std::string or const std::string
 */
  explicit operator std::string&() noexcept{ return m_String; }

/**
 * @brief onversion operators for the String Class to convert to std::string class
 *
 * @return A variable of type std::string or const std::string
 */
  explicit operator const std::string&() const noexcept{ return m_String; }
private:
  std::string m_String;
};

} // ns DlSystem

ALIAS_IN_ZDL_NAMESPACE(DlSystem, String)
