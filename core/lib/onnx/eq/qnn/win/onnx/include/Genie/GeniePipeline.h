//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 * @brief An API component for Genie composable pipeline.
 */

#ifndef GENIE_PIPELINE_H
#define GENIE_PIPELINE_H

#include "GenieCommon.h"
#include "GenieLog.h"
#include "GenieNode.h"
#include "GenieProfile.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for pipeline configuration instances.
 */
typedef const struct _GeniePipelineConfig_Handle_t* GeniePipelineConfig_Handle_t;

/**
 * @brief A handle for pipeline instances.
 */
typedef const struct _GeniePipeline_Handle_t* GeniePipeline_Handle_t;

/**
 * @brief An enum which defines the pipeline priority.
 */
typedef enum {
  /// GENIE_PIPELINE_PRIORITY_LOW is always available for use.
  GENIE_PIPELINE_PRIORITY_LOW = 0,
  /// GENIE_PIPELINE_PRIORITY_NORMAL is always available for use. This is the default.
  GENIE_PIPELINE_PRIORITY_NORMAL = 100,
  /// GENIE_PIPELINE_PRIORITY_NORMAL_HIGH usage may be restricted and would silently be treated as
  /// GENIE_PIPELINE_PRIORITY_NORMAL
  GENIE_PIPELINE_PRIORITY_NORMAL_HIGH = 150,
  /// GENIE_PIPELINE_PRIORITY_HIGH usage may be restricted and would silently be treated as
  /// GENIE_PIPELINE_PRIORITY_NORMAL
  GENIE_PIPELINE_PRIORITY_HIGH = 200,
} GeniePipeline_Priority_t;

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to create a pipeline configuration from a JSON string.
 *
 * @param[in] str A configuration string. Must not be NULL.
 *
 * @param[out] configHandle A handle to the created config. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 *         - GENIE_STATUS_ERROR_INVALID_CONFIG: At least one configuration option is invalid.
 */
GENIE_API
Genie_Status_t GeniePipelineConfig_createFromJson(const char* str,
                                                  GeniePipelineConfig_Handle_t* configHandle);

/**
 * @brief A function to bind a profile handle to pipeline config. The profile handle
 *        will also be bound to any pipeline handle created from this pipeline config handle.
 *
 * @param[in] configHandle A handle to a valid config.
 *
 * @param[in] profileHandle The profile handle on which metrics are populated and can
 *                          be queried. Cannot be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle or profile handle is invalid.
 */
GENIE_API
Genie_Status_t GeniePipelineConfig_bindProfiler(const GeniePipelineConfig_Handle_t configHandle,
                                                const GenieProfile_Handle_t profileHandle);
/**
 * @brief A function to bind a log handle to pipeline config. The log handle
 *        will also be bound to any pipeline handle created from this pipeline config handle.
 *
 * @param[in] configHandle A handle to a valid config.
 *
 * @param[in] logHandle The log handle using which logs are recorded and
 *                      outputted. Cannot be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle or log handle is invalid.
 */
GENIE_API
Genie_Status_t GeniePipelineConfig_bindLogger(const GeniePipelineConfig_Handle_t configHandle,
                                              const GenieLog_Handle_t logHandle);

/**
 * @brief A function to free a pipeline config.
 *
 * @param[in] configHandle A config handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GeniePipelineConfig_free(const GeniePipelineConfig_Handle_t configHandle);

/**
 * @brief A function to create a pipeline. The pipeline can be configured using a
 *        builder pattern.
 *
 * @param[in] configHandle A handle to a valid config. Must not be NULL.
 *
 * @param[out] pipelineHandle A handle to the created pipeline. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 */
GENIE_API
Genie_Status_t GeniePipeline_create(const GeniePipelineConfig_Handle_t configHandle,
                                    GeniePipeline_Handle_t* pipelineHandle);

/**
 * @brief A function to save state of a node to a file.
 *
 * @param[in] pipelineHandle A handle to the created pipeline. Must not be NULL.
 *
 * @param[in] path File Path where node state will be saved.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GeniePipeline_save(const GeniePipeline_Handle_t pipelineHandle, const char* path);

/**
 * @brief A function to restore state of a node from a file.
 *
 * @param[in] pipelineHandle A handle to the created pipeline. Must not be NULL.
 *
 * @param[in] path File Path where pipeline state will be restored from.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GeniePipeline_restore(const GeniePipeline_Handle_t pipelineHandle, const char* path);

/**
 * @brief A function to reset a pipeline.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Pipeline handle is invalid.
 */
GENIE_API
Genie_Status_t GeniePipeline_reset(const GeniePipeline_Handle_t pipelineHandle);

/**
 * @brief A function to set an engine's QNN context priority.
 *
 * @note GENIE_PIPELINE_PRIORITY_NORMAL_HIGH and GENIE_PIPELINE_PRIORITY_HIGH may be reserved on
 * some platforms for OEM use. See GeniePipeline_setOemKey.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @param[in] engineRole Engine role to which the priority is being applied (e.g. "primary").
 *
 * @param[in] priority The requested priority.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The requested priority could not be applied.
 */
GENIE_API
Genie_Status_t GeniePipeline_setPriority(const GeniePipeline_Handle_t pipelineHandle,
                                         const char* engineRole,
                                         const GeniePipeline_Priority_t priority);

/**
 * @brief A function to provide an OEM key to the QNN backend.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @param[in] oemKey OEM key to be provided to the QNN backend.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The OEM key could not be applied.
 */
GENIE_API
Genie_Status_t GeniePipeline_setOemKey(const GeniePipeline_Handle_t pipelineHandle,
                                       const char* oemKey);

/**
 * @brief A function to add node to the pipeline.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node/ Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The OEM key could not be applied.
 */
GENIE_API
Genie_Status_t GeniePipeline_addNode(const GeniePipeline_Handle_t pipelineHandle,
                                     const GenieNode_Handle_t nodeHandle);

/**
 * @brief A function to connect two nodes added in a pipeline.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @param[in] producerHandle A node handle to be connected as a producer.
 *
 * @param[in] producerName IO name of the producer node.
 *
 * @param[in] consumerHandle A node handle to be connected as a consumer.
 *
 * @param[in] consumerName IO name of the consumer node.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node/ Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The OEM key could not be applied.
 */
GENIE_API
Genie_Status_t GeniePipeline_connect(const GeniePipeline_Handle_t pipelineHandle,
                                     const GenieNode_Handle_t producerHandle,
                                     const GenieNode_IOName_t producerName,
                                     const GenieNode_Handle_t consumerHandle,
                                     const GenieNode_IOName_t consumerName);

/**
 * @brief A function to execute all nodes connected in a pipeline.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @param[in] userData The userData field provided to GeniePipeline_execute.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The OEM key could not be applied.
 */
GENIE_API
Genie_Status_t GeniePipeline_execute(const GeniePipeline_Handle_t pipelineHandle, void* userData);

/**
 * @brief A function to free a pipeline.
 *
 * @param[in] pipelineHandle A pipeline handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Pipeline handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GeniePipeline_free(const GeniePipeline_Handle_t pipelineHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_PIPELINE_H
