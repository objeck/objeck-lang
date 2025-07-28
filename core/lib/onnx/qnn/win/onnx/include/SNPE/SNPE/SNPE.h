//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

/**
 *  @file
 */

#ifndef _SNPE_SNPE_H_
#define _SNPE_SNPE_H_


#include "DlSystem/IBufferAttributes.h"
#include "DlSystem/ITensor.h"
#include "DlSystem/TensorShape.h"
#include "DlSystem/TensorMap.h"
#include "DlSystem/StringList.h"
#include "DlSystem/IUserBuffer.h"
#include "DlSystem/UserBufferMap.h"
#include "DlSystem/UserMemoryMap.h"
#include "DlSystem/DlError.h"
#include "DlSystem/SnpeApiExportDefine.h"
#include "DlSystem/DlEnums.h"
#include "DlSystem/SNPEPerfProfile.h"
#include "DiagLog/IDiagLog.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A typedef to indicate a SNPE handle
 */
typedef void* Snpe_SNPE_Handle_t;

/**
 * Destroys/frees a SNPE object
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @return SNPE_SUCCESS if Delete operation successful.
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_Delete(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Gets the names of input tensors to the network
 *
 * To support multiple input scenarios, where multiple tensors are
 * passed through execute() in a TensorMap, each tensor needs to
 * be uniquely named. The names of tensors can be retrieved
 * through this function.
 *
 * In the case of a single input, one name will be returned.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @return A StringList of input tensor names.
 *
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetInputTensorNames(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Gets the names of output tensors to the network
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @return List of output tensor names.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetOutputTensorNames(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Gets the names of output tensor from the input layer name
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 * @param[in] name Layer name
 *
 * @return Output tensor names.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetOutputTensorNamesByLayerName(Snpe_SNPE_Handle_t snpeHandle, const char* name);


/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] inputHandle A map of tensors that contains the input data for
 *            each input. The names of tensors needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] outputHandle An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output TensorMap has to be empty. To forward propagate
 *       and get results in user-supplied tensors, use
 *       Snpe_SNPE_ExecuteUserBuffers().
 *
 * @return SNPE_SUCCESS upon successful execution
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_ExecuteITensors(Snpe_SNPE_Handle_t snpeHandle, Snpe_TensorMap_Handle_t inputHandle,
                                                                        Snpe_TensorMap_Handle_t outputHandle);

/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] inputHandle A single tensor contains the input data.
 *
 * @param[in,out] outputHandle An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output TensorMap has to be empty. To forward propagate
 *       and get results in user-supplied tensors, use
 *       Snpe_SNPE_ExecuteUserBuffers.
 *
 * @return SNPE_SUCCESS upon successful execution
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_ExecuteITensor(Snpe_SNPE_Handle_t snpeHandle, Snpe_ITensor_Handle_t inputHandle, Snpe_TensorMap_Handle_t outputHandle);

/**
 * @brief Processes the input data and returns the output, using
 *        user-supplied buffers
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] inputHandle A map of UserBuffers that contains the input data for
 *            each input. The names of UserBuffers needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] outputHandle A map of UserBuffers that will hold the output
 *                data of potentially multiple layers (the key
 *                in the map is the UserBuffer name)
 *
 * @note input and output UserBuffer maps must be fully pre-populated. with
 *       dimensions matching what the network expects.
 *       For example, if there are 5 output UserBuffers they all have to be
 *       present in map.
 *
 *       Caller must guarantee that for the duration of execute(), the buffer
 *       stored in UserBuffer would remain valid.  For more detail on buffer
 *       ownership and lifetime requirements, please refer to zdl::DlSystem::UserBuffer
 *       documentation.
 *
 * @return SNPE_SUCCESS upon successful execution
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_ExecuteUserBuffers(Snpe_SNPE_Handle_t snpeHandle, Snpe_UserBufferMap_Handle_t inputHandle,
                                                                           Snpe_UserBufferMap_Handle_t outputHandle);


/**
 * @brief Register Client ION Buffers
 *
 * @note To be deprecated, please use new api Snpe_SNPE_RegisterUserMemoryMappedBuffers
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] ionBufferMapHandle A UserMemoryMap of virtual addresses
 *
 * @return SNPE_SUCCESS upon successful ION Buffer registration
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_RegisterIonBuffers(Snpe_SNPE_Handle_t snpeHandle, Snpe_UserMemoryMap_Handle_t ionBufferMapHandle);

/**
 * @brief Deregister Client ION Buffers
 *
 * @note To be deprecated, please use new api Snpe_SNPE_DeregisterUserMemoryMappedBuffers
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] ionBufferNamesHandle A StringList of ION Buffer names
 *
 * @return SNPE_SUCCESS upon successful ION Buffer deregistration
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_DeregisterIonBuffers(Snpe_SNPE_Handle_t snpeHandle, Snpe_StringList_Handle_t ionBufferNamesHandle);

/**
 * @brief Register Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] bufferMapHandle A UserMemoryMap of virtual addresses
 *
 * @note UserBuffer type passed for registration must match the data type of the tensor in the dlc
 *       For regular UserBuffers SNPE performs an online data conversion (quantization or
 *       dequantization etc). This is not possible for memory mapped buffers hence can lead to
 *       issues during execution or accuracy degradation
 *
 * @return SNPE_SUCCESS upon successful memory mapped buffer registration
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_RegisterUserMemoryMappedBuffers(Snpe_SNPE_Handle_t snpeHandle, Snpe_UserMemoryMap_Handle_t bufferMapHandle);

/**
 * @brief Deregister Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] bufferNamesHandle A StringList of memory mapped buffer names
 *
 * @return SNPE_SUCCESS upon successful memory mapped buffer deregistration
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_DeregisterUserMemoryMappedBuffers(Snpe_SNPE_Handle_t snpeHandle, Snpe_StringList_Handle_t bufferNamesHandle);

/**
 * @brief Returns the version string embedded at model conversion
 * time.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @return Model version string, which is a free-form string
 *         supplied at the time of the conversion
 *
 */
SNPE_API
const char* Snpe_SNPE_GetModelVersion(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Returns the dimensions of the input data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] name input name.
 *
 * @note Note that this function only makes sense for networks
 *       that have a fixed input size. For networks in which the
 *       input size varies with each call of Execute(), this
 *       function should not be used.
 *
 * @return a TensorShape that maintains dimensions,
 *         matching the tensor dimensions for input to the model,
 *         where the last entry is the fastest varying dimension, etc.
 *
 * @see Snpe_ITensor_Handle_t
 * @see Snpe_TensorShape_Handle_t
 */
SNPE_API
Snpe_TensorShape_Handle_t Snpe_SNPE_GetInputDimensions(Snpe_SNPE_Handle_t snpeHandle, const char* name);

/**
 * @brief Returns the dimensions of the first input's data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @note Note that this function only makes sense for networks
 *       that have a fixed input size. For networks in which the
 *       input size varies with each call of Execute(), this
 *       function should not be used.
 *
 * @return a TensorShape that maintains dimensions,
 *         matching the tensor dimensions for first input to the model,
 *         where the last entry is the fastest varying dimension, etc.
 *
 * @see Snpe_ITensor_Handle_t
 * @see Snpe_TensorShape_Handle_t
 */
SNPE_API
Snpe_TensorShape_Handle_t Snpe_SNPE_GetInputDimensionsOfFirstTensor(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Gets the output layer(s) for the network.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @note The output layers returned by this function may be
 * different than those specified when the network was created
 * via the @ref CAPI_SNPEBuilder "SNPEBuilder". For example, if the
 * network was created in debug mode with no explicit output
 * layers specified, this will contain all layers.
 *
 *
 * @return A StringList of output layer names.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetOutputLayerNames(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Returns attributes of buffers used to feed input tensors and receive result from output tensors.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] name Tensor name.
 *
 * @return BufferAttributes of input/output tensor named
 */
SNPE_API
Snpe_IBufferAttributes_Handle_t Snpe_SNPE_GetInputOutputBufferAttributes(Snpe_SNPE_Handle_t snpeHandle, const char *name);

/**
 * @brief Gets the names of input tensors to the network
 *
 * To support multiple input scenarios, where multiple tensors are
 * passed through execute() in a TensorMap, each tensor needs to
 * be uniquely named. The names of tensors can be retrieved
 * through this function.
 *
 * In the case of a single input, one name will be returned.
 *
 * @note The networkName is specified in snpe-dlc-info and defaults to the
 * name of the first graph in the DLC.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @return A StringList of input tensor names.
 *
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetInputTensorNamesForNetwork(Snpe_SNPE_Handle_t snpeHandle, const char *networkName);

/**
 * @brief Gets the names of output tensors to the network
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @return List of output tensor names.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetOutputTensorNamesForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                                                                       const char *networkName);

/**
 * @brief Gets the names of output tensor from the input layer name
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] name Layer name
 *
 * @return Output tensor names.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetOutputTensorNamesByLayerNameForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                                                                const char *networkName, const char* name);


/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] inputHandle A map of tensors that contains the input data for
 *            each input. The names of tensors needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] outputHandle An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output TensorMap has to be empty. To forward propagate
 *       and get results in user-supplied tensors, use
 *       Snpe_SNPE_ExecuteUserBuffers().
 *
 * @return SNPE_SUCCESS upon successful execution
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_ExecuteITensorsForNetwork(Snpe_SNPE_Handle_t snpeHandle, const char *networkName,
                                 Snpe_TensorMap_Handle_t inputHandle, Snpe_TensorMap_Handle_t outputHandle);

/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] inputHandle A single tensor contains the input data.
 *
 * @param[in,out] outputHandle An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output TensorMap has to be empty. To forward propagate
 *       and get results in user-supplied tensors, use
 *       Snpe_SNPE_ExecuteUserBuffers.
 *
 * @return SNPE_SUCCESS upon successful execution
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_ExecuteITensorForNetwork(Snpe_SNPE_Handle_t snpeHandle, const char *networkName,
                                  Snpe_ITensor_Handle_t inputHandle, Snpe_TensorMap_Handle_t outputHandle);

/**
 * @brief Processes the input data and returns the output, using
 *        user-supplied buffers
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] inputHandle A map of UserBuffers that contains the input data for
 *            each input. The names of UserBuffers needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] outputHandle A map of UserBuffers that will hold the output
 *                data of potentially multiple layers (the key
 *                in the map is the UserBuffer name)
 *
 * @note input and output UserBuffer maps must be fully pre-populated. with
 *       dimensions matching what the network expects.
 *       For example, if there are 5 output UserBuffers they all have to be
 *       present in map.
 *
 *       Caller must guarantee that for the duration of execute(), the buffer
 *       stored in UserBuffer would remain valid.  For more detail on buffer
 *       ownership and lifetime requirements, please refer to zdl::DlSystem::UserBuffer
 *       documentation.
 *
 * @return SNPE_SUCCESS upon successful execution
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_ExecuteUserBuffersForNetwork(Snpe_SNPE_Handle_t snpeHandle, const char *networkName,
                            Snpe_UserBufferMap_Handle_t inputHandle, Snpe_UserBufferMap_Handle_t outputHandle);

/**
 * @brief Register Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] bufferMapHandle A UserMemoryMap of virtual addresses
 *
 * @note UserBuffer type passed for registration must match the data type of the tensor in the dlc
 *       For regular UserBuffers SNPE performs an online data conversion (quantization or
 *       dequantization etc). This is not possible for memory mapped buffers hence can lead to
 *       issues during execution or accuracy degradation
 *
 * @return SNPE_SUCCESS upon successful memory mapped buffer registration
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_RegisterUserMemoryMappedBuffersForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                             const char *networkName, Snpe_UserMemoryMap_Handle_t bufferMapHandle);

/**
 * @brief Deregister Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] bufferNamesHandle A StringList of memory mapped buffer names
 *
 * @return SNPE_SUCCESS upon successful memory mapped buffer deregistration
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_DeregisterUserMemoryMappedBuffersForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                                const char *networkName, Snpe_StringList_Handle_t bufferNamesHandle);

/**
 * @brief Returns the dimensions of the input data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] name input name.
 *
 * @note Note that this function only makes sense for networks
 *       that have a fixed input size. For networks in which the
 *       input size varies with each call of Execute(), this
 *       function should not be used.
 *
 * @return a TensorShape that maintains dimensions,
 *         matching the tensor dimensions for input to the model,
 *         where the last entry is the fastest varying dimension, etc.
 *
 * @see Snpe_ITensor_Handle_t
 * @see Snpe_TensorShape_Handle_t
 */
SNPE_API
Snpe_TensorShape_Handle_t Snpe_SNPE_GetInputDimensionsForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                                                    const char *networkName, const char* name);

/**
 * @brief Returns the dimensions of the first input's data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @note Note that this function only makes sense for networks
 *       that have a fixed input size. For networks in which the
 *       input size varies with each call of Execute(), this
 *       function should not be used.
 *
 * @return a TensorShape that maintains dimensions,
 *         matching the tensor dimensions for first input to the model,
 *         where the last entry is the fastest varying dimension, etc.
 *
 * @see Snpe_ITensor_Handle_t
 * @see Snpe_TensorShape_Handle_t
 */
SNPE_API
Snpe_TensorShape_Handle_t Snpe_SNPE_GetInputDimensionsOfFirstTensorForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                                                                              const char *networkName);

/**
 * @brief Gets the output layer(s) for the network.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @note The output layers returned by this function may be
 * different than those specified when the network was created
 * via the @ref CAPI_SNPEBuilder "SNPEBuilder". For example, if the
 * network was created in debug mode with no explicit output
 * layers specified, this will contain all layers.
 *
 *
 * @return A StringList of output layer names.
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetOutputLayerNamesForNetwork(Snpe_SNPE_Handle_t snpeHandle, const char *networkName);

/**
 * @brief Returns attributes of buffers used to feed input tensors and receive result from output tensors.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] networkName Network name.
 *
 * @param[in] name Tensor name.
 *
 * @return BufferAttributes of input/output tensor named
 */
SNPE_API
Snpe_IBufferAttributes_Handle_t Snpe_SNPE_GetInputOutputBufferAttributesForNetwork(Snpe_SNPE_Handle_t snpeHandle,
                                                                      const char *networkName, const char *name);

/**
 * @brief .
 *
 * Get the diagnostic logging interface
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 */
SNPE_API
Snpe_IDiagLog_Handle_t Snpe_SNPE_GetDiagLogInterface_Ref(Snpe_SNPE_Handle_t snpeHandle);


/**
 * @brief Returns a stringList of network names managed by the snpeHandle.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 *
 * @return StringListHandle of networkNames
 */
SNPE_API
Snpe_StringList_Handle_t Snpe_SNPE_GetNetworkNames(Snpe_SNPE_Handle_t snpeHandle);

/**
 * @brief Set Performance profile
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] performanceProfile Value of performance profile to be set
 *
 * @return true if successfully applied
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_SetPerformanceProfile(Snpe_SNPE_Handle_t snpeHandle, Snpe_PerformanceProfile_t performanceProfile);

/**
 * @brief Set custom performance profile
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] perfProfileHandle Handle of perfProfile object
 *
 * @return true if successfully applied
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_SetCustomPerfProfile(Snpe_SNPE_Handle_t snpeHandle, Snpe_SNPEPerfProfile_Handle_t perfProfileHandle);

/**
 * @brief Sets a preference for execution priority.
 *
 * @brief Sets a preference for execution priority. Allows users to set the priority
 * of the graph. Setting this option overwrites the previous priority. SNPE runtime
 * is free to use this information to co-ordinate between different workloads that
 * may or may not extend beyond SNPE.
 *
 * @param[in] snpeHandle Handle to access the SNPE object
 *
 * @param[in] priority The target performance profile
 *
 * @return true if successfully applied
 *
 * @note On the Android platform, performance is determined by the priority level.
 * In contrast, on Windows, the operating system can adjust the priority level, which
 * means that performance cannot be guaranteed.
 *
 */
SNPE_API
Snpe_ErrorCode_t Snpe_SNPE_SetExecutionPriorityHint(Snpe_SNPE_Handle_t snpeHandle, Snpe_ExecutionPriorityHint_t priority);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // _SNPE
