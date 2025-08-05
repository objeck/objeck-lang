//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================
#pragma once

#include <memory>
#include "Wrapper.hpp"

#include "SNPE.hpp"
#include "DlSystem/RuntimeList.hpp"
#include "DlContainer/IDlContainer.hpp"
#include "DlSystem/PlatformConfig.hpp"
#include "DlSystem/TensorShapeMap.hpp"
#include "DlSystem/SNPEPerfProfile.hpp"

#include "DlSystem/DlEnums.hpp"

#include "DlSystem/IOBufferDataTypeMap.hpp"

#include "SNPE/SNPEBuilder.h"


namespace SNPE {

class SNPEBuilder :
public Wrapper<SNPEBuilder, Snpe_SNPEBuilder_Handle_t>
{
  friend BaseType;

  static constexpr DeleteFunctionType DeleteFunction{Snpe_SNPEBuilder_Delete};
public:
/**
 * @brief Constructor of NeuralNetwork Builder ith a supplied model.
 *
 * @param[in] container A DlContainer holding the model.
 *
 * @return A new instance of a SNPEBuilder object
 *         that can be used to configure and build
 *         an instance of SNPE.
 */
  explicit SNPEBuilder(DlContainer::IDlContainer *container)
    :  BaseType(Snpe_SNPEBuilder_Create(getHandle(container)))
  {  }

/**
 * @brief Requests a performance profile.
 *
 * @param[in] performanceProfile The target performance profile.
 */
  SNPEBuilder& setPerformanceProfile(DlSystem::PerformanceProfile_t performanceProfile){
    Snpe_SNPEBuilder_SetPerformanceProfile(handle(), static_cast<Snpe_PerformanceProfile_t>(performanceProfile));
    return *this;
  }

/**
 * @brief Sets the profiling level. Default profiling level for
 *        SNPEBuilder is off. Off and basic only applies to DSP runtime.
 *
 * @param[in] profilingLevel The target profiling level.
 */
  SNPEBuilder& setProfilingLevel(DlSystem::ProfilingLevel_t profilingLevel){
    Snpe_SNPEBuilder_SetProfilingLevel(handle(), static_cast<Snpe_ProfilingLevel_t>(profilingLevel));
    return *this;
  }

/**
 * @brief Sets a preference for execution priority.
 * This allows the caller to give coarse hint to SNPE runtime
 * about the priority of the network.  SNPE runtime is free to use
 * this information to co-ordinate between different workloads
 * that may or may not extend beyond SNPE.
 *
 * @param[in] priority The target performance profile.
 */
  SNPEBuilder& setExecutionPriorityHint(DlSystem::ExecutionPriorityHint_t priority){
    Snpe_SNPEBuilder_SetExecutionPriorityHint(handle(), static_cast<Snpe_ExecutionPriorityHint_t>(priority));
    return *this;
  }

/**
 * @brief Sets the graphs that will be initialized in a multi-graph DLC.
 *
 * @note The networkName is specified in snpe-dlc-info and defaults to the
 * name of the first graph in the DLC.
 *
 * @param[in] networkNames List of network names to be initialized.
 */
  SNPEBuilder& networkInit(const DlSystem::StringList& networkNames){
    Snpe_SNPEBuilder_NetworkInit(handle(), getHandle(networkNames));
    return *this;
  }

/**
 * @brief Sets the layers that will generate output.
 *
 * @param[in] networkName specifies network name on which the output layer names are to be set
 *
 * @param[in] outputLayerNames List of layer names to output. An empty list will
 * result in only the final layer of the model being the output layer. The list will be copied.
 */
  SNPEBuilder& setOutputLayers(const char* networkName, const DlSystem::StringList& outputLayerNames){
    Snpe_SNPEBuilder_SetOutputLayersForNetwork(handle(), networkName, getHandle(outputLayerNames));
    return *this;
  }

/**
 * @brief Sets the layers that will generate output.
 *
 * @param[in] outputLayerNames List of layer names to output. An empty list will
 * result in only the final layer of the model being the output layer. The list will be copied.
 */
  SNPEBuilder& setOutputLayers(const DlSystem::StringList& outputLayerNames){
    Snpe_SNPEBuilder_SetOutputLayers(handle(), getHandle(outputLayerNames));
    return *this;
  }

/**
 * @brief Sets the output tensor names.
 *
 * @param[in] outputTensorNames List of tensor names to output. An empty list will
 * result in producing output for the final output tensor of the model. The list will be copied.
 */
  SNPEBuilder& setOutputTensors(const DlSystem::StringList& outputTensorNames){
    Snpe_SNPEBuilder_SetOutputTensors(handle(), getHandle(outputTensorNames));
    return *this;
  }

/**
 * @brief Sets the output tensor names.
 *
 * @param[in] networkName specifies network name on which the output tensor names are to be set
 *
 * @param[in] outputTensorNames List of tensor names to output. An empty list will
 * result in producing output for the final output tensor of the model. The list will be copied.
 */
  SNPEBuilder& setOutputTensors(const char* networkName, const DlSystem::StringList& outputTensorNames){
    Snpe_SNPEBuilder_SetOutputTensorsForNetwork(handle(), networkName, getHandle(outputTensorNames));
    return *this;
  }

/**
 * @brief Sets whether this neural network will perform inference with
 *        input from user-supplied buffers, and write output to user-supplied
 *        buffers.  Default behaviour is to use tensors created by
 *        ITensorFactory.
 *
 * @param[in] bufferMode Boolean whether to use user-supplied buffer or not.
 */
  SNPEBuilder& setUseUserSuppliedBuffers(int bufferMode){
    Snpe_SNPEBuilder_SetUseUserSuppliedBuffers(handle(), bufferMode);
    return *this;
  }

/**
 * @brief Sets the debug mode of the runtime.
 *
 * @param[in] debugMode This enables debug mode for the runtime. It does two things.
 * For an empty outputLayerNames list, all layers will be output. It might also disable
 * some internal runtime optimizations (e.g., some networks might be optimized by
 * combining layers etc.).
 */
  SNPEBuilder& setDebugMode(int debugMode){
    Snpe_SNPEBuilder_SetDebugMode(handle(), debugMode);
    return *this;
  }

/**
 * @brief Sets network's input dimensions to enable resizing of
 *        the spatial dimensions of each layer for fully convolutional networks,
 *        and the batch dimension for all networks.
 *
 * @param[in] inputDimensionsMap : Handle to the map of input names and their new dimensions.
 * The new dimensions overwrite the input dimensions embedded in the model and then
 * resize each layer of the model. If the model contains layers whose dimensions cannot be
 * resized e.g FullyConnected, exception will be thrown when SNPE instance is actually built.
 * In general the batch dimension is always resizable. After resizing of layers' dimensions
 * in model based on new input dimensions, the new model is revalidated against all runtime
 * constraints, whose failures may result in cpu fallback situation.
 */
  SNPEBuilder& setInputDimensions(const DlSystem::TensorShapeMap& inputDimensionsMap){
    Snpe_SNPEBuilder_SetInputDimensions(handle(), getHandle(inputDimensionsMap));
    return *this;
  }

/**
 * @brief Sets network's input dimensions to enable resizing of
 *        the spatial dimensions of each layer for fully convolutional networks,
 *        and the batch dimension for all networks.
 *
 * @param[in] networkName : specifies network name on which the input dims are to be set
 *
 * @param[in] inputDimensionsMap : Handle to the map of input names and their new dimensions.
 * The new dimensions overwrite the input dimensions embedded in the model and then
 * resize each layer of the model. If the model contains layers whose dimensions cannot be
 * resized e.g FullyConnected, exception will be thrown when SNPE instance is actually built.
 * In general the batch dimension is always resizable. After resizing of layers' dimensions
 * in model based on new input dimensions, the new model is revalidated against all runtime
 * constraints, whose failures may result in cpu fallback situation.
 */
  SNPEBuilder& setInputDimensions(const char* networkName, const DlSystem::TensorShapeMap& inputDimensionsMap){
    Snpe_SNPEBuilder_SetInputDimensionsForNetwork(handle(), networkName, getHandle(inputDimensionsMap));
    return *this;
  }

/**
 * @brief Sets the mode of init caching functionality.
 *
 * @param[in] mode   Boolean. This flag enables/disables the functionality of init caching.
 * When init caching functionality is enabled, a set of init caches will be created during
 * network building/initialization process and will be added to DLC container. If such DLC
 * is saved by the user, in subsequent network building/initialization processes these init
 * caches will be loaded from the DLC so as to reduce initialization time. In disable mode,
 * no init caches will be added to DLC container.
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setInitCacheMode(int cacheMode){
    Snpe_SNPEBuilder_SetInitCacheMode(handle(), cacheMode);
    return *this;
  }

/**
 * @brief Sets the platform configuration.
 *
 * @param[in] platformConfig The platform configuration.
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setPlatformConfig(const DlSystem::PlatformConfig& platformConfigHandle){
    Snpe_SNPEBuilder_SetPlatformConfig(handle(), getHandle(platformConfigHandle));
    return *this;
  }

/**
 * @brief Sets network's runtime order of precedence. Example:
 *        CPU_FLOAT32, GPU_FLOAT16, AIP_FIXED8_TF
 *
 * @param[in] runtimeListHandle The list of runtime in order of precedence
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setRuntimeProcessorOrder(const DlSystem::RuntimeList& runtimeList){
    Snpe_SNPEBuilder_SetRuntimeProcessorOrder(handle(), getHandle(runtimeList));
    return *this;
  }

/**
 * @brief Sets the unconsumed tensors as output
 *
 * @param[in] setOutput Boolean. This enables unconsumed tensors (i.e)
 * outputs which are not inputs to any layer (basically dead ends) to be marked
 * for output
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setUnconsumedTensorsAsOutputs(int setOutput){
    Snpe_SNPEBuilder_SetUnconsumedTensorsAsOutputs(handle(), setOutput);
    return *this;
  }

/**
 * @brief Execution terminated when exceeding time limit. Only valid for dsp runtime currently.
 *
 * @param[in] timeout Time limit value in microseconds
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setTimeOut(uint64_t timeoutMicroSec){
    Snpe_SNPEBuilder_SetTimeOut(handle(), timeoutMicroSec);
    return *this;
  }

/**
 * @brief Sets the datatype of the buffer. Only valid for dsp runtime currently.
 *
 * @param[in] dataTypeMapHandle Map of the buffer names and the datatype that needs to be set.
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setBufferDataType(const DlSystem::IOBufferDataTypeMap& dataTypeMap){
    Snpe_SNPEBuilder_SetBufferDataType(handle(), getHandle(dataTypeMap));
    return *this;
  }

/**
 * @brief Sets up the entire initialization callflow to happen on the user's thread
 *
 * @param[in] singleThreadedInit Flag indicating user's intent to perform initialization callflow
 * on caller's thread. When set to 1, initialization will happen on the user's thread.
 * When set to 0, initialization will happen on a new thread. This is the default behaviour(analogous to not calling this API)
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setSingleThreadedInit(int singleThreadedInit){
    Snpe_SNPEBuilder_SetSingleThreadedInit(handle(), singleThreadedInit);
    return *this;
  }

/**
 * @brief Sets the fixed point execution mode for CPU runtime.
 * If a floating point DLC is executed with this option set, the program will be terminated with an exception.
 * If a quantized DLC is executed without this option set, the execution will be in floating point mode in CPU.
 *
 * @param[in] cpuFxpMode Boolean If set to true, enables the fixed point mode.
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setCpuFixedPointMode(bool cpuFxpMode){
    Snpe_SNPEBuilder_SetCpuFixedPointMode(handle(), cpuFxpMode);
    return *this;
  }

/**
 * @brief Sets memoryLimitHint for DSP
 *        Based on the memory limit provided graphs will be switched from cache blob when execute is invoked.
 *
 *
 * @param[in] memoryLimitHintInMb Sets the peak memory limit hint of a cache in megabytes.
 *
 * @note Memory footprint is reduced at the cost of slower first execution after graph switching.
 *       Default is set to 0, set to any non zero value to enter low memory mode with multi graph init.
 *       User must first load the largest graph inside the cache to avoid undefined behaviors. User can
 *       strategically specify networkInit to ensure the first graph is the largest graph if
 *       it has not been serialized as the first graph in the cache.
 *       Currently any non zero value would enable graph switching.
 */
  SNPEBuilder& setMemoryLimitHint(uint64_t memoryLimitHintInMb){
    Snpe_SNPEBuilder_SetMemoryLimitHint(handle(), memoryLimitHintInMb);
    return *this;
  }

/**
 * @brief Sets model name for logging
 *
 * @param[in] modelName String Model name for logging.
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setModelName(DlSystem::String modelName){
    Snpe_SNPEBuilder_SetModelName(handle(), modelName.c_str());
    return *this;
  }

/**
 * @brief Sets compatibility check mode
 *
 * @param[in] mode Mode of checking compatibility
 *
 * @return The current instance of SNPEBuilder.
 */
  SNPEBuilder& setCacheCompatibilityMode(DlSystem::CacheCompatibility_t mode){
    Snpe_SNPEBuilder_SetCacheCompatibilityMode(handle(), static_cast<Snpe_CacheCompatibility_t>(mode));
    return *this;
  }

/**
 * @brief Validates the cache records available in the DLC for compatibility with the current hardware
 *        depending on the set cache compatibility mode
 *
 * @note The intended usage of this function is to validate the cache ahead of time to determine the need for
 *       setting the init cache option before calling the build function. It is expected to be called after all
 *       other builder options have been set and before the build function is invoked. In particular, this
 *       function is sensitive to the resized input dimensions, output op and tensor names, and cache
 *       compatibility mode settings
 *
 * @note Validation will always fail with SNPE_ERRORCODE_CONFIG_INVALID_PARAM if coupled with cache compatibility
 *       mode ALWAYS_GENERATE_NEW_CACHE
 *
 * @return  One of the following Snpe_ErrorCode_t
 *          SNPE_SUCCESS: The DLC contains a valid and usable cache record
 *          SNPE_ERRORCODE_DLCACHING_INCOMPATIBLE: None of the existing caches are compatible on the current hardware
 *          SNPE_ERRORCODE_DLCACHING_SUBOPTIMAL_CACHE: Compatibility mode is strict and the best available cache is suboptimal
 *          SNPE_ERRORCODE_DLCONTAINER_MISSING_RECORDS: The DLC contains no cache records
 *          SNPE_ERRORCODE_DLCONTAINER_INVALID_RECORD: Failed to read DLC records
 *          SNPE_ERRORCODE_CONFIG_INVALID_PARAM: An invalid builder configurations is set at the time of invocation
 *          SNPE_ERRORCODE_UNKNOWN_EXCEPTION: Encountered an unexpected miscellaneous error
 */
  Snpe_ErrorCode_t validateCache(){
    return Snpe_SNPEBuilder_ValidateCache(handle());
  }

/**
 * @brief Sets custom performance profile
 *
 * @param[in] perfProfile, performance profile level
 *
 * @return SNPE_SUCCESS upon successful setting of performance profile
 */
  Snpe_ErrorCode_t setCustomPerfProfile(DlSystem::SNPEPerfProfile perfProfile){
    return Snpe_SNPEBuilder_SetCustomPerfProfile(handle(), getHandle(perfProfile));
  }

/**
 * @brief Returns an instance of SNPE based on the current parameters.
 *
 * @return A new instance of a SNPE object that can be used
 *         to execute models or null if any errors occur.
 */
  std::unique_ptr<SNPE> build() noexcept{
    auto h = Snpe_SNPEBuilder_Build(handle());
    return h ?  makeUnique<SNPE>(h) : nullptr;
  }

};

} // ns SNPE

ALIAS_IN_ZDL_NAMESPACE(SNPE, SNPEBuilder)