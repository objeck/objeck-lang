//==============================================================================
//
//  Copyright (c) 2022-2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

/**
 *  @file
 */

#ifndef _SNPE_RUNTIME_CONFIG_LIST_H_
#define _SNPE_RUNTIME_CONFIG_LIST_H_


#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif

#include "DlSystem/SnpeApiExportDefine.h"
#include "DlSystem/DlError.h"

#include "DlSystem/DlEnums.h"
#include "DlSystem/RuntimeList.h"
#include "DlSystem/TensorShapeMap.h"
#include "DlSystem/PlatformConfig.h"
#include "DlSystem/SNPEPerfProfile.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * A typedef to indicate a SNPE RuntimeConfig handle.
 */
typedef void* Snpe_RuntimeConfig_Handle_t;

/**
 * @brief Create a new runtime config.
 */
SNPE_API
Snpe_RuntimeConfig_Handle_t Snpe_RuntimeConfig_Create();

/**
 * @brief Copy-Constructs a RuntimeConfig and returns a handle to it.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return a runtime config object.
 */
SNPE_API
Snpe_RuntimeConfig_Handle_t Snpe_RuntimeConfig_CreateCopy(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Destroys the RuntimeConfig.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Error code. Returns SNPE_SUCCESS if destruction successful.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_Delete(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Returns the Runtime from runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Returns the Runtime.
*/
SNPE_API
Snpe_Runtime_t Snpe_RuntimeConfig_GetRuntime(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Set the Runtime into runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] runtime The Runtime.
 *
 * @return SNPE_SUCCESS on success.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetRuntime(Snpe_RuntimeConfig_Handle_t rcHandle, Snpe_Runtime_t runtime);

/**
 * @brief Set snpe runtime list into psnpe runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] rlHandle Handle to access the runtime list.
 *
 * @return Error code. Ruturns SNPE_SUCCESS if the runtime list added successfully.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetRuntimeList(Snpe_RuntimeConfig_Handle_t rcHandle, Snpe_RuntimeList_Handle_t rlHandle);

/**
 * @brief Get the snpe runtime list from runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Returns snpe runtime list.
*/
SNPE_API
Snpe_RuntimeList_Handle_t Snpe_RuntimeConfig_GetRuntimeList_Ref(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Get the performance profile type from runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Returns the Performance Profile.
*/
SNPE_API
Snpe_PerformanceProfile_t Snpe_RuntimeConfig_GetPerformanceProfile(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Set the performance profile into runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] perfProfile The performance profile level.
 *
 * @return Error code. Ruturns SNPE_SUCCESS if the runtime list added successfully.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetPerformanceProfile(Snpe_RuntimeConfig_Handle_t rcHandle, Snpe_PerformanceProfile_t perfProfile);

/**
 * @brief Set the custom performance profile into runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] perfProfileHandle Handle to access the SNPEPerfProfile.
 *
 * @return Error code. Ruturns SNPE_SUCCESS if the runtime list added successfully.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetCustomPerfProfile(Snpe_RuntimeConfig_Handle_t rcHandle, Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Get true(1) or false(0) about enabling cpu fallback for runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Returns 1 or 0.
*/
SNPE_API
int Snpe_RuntimeConfig_GetEnableCPUFallback(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Set 1 or 0 about enabling cpu fallback for runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] enableCpuFallback If true = 1 or false = 0.
 *
 * @return Returns SNPE_SUCCESS if the enableCpuFallback added successfully.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetEnableCPUFallback(Snpe_RuntimeConfig_Handle_t rcHandle, int enableCpuFallback);

/**
 * @brief Set the PlatformOptionLocal for runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] pcHandle Handle to access platformConfig.
 *
 * @return Returns SNPE_SUCCESS if the PlatformOptionLocal added successfully.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetPlatformOptionLocal(Snpe_RuntimeConfig_Handle_t rcHandle, Snpe_PlatformConfig_Handle_t pcHandle);

/**
 * @brief Get the PlatformOptionLocal from runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Returns Handle to access platformConfig.
*/
SNPE_API
Snpe_PlatformConfig_Handle_t Snpe_RuntimeConfig_GetPlatformOptionLocal_Ref(Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Set the input dimension map for runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @param[in] tsmHandle Handle to access the tensor shap map.
 *
 * @return Returns SNPE_SUCCESS if the tensor shap map added successfully.
*/
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfig_SetInputDimensionsMap(Snpe_RuntimeConfig_Handle_t rcHandle, Snpe_TensorShapeMap_Handle_t tsmHandle);

/**
 * @brief Get the input dimension map from runtime config.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Returns the TensorShapeMap.
*/
SNPE_API
Snpe_TensorShapeMap_Handle_t Snpe_RuntimeConfig_GetInputDimensionsMap_Ref(Snpe_RuntimeConfig_Handle_t rcHandle);


/**
 * A typedef to indicate a SNPE RuntimeConfigList handle.
 */
typedef void* Snpe_RuntimeConfigList_Handle_t;

/**
 * @brief Create a new runtime config list.
 */
SNPE_API
Snpe_RuntimeConfigList_Handle_t Snpe_RuntimeConfigList_Create();

/**
 * @brief set the size of runtime config list.
 *
 * @param[in] size size of runtime config list.
*/
SNPE_API
Snpe_RuntimeConfigList_Handle_t Snpe_RuntimeConfigList_CreateSize(size_t size);

/**
 * @brief Destroys the RuntimeConfigList.
 *
 * @param[in] rcHandle Handle to access the runtime config list.
 *
 * @return Error code. Returns SNPE_SUCCESS if destruction successful.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfigList_Delete(Snpe_RuntimeConfigList_Handle_t rclHandle);

/**
 * @brief Push runtime config into runtime config list.
 *
 * @param[in] rclHandle Handle to access the runtime config list.
 *
 * @param[in] rcHandle Handle to access the runtime config.
 *
 * @return Error code. Returns SNPE_SUCCESS if runtime config pushed successfully.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfigList_PushBack(Snpe_RuntimeConfigList_Handle_t rclHandle, Snpe_RuntimeConfig_Handle_t rcHandle);

/**
 * @brief Returns the Runtime Config from list at position index.
 *
 * @param[in] rclHandle Handle to access the runtime config list.
 *
 * @param[in] idx Position in runtimeList.
 *
 * @return Returns the runtime config from list at position index.
 */
SNPE_API
Snpe_RuntimeConfig_Handle_t Snpe_RuntimeConfigList_At_Ref(Snpe_RuntimeConfigList_Handle_t rclHandle, size_t idx);

/**
 * @brief Copy-assigns the contents of rclSrcHandle into rclDstHandle.
 *
 * @param[in] rclHandle Source RuntimeConfigList handle.
 *
 * @param[in] idx Destination RuntimeConfigList handle.
 *
 * @return SNPE_SUCCESS on successful copy-assignment.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfigList_Assign(Snpe_RuntimeConfigList_Handle_t rclSrcHandle, Snpe_RuntimeConfigList_Handle_t rclDstHandle);

/**
 * @brief Returns the number of runtime configs in the list.
 *
 * @param[in] rclHandle Handle to access the runtime config list.
 *
 * @return Returns number of entries in the runtimeConfigList.
 */
SNPE_API
size_t Snpe_RuntimeConfigList_Size(Snpe_RuntimeConfigList_Handle_t rclHandle);

/**
 * @brief Returns the capacity of runtime configs in the list.
 *
 * @param[in] rclHandle Handle to access the runtime config list.
 *
 * @return Returns number of capacity in the runtimeConfigList.
 */
SNPE_API
size_t Snpe_RuntimeConfigList_Capacity(Snpe_RuntimeConfigList_Handle_t rclHandle);

/**
 * @brief Removes all runtime configs from the list.
 *
 * @param[in] rclHandle Handle to access the runtime config list.
 *
 * @return Error code. Returns SNPE_SUCCESS if runtime config list is cleared successfully.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_RuntimeConfigList_Clear(Snpe_RuntimeConfigList_Handle_t rclHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // _SNPE_RUNTIME_CONFIG_LIST_H_
