//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 *  @file
 *  @brief  Dialog API providing query functionality.
 */

#ifndef GENIE_DIALOG_H
#define GENIE_DIALOG_H

#include "GenieCommon.h"
#include "GenieEngine.h"
#include "GenieLog.h"
#include "GenieProfile.h"
#include "GenieSampler.h"
#include "GenieTokenizer.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for dialog configuration instances.
 */
typedef const struct _GenieDialogConfig_Handle_t* GenieDialogConfig_Handle_t;

/**
 * @brief A handle for dialog instances.
 */
typedef const struct _GenieDialog_Handle_t* GenieDialog_Handle_t;

/**
 * @brief An enum which defines the dialog signal actions.
 */
typedef enum {
  /// Signals abort as an action to active dialog.
  GENIE_DIALOG_ACTION_ABORT = 0x01,
  /// Signals to pause an active query
  GENIE_DIALOG_ACTION_PAUSE = 0x02
} GenieDialog_Action_t;

/**
 * @brief An enum which defines the dialog sentence code.
 */
typedef enum {
  /// The string is the entire query/response.
  GENIE_DIALOG_SENTENCE_COMPLETE = 0,
  /// The string is the beginning of the query/response.
  GENIE_DIALOG_SENTENCE_BEGIN = 1,
  /// The string is a part of the query/response and not the beginning or end.
  GENIE_DIALOG_SENTENCE_CONTINUE = 2,
  /// The string is the end of the query/response.
  GENIE_DIALOG_SENTENCE_END = 3,
  /// The query has been aborted.
  GENIE_DIALOG_SENTENCE_ABORT = 4,
  /// Rewind the KV cache as per prefix query match before processing the query
  GENIE_DIALOG_SENTENCE_REWIND = 5,
  /// A paused query has resumed.
  GENIE_DIALOG_SENTENCE_RESUME = 6,
} GenieDialog_SentenceCode_t;

/**
 * @brief An enum which defines the dialog priority.
 */
typedef enum {
  /// GENIE_DIALOG_PRIORITY_LOW is always available for use.
  GENIE_DIALOG_PRIORITY_LOW = 0,
  /// GENIE_DIALOG_PRIORITY_NORMAL is always available for use. This is the default.
  GENIE_DIALOG_PRIORITY_NORMAL = 100,
  /// GENIE_DIALOG_PRIORITY_NORMAL_HIGH usage may be restricted and would silently be treated as
  /// GENIE_DIALOG_PRIORITY_NORMAL
  GENIE_DIALOG_PRIORITY_NORMAL_HIGH = 150,
  /// GENIE_DIALOG_PRIORITY_HIGH usage may be restricted and would silently be treated as
  /// GENIE_DIALOG_PRIORITY_NORMAL
  GENIE_DIALOG_PRIORITY_HIGH = 200,
} GenieDialog_Priority_t;

/**
 * @brief A client-defined callback function to handle GenieDialog_query responses.
 *
 * @param[in] response The null-terminated query response.
 *
 * @param[in] sentenceCode The sentence code related to the responseStr.
 *
 * @param[in] userData The userData field provided to GenieDialog_query.
 *
 * @return None
 *
 */
typedef void (*GenieDialog_QueryCallback_t)(const char* response,
                                            const GenieDialog_SentenceCode_t sentenceCode,
                                            const void* userData);

/**
 * @brief A client-defined callback function to handle conversion from tokens to embeddings.
 *
 * @param[in] token The token to be converted.
 *
 * @param[out] embedding The buffer for the embedding representation of the token.
 *
 * @param[in] embeddingSize The size of the embedding buffer in bytes.
 *
 * @param[in] userData The userData field provided to GenieDialog_embeddingQuery.
 *
 * @return None
 *
 */
typedef void (*GenieDialog_TokenToEmbeddingCallback_t)(const int32_t token,
                                                       void* embedding,
                                                       const uint32_t embeddingSize,
                                                       const void* userData);

/**
 * @brief A client-defined callback function to handle GenieDialog_tokenQuery responses.
 *
 * @param[in] response The response token array
 *
 * @param[in] numTokens The response token array size
 *
 * @param[in] sentenceCode The sentence code related to the response token.
 *
 * @param[in] userData The userData field provided to GenieDialog_tokenQuery.
 *
 * @return None
 *
 */
typedef void (*GenieDialog_TokenQueryCallback_t)(const uint32_t* response,
                                                 const uint32_t numTokens,
                                                 const GenieDialog_SentenceCode_t sentenceCode,
                                                 const void* userData);

//=============================================================================
// Functions
//=============================================================================

/**
 * @brief A function to create a dialog configuration from a JSON string.
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
Genie_Status_t GenieDialogConfig_createFromJson(const char* str,
                                                GenieDialogConfig_Handle_t* configHandle);

/**
 * @brief A function to bind a profile handle to dialog config. The profile handle
 *        will also be bound to any dialog handle created from this dialog config handle.
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
Genie_Status_t GenieDialogConfig_bindProfiler(const GenieDialogConfig_Handle_t configHandle,
                                              const GenieProfile_Handle_t profileHandle);
/**
 * @brief A function to bind a log handle to dialog config. The log handle
 *        will also be bound to any dialog handle created from this dialog config handle.
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
Genie_Status_t GenieDialogConfig_bindLogger(const GenieDialogConfig_Handle_t configHandle,
                                            const GenieLog_Handle_t logHandle);

/**
 * @brief A function to free a dialog config.
 *
 * @param[in] configHandle A config handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieDialogConfig_free(const GenieDialogConfig_Handle_t configHandle);

/**
 * @brief A function to create a dialog. The dialog can be configured using a
 *        builder pattern.
 *
 * @param[in] configHandle A handle to a valid config. Must not be NULL.
 *
 * @param[out] dialogHandle A handle to the created dialog. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 */
GENIE_API
Genie_Status_t GenieDialog_create(const GenieDialogConfig_Handle_t configHandle,
                                  GenieDialog_Handle_t* dialogHandle);

/**
 * @brief A function to execute a query.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] queryStr The input query.
 *
 * @param[in] sentenceCode The sentence code indicating the contents of the queryStr.
 *
 * @param[in] callback Callback function to handle query responses. Cannot be NULL.
 *
 * @param[in] userData User defined field provided in the query responses. Can be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_QUERY_FAILED: Dialog query failure.
 */
GENIE_API
Genie_Status_t GenieDialog_query(const GenieDialog_Handle_t dialogHandle,
                                 const char* queryStr,
                                 const GenieDialog_SentenceCode_t sentenceCode,
                                 const GenieDialog_QueryCallback_t callback,
                                 const void* userData);

/**
 * @brief A function to execute a query with embeddings.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] embeddings The input embeddings buffer.
 *
 * @param[in] embeddingsSize The total size of the embeddings buffer in bytes.
 *
 * @param[in] sentenceCode The sentence code indicating the contents of the queryStr.
 *
 * @param[in] t2eCallback Callback function to handle token-to-embedding conversions.
 *
 * @param[in] callback Callback function to handle query responses. Cannot be NULL.
 *
 * @param[in] userData User defined field provided in the query responses. Can be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_QUERY_FAILED: Dialog query failure.
 */
GENIE_API
Genie_Status_t GenieDialog_embeddingQuery(const GenieDialog_Handle_t dialogHandle,
                                          const void* embeddings,
                                          const uint32_t embeddingsSize,
                                          const GenieDialog_SentenceCode_t sentenceCode,
                                          const GenieDialog_TokenToEmbeddingCallback_t t2eCallback,
                                          const GenieDialog_QueryCallback_t callback,
                                          const void* userData);

/**
 * @brief A function to execute token to token query.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] inputTokens The input tokens array.
 *
 * @param[in] numTokens The size of input tokens array.
 *
 * @param[in] sentenceCode The sentence code indicating the contents of the queryStr.
 *
 * @param[in] callback Callback function to handle token to token query responses. Cannot be NULL.
 *
 * @param[in] userData User defined field provided in the query responses. Can be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_tokenQuery(const GenieDialog_Handle_t dialogHandle,
                                      const uint32_t* inputTokens,
                                      const uint32_t numTokens,
                                      const GenieDialog_SentenceCode_t sentenceCode,
                                      const GenieDialog_TokenQueryCallback_t callback,
                                      const void* userData);

/**
 * @brief A function to save state of a dialog to a file.
 *
 * @param[in] dialogHandle A handle to the created dialog. Must not be NULL.
 *
 * @param[in] path File Path where dialog state will be saved.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_save(const GenieDialog_Handle_t dialogHandle, const char* path);

/**
 * @brief A function to restore state of a dialog from a file.
 *
 * @param[in] dialogHandle A handle to the created dialog. Must not be NULL.
 *
 * @param[in] path File Path where dialog state will be restored from.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Config handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_restore(const GenieDialog_Handle_t dialogHandle, const char* path);

/**
 * @brief A function to reset a dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_reset(const GenieDialog_Handle_t dialogHandle);

/**
 * @brief A function to apply a LoRA Adapter.
 *
 * @note When switching LoRA adapters, it is recommended to call GenieDialog_reset.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] engine Engine to which the LoRA Adapter is being applied.
 *
 * @param[in] loraAdapterName Name of the LoRA adapter.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The LoRA adapter could not be applied.
 */
GENIE_API
Genie_Status_t GenieDialog_applyLora(const GenieDialog_Handle_t dialogHandle,
                                     const char* engine,
                                     const char* loraAdapterName);

/**
 * @brief A function to apply the LoRA Strength.
 *
 * @note When changing LoRA alpha strengths, it is recommended to call GenieDialog_reset.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] engine Engine to which the LoRA Adapter is being applied.
 *
 * @param[in] tensorName LoRA Alpha Tensor name.
 *
 * @param[in] alpha Value of the LoRA alpha strength.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: In case due alpha tensor values could not be applied.
 */
GENIE_API
Genie_Status_t GenieDialog_setLoraStrength(const GenieDialog_Handle_t dialogHandle,
                                           const char* engine,
                                           const char* tensorName,
                                           const float alpha);

/**
 * @brief A function to get the sampler handle associated with a dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] dialogSamplerHandle A sampler handle tied to dialogHandle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GET_HANDLE_FAILED: Get sampler failure.
 */
GENIE_API
Genie_Status_t GenieDialog_getSampler(const GenieDialog_Handle_t dialogHandle,
                                      GenieSampler_Handle_t* dialogSamplerHandle);

/**
 * @brief A function to get the tokenizer handle associated with a dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] tokenizerHandle A tokenizer handle tied to dialogHandle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GET_HANDLE_FAILED: Get tokenizer failure.
 */
GENIE_API
Genie_Status_t GenieDialog_getTokenizer(const GenieDialog_Handle_t dialogHandle,
                                        GenieTokenizer_Handle_t* tokenizerHandle);

/**
 * @brief A function to set/update the list of stop sequences of a dialog. Old sequences if any are
 *        discarded. Call with a nullptr or an empty JSON or an empty string to reset to no stop
 *        sequence.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] newStopSequences A JSON string with list of new stop sequences. Must be null
 *                             terminated.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_JSON_FORMAT: JSON string is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_setStopSequence(const GenieDialog_Handle_t dialogHandle,
                                           const char* newStopSequences);

/**
 * @brief A function to signal actions to an active dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] action Action to perform.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: Signal Action failed.
 */
GENIE_API
Genie_Status_t GenieDialog_signal(const GenieDialog_Handle_t dialogHandle,
                                  const GenieDialog_Action_t action);

/**
 * @brief A function to set an engine's QNN context priority.
 *
 * @note GENIE_DIALOG_PRIORITY_NORMAL_HIGH and GENIE_DIALOG_PRIORITY_HIGH may be reserved on some
 *       platforms for OEM use. See GenieDialog_setOemKey.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] engineRole Engine role to which the priority is being applied (e.g. "primary").
 *
 * @param[in] priority The requested priority.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The requested priority could not be applied.
 */
GENIE_API
Genie_Status_t GenieDialog_setPriority(const GenieDialog_Handle_t dialogHandle,
                                       const char* engineRole,
                                       const GenieDialog_Priority_t priority);

/**
 * @brief A function to provide an OEM key to the QNN backend.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] oemKey OEM key to be provided to the QNN backend.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GENERAL: The OEM key could not be applied.
 */
GENIE_API
Genie_Status_t GenieDialog_setOemKey(const GenieDialog_Handle_t dialogHandle, const char* oemKey);

/**
 * @brief A function to bind engine handle to dialog.
 *
 * @note It will unload the active engine (defined by engineType) of the dialog, if it is not
 *       retrieved by GenieDialog_getEngine API.
 *
 * @note Only draft engine binding is supported.
 *
 * @param[in] dialogHandle A dialog Handle
 *
 * @param[in] engineType type of the engine i.e., target or draft.
 *
 * @param[in] engineHandle The engine handle. Cannot be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: dialog handle or engine handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: invalid engineType.
 */
GENIE_API
Genie_Status_t GenieDialog_bindEngine(const GenieDialog_Handle_t dialogHandle,
                                      const char* engineType,
                                      const GenieEngine_Handle_t engineHandle);

/**
 * @brief A function to get the engine handle associated with a dialog.
 *
 * @note Each call to GenieDialog_getEngine API increases the engine's reference count.
 *       The engine is freed only when reference count reaches zero either with GenieDialog_free or
 *       GenieEngine_free.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] engineType type of the engine i.e., target or draft.
 *
 * @param[in] dialogEngineHandle A engine handle tied to dialogHandle. Must not be NULL.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_GET_HANDLE_FAILED: Get engine handle failure.
 */
GENIE_API
Genie_Status_t GenieDialog_getEngine(const GenieDialog_Handle_t dialogHandle,
                                     const char* engineType,
                                     GenieEngine_Handle_t* dialogEngineHandle);

/**
 * @brief A function to set the performance policy for a dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[in] perfProfile The requested performance profile for the dialog.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_setPerformancePolicy(const GenieDialog_Handle_t dialogHandle,
                                                const Genie_PerformancePolicy_t perfProfile);

/**
 * @brief A function to get the performance policy of a dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @param[out] perfProfile The performance profile of the dialog.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 */
GENIE_API
Genie_Status_t GenieDialog_getPerformancePolicy(const GenieDialog_Handle_t dialogHandle,
                                                Genie_PerformancePolicy_t* perfProfile);

/**
 * @brief A function to free a dialog.
 *
 * @param[in] dialogHandle A dialog handle.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Dialog handle is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory (de)allocation failure.
 */
GENIE_API
Genie_Status_t GenieDialog_free(const GenieDialog_Handle_t dialogHandle);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_DIALOG_H
