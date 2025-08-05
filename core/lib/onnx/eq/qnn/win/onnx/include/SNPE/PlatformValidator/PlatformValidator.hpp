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

#include "DlSystem/DlEnums.hpp"


#include "PlatformValidator/PlatformValidator.h"


namespace SNPE {

class PlatformValidator :
public Wrapper<PlatformValidator, Snpe_PlatformValidator_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_PlatformValidator_Delete};
public:
  PlatformValidator()
    : BaseType(Snpe_PlatformValidator_Create())
  {  }

/**
 * @brief Sets the runtime processor for compatibility check
 *
 * @param[in] runtime : Runtime to be set
 *
 * @param[in] unsignedPD : Bool value to set unsignedPD as true or false. By default, true
 *
 * @return Void
 */
  void setRuntime(DlSystem::Runtime_t runtime, bool unsignedPD=true){
    Snpe_PlatformValidator_SetRuntime(handle(), static_cast<Snpe_Runtime_t>(runtime), unsignedPD);
  }

/**
 * @brief Checks if the Runtime prerequisites for SNPE are available.
 *
 * @param[in] unsignedPD : Bool value to indicate if unsignedPD is true or false. By default true
 *
 * @return 1 if the Runtime prerequisites are available, else 0.
 */
  bool isRuntimeAvailable(bool unsignedPD=true){
    return Snpe_PlatformValidator_IsRuntimeAvailable(handle(), unsignedPD);
  }

/**
 * @brief Returns the core version for the Runtime selected.
 *
 * @return char* which contains the actual core version value
 */
  std::string getCoreVersion(){
    return Snpe_PlatformValidator_GetCoreVersion(handle());
  }

/**
 * @brief Returns the library version for the Runtime selected.
 *
 * @return char* which contains the actual lib version value
 */
  std::string getLibVersion(){
    return Snpe_PlatformValidator_GetLibVersion(handle());
  }

/**
 * @brief Runs a small program on the runtime and Checks if SNPE is supported for Runtime.
 *
 * @param[in] unsignedPD : Bool value to indicate if unsignedPD is true or false. By default true
 *
 * @return If 1, the device is ready for SNPE execution, else return 0.
 */
  bool runtimeCheck(bool unsignedPD=true){
    return Snpe_PlatformValidator_RuntimeCheck(handle(), unsignedPD);
  }

};

} // ns SNPE

ALIAS_IN_ZDL_NAMESPACE(SNPE, PlatformValidator)
