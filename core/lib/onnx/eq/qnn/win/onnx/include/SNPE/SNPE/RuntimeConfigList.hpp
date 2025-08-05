//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once

#include "Wrapper.hpp"


#include "DlSystem/DlEnums.hpp"
#include "DlSystem/RuntimeList.hpp"
#include "DlSystem/TensorShapeMap.hpp"
#include "DlSystem/PlatformConfig.hpp"


#include "SNPE/RuntimeConfigList.h"

namespace PSNPE {



struct RuntimeConfig :
public Wrapper<RuntimeConfig, Snpe_RuntimeConfig_Handle_t, true>
{
private:
  friend BaseType;
    // Use this to get free move Ctor and move assignment operator, provided this class does not specify
    // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_RuntimeConfig_Delete};

  template<typename RuntimeType>
  using RuntimeReference = WrapperDetail::GenericMemberReference
    <RuntimeConfig, HandleType, RuntimeType,
      CastingGetter<Snpe_Runtime_t, DlSystem::Runtime_t, Snpe_RuntimeConfig_GetRuntime>,
      CastingSetter<Snpe_Runtime_t, DlSystem::Runtime_t, Snpe_RuntimeConfig_SetRuntime> >;


  template<typename RuntimeListType>
  using RuntimeListReference = WrapperMemberReference<
    RuntimeListType,
    Snpe_RuntimeList_Handle_t,
    Snpe_RuntimeConfig_GetRuntimeList_Ref,
    Snpe_RuntimeConfig_SetRuntimeList
  >;

  template<typename InputDimensionsMapType>
  using InputDimensionsMapReference = WrapperMemberReference<
    InputDimensionsMapType,
    Snpe_TensorShapeMap_Handle_t,
    Snpe_RuntimeConfig_GetInputDimensionsMap_Ref,
    Snpe_RuntimeConfig_SetInputDimensionsMap
  >;


  template<typename PlatformConfigType>
  using PlatformConfigReference = WrapperMemberReference<
    PlatformConfigType,
    Snpe_PlatformConfig_Handle_t,
    Snpe_RuntimeConfig_GetPlatformOptionLocal_Ref,
    Snpe_RuntimeConfig_SetPlatformOptionLocal
  >;

  template<typename PerfProfileType>
  using PerfProfileReference = WrapperDetail::GenericMemberReference
    <RuntimeConfig, HandleType, PerfProfileType,
      CastingGetter<Snpe_PerformanceProfile_t, DlSystem::PerformanceProfile_t, Snpe_RuntimeConfig_GetPerformanceProfile>,
      CastingSetter<Snpe_PerformanceProfile_t, DlSystem::PerformanceProfile_t, Snpe_RuntimeConfig_SetPerformanceProfile> >;

  template<typename EnableCPUFallbackType>
  using EnableCPUFallbackReference = WrapperDetail::GenericMemberReference
    <RuntimeConfig, HandleType, EnableCPUFallbackType,
      CastingGetter<int, bool, Snpe_RuntimeConfig_GetEnableCPUFallback>,
      CastingSetter<int, bool, Snpe_RuntimeConfig_SetEnableCPUFallback> >;
public:
  RuntimeConfig()
    : BaseType(Snpe_RuntimeConfig_Create())
  {  }
  RuntimeConfig(const RuntimeConfig& other)
    : BaseType(Snpe_RuntimeConfig_CreateCopy(other.handle()))
  {  }

  RuntimeConfig(RuntimeConfig&& other) noexcept
    : BaseType(std::move(other))
  {  }

  RuntimeConfig& operator=(RuntimeConfig&& other) noexcept{
    return moveAssign(std::move(other));
  }


  RuntimeReference<DlSystem::Runtime_t> runtime{*this, DlSystem::Runtime_t::CPU_FLOAT32};
  RuntimeListReference<DlSystem::RuntimeList> runtimeList{*this};
  PerfProfileReference<DlSystem::PerformanceProfile_t> perfProfile{*this, DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE};
  InputDimensionsMapReference<DlSystem::TensorShapeMap> inputDimensionsMap{*this};
  EnableCPUFallbackReference<bool> enableCPUFallback{*this, false};
  PlatformConfigReference<DlSystem::PlatformConfig> platformConfig{*this};

};


class RuntimeConfigList :
public Wrapper<RuntimeConfigList, Snpe_RuntimeConfigList_Handle_t, true>
{
private:
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_RuntimeConfigList_Delete};
public:
  RuntimeConfigList()
    : BaseType(Snpe_RuntimeConfigList_Create())
  {  }
  RuntimeConfigList(size_t size)
  : BaseType(Snpe_RuntimeConfigList_CreateSize(size))
  {  }

  RuntimeConfigList(RuntimeConfigList&& other) noexcept
    : BaseType(std::move(other))
  {  }

  RuntimeConfigList& operator=(RuntimeConfigList&& other) noexcept{
    return moveAssign(std::move(other));
  }
  RuntimeConfigList& operator=(const RuntimeConfigList& other){
    Snpe_RuntimeConfigList_Assign(other.handle(), handle());
    return *this;
  }

/**
 * @brief Push runtime config into runtime config list.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Error code. Returns SNPE_SUCCESS if runtime config pushed successfully.
 */
  void push_back(const RuntimeConfig& runtimeConfig){
    Snpe_RuntimeConfigList_PushBack(handle(), getHandle(runtimeConfig));
  }

/**
 * @brief Copy-assigns the contents of rclSrcHandle into rclDstHandle.
 *
 * @param[in] idx Destination RuntimeConfigList handle.
 *
 * @return SNPE_SUCCESS on successful copy-assignment.
 */
  RuntimeConfig& operator[](size_t index){
    return *makeReference<RuntimeConfig>(Snpe_RuntimeConfigList_At_Ref(handle(), index));
  }

/**
 * @brief Copy-assigns the contents of rclSrcHandle into rclDstHandle.
 *
 * @param[in] idx Destination RuntimeConfigList handle.
 *
 * @return SNPE_SUCCESS on successful copy-assignment.
 */
  const RuntimeConfig& operator[](size_t index) const{
    return *makeReference<RuntimeConfig>(Snpe_RuntimeConfigList_At_Ref(handle(), index));
  }

/**
 * @brief Returns the number of runtime configs in the list.
 *
 * @return Returns number of entries in the runtimeConfigList.
 */
  size_t size() const noexcept{
    return Snpe_RuntimeConfigList_Size(handle());
  }

/**
 * @brief Returns the capacity of runtime configs in the list.
 *
 * @return Returns number of capacity in the runtimeConfigList.
 */
  size_t capacity() const noexcept{
    return Snpe_RuntimeConfigList_Capacity(handle());
  }

/**
 * @brief Removes all runtime configs from the list.
 *
 * @return Error code. Returns SNPE_SUCCESS if runtime config list is cleared successfully.
 */
  void clear() noexcept{
    Snpe_RuntimeConfigList_Clear(handle());
  }

};

} // ns PSNPE


ALIAS_IN_ZDL_NAMESPACE(PSNPE, RuntimeConfig)
ALIAS_IN_ZDL_NAMESPACE(PSNPE, RuntimeConfigList)
