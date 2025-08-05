//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 *  @file
 *  @brief  API providing performance profiling functionality.
 */

#ifndef GENIE_PROFILE_H
#define GENIE_PROFILE_H

#include "GenieCommon.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for profile configuration instances.
 *
 * @note The profile configuration handle is currently defined as a placeholder
 *       for future profile configuration options and is not currently in use.
 */
typedef const struct _GenieProfileConfig_Handle_t* GenieProfileConfig_Handle_t;

/**
 * @brief A handle for profile instance.
 */
typedef const struct _GenieProfile_Handle_t* GenieProfile_Handle_t;

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to create a handle to a profile object.
 *
 * @param[in] configHandle A placeholder for future profile configuration options. Must be NULL.
 *
 * @param[out] profileHandle A handle to the created profile handle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 */
GENIE_API
Genie_Status_t GenieProfile_create(const GenieProfileConfig_Handle_t configHandle,
                                   GenieProfile_Handle_t* profileHandle);

/**
 * @brief A function to get data collected on a profile handle. The provided
 *        callback will be called for the client to provide memory allocation
 *        on which the JSON object will be copied.
 *
 * @param[in] profileHandle A profile handle. Must not be NULL
 *
 * @param[in] callback A callback function handle. Must not be NULL.
 *
 * @param[out] jsonData The collected profile data. The associated buffer was
 *                      allocated in the client defined allocation callback and
 *                      the memory needs to be managed by the client.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Profile handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 */
GENIE_API
Genie_Status_t GenieProfile_getJsonData(const GenieProfile_Handle_t profileHandle,
                                        Genie_AllocCallback_t callback,
                                        const char** jsonData);

/**
 * @brief A function to free memory associated with a profile handle,
 *        including the event data collected on the handle. This call
 *        will fail if the profile handle is still bound to another object.
 *
 * @param[in] profileHandle A profile handle. Must not be NULL
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Profile handle is invalid.
 *         - GENIE_STATUS_ERROR_BOUND_HANDLE: Profile handle is bound to another handle.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieProfile_free(const GenieProfile_Handle_t profileHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_PROFILE_H
