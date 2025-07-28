//=============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 * @file
 * @brief A handle for tokenizer configuration instances.
 */

#ifndef GENIE_TOKENIZER_H
#define GENIE_TOKENIZER_H

#include "GenieCommon.h"
#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Data Types
//=============================================================================

/**
 * @brief A handle for tokenizer instances.
 */
typedef const struct _GenieTokenizer_Handle_t* GenieTokenizer_Handle_t;

//=============================================================================
// Functions
//=============================================================================
/**
 * @brief A function to encode input text into token ids.
 *
 * @param[in] tokenizerHandle A handle to the tokenizer. Must not be NULL.
 *
 * @param[in] inputString Null-terminated Input string. Must not be NULL.
 *
 * @param[in] callback A callback function to allocate tokenIds. Must not be NULL.
 *
 * @param[out] tokenIds The encoded token ids. The associated buffer was
 *                      allocated in the client defined allocation callback and
 *                      the memory needs to be managed by the client.
 *
 * @param[out] numTokenIds The number of encoded token ids.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Tokenizer handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 *         - GENIE_STATUS_ERROR_GENERAL: Tokenizer Encode failure.
 */
GENIE_API
Genie_Status_t GenieTokenizer_encode(const GenieTokenizer_Handle_t tokenizerHandle,
                                     const char* inputString,
                                     const Genie_AllocCallback_t callback,
                                     const int32_t** tokenIds,
                                     uint32_t* numTokenIds);

/**
 * @brief A function to decode input token ids into text.
 *
 * @param[in] tokenizerHandle A handle to the tokenizer. Must not be NULL.
 *
 * @param[in] tokenIds Input token ids. Must not be NULL.
 *
 * @param[in] numTokenIds The number of input token ids. Must be non-zero.
 *
 * @param[in] callback A callback function to allocate outputString. Must not be NULL.
 *
 * @param[out] outputString The decoded null-terminated string.The associated buffer was
 *                          allocated in the client defined allocation callback and
 *                          the memory needs to be managed by the client.
 *
 * @return Status code:
 *         - GENIE_STATUS_SUCCESS: API call was successful.
 *         - GENIE_STATUS_ERROR_INVALID_HANDLE: Tokenizer handle is invalid.
 *         - GENIE_STATUS_ERROR_INVALID_ARGUMENT: At least one argument is invalid.
 *         - GENIE_STATUS_ERROR_MEM_ALLOC: Memory allocation failure.
 *         - GENIE_STATUS_ERROR_GENERAL: Tokenizer Decode failure.
 */
GENIE_API
Genie_Status_t GenieTokenizer_decode(const GenieTokenizer_Handle_t tokenizerHandle,
                                     const int32_t* tokenIds,
                                     const uint32_t numTokenIds,
                                     const Genie_AllocCallback_t callback,
                                     const char** outputString);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GENIE_TOKENIZER_H
