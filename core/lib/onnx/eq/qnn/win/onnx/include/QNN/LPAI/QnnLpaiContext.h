//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/** @file
 *  @brief QNN LPAI Context components
 */

#ifndef QNN_LPAI_CONTEXT_H
#define QNN_LPAI_CONTEXT_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "QnnLpaiContextInt.h"

typedef struct {
  uint32_t option;
  void* config;
} QnnLpaiContext_CustomConfig_t;
// clang-format on

typedef enum {
  // see QnnLpaiMem_MemType_t
  QNN_LPAI_CONTEXT_SET_CFG_MODEL_BUFFER_MEM_TYPE =
      QNN_LPAI_CONTEXT_SET_CFG_MODEL_BUFFER_MEM_TYPE_DEFAULT,

  /** Can be set at context level or per graph
   * 1. Context Level
   *    All graphs in the context will run in island mode
   *    Config value: NULL
   *
   * 2. Graph Level
   *    Only graph names specified in configs will run in island
   *    Each config will contain one graph name as a char*
   *    Config value: char* specifying which graph name is to be run in island
   *
   * Note: Both can not be set simultaneously.
   */
  QNN_LPAI_CONTEXT_SET_CFG_ENABLE_ISLAND =
    QNN_LPAI_CONTEXT_SET_CFG_ENABLE_ISLAND_DEFAULT,
  // Unused, present to ensure 32 bits.
  QNN_LPAI_CONTEXT_SET_CFG_UNDEFINED = 0x7fffffff
} QnnLpaiContext_SetConfigOption_t;

// clang-format off
// QnnLpaiContext_CustomConfig_t initializer macro
#define QNN_LPAI_CONTEXT_CUSTOM_CONFIG_INIT                        \
  {                                                                \
    QNN_LPAI_CONTEXT_SET_CFG_UNDEFINED,               /*option*/   \
    NULL                                              /*config*/   \
  }

// clang-format on
#ifdef __cplusplus
}  // extern "C"
#endif

#endif // QNN_LPAI_CONTEXT_H
