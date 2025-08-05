//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
/**
 * @brief An API component for Genie composable pipeline IO.
 */

#ifndef GENIE_NODE_H
#define GENIE_NODE_H

#include "GenieCommon.h"
#include "GenieEngine.h"
#include "GenieLog.h"
#include "GenieProfile.h"
#include "GenieSampler.h"
#include "GenieTokenizer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A handle for node instances.
 */
typedef const struct _GenieNode_Handle_t* GenieNode_Handle_t;
/**
 * @brief A handle for node config instances.
 */
typedef const struct _GenieNodeConfig_Handle_t* GenieNodeConfig_Handle_t;

/**
 * @brief An enum which defines the node sentence code.
 */
typedef enum {
  /// The string is the entire query/response.
  GENIE_NODE_SENTENCE_COMPLETE = 0,
  /// The string is the beginning of the query/response.
  GENIE_NODE_SENTENCE_BEGIN = 1,
  /// The string is a part of the query/response and not the beginning or end.
  GENIE_NODE_SENTENCE_CONTINUE = 2,
  /// The string is the end of the query/response.
  GENIE_NODE_SENTENCE_END = 3,
  /// The query has been aborted.
  GENIE_NODE_SENTENCE_ABORT = 4,
  /// Rewind the KV cache as per prefix query match before processing the query
  GENIE_NODE_SENTENCE_REWIND = 5,
} GenieNode_TextOutput_SentenceCode_t;

typedef enum {
  GENIE_NODE_TEXT_GENERATOR_TEXT_INPUT      = 0,
  GENIE_NODE_TEXT_GENERATOR_EMBEDDING_INPUT = 1,
  GENIE_NODE_TEXT_GENERATOR_TEXT_OUTPUT     = 2,
  GENIE_NODE_TEXT_ENCODER_TEXT_INPUT        = 100,
  GENIE_NODE_TEXT_ENCODER_EMBEDDING_OUTPUT  = 101,
  GENIE_NODE_IMAGE_ENCODER_IMAGE_INPUT      = 200,
  GENIE_NODE_IMAGE_ENCODER_EMBEDDING_OUTPUT = 201
} GenieNode_IOName_t;

/**
 * @brief A client-defined callback function to handle text-generator responses.
 *
 * @param[in] response The null-terminated query response.
 *
 * @param[in] sentenceCode The sentence code related to the responseStr.
 *
 * @param[in] userData The userData field provided to GeniePipeline_execute.
 *
 * @return None
 *
 */
typedef Genie_Status_t (*GenieNode_TextOutput_Callback_t)(
    const char* response,
    const GenieNode_TextOutput_SentenceCode_t sentenceCode,
    const void* userData);

/**
 * @brief A client-defined callback function to get embedding buffer for node encoders.
 *
 * @param[in] dimensions Dimensions of the embedding buffer.
 *
 * @param[in] rank Rank of the embedding buffer dimensions.
 *
 * @param[in] embeddingBufferSize Embedding buffer size in bytes.
 *
 * @param[in] embeddingBuffer Embedding buffer capturing encoder output.
 *
 * @param[in] userData The userData field provided to GeniePipeline_execute.
 *
 * @return None
 *
 */
typedef void (*GenieNode_EmbeddingOutputCallback_t)(const uint32_t* dimensions,
                                                    const uint32_t rank,
                                                    const size_t embeddingBufferSize,
                                                    const void* embeddingBuffer,
                                                    const void* userData);

/**
 * @brief A function to create a node configuration from a JSON string.
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
Genie_Status_t GenieNodeConfig_createFromJson(const char* str,
                                              GenieNodeConfig_Handle_t* configHandle);

/**
 * @brief A function to bind a profile handle to node config. The profile handle
 *        will also be bound to any node handle created from this node config handle.
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
Genie_Status_t GenieNodeConfig_bindProfiler(const GenieNodeConfig_Handle_t configHandle,
                                            const GenieProfile_Handle_t profileHandle);

/**
 * @brief A function to bind a log handle to node config. The log handle
 *        will also be bound to any node handle created from this node config handle.
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
Genie_Status_t GenieNodeConfig_bindLogger(const GenieNodeConfig_Handle_t configHandle,
                                          const GenieLog_Handle_t logHandle);

/**
 * @brief A function to free a node config.
 *
 * @param[in] configHandle A config handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieNodeConfig_free(const GenieNodeConfig_Handle_t configHandle);

/**
 * @brief A function to create a node. The node can be configured using a
 *        builder pattern.
 *
 * @param[in] nodeConfigHandle A handle to a valid config. Must not be NULL.
 *
 * @param[out] nodeHandle A handle to the created node. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 */
GENIE_API
Genie_Status_t GenieNode_create(const GenieNodeConfig_Handle_t nodeConfigHandle,
                                GenieNode_Handle_t* nodeHandle);

/**
 * @brief A function to setCallback for text generator node output.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] nodeIOName Node IO name.
 *
 * @param[in] callback A user callback function for text output.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieNode_setTextCallback(const GenieNode_Handle_t nodeHandle,
                                         const GenieNode_IOName_t nodeIOName,
                                         const GenieNode_TextOutput_Callback_t callback);

/**
 * @brief A function to setCallback for encoder node output.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] nodeIOName Node IO name.
 *
 * @param[in] callback A user callback function for embedding output.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieNode_setEmbeddingCallback(const GenieNode_Handle_t nodeHandle,
                                              const GenieNode_IOName_t nodeIOName,
                                              const GenieNode_EmbeddingOutputCallback_t callback);

/**
 * @brief A function to set data on a node.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] nodeIOName Node IO name.
 *
 * @param[in] data A pointer to the data buffer.
 *
 * @param[in] dataSize Databuffer size in bytes.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieNode_setData(const GenieNode_Handle_t nodeHandle,
                                 const GenieNode_IOName_t nodeIOName,
                                 const void* data,
                                 const size_t dataSize,
                                 const char* dataConfig);

/**
 * @brief A function to apply a LoRA Adapter.
 *
 * @note When switching LoRA adapters, it is recommended to call GenieNode_reset.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] engine Engine to which the LoRA Adapter is being applied.
 *
 * @param[in] loraAdapterName Name of the LoRA adapter.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The LoRA adapter could not be applied.
 */
GENIE_API
Genie_Status_t GenieNode_applyLora(const GenieNode_Handle_t nodeHandle,
                                   const char* engine,
                                   const char* loraAdapterName);

/**
 * @brief A function to apply the LoRA Strength.
 *
 * @note When changing LoRA alpha strengths, it is recommended to call GenieNode_reset.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] engine Engine to which the LoRA Adapter is being applied.
 *
 * @param[in] tensorName LoRA Alpha Tensor name.
 *
 * @param[in] alpha Value of the LoRA alpha strength.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: In case due alpha tensor values could not be applied.
 */
GENIE_API
Genie_Status_t GenieNode_setLoraStrength(const GenieNode_Handle_t nodeHandle,
                                         const char* engine,
                                         const char* tensorName,
                                         const float alpha);

/**
 * @brief A function to bind engine handle to Node.
 *
 * @note It will unload the active engine (defined by engineType) of the Node, if it is not
 *       retrieved by GenieNode_getEngine API.
 *
 * @note Only draft engine binding is supported.
 *
 * @param[in] nodeHandle A node Handle
 *
 * @param[in] engineType type of the engine i.e., target or draft.
 *
 * @param[in] engineHandle The engine handle. Cannot be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: node handle or engine handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: invalid engineType.
 */
GENIE_API
Genie_Status_t GenieNode_bindEngine(const GenieNode_Handle_t nodeHandle,
                                    const char* engineType,
                                    const GenieEngine_Handle_t engineHandle);

/**
 * @brief A function to get the engine handle associated with a node.
 *
 * @note Each call to GenieNode_getEngine API increases the engine's reference count.
 *       The engine is freed only when reference count reaches zero either with GenieNode_free or
 *       GenieEngine_free.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] engineType type of the engine i.e., target or draft.
 *
 * @param[in] nodeEngineHandle A engine handle tied to nodeHandle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GET_HANDLE_FAILED: Get engine handle failure.
 */
GENIE_API
Genie_Status_t GenieNode_getEngine(const GenieNode_Handle_t nodeHandle,
                                   const char* engineType,
                                   GenieEngine_Handle_t* nodeEngineHandle);

/**
 * @brief A function to get the sampler handle associated with a node.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] nodeSamplerHandle A sampler handle tied to nodeHandle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GET_HANDLE_FAILED: Get sampler failure.
 */
GENIE_API
Genie_Status_t GenieNode_getSampler(const GenieNode_Handle_t nodeHandle,
                                    GenieSampler_Handle_t* nodeSamplerHandle);

/**
 * @brief A function to get the tokenizer handle associated with a node.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @param[in] tokenizerHandle A tokenizer handle tied to nodeHandle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GET_HANDLE_FAILED: Get tokenizer failure.
 */
GENIE_API
Genie_Status_t GenieNode_getTokenizer(const GenieNode_Handle_t nodeHandle,
                                      GenieTokenizer_Handle_t* tokenizerHandle);

/**
 * @brief A function to free a node.
 *
 * @param[in] nodeHandle A node handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Node handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieNode_free(const GenieNode_Handle_t nodeHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_PIPELINE_H
