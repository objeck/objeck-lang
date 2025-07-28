//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================
#pragma once

#include "Wrapper.hpp"

#include "DlSystem/StringList.hpp"
#include "DlSystem/TensorMap.hpp"
#include "DlSystem/UserBufferMap.hpp"
#include "DlSystem/UserMemoryMap.hpp"
#include "DlSystem/IBufferAttributes.hpp"
#include "DiagLog/IDiagLog.hpp"
#include "DlSystem/SNPEPerfProfile.hpp"
#include "DlSystem/DlOptional.hpp"
#include "SNPE/SNPE.h"

namespace SNPE{

class SNPE :
    public Wrapper<SNPE, Snpe_SNPE_Handle_t, true>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_SNPE_Delete};

  template<typename T, typename H>
  static DlSystem::Optional<T> makeOptional(H handle){
    return DlSystem::Optional<T>(T(moveHandle(handle)));
  }
public:

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
 * @note Note that because the returned value is an Optional list,
 * the list must be verified as boolean true value before being
 * dereferenced.
 *
 * @return An Optional List of input tensor names.
 */
  DlSystem::Optional<DlSystem::StringList> getInputTensorNames() const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetInputTensorNames(handle()));
  }

/**
 * @brief Gets the names of output tensors to the network
 *
 * @return List of output tensor names.
 */
  DlSystem::Optional<DlSystem::StringList> getOutputTensorNames() const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetOutputTensorNames(handle()));
  }

/**
 * @brief Gets the names of output tensor from the input layer name
 *
 * @param[in] name Layer name
 *
 * @return Output tensor names.
 */
  DlSystem::StringList getOutputTensorNamesByLayerName(const char *name) const noexcept{
    return moveHandle(Snpe_SNPE_GetOutputTensorNamesByLayerName(handle(), name));
  }

/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] A map of tensors that contains the input data for
 *            each input. The names of tensors needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output tensormap has to be empty.  To forward propagate
 *       and get results in user-supplied tensors, use
 *       executeWithSuppliedOutputTensors.
 */
  bool execute(const DlSystem::TensorMap& input, DlSystem::TensorMap& output) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_ExecuteITensors(handle(), getHandle(input), getHandle(output));
  }

/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] A single tensor contains the input data.
 *
 * @param[in,out] An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output tensormap has to be empty.
 */
  bool execute(const DlSystem::ITensor* input, DlSystem::TensorMap& output) noexcept{
    if(!input) return false;
    return SNPE_SUCCESS == Snpe_SNPE_ExecuteITensor(handle(), getHandle(*input), getHandle(output));
  }

/**
 * @brief Processes the input data and returns the output, using
 *        user-supplied buffers
 *
 * @param[in] A map of UserBuffers that contains the input data for
 *            each input. The names of UserBuffers needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] A map of UserBuffers that will hold the output
 *                data of potentially multiple layers (the key
 *                in the map is the UserBuffer name)
 *
 * @note input and output UserBuffer maps must be fully pre-populated with
 *       dimensions matching what the network expects.
 *       For example, if there are 5 output UserBuffers they all have to be
 *       present in map.
 *
 *       Caller must guarantee that for the duration of execute(), the buffer
 *       stored in UserBuffer would remain valid.  For more detail on buffer
 *       ownership and lifetime requirements, please refer to zdl::DlSystem::UserBuffer
 *       documentation.
 */
  bool execute(const DlSystem::UserBufferMap& input, const DlSystem::UserBufferMap& output) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_ExecuteUserBuffers(handle(), getHandle(input), getHandle(output));
  }

/**
 * @brief Register Client ION Buffers
 *
 * @note To be deprecated, please use new api registerMemoryMappedBuffers
 *
 * @param[in] A UserMemoryMap of virtual addresses
 *
 * @note UserBuffer type passed for registration must match the data type of the tensor in the dlc
 *       For regular UserBuffers SNPE performs an online data conversion (quantization or
 *       dequantization etc). This is not possible for ion buffers hence can lead to
 *       issues during execution or accuracy degradation
 */
  bool registerIonBuffers(const DlSystem::UserMemoryMap& ionBufferMap) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_RegisterUserMemoryMappedBuffers(handle(), getHandle(ionBufferMap));
  }

/**
 * @brief Deregister Client ION Buffers
 *
 * @note To be deprecated, please use new api deregisterMemoryMappedBuffers
 *
 * @param[in] A StringList of ION Buffer names
 */
  bool deregisterIonBuffers(const DlSystem::StringList& ionBufferNames) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_DeregisterUserMemoryMappedBuffers(handle(), getHandle(ionBufferNames));
  }

/**
 * @brief Register Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] A UserMemoryMap of virtual addresses
 *
 * @note UserBuffer type passed for registration must match the data type of the tensor in the dlc
 *       For regular UserBuffers SNPE performs an online data conversion (quantization or
 *       dequantization etc). This is not possible for memory mapped buffers hence can lead to
 *       issues during execution or accuracy degradation
 */
  bool registerMemoryMappedBuffers(const DlSystem::UserMemoryMap& memoryMappedBufferMap) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_RegisterUserMemoryMappedBuffers(handle(), getHandle(memoryMappedBufferMap));
  }

/**
 * @brief Deregister Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] A StringList of memory mapped buffer names
 */
  bool deregisterMemoryMappedBuffers(const DlSystem::StringList& bufferNames) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_DeregisterUserMemoryMappedBuffers(handle(), getHandle(bufferNames));
  }

/**
 * @brief Returns the version string embedded at model conversion
 * time.
 *
 * @return Model version string, which is a free-form string
 *         supplied at the time of the conversion
 */
  std::string getModelVersion() const{
    auto str = Snpe_SNPE_GetModelVersion(handle());
    return str ? str : "";
  }

/**
 * @brief Returns the dimensions of the input data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
 *
 * @note Note that this function only makes sense for networks
 *       that have a fixed input size. For networks in which the
 *       input size varies with each call of Execute(), this
 *       function should not be used.
 *
 * @note Because the returned type is an Optional instance, it must
 *       be verified as a boolean true value before being dereferenced.
 *
 * @return An Optional instance of TensorShape that maintains dimensions,
 *         matching the tensor dimensions for input to the model,
 *         where the last entry is the fastest varying dimension, etc.
 *
 * @see DlSystem::ITensor
 * @see DlSystem::TensorShape
 * @see DlSystem::Optional
 */
  DlSystem::Optional<DlSystem::TensorShape> getInputDimensions() const noexcept{
    return makeOptional<DlSystem::TensorShape>(Snpe_SNPE_GetInputDimensionsOfFirstTensor(handle()));
  }

/**
 * @brief Returns the dimensions of the input data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
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
  DlSystem::Optional<DlSystem::TensorShape> getInputDimensions(const char* name) const noexcept{
    return makeOptional<DlSystem::TensorShape>(Snpe_SNPE_GetInputDimensions(handle(), name));
  }

/**
 * @brief Gets the output layer(s) for the network.
 *
 * Note that the output layers returned by this function may be
 * different than those specified when the network was created
 * via the zdl::SNPE::SNPEBuilder. For example, if the
 * network was created in debug mode with no explicit output
 * layers specified, this will contain all layers.
 *
 * @note Note that because the returned value is an Optional StringList,
 * the list must be verified as a boolean true value before being
 * dereferenced.
 *
 * @return A List of output layer names.
 *
 * @see zdl::DlSystem::Optional
 */
  DlSystem::Optional<DlSystem::StringList> getOutputLayerNames() const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetOutputLayerNames(handle()));
  }

/**
 * @brief Returns attributes of buffers used to feed input tensors and receive result from output tensors.
 *
 * @param[in] Tensor name.
 *
 * @return BufferAttributes of input/output tensor named
 */
  DlSystem::Optional<DlSystem::IBufferAttributes*> getInputOutputBufferAttributes(const char* name) const noexcept{
    return DlSystem::Optional<DlSystem::IBufferAttributes*>(
      new DlSystem::IBufferAttributes(moveHandle(Snpe_SNPE_GetInputOutputBufferAttributes(handle(), name))),
      DlSystem::Optional<DlSystem::IBufferAttributes*>::LIFECYCLE::POINTER_OWNED
    );
  }

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
 * @param[in] networkName Network name.
 *
 * @return A StringList of input tensor names.
 */
  DlSystem::Optional<DlSystem::StringList> getInputTensorNamesForNetwork(const char* networkName) const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetInputTensorNamesForNetwork(handle(), networkName));
  }

/**
 * @brief Gets the names of output tensors to the network
 *
 * @note The networkName is specified in snpe-dlc-info and defaults to the
 * name of the first graph in the DLC.
 *
 * @param[in] networkName Network name.
 *
 * @return List of output tensor names.
 */
  DlSystem::Optional<DlSystem::StringList> getOutputTensorNamesForNetwork(const char* networkName) const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetOutputTensorNamesForNetwork(handle(), networkName));
  }

/**
 * @brief Gets the names of output tensor from the input layer name
 *
 * @param[in] networkName Network name.
 *
 * @param[in] name Layer name
 *
 * @return Output tensor names.
 */
  DlSystem::StringList getOutputTensorNamesByLayerNameForNetwork(const char* networkName, const char *name) const noexcept{
    return moveHandle(Snpe_SNPE_GetOutputTensorNamesByLayerNameForNetwork(handle(), networkName, name));
  }

/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] input A map of tensors that contains the input data for
 *            each input. The names of tensors needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] output An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output TensorMap has to be empty. To forward propagate
 *       and get results in user-supplied tensors, use
 *       Snpe_SNPE_ExecuteUserBuffers().
 *
 * @return SNPE_SUCCESS upon successful execution
 */
  bool execute(const char* networkName, const DlSystem::TensorMap& input, DlSystem::TensorMap& output) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_ExecuteITensorsForNetwork(handle(), networkName, getHandle(input), getHandle(output));
  }

/**
 * @brief Processes the input data and returns the output
 *
 * @param[in] networkName Network name.
 *
 * @param[in] input A single tensor contains the input data.
 *
 * @param[in,out] output An empty map of tensors that will contain the output
 *                data of potentially multiple layers (the key
 *                in the map is the layer name) upon return
 *
 * @note output TensorMap has to be empty. To forward propagate
 *       and get results in user-supplied tensors, use
 *       Snpe_SNPE_ExecuteUserBuffers.
 *
 * @return SNPE_SUCCESS upon successful execution
 */
  bool execute(const char* networkName, const DlSystem::ITensor* input, DlSystem::TensorMap& output) noexcept{
    if(!input) return false;
    return SNPE_SUCCESS == Snpe_SNPE_ExecuteITensorForNetwork(handle(), networkName, getHandle(*input), getHandle(output));
  }

/**
 * @brief Processes the input data and returns the output, using
 *        user-supplied buffers
 *
 * @param[in] networkName Network name.
 *
 * @param[in] input A map of UserBuffers that contains the input data for
 *            each input. The names of UserBuffers needs to be
 *            matched with names retrieved through
 *            getInputTensorNames()
 *
 * @param[in,out] output A map of UserBuffers that will hold the output
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
  bool execute(const char* networkName, const DlSystem::UserBufferMap& input, const DlSystem::UserBufferMap& output) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_ExecuteUserBuffersForNetwork(handle(), networkName, getHandle(input), getHandle(output));
  }

/**
 * @brief Register Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] networkName Network name.
 *
 * @param[in] memoryMappedBuuferMap A UserMemoryMap of virtual addresses
 *
 * @note UserBuffer type passed for registration must match the data type of the tensor in the dlc
 *       For regular UserBuffers SNPE performs an online data conversion (quantization or
 *       dequantization etc). This is not possible for memory mapped buffers hence can lead to
 *       issues during execution or accuracy degradation
 *
 * @return SNPE_SUCCESS upon successful memory mapped buffer registration
 */
  bool registerMemoryMappedBuffersForNetwork(const char* networkName, const DlSystem::UserMemoryMap& memoryMappedBufferMap) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_RegisterUserMemoryMappedBuffersForNetwork(handle(), networkName, getHandle(memoryMappedBufferMap));
  }

/**
 * @brief Deregister Client Memory-Mapped Buffers (Example ION buffers in Android)
 *
 * @param[in] networkName Network name.
 *
 * @param[in] bufferNames A StringList of memory mapped buffer names
 *
 * @return SNPE_SUCCESS upon successful memory mapped buffer deregistration
 */
  bool deregisterMemoryMappedBuffersForNetwork(const char* networkName, const DlSystem::StringList& bufferNames) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_DeregisterUserMemoryMappedBuffersForNetwork(handle(), networkName, getHandle(bufferNames));
  }

/**
 * @brief Returns the dimensions of the input data to the model in the
 * form of TensorShape. The dimensions in TensorShape corresponds to
 * what the tensor dimensions would need to be for an input tensor to
 * the model.
 *
 * @param[in] networkName Network name.
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
  DlSystem::Optional<DlSystem::TensorShape> getInputDimensionsForNetwork(const char* networkName) const noexcept{
    return makeOptional<DlSystem::TensorShape>(Snpe_SNPE_GetInputDimensionsOfFirstTensorForNetwork(handle(), networkName));
  }

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
  DlSystem::Optional<DlSystem::TensorShape> getInputDimensionsForNetwork(const char* networkName, const char* name) const noexcept{
    return makeOptional<DlSystem::TensorShape>(Snpe_SNPE_GetInputDimensionsForNetwork(handle(), networkName, name));
  }

/**
 * @brief Gets the output layer(s) for the network.
 *
 * @param[in] networkName Network name.
 *
 * @note The output layers returned by this function may be
 * different than those specified when the network was created
 * via the @ref CAPI_SNPEBuilder "SNPEBuilder". For example, if the
 * network was created in debug mode with no explicit output
 * layers specified, this will contain all layers.
 *
 * @return A StringList of output layer names.
 */
  DlSystem::Optional<DlSystem::StringList> getOutputLayerNamesForNetwork(const char* networkName) const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetOutputLayerNamesForNetwork(handle(), networkName));
  }

/**
 * @brief Returns attributes of buffers used to feed input tensors and receive result from output tensors.
 *
 * @param[in] networkName Network name.
 *
 * @param[in] name Tensor name.
 *
 * @return BufferAttributes of input/output tensor named
 */
  DlSystem::Optional<DlSystem::IBufferAttributes*> getInputOutputBufferAttributesForNetwork(const char* networkName, const char* name) const noexcept{
    return DlSystem::Optional<DlSystem::IBufferAttributes*>(
      new DlSystem::IBufferAttributes(moveHandle(Snpe_SNPE_GetInputOutputBufferAttributesForNetwork(handle(), networkName, name))),
      DlSystem::Optional<DlSystem::IBufferAttributes*>::LIFECYCLE::POINTER_OWNED
    );
  }

/**
 * @brief Get the diagnostic logging interface
 *
 * @note Note that because the returned type is an Optional instance,
 * it must be verified as a boolean true value before being
 * dereferenced.
 *
 * @see DlSystem::Optional
 */
  DlSystem::Optional<DiagLog::IDiagLog*> getDiagLogInterface() noexcept{
    auto diagLogHandle = Snpe_SNPE_GetDiagLogInterface_Ref(handle());
    if(!diagLogHandle) return {};
    // Bind lifespan of this reference to this object
    auto toret = makeReference<DiagLog::IDiagLog>(diagLogHandle);
    return {toret, DlSystem::Optional<DiagLog::IDiagLog*>::LIFECYCLE::POINTER_NOT_OWNED};
  }

 /**
 * @brief Returns a stringList of network names managed by the snpeHandle.
 *
 * @return StringListHandle of networkNames
 */
  DlSystem::Optional<DlSystem::StringList> getNetworkNames() const noexcept{
    return makeOptional<DlSystem::StringList>(Snpe_SNPE_GetNetworkNames(handle()));
  }

  /**
  * @brief Sets performance profile
  *
  * @param[in] perfProfile, performance profile level
  *
  * @return true upon successful setting of performance profile
  */
  bool setPerformanceProfile(DlSystem::PerformanceProfile_t perfProfile) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_SetPerformanceProfile(handle(), static_cast<Snpe_PerformanceProfile_t>(perfProfile));
  }

  /**
  * @brief Sets custom performance profile
  *
  * @param[in] perfProfile, custom performance profile level
  *
  * @return true upon successful setting of custom performance profile
  */
  bool setCustomPerfProfile(DlSystem::SNPEPerfProfile perfProfile) noexcept{
    return SNPE_SUCCESS == Snpe_SNPE_SetCustomPerfProfile(handle(), getHandle(perfProfile));
  }

  /**
  * @brief Sets a preference for execution priority. Allows users to set the priority
  * of the graph. Setting this option overwrites the previous priority. SNPE runtime
  * is free to use this information to co-ordinate between different workloads that
  * may or may not extend beyond SNPE.
  *
  * @param[in] priority The target performance profile.
  *
  * @return true upon successful setting of custom performance profile
  *
  * @note On the Android platform, performance is determined by the priority level.
  * In contrast, on Windows, the operating system can adjust the priority level, which
  * means that performance cannot be guaranteed.
  *
  */
  bool setExecutionPriorityHint(DlSystem::ExecutionPriorityHint_t priority){
    return SNPE_SUCCESS == Snpe_SNPE_SetExecutionPriorityHint(handle(), static_cast<Snpe_ExecutionPriorityHint_t>(priority));
  }
private:
  SNPE(const SNPE&) = delete;
  SNPE& operator=(const SNPE&) = delete;
};

} // ns SNPE

ALIAS_IN_ZDL_NAMESPACE(SNPE, SNPE)