//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 *  @file
 *  @brief  API providing logit sampling functionality.
 */

#ifndef GENIE_SAMPLER_H
#define GENIE_SAMPLER_H

#include "GenieCommon.h"
#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for sampler configuration instances.
 */
typedef const struct _GenieSamplerConfig_Handle_t* GenieSamplerConfig_Handle_t;

/**
 * @brief A handle for sampler instances.
 */
typedef const struct _GenieSampler_Handle_t* GenieSampler_Handle_t;

/**
 * @brief A client-defined callback function to set sampler process implementation.
 *
 * @note This typedef will soon be deprecated in favor of GenieSampler_ProcessCallback_t.
 *
 * @param[in] logitsSize Size of output logits in logits array
 *
 * @param[in] logits Output logits from engine execution
 *
 * @param[in] numTokens Number of tokens to be returned.
 *
 * @param[out] tokens pointer to array of tokens based on numTokens argument passed in input
 *
 */
typedef void (*GenieSampler_ProcessCallback_t)(const uint32_t logitsSize,
                                               const void* logits,
                                               const uint32_t numTokens,
                                               int32_t* tokens);

/**
 * @brief A client-defined callback function to set sampler process implementation.
 *
 * @param[in] logitsSize Size of output logits in logits array
 *
 * @param[in] logits Output logits from engine execution
 *
 * @param[in] numTokens Number of tokens to be returned.
 *
 * @param[in] userData Void pointer to add user data pertaining to callback function.
 *
 * @param[out] tokens pointer to array of tokens based on numTokens argument passed in input
 *
 */
typedef void (*GenieSampler_UserDataCallback_t)(const uint32_t logitsSize,
                                                const void* logits,
                                                const uint32_t numTokens,
                                                int32_t* tokens,
                                                const void* userData);

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to create a sampler configuration from a JSON string.
 *
 * @param[in] str A configuration string. Must not be NULL.
 *
 * @param[out] configHandle A handle to the created sampler config. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 *         - GENIE_STATUS_ERROR_INVALID_CONFIG: At least one configuration option is invalid.
 */
GENIE_API
Genie_Status_t GenieSamplerConfig_createFromJson(const char* str,
                                                 GenieSamplerConfig_Handle_t* configHandle);

/**
 * @brief A function to set sampler params(s) associated with configHandle.
 *
 * @param[in] configHandle A sampler config handle.
 *
 * @param[in] keyStr A string indicating the sampler parameter to update. If NULL, valueStr must
 * have the entire sampler configuration string. See SDK documentation for valid keys.
 *
 * @param[in] valueStr A string that holds the corresponding value of keyStr parameter
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Sampler config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_SET_PARAM_FAILED: Set Param failure.
 */
GENIE_API
Genie_Status_t GenieSamplerConfig_setParam(const GenieSamplerConfig_Handle_t configHandle,
                                           const char* keyStr,
                                           const char* valueStr);

/**
 * @brief A function to free a sampler config.
 *
 * @param[in] configHandle A sampler config handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Sampler config handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieSamplerConfig_free(const GenieSamplerConfig_Handle_t configHandle);

/**
 * @brief A function to apply the sampler config parameters to a sampler.
 *
 * @param[in] samplerHandle A sampler handle.
 *
 * @param[in] configHandle A sampler config handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Sampler handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_APPLY_CONFIG_FAILED: Apply Config failure.
 */
GENIE_API
Genie_Status_t GenieSampler_applyConfig(const GenieSampler_Handle_t samplerHandle,
                                        const GenieSamplerConfig_Handle_t configHandle);

/**
 * @brief A function to register Sampler Process callback.
 *
 * @note This API will soon be deprecated in favor of GenieSampler_registerUserDataCallback.
 *
 * @param[in] name Name associated to the callback. Needs to be specified in Sampler config,
 *                 to indicate which process callback to use during sampling.
 *
 * @param[in] samplerCallback Callback with the sampler process implementation.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: Register Callback failed.
 */
GENIE_API
Genie_Status_t GenieSampler_registerCallback(const char* name,
                                             GenieSampler_ProcessCallback_t samplerCallback);

/**
 * @brief A function to register Sampler Process callback.
 *
 * @param[in] name Name associated to the callback. Needs to be specified in Sampler config,
 *                 to indicate which process callback to use during sampling.
 *
 * @param[in] samplerCallback Callback with the sampler process implementation.
 *
 * @param[in] userData Void pointer to add user data pertaining to callback function.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: Register Callback failed.
 */
GENIE_API
Genie_Status_t GenieSampler_registerUserDataCallback(
    const char* name, GenieSampler_UserDataCallback_t samplerCallback, const void* userData);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_SAMPLER_H
