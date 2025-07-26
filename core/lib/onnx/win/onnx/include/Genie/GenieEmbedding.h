//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 *  @file
 *  @brief  Embedding API providing text query to embedding functionality.
 */

#ifndef GENIE_EMBEDDING_H
#define GENIE_EMBEDDING_H

#include "GenieCommon.h"
#include "GenieLog.h"
#include "GenieProfile.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for embedding configuration instances.
 */
typedef const struct _GenieEmbeddingConfig_Handle_t* GenieEmbeddingConfig_Handle_t;

/**
 * @brief A handle for embedding instances.
 */
typedef const struct _GenieEmbedding_Handle_t* GenieEmbedding_Handle_t;

/**
 * @brief A client-defined callback function to get embedding buffer for query.
 *
 * @param[in] dimensions Dimensions of the embedding buffer.
 *
 * @param[in] rank Rank of the embedding buffer dimensions.
 *
 * @param[in] embeddingBuffer Embedding buffer for query string.
 *
 * @param[in] userData The userData field provided to GenieEmbedding_query.
 *
 * @return None
 *
 */
typedef void (*GenieEmbedding_GenerateCallback_t)(const uint32_t* dimensions,
                                                  const uint32_t rank,
                                                  const float* embeddingBuffer,
                                                  const void* userData);

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to create a embedding configuration from a JSON string.
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
Genie_Status_t GenieEmbeddingConfig_createFromJson(const char* str,
                                                   GenieEmbeddingConfig_Handle_t* configHandle);

/**
 * @brief A function to bind a log handle to embedding config. The log handle
 *        will also be bound to any embedding handle created from this config handle.
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
Genie_Status_t GenieEmbeddingConfig_bindLogger(const GenieEmbeddingConfig_Handle_t configHandle,
                                               const GenieLog_Handle_t logHandle);

/**
 * @brief A function to bind a profile handle to embedding config. The profile handle
 *        will also be bound to any embedding handle created from this dialog config handle.
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
Genie_Status_t GenieEmbeddingConfig_bindProfiler(const GenieEmbeddingConfig_Handle_t configHandle,
                                                 const GenieProfile_Handle_t profileHandle);

/**
 * @brief A function to free a embedding config.
 *
 * @param[in] configHandle A config handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Embedding handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieEmbeddingConfig_free(const GenieEmbeddingConfig_Handle_t configHandle);

/**
 * @brief A function to create a embedding. The embedding can be configured using a
 *        builder pattern.
 *
 * @param[in] configHandle A handle to a valid config. Must not be NULL.
 *
 * @param[out] embeddingHandle A handle to the created embedding. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 */
GENIE_API
Genie_Status_t GenieEmbedding_create(const GenieEmbeddingConfig_Handle_t configHandle,
                                     GenieEmbedding_Handle_t* embeddingHandle);

/**
 * @brief A function to generate embedding for prompted text.
 *
 * @param[in] embeddingHandle A embedding handle.
 *
 * @param[in] queryStr The input query.
 *
 * @param[in] callback Callback function to handle generated embeddings. Cannot be NULL.
 *
 * @param[in] userData User defined field provided in the query responses. Can be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Embedding handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERATE_FAILED: Embedding generate failure.
 */
GENIE_API
Genie_Status_t GenieEmbedding_generate(const GenieEmbedding_Handle_t embeddingHandle,
                                       const char* queryStr,
                                       const GenieEmbedding_GenerateCallback_t callback,
                                       const void* userData);

/**
 * @brief A function to set the performance policy for a embedding.
 *
 * @param[in] embeddingHandle A embedding handle.
 *
 * @param[in] perfProfile The requested performance profile for the embedding.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Embedding handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieEmbedding_setPerformancePolicy(const GenieEmbedding_Handle_t embeddingHandle,
                                                   const Genie_PerformancePolicy_t perfProfile);

/**
 * @brief A function to get the performance policy of a embedding.
 *
 * @param[in] embeddingHandle A embedding handle.
 *
 * @param[out] perfProfile The performance profile of the embedding.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Embedding handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieEmbedding_getPerformancePolicy(const GenieEmbedding_Handle_t embeddingHandle,
                                                   Genie_PerformancePolicy_t* perfProfile);

/**
 * @brief A function to free a embedding.
 *
 * @param[in] embeddingHandle A embedding handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Embedding handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieEmbedding_free(const GenieEmbedding_Handle_t embeddingHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_EMBEDDING_H
