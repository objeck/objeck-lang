//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 *  @file
 *  @brief  API providing logging functionality.
 */

#ifndef GENIE_LOG_H
#define GENIE_LOG_H

#include "GenieCommon.h"

#ifdef __cplusplus
#include <cstdarg>
#else
#include <stdarg.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for logger configuration instances.
 */
typedef const struct _GenieLogConfig_Handle_t* GenieLogConfig_Handle_t;

/**
 * @brief A handle for logger instance.
 */
typedef const struct _GenieLog_Handle_t* GenieLog_Handle_t;

/**
 * @brief An enum which defines the log level.
 */
typedef enum {
  GENIE_LOG_LEVEL_ERROR   = 1,
  GENIE_LOG_LEVEL_WARN    = 2,
  GENIE_LOG_LEVEL_INFO    = 3,
  GENIE_LOG_LEVEL_VERBOSE = 4,
} GenieLog_Level_t;

/**
 * @brief Signature for user-supplied logging callback.
 *
 * @warning The backend may call this callback from multiple threads, and expects that it is
 *          re-entrant.
 *
 * @param[in] handle The GenieLog_Handle_t generating this callback.
 *
 * @param[in] fmt printf-style message format specifier.
 *
 * @param[in] level Log level for the message.
 *
 * @param[in] timestamp Backend-generated timestamp which is monotonically increasing, but
 *                      otherwise meaningless.
 * @param[in] args Message-specific parameters, to be used with fmt.
 */
typedef void (*GenieLog_Callback_t)(const GenieLog_Handle_t handle,
                                    const char* fmt,
                                    GenieLog_Level_t level,
                                    uint64_t timestamp,
                                    va_list args);

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to create a handle to a logger object.
 *
 * @param[in] configHandle A handle to a config. This is a placeholder for future logger
 *                         configurability. Currently, it must be NULL.
 *
 * @param[in] callback Callback function which is called when new log messages are generated.
 *                     Can be NULL which indicates that the default system logger will be used.
 *
 * @param[in] logLevel Maximum level of messages which will be generated.
 *
 * @param[out] logHandle A handle to the created logger handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The log handle could not be created.
 */
GENIE_API
Genie_Status_t GenieLog_create(const GenieLogConfig_Handle_t configHandle,
                               const GenieLog_Callback_t callback,
                               const GenieLog_Level_t logLevel,
                               GenieLog_Handle_t* logHandle);

/**
 * @brief A function to free memory associated with a log handle.
 *
 * @param[in] logHandle A log handle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Log handle is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The log handle could not be freed.
 */
GENIE_API
Genie_Status_t GenieLog_free(GenieLog_Handle_t logHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_LOG_H
