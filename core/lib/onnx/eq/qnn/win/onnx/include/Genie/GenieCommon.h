//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 * @file
 * @brief   A header which contains common components shared between different
 *          components of the Genie C API.
 */

#ifndef GENIE_COMMON_H
#define GENIE_COMMON_H
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Macros
//=============================================================================

// Macro controlling visibility of Genie API
#ifndef GENIE_API
#define GENIE_API
#endif

// Provide values to use for the Genie API version.
#define GENIE_API_VERSION_MAJOR 1
#define GENIE_API_VERSION_MINOR 10
#define GENIE_API_VERSION_PATCH 0

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A typedef to indicate GENIE API return handle.
 */
typedef int32_t Genie_Status_t;

/**
 * @brief A client defined callback function to provide the Genie library with client-allocated
 * memory.
 *
 * @param[in] size The size of the memory to be allocated.
 *
 * @param[out] allocatedData An output pointer to a pointer of client-allocated memory.
 *
 * @return None
 *
 */
typedef void (*Genie_AllocCallback_t)(const size_t size, const char** allocatedData);

//=============================================================================
// Status Codes
//=============================================================================

#define GENIE_STATUS_SUCCESS                   0
#define GENIE_STATUS_WARNING_ABORTED           1
#define GENIE_STATUS_WARNING_BOUND_HANDLE      2
#define GENIE_STATUS_WARNING_PAUSED            3
#define GENIE_STATUS_ERROR_GENERAL             -1
#define GENIE_STATUS_ERROR_INVALID_ARGUMENT    -2
#define GENIE_STATUS_ERROR_MEM_ALLOC           -3
#define GENIE_STATUS_ERROR_INVALID_CONFIG      -4
#define GENIE_STATUS_ERROR_INVALID_HANDLE      -5
#define GENIE_STATUS_ERROR_QUERY_FAILED        -6
#define GENIE_STATUS_ERROR_JSON_FORMAT         -7
#define GENIE_STATUS_ERROR_JSON_SCHEMA         -8
#define GENIE_STATUS_ERROR_JSON_VALUE          -9
#define GENIE_STATUS_ERROR_GENERATE_FAILED     -10
#define GENIE_STATUS_ERROR_GET_HANDLE_FAILED   -11
#define GENIE_STATUS_ERROR_APPLY_CONFIG_FAILED -12
#define GENIE_STATUS_ERROR_SET_PARAMS_FAILED   -13
#define GENIE_STATUS_ERROR_BOUND_HANDLE        -14

//=============================================================================
// Enums
//=============================================================================
/**
 * @brief An enum which defines the dialog performance policy.
 */
typedef enum {
  GENIE_PERFORMANCE_BURST                      = 10,
  GENIE_PERFORMANCE_SUSTAINED_HIGH_PERFORMANCE = 20,
  GENIE_PERFORMANCE_HIGH_PERFORMANCE           = 30,
  GENIE_PERFORMANCE_BALANCED                   = 40,
  GENIE_PERFORMANCE_LOW_BALANCED               = 50,
  GENIE_PERFORMANCE_HIGH_POWER_SAVER           = 60,
  GENIE_PERFORMANCE_POWER_SAVER                = 70,
  GENIE_PERFORMANCE_LOW_POWER_SAVER            = 80,
  GENIE_PERFORMANCE_EXTREME_POWER_SAVER        = 90,
} Genie_PerformancePolicy_t;

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to get the major API version.
 *
 * @return GENIE_API_VERSION_MAJOR.
 */
GENIE_API
uint32_t Genie_getApiMajorVersion(void);

/**
 * @brief A function to get the mino API version.
 *
 * @return GENIE_API_VERSION_MINOR.
 */
GENIE_API
uint32_t Genie_getApiMinorVersion(void);

/**
 * @brief A function to get the patch API version.
 *
 * @return GENIE_API_VERSION_PATCH.
 */
GENIE_API
uint32_t Genie_getApiPatchVersion(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_COMMON_H
