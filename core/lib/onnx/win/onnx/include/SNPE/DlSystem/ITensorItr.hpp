//=============================================================================
//
//  Copyright (c) 2023,2024 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include <type_traits>
#include <algorithm>
#include <iterator>

#include "Wrapper.hpp"
#include "ITensorItrImpl.hpp"

namespace DlSystem{

/** @addtogroup c_plus_plus_apis C++
@{ */

/**
 * @brief A bidirectional iterator (with limited random access
 * capabilities) for the zdl::DlSystem::ITensor class.
 *
 * This is a standard bidrectional iterator and is compatible
 * with standard algorithm functions that operate on bidirectional
 * access iterators (e.g., std::copy, std::fill, etc.). It uses a
 * template parameter to create const and non-const iterators
 * from the same code. Iterators are easiest to declare via the
 * typedefs iterator and const_iterator in the ITensor class
 * (e.g., zdl::DlSystem::ITensor::iterator).
 *
 * Note that if the tensor the iterator is traversing was
 * created with nondefault (i.e., nontrivial) strides, the
 * iterator will obey the strides when traversing the tensor
 * data.
 *
 * Also note that nontrivial strides dramatically affect the
 * performance of the iterator (on the order of 20x slower).
 */
template<bool IS_CONST=true>
class ITensorItr{
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using pointer = typename std::conditional<IS_CONST, const float*, float*>::type;
  using value_type = float;
  using difference_type = std::ptrdiff_t;
  using reference = typename std::conditional<IS_CONST, const float&, float&>::type;


  ITensorItr() = delete;
  virtual ~ITensorItr() = default;

  /**
   * @brief Constructor of ITensorItr class
   * @param[in] data is pointer to data
   */
  explicit ITensorItr(pointer data) noexcept
    : m_Impl{nullptr},
      m_IsTrivial{true},
      m_Data{data},
      m_DataStart{data}
  {  }

  /**
   * @brief Constructor of ITensorItr class using member variables
   * @param[in] impl is Pointer to ITensorItrImpl
   * @param[in] isTrivial is set to false by default
   * @param[in] data is pointer to data
   */
  ITensorItr(std::unique_ptr<ITensorItrImpl> impl,
             bool isTrivial = false,
             float* data = nullptr)
    : m_Impl(impl->clone()),
      m_IsTrivial(isTrivial),
      m_Data(data),
      m_DataStart(data)
  {  }

  /**
   * @brief Constructor of ITensorItr class using another object
   * @param[in] itr is reference to another object
   */
  ITensorItr(const ITensorItr& itr)
    : m_Impl(itr.m_Impl ? itr.m_Impl->clone() : nullptr),
      m_IsTrivial(itr.m_IsTrivial),
      m_Data(itr.m_Data),
      m_DataStart(itr.m_DataStart)
  {  }

  /**
   * @brief Constructor of ITensorItr class using move constructor
   * @param[in] itr is reference to another object
   */
  ITensorItr(ITensorItr&& itr) noexcept
    : m_Impl(std::move(itr.m_Impl)),
      m_IsTrivial(itr.m_IsTrivial),
      m_Data(itr.m_Data),
      m_DataStart(itr.m_DataStart)
  {  }

  /**
   * @brief Constructor of ITensorItr class using assignment operator
   * @param[in] other is reference to another object
   */
  ITensorItr& operator=(const ITensorItr& other){
    if (this == &other) return *this;

    m_Impl = other.m_Impl ? other.m_Impl->clone() : nullptr;
    m_IsTrivial = other.m_IsTrivial;
    m_Data = other.m_Data;
    m_DataStart = other.m_DataStart;
    return *this;
  }

  /**
   * @brief Constructor of ITensorItr class using assignment operator
   * @param[in] other is reference to another object
   */
  ITensorItr& operator=(ITensorItr&& other) noexcept{
    if(this != &other){
      m_Impl = std::move(other.m_Impl);
      m_IsTrivial = other.m_IsTrivial;
      m_Data = other.m_Data;
      m_DataStart = other.m_DataStart;
    }
    return *this;
  }

  /**
   * @brief Overloading increment operator to parse through data
   */
  inline ITensorItr& operator++(){
    if (m_IsTrivial){
      m_Data++;
    } else {
      m_Impl->increment();
    }
    return *this;
  }

  /**
   * @brief Overloading increment operator to parse through data
   * @param[in] int is unused
   */
  inline ITensorItr operator++(int){
    ITensorItr tmp(*this);
    operator++();
    return tmp;
  }

  /**
   * @brief Overloading decrement operator to parse through data
   */
  inline ITensorItr& operator--(){
    if (m_IsTrivial){
      m_Data--;
    } else {
      m_Impl->decrement();
    }
    return *this;
  }

  /**
   * @brief Overloading decrement operator to parse through data
   * @param[in] int is unused
   */
  inline ITensorItr operator--(int){
    ITensorItr tmp(*this);
    operator--();
    return tmp;
  }

  /**
   * @brief Overloading addition operator to parse through data
   * @param[in] Moves forward the data pointer by rhs elements
   */
  inline ITensorItr& operator+=(int rhs){
    if (m_IsTrivial){
      m_Data += rhs;
    } else {
      m_Impl->increment(rhs);
    }
    return *this;
  }

  /**
   * @brief Overloading addition operator to parse through data
   * @param[in] lhs is reference to the object
   * @param[in] Moves forward the data pointer of the object by rhs elements
   */
  inline friend ITensorItr operator+(ITensorItr lhs, int rhs){
    lhs += rhs;
    return lhs;
  }

  /**
   * @brief Overloading subtraction operator to parse through data
   * @param[in] Moves back the data pointer of the object by rhs elements
   */
  inline ITensorItr& operator-=(int rhs){
   if (m_IsTrivial){
     m_Data -= rhs;
   } else {
     m_Impl->decrement(rhs);
   }
   return *this;
  }

  /**
   * @brief Overloading subtraction operator to parse through data
   * @param[in] lhs is reference to the object
   * @param[in] Moves back the data pointer of the object by rhs elements
   */
  inline friend ITensorItr operator-(ITensorItr lhs, int rhs){
    lhs -= rhs;
    return lhs;
  }

  /**
   * @brief Overloading subtartion operator to get the offset between two objects
   * @param[in] rhs is reference to the object
   * @return Offset between the two objects
   */
  inline size_t operator-(const ITensorItr& rhs){
    if (m_IsTrivial) return (m_Data - m_DataStart) - (rhs.m_Data - rhs.m_DataStart);
    return m_Impl->getPosition() - rhs.m_Impl->getPosition();
  }

  /**
   * @brief Overloading < operator to work with ITensorItr objects
   * @param[in] lhs is reference to the first object
   * @param[in] rhs is reference to the second object
   * @return True if data pointer of first object is lesser than the second object, else false
   */
  inline friend bool operator<(const ITensorItr& lhs, const ITensorItr& rhs){
    if (lhs.m_IsTrivial) return lhs.m_Data < rhs.m_Data;
    return lhs.m_Impl->dataPointer() < rhs.m_Impl->dataPointer();
  }

  /**
   * @brief Overloading < operator to work with ITensorItr objects
   * @param[in] lhs is reference to the first object
   * @param[in] rhs is reference to the second object
   * @return True if data pointer of first object is greater than the second object, else false
   */
  inline friend bool operator>(const ITensorItr& lhs, const ITensorItr& rhs){
    return rhs > lhs;
  }

  /**
   * @brief Overloading <= operator to work with ITensorItr objects
   * @param[in] lhs is reference to the first object
   * @param[in] rhs is reference to the second object
   * @return True if data pointer of first object is <= data pointer of second object, else false
   */
  inline friend bool operator<=(const ITensorItr& lhs, const ITensorItr& rhs){
    return !(lhs > rhs);
  }

  /**
   * @brief Overloading >= operator to work with ITensorItr objects
   * @param[in] lhs is reference to the first object
   * @param[in] rhs is reference to the second object
   * @return True if data pointer of first object is >= data pointer of second object, else false
   */
  inline friend bool operator>=(const ITensorItr& lhs, const ITensorItr& rhs){
    return !(lhs < rhs);
  }

  /**
   * @brief Overloading == operator to work with ITensorItr objects
   * @param[in] lhs is reference to the first object
   * @param[in] rhs is reference to the second object
   * @return True if data pointer of first object equals the data pointer of second object, else false
   */
  inline bool operator==(const ITensorItr& rhs) const{
    if (m_IsTrivial) return m_Data == rhs.m_Data;
    return m_Impl->dataPointer() == rhs.m_Impl->dataPointer();
  }

  /**
   * @brief Overloading != operator to work with ITensorItr objects
   * @param[in] lhs is reference to the first object
   * @param[in] rhs is reference to the second object
   * @return True if data pointer of first object is not equal to the data pointer of second object, else false
   */
  inline bool operator!=(const ITensorItr& rhs) const{
    return !operator==(rhs);
  }

  /**
   * @brief Overloading [] operator to work with ITensorItr objects
   * @param[in] idx is index of the data
   * @return Data at idx index
   */
  inline reference operator[](size_t idx){
    if (m_IsTrivial) return *(m_DataStart + idx);
    return m_Impl->getReferenceAt(idx);
  }

  /**
   * @brief Overloading derefencing operator to work with ITensorItr objects
   * @return Data at 0th index
   */
  inline reference operator*(){
    if (m_IsTrivial) return *m_Data;
    return m_Impl->getReference();
  }

  /**
   * @brief Overloading -> operator to work with ITensorItr objects
   * @return reference of object
   */
  inline reference operator->(){
    return *(*this);
  }

  /**
   * @brief Returns data pointer of the object
   */
  inline pointer dataPointer() const{
    if (m_IsTrivial) return m_Data;
    return m_Impl->dataPointer();
  }

protected:
  std::unique_ptr<::DlSystem::ITensorItrImpl> m_Impl;
  bool m_IsTrivial = false;
  pointer m_Data = nullptr;
  pointer m_DataStart = nullptr;
};

inline void fill(ITensorItr<false> first, ITensorItr<false> end, float val){
  std::fill(first, end, val);
}
template<class InItr, class OutItr>
OutItr copy(InItr first, InItr last, OutItr result){
  return std::copy(first, last, result);
}

} // ns DlSystem

// ALIAS_IN_ZDL_NAMESPACE
namespace zdl{ namespace DlSystem{
  template<bool IS_CONST>
  using ITensorItr = ::DlSystem::ITensorItr<IS_CONST>;
}}

/** @} */ /* end_addtogroup c_plus_plus_apis C++ */
