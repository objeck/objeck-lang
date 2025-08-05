//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================
#pragma once
#include "Wrapper.hpp"
#include "DlSystem/DlError.hpp"
#include "DlSystem/DlEnums.hpp"
#include "DlSystem/DlEnums.h"

#include "DlSystem/SNPEPerfProfile.h"


namespace DlSystem {

class SNPEPerfProfile :
public Wrapper<SNPEPerfProfile, Snpe_SNPEPerfProfile_Handle_t>
{
  friend BaseType;
  using BaseType::BaseType;
  static constexpr DeleteFunctionType DeleteFunction = Snpe_SNPEPerfProfile_Delete;
public:
  SNPEPerfProfile()
    : BaseType(Snpe_SNPEPerfProfile_Create())
  {  }
  explicit SNPEPerfProfile(zdl::DlSystem::PerformanceProfile_t preset)
    : BaseType(Snpe_SNPEPerfProfile_CreatePreset(static_cast<Snpe_PerformanceProfile_t>(preset)))
  {  }
  SNPEPerfProfile(const SNPEPerfProfile& other)
    : BaseType(Snpe_SNPEPerfProfile_CreateCopy(other.handle()))
  {  }
  SNPEPerfProfile(SNPEPerfProfile&& other) noexcept
    : BaseType(std::move(other))
  {  }


/**
 * @brief Overloaded assignment operator to copy const snpeperfprofile
 *
 * @param other snpeperfprofile
 *
 * @return new snpeperfprofile
 */
  SNPEPerfProfile& operator=(const SNPEPerfProfile& other){
    if(this != &other){
      Snpe_SNPEPerfProfile_Assign(other.handle(), handle());
    }
    return *this;
  }

/**
 * @brief Overloaded assignment operator to copy snpeperfprofile
 *
 * @param other snpeperfprofile
 *
 * @return new snpeperfprofile
 */
  SNPEPerfProfile& operator=(SNPEPerfProfile&& other) noexcept{
    return moveAssign(std::move(other));
  }

/**
 * @brief Set DCVS enable/disable for the start of event.
 *
 * @param value, DCVS value true/false
 *
 * @return true if the parameters were successfully set
 */
  bool setEnableDcvsStart(DspPerfDcvsEnable_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetEnableDspDcvsStart(handle(), static_cast<Snpe_DspPerf_DcvsEnable_t>(value));
  }

/**
 * @brief Get DCVS enable/disable for the start of event
 *
 * @param perfProfileHandle
 *
 * @return the DCVS status for the start of event
 */
  DspPerfDcvsEnable_t getEnableDcvsStart() const{
    return static_cast<DspPerfDcvsEnable_t>(Snpe_SNPEPerfProfile_GetEnableDspDcvsStart(handle()));
  }

/**
* @brief Set DCVS enable/disable for the end of event.
*
* @param value, DCVS value true/false
*
* @return true if the parameters were successfully set
*/
  bool setEnableDcvsDone(DspPerfDcvsEnable_t value){
      return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetEnableDspDcvsDone(handle(), static_cast<Snpe_DspPerf_DcvsEnable_t>(value));
  }

/**
* @brief Get DCVS enable/disable for the end of event.
*
* @return The DCVS status for end of event
*/
  DspPerfDcvsEnable_t getEnableDcvsDone() const{
      return static_cast<DspPerfDcvsEnable_t>(Snpe_SNPEPerfProfile_GetEnableDspDcvsDone(handle()));
  }

/**
 * @brief Set SleepLatency for the start of event.
 *
 * @param sleepLatency Sleep latency values
 *
 * @return true if the parameters were successfully set
 */
  bool setSleepLatencyStart(DspPerfSleepLatency_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetSleepLatencyStart(handle(), static_cast<Snpe_DspPerf_SleepLatency_t>(value));
  }

/**
 * @brief Get SleepLatency for the start of event.
 *
 * @return The SleepLatency for the start of event
 */
  DspPerfSleepLatency_t getSleepLatencyStart() const{
      return static_cast<DspPerfSleepLatency_t>(Snpe_SNPEPerfProfile_GetSleepLatencyStart(handle()));
  }

/**
 * @brief Set SleepLatency for the end of event.
 *
 * @param sleepLatency Sleep latency values
 *
 * @return true if the parameters were successfully set
 */
  bool setSleepLatencyDone(DspPerfSleepLatency_t value){
      return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetSleepLatencyDone(handle(), static_cast<Snpe_DspPerf_SleepLatency_t>(value));
  }

/**
 * @brief Get SleepLatency for the end of event
 *
 * @return The SleepLatency for the end of the event.
 */
  DspPerfSleepLatency_t getSleepLatencyDone() const{
      return static_cast<DspPerfSleepLatency_t>(Snpe_SNPEPerfProfile_GetSleepLatencyDone(handle()));
  }

/**
 * @brief Set rpcpolling time.
 *
 * @param rpcPollingTime Rpc polling time
 *
 * @return true if the parameters were successfully set
 */
  bool setRpcPollingTime(DspPerfRpcPollingTime_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDspRpcPollingTime(handle(), static_cast<Snpe_DspPerf_RpcPollingTime_t>(value));
  }

/**
 * @brief Get rpc polling time.
 *
 * @return The rpc polling time
 */
  DspPerfRpcPollingTime_t getRpcPollingTime() const{
    return static_cast<DspPerfSleepLatency_t>(Snpe_SNPEPerfProfile_GetDspRpcPollingTime(handle()));
  }

/**
 * @brief Set hysteresisTime time
 *
 * @param hysteresisTime Hysteresis time
 *
 * @return true if the parameters were successfully set
 */
  bool setHysteresisTime(DspPerfHysteresisTime_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDspHysteresisTime(handle(), static_cast<Snpe_DspPerf_HysteresisTime_t>(value));
  }

/**
 * @brief Get hysteresisTime.
 *
 * @return The hysteresis time.
 */
  DspPerfHysteresisTime_t getHysteresisTime() const{
    return static_cast<DspPerfHysteresisTime_t>(Snpe_SNPEPerfProfile_GetDspHysteresisTime(handle()));
  }

/**
 * @brief Set if async voting enable for perfProfileHandle
 *
 * @param asyncVotingEnable Async voting enable true/false
 *
 * @return true if the parameters were successfully set
 */
  bool setEnableAsyncVoting(DspPerfAsyncVoteEnable_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetEnableAsyncVoting(handle(), static_cast<Snpe_DspPerf_AsyncVoteEnable_t>(value));
  }

/**
 * @brief Get if AsyncVoteEnable.
 *
 * @return The hysteresis time
 */
  DspPerfAsyncVoteEnable_t getEnableAsyncVoting() const{
    return static_cast<DspPerfAsyncVoteEnable_t>(Snpe_SNPEPerfProfile_GetEnableAsyncVoting(handle()));
  }

/**
 * @brief Set sleepDisable.
 *
 * @param sleepDisable Sleep disable value
 *
 * @return true if the parameters were successfully set
 */
  bool setSleepDisable(DspPerfSleepDisable_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetSleepDisable(handle(), static_cast<Snpe_DspPerf_SleepDisable_t>(value));
  }

/**
 * @brief Get sleepDisable.
 *
 * @return The hysteresis time.
 */
  DspPerfSleepDisable_t setSleepDisable() const{
    return static_cast<DspPerfSleepDisable_t>(Snpe_SNPEPerfProfile_GetSleepDisable(handle()));
  }

/**
 * @brief Set powermode for start of event.
 *
 * @param powerMode Powermode value
 *
 * @return true if the parameters were successfully set
 */
  bool setPowerModeStart(DspPerfPowerMode_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetPowerModeStart(handle(), static_cast<Snpe_DspPerf_PowerMode_t>(value));
  }

/**
 * @brief Get powermode for start of event.
 *
 * @return powermode values for the start of event
 */
  DspPerfPowerMode_t getPowerModeStart() const{
    return static_cast<DspPerfPowerMode_t>(Snpe_SNPEPerfProfile_GetPowerModeStart(handle()));
  }

/**
 * @brief Set powermode for end of event.
 *
 * @param powerMode Powermode value
 *
 * @return true if the parameters were successfully set
 */
  bool setPowerModeDone(DspPerfPowerMode_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetPowerModeDone(handle(), static_cast<Snpe_DspPerf_PowerMode_t>(value));
  }

/**
 * @brief Get powermode for end of event.
 *
 * @return powermode values for the end of event
 */
  DspPerfPowerMode_t getPowerModeDone() const{
    return static_cast<DspPerfPowerMode_t>(Snpe_SNPEPerfProfile_GetPowerModeDone(handle()));
  }

/**
 * @brief Set BusVoltageCornerMin for start of event.
 *
 * @param minMvStart, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setBusVoltageCornerMinStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetBusVoltageCornerMinStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerMin for start of event.
 *
 * @return BusVoltageCornerMin values for the start of event
 */
  DspPerfVoltageCorner_t getBusVoltageCornerMinStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetBusVoltageCornerMinStart(handle()));
  }

/**
 * @brief Set BusVoltageCornerMin for end of event.
 *
 * @param minMvDone, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setBusVoltageCornerMinDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetBusVoltageCornerMinDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerMin for end of event.
 *
 * @return BusVoltageCornerMin values for the end of event
 */
  DspPerfVoltageCorner_t getBusVoltageCornerMinDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetBusVoltageCornerMinDone(handle()));
  }

/**
 * @brief Set BusVoltageCornerTargetStart for end of event.
 *
 * @param targetMvStart, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setBusVoltageCornerTargetStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetBusVoltageCornerTargetStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerTargetStart for end of event for perfProfileHandle
 *
 * @return BusVoltageCornerTargetStart values for the end of event
 */
  DspPerfVoltageCorner_t getBusVoltageCornerTargetStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetBusVoltageCornerTargetStart(handle()));
  }

/**
 * @brief Set BusVoltageCornerTargetDone for end of event.
 *
 * @param targetMvDone, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setBusVoltageCornerTargetDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetBusVoltageCornerTargetDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerTargetDone for end of event  for perfProfileHandle
 *
 * @return BusVoltageCornerTargetDone values for the end of event
 */
  DspPerfVoltageCorner_t getBusVoltageCornerTargetDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetBusVoltageCornerTargetDone(handle()));
  }

/**
 * @brief Set BusVoltageCornerMax for start of event.
 *
 * @param maxMvStart, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setBusVoltageCornerMaxStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetBusVoltageCornerMaxStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerMax for start of event for perfProfileHandle
 *
 * @return BusVoltageCornerMax values for the start of event
 */
  DspPerfVoltageCorner_t getBusVoltageCornerMaxStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetBusVoltageCornerMaxStart(handle()));
  }

/**
 * @brief Set BusVoltageCornerMax for End of event for perfProfileHandle
 *
 * @param maxMvDone, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setBusVoltageCornerMaxDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetBusVoltageCornerMaxDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerMax for end of event.
 *
 * @return BusVoltageCornerMax values for the end  of event
 */
  DspPerfVoltageCorner_t getBusVoltageCornerMaxDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetBusVoltageCornerMaxDone(handle()));
  }

/**
 * @brief Set CoreVoltageCornermin for start of event.
 *
 * @param minMvStart, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setCoreVoltageCornerminMvStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetCoreVoltageCornerminMvStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get CoreVoltageCornermin for start of event.
 *
 * @return CoreVoltageCornermin values for the start of event
 */
  DspPerfVoltageCorner_t getCoreVoltageCornerminMvStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetCoreVoltageCornerMinMvStart(handle()));
  }

/**
 * @brief Set CoreVoltageCornerMin for End of event.
 *
 * @param minMvDone, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setCoreVoltageCornerMinMvDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetCoreVoltageCornerMinMvDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Set CoreVoltageCornerTarget for start of event.
 *
 * @param targetMvStart, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setCoreVoltageCornerTargetMvStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetCoreVoltageCornerTargetMvStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get CoreVoltageCornerTarget for start of event.
 *
 * @return CoreVoltageCornerTarget values for the start of event
 */
  DspPerfVoltageCorner_t getCoreVoltageCornerTargetMvStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetCoreVoltageCornerTargetMvStart(handle()));
  }

/**
 * @brief Set CoreVoltageCornerTarget for end of event.
 *
 * @param targetMvDone, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setCoreVoltageCornerTargetMvDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetCoreVoltageCornerTargetMvDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get CoreVoltageCornerTarget for end of event.
 *
 * @return CoreVoltageCornerTarget values for the end of event
 */
  DspPerfVoltageCorner_t getCoreVoltageCornerTargetMvDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetCoreVoltageCornerTargetMvDone(handle()));
  }

/**
 * @brief Set CoreVoltageCornerMax for start of event.
 *
 * @param maxMvStart, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setCoreVoltageCornerMaxMvStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetCoreVoltageCornerMaxMvStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get BusVoltageCornerMin for start of event.
 *
 * @return BusVoltageCornerMin values for the start of event
 */
  DspPerfVoltageCorner_t getCoreVoltageCornerMaxMvStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetCoreVoltageCornerMaxMvStart(handle()));
  }

/**
 * @brief Set CoreVoltageCornerMax for end of event  for perfProfileHandle
 *
 * @param maxMvDone, voltage corner value, for DSP architectures v68 and above
 *
 * @return true if the parameters were successfully set
 */
  bool setCoreVoltageCornerMaxMvDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetCoreVoltageCornerMaxMvDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get CoreVoltageCornerMax for end of event.
 *
 * @return CoreVoltageCornerMax values for the end of event
 */
  DspPerfVoltageCorner_t getCoreVoltageCornerMaxMvDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetCoreVoltageCornerMaxMvDone(handle()));
  }

/**
 * @brief Set DcvsVCornerMin for start of event.
 *
 * @param dcvsVCornerMinStart, voltage corner value, for DSP arch v66
 *
 * @return true if the parameters were successfully set
 */
  bool setDcvsVoltageCornerDcvsVCornerMinStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMinStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get DcvsVCornerMin for start of event
 *
 * @return DcvsVCornerMin values for the start of event
 */
  DspPerfVoltageCorner_t getDcvsVoltageCornerDcvsVCornerMinStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMinStart(handle()));
  }

/**
 * @brief Set DcvsVCornerMin for end of event.
 *
 * @param dcvsVCornerMinDone, voltage corner value, for DSP arch v66
 *
 * @return true if the parameters were successfully set
 */
  bool setDcvsVoltageCornerDcvsVCornerMinDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMinDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get DcvsVCornerMin for end of event
 *
 * @return DcvsVCornerMin values for the end of event
 */
  DspPerfVoltageCorner_t getDcvsVoltageCornerDcvsVCornerMinDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMinDone(handle()));
  }

/**
 * @brief Set DcvsVCornerMax for start of event for perfProfileHandle
 *
 * @param dcvsVCornerMaxStart, voltage corner value, for DSP arch v66
 *
 * @return true if the parameters were successfully set
 */
  bool setDcvsVoltageCornerDcvsVCornerMaxStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMaxStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get DcvsVCornerMax for start of event.
 *
 * @return DcvsVCornerMax values for the start of event
 */
  DspPerfVoltageCorner_t getDcvsVoltageCornerDcvsVCornerMaxStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMaxStart(handle()));
  }

/**
 * @brief Set DcvsVCornerMax for end of event.
 *
 * @param dcvsVCornerMaxDone, voltage corner value, for DSP arch v66
 *
 * @return true if the parameters were successfully set
 */
  bool setDcvsVoltageCornerDcvsVCornerMaxDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMaxDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get DcvsVCornerMax for end of event.
 *
 * @return DcvsVCornerMax values for the end of event
 */
  DspPerfVoltageCorner_t getDcvsVoltageCornerDcvsVCornerMaxDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMaxDone(handle()));
  }

/**
 * @brief Set DcvsVCornerTarget for start of event.
 *
 * @param dcvsVCornerTargetStart, voltage corner value, for DSP arch v66
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */
  bool setDcvsVoltageCornerDcvsVCornerTargetStart(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerTargetStart(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get DcvsVCornerTarget for start of event.
 *
 * @return DcvsVCornerTarget values for the start of event
 */
  DspPerfVoltageCorner_t getDcvsVoltageCornerDcvsVCornerTargetStart() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerTargetStart(handle()));
  }

/**
 * @brief Set DcvsVCornerTarget for end of event for perfProfileHandle
 *
 * @param dcvsVCornerTargetDone, voltage corner value, for DSP arch v66
 *
 * @return true if the parameters were successfully set
 */
  bool setDcvsVoltageCornerDcvsVCornerTargetDone(DspPerfVoltageCorner_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerTargetDone(handle(), static_cast<Snpe_DspPerf_VoltageCorner_t>(value));
  }

/**
 * @brief Get DcvsVCornerTarget for end of event.
 *
 * @return DcvsVCornerTarget values for the end of event
 */
  DspPerfVoltageCorner_t getDcvsVoltageCornerDcvsVCornerTargetDone() const{
    return static_cast<DspPerfVoltageCorner_t>(Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerTargetDone(handle()));
  }

/**
 * @brief Set HighPerformance mode(true/false) to use CPU in prime core
 *
 * @param value, true/false
 *
 * @return true if the parameters were successfully set
 */
  bool setHighPerformanceModeEnabled(HighPerformanceModeEnabled_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetHighPerformanceModeEnabled(handle(),static_cast<Snpe_HighPerformanceModeEnabled_t>(value));
  }

/**
 * @brief Get HighPerformance mode.
 *
 * @param perfProfileHandle, current SNPEPerfProfile handle
 *
 * @return HighPerformanceModeEnabled(true/false)
 */
  HighPerformanceModeEnabled_t getHighPerformanceModeEnabled() const{
     return static_cast<HighPerformanceModeEnabled_t>(Snpe_SNPEPerfProfile_GetHighPerformanceModeEnabled(handle()));
  }

  /**
  * @brief Set Fast Init mode(true/false) to do init with high perf mode for power saver/High Power saver/Low Power saver perf mode
  *
  * @param value, true/false
  *
  * @return true if the parameters were successfully set
  */
  bool setFastInitEnabled(FastInitModeEnabled_t value){
    return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetFastInitEnabled(handle(),static_cast<Snpe_FastInitModeEnabled_t>(value));
  }

/**
 * @brief Get Fast Init mode(true/false) values
 *
 * @return FastInitEnabled(true/false)
 */
  FastInitModeEnabled_t getFastInitEnabled() const{
    return static_cast<FastInitModeEnabled_t>(Snpe_SNPEPerfProfile_GetFastInitEnabled(handle()));
  }

/**
 * @brief Set HmxClkPerfMode value for the Perf config
 *
 * @param HmxClkPerfMode, voltage corner value
 *
 * @return true if the parameters were successfully set
 */
  bool setHmxClkPerfMode(const DspHmx_ClkPerfMode_t& value) {
      return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetHmxClkPerfMode(handle(), static_cast<Snpe_DspHmx_ClkPerfMode_t>(value));
  }

/**
 * @brief Get HMX Clock Perf Mode
 *
 * @return HmxClockPerfMode for current Perf config
 */
  DspHmx_ClkPerfMode_t getHmxClkPerfMode() const {
      return static_cast<DspHmx_ClkPerfMode_t>(Snpe_SNPEPerfProfile_GetHmxClkPerfMode(handle()));
  }

/**
 * @brief Set HmxVoltageCornerMin value for the Perf config
 *
 * @param HmxVoltageCornerMin, voltage corner value
 *
 * @return true if the parameters were successfully set
 */
  bool setHmxVoltageCornerMin(const DspHmx_ExpVoltageCorner_t& value) {
      return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetHmxVoltageCornerMin(handle(), static_cast<Snpe_DspHmx_ExpVoltageCorner_t>(value));
  }

/**
 * @brief Get HmxVoltageCornerMin
 *
 * @return HmxVoltageCornerMin value for current Perf config
 */
  DspHmx_ExpVoltageCorner_t getHmxVoltageCornerMin() const {
      return static_cast<DspHmx_ExpVoltageCorner_t>(Snpe_SNPEPerfProfile_GetHmxVoltageCornerMin(handle()));
  }

/**
 * @brief Set HmxVoltageCornerTarget value for the Perf config
 *
 * @param HmxVoltageCornerTarget, voltage corner value
 *
 * @return true if the parameters were successfully set
 */
  bool setHmxVoltageCornerTarget(const DspHmx_ExpVoltageCorner_t& value) {
      return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetHmxVoltageCornerTarget(handle(), static_cast<Snpe_DspHmx_ExpVoltageCorner_t>(value));
  }

/**
 * @brief Get HmxVoltageCornerTarget
 *
 * @return HmxVoltageCornerTarget value for current Perf config
 */
  DspHmx_ExpVoltageCorner_t getHmxVoltageCornerTarget() const {
      return static_cast<DspHmx_ExpVoltageCorner_t>(Snpe_SNPEPerfProfile_GetHmxVoltageCornerTarget(handle()));
  }

/**
 * @brief Set HmxVoltageCornerMax value for the Perf config
 *
 * @param HmxVoltageCornerMax, voltage corner value
 *
 * @return true if the parameters were successfully set
 */
  bool setHmxVoltageCornerMax(const DspHmx_ExpVoltageCorner_t& value) {
      return SNPE_SUCCESS == Snpe_SNPEPerfProfile_SetHmxVoltageCornerMax(handle(),  static_cast<Snpe_DspHmx_ExpVoltageCorner_t>(value));
  }

/**
 * @brief Get HmxVoltageCornerMax
 *
 * @return HmxVoltageCornerMax value for current Perf config
 */
  DspHmx_ExpVoltageCorner_t getHmxVoltageCornerMax() const {
    return static_cast<DspHmx_ExpVoltageCorner_t>(Snpe_SNPEPerfProfile_GetHmxVoltageCornerMax(handle()));
  }
};

} // ns DlSystem


ALIAS_IN_ZDL_NAMESPACE(DlSystem, SNPEPerfProfile)
