//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

/**
 *  @file
 */

#ifndef DL_SYSTEM_SNPE_PERF_PROFILE_H
#define DL_SYSTEM_SNPE_PERF_PROFILE_H

#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif

#include "DlSystem/DlError.h"
#include "DlSystem/DlEnums.h"
#include "DlSystem/SnpeApiExportDefine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A typedef to indicate a SNPE StringList handle
 */
typedef void* Snpe_SNPEPerfProfile_Handle_t;

/**
 * @brief Constructs a SNPEPerfProfile and returns a handle to it
 *
 * @return The handle to the created SNPEPerfProfile
 */
SNPE_API
Snpe_SNPEPerfProfile_Handle_t Snpe_SNPEPerfProfile_Create();

/**
 * @brief Constructs a SNPEPerfProfile copy from another SNPEPerfProfile and returns a handle to it
 *
 * @return The handle to the created SNPEPerfProfile
 */
SNPE_API
Snpe_SNPEPerfProfile_Handle_t Snpe_SNPEPerfProfile_CreateCopy(Snpe_SNPEPerfProfile_Handle_t other);

/**
 * @brief Constructs a SNPEPerfProfile from a preset and returns a handle to it
 *
 * @return The handle to the created SNPEPerfProfile
 */
SNPE_API
Snpe_SNPEPerfProfile_Handle_t Snpe_SNPEPerfProfile_CreatePreset(Snpe_PerformanceProfile_t preset);

/**
 * @brief Delete the SNPEPerfProfile handle
 *
 * @return SNPE_SUCCESS on successful handle deletion.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_Delete(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);



/**
 * @brief Copy-assigns the contents of src into dst
 *
 * @param src Source SNPEPerfProfile handle
 * @param dst Destination SNPEPerfProfile handle
 *
 * @return SNPE_SUCCESS on successful copy-assignment
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_Assign(Snpe_SNPEPerfProfile_Handle_t src, Snpe_SNPEPerfProfile_Handle_t dst);


/**
 * @brief Updated the perfprofile handle parameters from existing perfprofile handle
 *
 * @param updateThis  SNPEPerfProfile handle to be updated
 * @param fromThis    SNPEPerfProfile handle from parameters to be updated
 *
 * @return SNPE_SUCCESS on successful application of parameters
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_AppySetParameters(Snpe_SNPEPerfProfile_Handle_t updateThis, Snpe_SNPEPerfProfile_Handle_t fromThis);

/**
 * @brief Set DCVS enable/disable for the start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param value The DCVS value as a true/false bool
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetEnableDspDcvsStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle, Snpe_DspPerf_DcvsEnable_t value);

/**
 * @brief Get DCVS enable/disable for the start of event, for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The DCVS status for the start of event for the perfProfileHandle
 */
SNPE_API
Snpe_DspPerf_DcvsEnable_t Snpe_SNPEPerfProfile_GetEnableDspDcvsStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
* @brief Set DCVS enable/disable for the end of event for perfProfileHandle
*
* @param perfProfileHandle Current SNPEPerfProfile handle to be updated
* @param value The DCVS value as a true/false bool
*
* @return SNPE_SUCCESS if the parameters were successfully set
*/

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetEnableDspDcvsDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle, Snpe_DspPerf_DcvsEnable_t value);

/**
* @brief Get DCVS enable/disable for the end of event, for perfProfileHandle
*
* @param perfProfileHandle Current SNPEPerfProfile handle
*
* @return The DCVS status for end of event for the perfProfileHandle
*/
SNPE_API
Snpe_DspPerf_DcvsEnable_t Snpe_SNPEPerfProfile_GetEnableDspDcvsDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set SleepLatency for the start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param sleepLatency Sleep latency values
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetSleepLatencyStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                      Snpe_DspPerf_SleepLatency_t sleepLatency);

/**
 * @brief Get SleepLatency for the start of event, for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The SleepLatency for the start of event for perfProfileHandle
 */
SNPE_API
Snpe_DspPerf_SleepLatency_t Snpe_SNPEPerfProfile_GetSleepLatencyStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set SleepLatency for the end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param sleepLatency Sleep latency values
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetSleepLatencyDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
        Snpe_DspPerf_SleepLatency_t sleepLatency);

/**
 * @brief Get SleepLatency for the end of event, for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The SleepLatency for the end of the event for the perfProfileHandle
 */

SNPE_API
Snpe_DspPerf_SleepLatency_t Snpe_SNPEPerfProfile_GetSleepLatencyDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);
/**
 * @brief Set rpcpolling time for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param rpcPollingTime Rpc polling time
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDspRpcPollingTime(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                        Snpe_DspPerf_RpcPollingTime_t rpcPollingTime);

/**
 * @brief Get rpc polling time for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The rpc polling time for the perfProfileHandle
 */
SNPE_API
Snpe_DspPerf_RpcPollingTime_t Snpe_SNPEPerfProfile_GetDspRpcPollingTime(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set hysteresisTime time for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param hysteresisTime The hysteresis time
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDspHysteresisTime(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                        Snpe_DspPerf_HysteresisTime_t hysteresisTime);

/**
 * @brief Get hysteresisTime for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The hysteresis time for the perfProfileHandle
 */

SNPE_API
Snpe_DspPerf_HysteresisTime_t Snpe_SNPEPerfProfile_GetDspHysteresisTime(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set if async voting enable for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param asyncVotingEnable Async voting enable true/false
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetEnableAsyncVoting(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                           Snpe_DspPerf_AsyncVoteEnable_t asyncVotingEnable);

/**
 * @brief Get if AsyncVoteEnable for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The hysteresis time for the perfProfileHandle
 */

SNPE_API
Snpe_DspPerf_AsyncVoteEnable_t Snpe_SNPEPerfProfile_GetEnableAsyncVoting(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set sleepDisable for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param sleepDisable, sleep disable value
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetSleepDisable(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                      Snpe_DspPerf_SleepDisable_t sleepDisable);
/**
 * @brief Get sleepDisable for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return The hysteresis time for the perfProfileHandle
 */

SNPE_API
Snpe_DspPerf_SleepDisable_t Snpe_SNPEPerfProfile_GetSleepDisable(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set powerMode for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param powerMode Powermode value
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetPowerModeStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                   Snpe_DspPerf_PowerMode_t powerMode);

/**
 * @brief Get powerMode for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return powerMode values for the start of event
 */

SNPE_API
Snpe_DspPerf_PowerMode_t Snpe_SNPEPerfProfile_GetPowerModeStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Get powerMode for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return powerMode values for the end of event
 */

SNPE_API
Snpe_DspPerf_PowerMode_t Snpe_SNPEPerfProfile_GetPowerModeDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set powerMode for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param powerMode Powermode value
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetPowerModeDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
Snpe_DspPerf_PowerMode_t powerMode);

/**
 * @brief Set BusVoltageCornerMin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param minMvStart Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetBusVoltageCornerMinStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                  Snpe_DspPerf_VoltageCorner_t minMvStart);

/**
 * @brief Get BusVoltageCornerMin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerMin values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetBusVoltageCornerMinStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set BusVoltageCornerMin for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param minMvDone Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetBusVoltageCornerMinDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                 Snpe_DspPerf_VoltageCorner_t minMvDone);
/**
 * @brief Get BusVoltageCornerMin for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerMin values for the end of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetBusVoltageCornerMinDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set BusVoltageCornerTarget for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param targetMvStart Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetBusVoltageCornerTargetStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                     Snpe_DspPerf_VoltageCorner_t targetMvStart);

/**
 * @brief Get BusVoltageCornerTarget for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerTarget values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetBusVoltageCornerTargetStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set BusVoltageCornerTarget for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param targetMvDone Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetBusVoltageCornerTargetDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                    Snpe_DspPerf_VoltageCorner_t targetMvDone);

/**
 * @brief Get BusVoltageCornerTarget for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerTarget values for the end of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetBusVoltageCornerTargetDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set BusVoltageCornerMax for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param maxMvStart Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetBusVoltageCornerMaxStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                  Snpe_DspPerf_VoltageCorner_t maxMvStart);
/**
 * @brief Get BusVoltageCornerMax  for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerMax  values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetBusVoltageCornerMaxStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set BusVoltageCornerMax for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param maxMvDone Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetBusVoltageCornerMaxDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                 Snpe_DspPerf_VoltageCorner_t maxMvDone);

/**
 * @brief Get BusVoltageCornerMax for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerMax values for the end  of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetBusVoltageCornerMaxDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set CoreVoltageCornermin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param minMvStart Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetCoreVoltageCornerminMvStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                     Snpe_DspPerf_VoltageCorner_t minMvStart);

/**
 * @brief Get CoreVoltageCornermin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return CoreVoltageCornermin values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetCoreVoltageCornerMinMvStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set CoreVoltageCornerMin for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param minMvDone Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetCoreVoltageCornerMinMvDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                    Snpe_DspPerf_VoltageCorner_t minMvDone);

/**
 * @brief Get CoreVoltageCornerMin for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return CoreVoltageCornerMin values for the end of event
 */
SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetCoreVoltageCornerMinMvDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set CoreVoltageCornerTarget for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param targetMvStart Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetCoreVoltageCornerTargetMvStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                        Snpe_DspPerf_VoltageCorner_t targetMvStart);

/**
 * @brief Get CoreVoltageCornerTarget for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return CoreVoltageCornerTarget values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetCoreVoltageCornerTargetMvStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set CoreVoltageCornerTarget for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param targetMvDone Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetCoreVoltageCornerTargetMvDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                       Snpe_DspPerf_VoltageCorner_t targetMvDone);

/**
 * @brief Get CoreVoltageCornerTarget for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return CoreVoltageCornerTarget values for the end of event
 */
SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetCoreVoltageCornerTargetMvDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set CoreVoltageCornerMax for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param maxMvStart Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetCoreVoltageCornerMaxMvStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                     Snpe_DspPerf_VoltageCorner_t maxMvStart);

/**
 * @brief Get BusVoltageCornerMin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return BusVoltageCornerMin values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetCoreVoltageCornerMaxMvStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set CoreVoltageCornerMax for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param maxMvDone Voltage corner value, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetCoreVoltageCornerMaxMvDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                    Snpe_DspPerf_VoltageCorner_t maxMvDone);

/**
 * @brief Get CoreVoltageCornerMax for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return CoreVoltageCornerMax values for the end of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetCoreVoltageCornerMaxMvDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set DcvsVCornerMin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param dcvsVCornerMinStart Voltage corner value, for the v66 DSP architecture
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMinStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                              Snpe_DspPerf_VoltageCorner_t dcvsVCornerMinStart);

/**
 * @brief Get DcvsVCornerMin for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return DcvsVCornerMin values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMinStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set DcvsVCornerMin for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param dcvsVCornerMinDone Voltage corner value, for the v66 DSP architecture
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMinDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                             Snpe_DspPerf_VoltageCorner_t dcvsVCornerMinDone);

/**
 * @brief Get DcvsVCornerMin for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return DcvsVCornerMin values for the end of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMinDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);


/**
 * @brief Set DcvsVCornerMax for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param dcvsVCornerMaxStart Voltage corner value, for the v66 DSP architecture
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMaxStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                              Snpe_DspPerf_VoltageCorner_t dcvsVCornerMaxStart);

/**
 * @brief Get DcvsVCornerMax for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return DcvsVCornerMax values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMaxStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set DcvsVCornerMax for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param dcvsVCornerMaxDone Voltage corner value, for the v66 DSP architecture
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerMaxDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                             Snpe_DspPerf_VoltageCorner_t dcvsVCornerMaxDone);

/**
 * @brief Get DcvsVCornerMax for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return DcvsVCornerMax values for the end of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerMaxDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set DcvsVCornerTarget for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param dcvsVCornerTargetStart Voltage corner value, for the v66 DSP architecture
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerTargetStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                                 Snpe_DspPerf_VoltageCorner_t dcvsVCornerTargetStart);

/**
 * @brief Get DcvsVCornerTarget for start of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return DcvsVCornerTarget values for the start of event
 */

SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerTargetStart(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set DcvsVCornerTarget for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param dcvsVCornerTargetDone Voltage corner value, for the v66 DSP architecture
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */


SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetDcvsVoltageCornerDcvsVCornerTargetDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                                                Snpe_DspPerf_VoltageCorner_t dcvsVCornerTargetDone);

/**
 * @brief Get DcvsVCornerTarget for end of event for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return DcvsVCornerTarget values for the end of event
 */


SNPE_API
Snpe_DspPerf_VoltageCorner_t Snpe_SNPEPerfProfile_GetDcvsVoltageCornerDcvsVCornerTargetDone(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set HighPerformance mode(true/false) to use CPU in prime core
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param value True/false
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetHighPerformanceModeEnabled(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle, Snpe_HighPerformanceModeEnabled_t value);


/**
 * @brief Get HighPerformance mode for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 *
 * @return HighPerformanceModeEnabled(true/false)
 */

SNPE_API
Snpe_HighPerformanceModeEnabled_t Snpe_SNPEPerfProfile_GetHighPerformanceModeEnabled(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set Fast Init mode(true/false) to do init with high perf mode for power saver/High Power saver/Low Power saver perf mode
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param value True/false
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetFastInitEnabled(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle, Snpe_FastInitModeEnabled_t value);

/**
 * @brief Get Fast Init mode(true/false) values
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return FastInitEnabled(true/false)
 */

SNPE_API
Snpe_FastInitModeEnabled_t Snpe_SNPEPerfProfile_GetFastInitEnabled(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Set HMX CLK PerfMode for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param clkPerfMode  Clk perf mode to apply voltage corner, for DSP architectures v68 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetHmxClkPerfMode(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                        Snpe_DspHmx_ClkPerfMode_t clkPerfMode);

/**
 * @brief Set HMX Min Voltage Corner for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param hmxVoltageCornerMin HmxMinVoltage corner value, for DSP architectures v75 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetHmxVoltageCornerMin(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle, Snpe_DspHmx_ExpVoltageCorner_t hmxVoltageCornerMin);

/**
 * @brief Set HMX Target Voltage Corner for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param hmxVoltageCornerTarget  HmxTarget Voltage corner value, for DSP architectures v75 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetHmxVoltageCornerTarget(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle, Snpe_DspHmx_ExpVoltageCorner_t hmxVoltageCornerTarget);


/**
 * @brief Set HMX Max Voltage Corner for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle to be updated
 * @param hmxVoltageCornermax HmxMax Voltage corner value, for DSP architectures v75 and above
 *
 * @return SNPE_SUCCESS if the parameters were successfully set
 */

SNPE_API
Snpe_ErrorCode_t Snpe_SNPEPerfProfile_SetHmxVoltageCornerMax(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle,
                                                             Snpe_DspHmx_ExpVoltageCorner_t hmxVoltageCornerMax);

/**
 * @brief Get HMX Clock Perf Mode for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return HmxClockPerfMode for current Perf config
 */

SNPE_API
Snpe_DspHmx_ClkPerfMode_t Snpe_SNPEPerfProfile_GetHmxClkPerfMode(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);


/**
 * @brief Get HMX Voltage Corner Min for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return HmxMin Voltage Corner for current Perf config
 */

SNPE_API
Snpe_DspHmx_ExpVoltageCorner_t Snpe_SNPEPerfProfile_GetHmxVoltageCornerMin(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Get HMX Voltage Corner Target for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return HmxTarget Voltage Corner for current Perf config
 */

SNPE_API
Snpe_DspHmx_ExpVoltageCorner_t Snpe_SNPEPerfProfile_GetHmxVoltageCornerTarget(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Get HMX Voltage Corner Max for perfProfileHandle
 *
 * @param perfProfileHandle Current SNPEPerfProfile handle
 *
 * @return HmxMax Voltage Corner for current Perf config
 */

SNPE_API
Snpe_DspHmx_ExpVoltageCorner_t Snpe_SNPEPerfProfile_GetHmxVoltageCornerMax(Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // DL_SYSTEM_SNPE_PERF_PROFILE_H
