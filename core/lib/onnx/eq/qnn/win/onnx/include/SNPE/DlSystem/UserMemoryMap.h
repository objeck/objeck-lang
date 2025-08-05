//=============================================================================
//
//  Copyright (c) 2022-2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================

/**
 *  @file
 */

#ifndef DL_SYSTEM_USER_MEMORY_MAP_H
#define DL_SYSTEM_USER_MEMORY_MAP_H

#include "DlSystem/StringList.h"
#include "DlSystem/DlError.h"
#include "DlSystem/SnpeApiExportDefine.h"


#ifdef __cplusplus
extern "C" {
#endif


/**
 * A typedef to indicate a SNPE User Memory handle
 */
typedef void* Snpe_UserMemoryMap_Handle_t;

/**
 * @brief .
 *
 * Creates a new empty UserMemory map
 */
SNPE_API
Snpe_UserMemoryMap_Handle_t Snpe_UserMemoryMap_Create();

/**
 * copy constructor.
 * @param[in] other : Handle to the other object to copy.
 */
SNPE_API
Snpe_UserMemoryMap_Handle_t Snpe_UserMemoryMap_Copy(Snpe_UserMemoryMap_Handle_t other);

/**
 * Copy-assigns the contents of srcHandle into dstHandle
 *
 * @param[in] srcHandle Source UserMemoryMap handle
 *
 * @param[out] dstHandle Destination UserMemoryMap handle
 *
 * @return SNPE_SUCCESS on successful copy-assignment
 */
SNPE_API
Snpe_ErrorCode_t Snpe_UserMemoryMap_Assign(Snpe_UserMemoryMap_Handle_t srcHandle, Snpe_UserMemoryMap_Handle_t dstHandle);

/**
 * Destroys/frees UserMemory Map
 *
 * @param[in] handle : Handle to access UserMemory Map
 *
 * @return SNPE_SUCCESS if Delete operation successful.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_UserMemoryMap_Delete(Snpe_UserMemoryMap_Handle_t handle);

/**
 * @brief Adds a name and the corresponding buffer address
 *        to the map
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the UserMemory
 * @param[in] address : The pointer to the buffer memory. The addess is assumed
 *                      to be DSP Fast RPC allocated memory (libcdsprpc.so/dll)
 *
 * @note If a UserBuffer with the same name already exists, the new
 *       address would be added to the map along with the existing entries.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_UserMemoryMap_Add(Snpe_UserMemoryMap_Handle_t handle, const char *name, void *address);

/**
 * @brief Adds a name and the corresponding buffer address
 *        to the map
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the UserMemory
 * @param[in] address : The pointer to the buffer memory
 * @param[in] totalAllocatedSize : Total allocated size in bytes
 * @param[in] fd : The file descriptor to the Buffer Memory. Passing -1 would lead SNPE to assume
 *            the address to be DSP Fast RPC allocated memory (libcdsprpc.so/dll)
 * @param[in] offset : The byte offset to the Buffer Memory. This allows a single large block
 *            of memory to be allocated for multiple tensors and individual tensors are
 *            identified by a common address/fd and an unique byte offset to the real address
 *
 * @note totalAllocatedSize is the total size of the allocation even if all of it is not used or
 *       only a portion of it is used via offsets. This will not always be equal to tensor size
 *
 * @note If a UserBuffer with the same name already exists, the new address/fd/offset would be added to
 *       the map along with the existing entries.
 *       Passing totalAllocatedSize=0, fd=-1 and offset=0 to this API is equivalent to invoking
 *       Snpe_UserMemoryMap_Add()
 *
 */
SNPE_API
Snpe_ErrorCode_t Snpe_UserMemoryMap_AddFdOffset(Snpe_UserMemoryMap_Handle_t handle, const char *name,
                               void *address, size_t totalAllocatedSize, int32_t fd, uint64_t offset);

/**
 * @brief Removes a mapping of one Buffer address and its name by its name
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of Memory address to be removed
 *
 * @note If no UserBuffer with the specified name is found, nothing
 *       is done.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_UserMemoryMap_Remove(Snpe_UserMemoryMap_Handle_t handle, const char *name);

/**
 * @brief Returns the number of User Memory addresses in the map
 * @param[in] handle : Handle to access UserMemory Map
 */
SNPE_API
size_t Snpe_UserMemoryMap_Size(Snpe_UserMemoryMap_Handle_t handle);

/**
 * @brief .
 *
 * Removes all User Memory from the map
 * @param[in] handle : Handle to access UserMemory Map
 */
SNPE_API
Snpe_ErrorCode_t Snpe_UserMemoryMap_Clear(Snpe_UserMemoryMap_Handle_t handle);

/**
 * @brief .
 * Returns the names of all User Memory
 *
 * @param[in] handle : Handle to access UserMemory Map
 *
 * @return Returns a handle to the stringList.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_UserMemoryMap_GetUserBufferNames(Snpe_UserMemoryMap_Handle_t handle);

/**
 * @brief Returns the no of UserMemory addresses mapped to the buffer
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the UserMemory
 *
 */
SNPE_API
size_t Snpe_UserMemoryMap_GetUserMemoryAddressCount(Snpe_UserMemoryMap_Handle_t handle, const char *name);

/**
 * @brief Returns address at a specified index corresponding to a UserMemory buffer name
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the buffer
 * @param[in] index : The index in the list of addresses
 *
 */
SNPE_API
void* Snpe_UserMemoryMap_GetUserMemoryAddressAtIndex(Snpe_UserMemoryMap_Handle_t handle, const char *name,
                                                                                          uint32_t index);
/**
 * @brief Returns total allocated size at a specified index corresponding to a UserMemory buffer name
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the buffer
 * @param[in] index : The index in the list of addresses
 *
 */
SNPE_API
size_t Snpe_UserMemoryMap_GetUserMemoryTotalAllocatedSizeAtIndex(Snpe_UserMemoryMap_Handle_t handle,
                                                                  const char *name, uint32_t index);

/**
 * @brief Returns file descriptor at a specified index corresponding to a UserMemory buffer name
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the buffer
 * @param[in] index : The index in the list of addresses
 *
 */
SNPE_API
int32_t Snpe_UserMemoryMap_GetUserMemoryFdAtIndex(Snpe_UserMemoryMap_Handle_t handle, const char *name,
                                                                                        uint32_t index);

/**
 * @brief Returns offset at a specified index corresponding to a UserMemory buffer name
 *
 * @param[in] handle : Handle to access UserMemory Map
 * @param[in] name : The name of the buffer
 * @param[in] index : The index in the list of addresses
 *
 */
SNPE_API
uint64_t Snpe_UserMemoryMap_GetUserMemoryOffsetAtIndex(Snpe_UserMemoryMap_Handle_t handle,
                                                         const char *name, uint32_t index);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // DL_SYSTEM_USER_MEMORY_MAP_H
