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

#ifndef _SNPE_UTIL_H_
#define _SNPE_UTIL_H_

#include "SNPE/SNPE.h"
#include "DlSystem/DlEnums.h"
#include "DlSystem/DlError.h"
#include "DlSystem/SnpeApiExportDefine.h"
#include "DlSystem/IUserBuffer.h"
#include "DlSystem/ITensor.h"
#include "DlSystem/TensorShape.h"
#include "DlSystem/DlVersion.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Creates a UserBuffer
 *
 * @param[in] buffer Pointer to the buffer that the caller supplies
 *
 * @param[in] bufSize Buffer size, in bytes
 *
 * @param[in] stridesHandle Total number of bytes between elements in each dimension.
 *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
 *
 * @param[in] userBufferEncodingHandle Handle to a UserBufferEncoding object
 *
 * @note Caller has to ensure that memory pointed to by buffer stays accessible
 *       for the lifetime of the object created
 *
 * @return Handle to the created UserBuffer
 */
SNPE_API
Snpe_IUserBuffer_Handle_t Snpe_Util_CreateUserBuffer(void *buffer,
                                                     size_t bufSize,
                                                     Snpe_TensorShape_Handle_t stridesHandle,
                                                     Snpe_IUserBuffer_Handle_t userBufferEncodingHandle);

/**
 * @brief Creates a UserBuffer
 *
 * @param[in] buffer Pointer to the buffer that the caller supplies
 *
 * @param[in] bufSize Buffer size, in bytes
 *
 * @param[in] addrOffset byte offset to the actual address from the start of the allocated buffer.
 *            This is useful for sharing a large buffer amongst different tensors. When set to 0 this API
 *            behaves like Snpe_Util_CreateUserBuffer()
 *
 * @param[in] stridesHandle Total number of bytes between elements in each dimension.
 *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
 *
 * @param[in] userBufferEncodingHandle Handle to a UserBufferEncoding object
 *
 * @note Caller has to ensure that memory pointed to by buffer stays accessible
 *       for the lifetime of the object created
 *
 * @return Handle to the created UserBuffer
 */
SNPE_API
Snpe_IUserBuffer_Handle_t Snpe_Util_CreateUserBufferShared(void *buffer,
                                                           size_t bufSize,
                                                           uint64_t addrOffset,
                                                           Snpe_TensorShape_Handle_t stridesHandle,
                                                           Snpe_IUserBuffer_Handle_t userBufferEncodingHandle);

/**
 * @brief Creates a UserBuffer with a provided UserBufferSource
 *
 * @param[in] buffer Pointer to the buffer that the caller supplies
 *
 * @param[in] bufSize Buffer size, in bytes
 *
 * @param[in] stridesHandle Total number of bytes between elements in each dimension.
 *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
 *
 * @param[in] userBufferEncodingHandle Handle to a UserBufferEncoding object
 *
 * @param[in] userBufferSourceHandle Handle to a UserBufferSource object
 *
 * @return Handle to the created UserBuffer
 */
SNPE_API
Snpe_IUserBuffer_Handle_t Snpe_Util_CreateUserBufferFromSource(void *buffer,
                                                               size_t bufSize,
                                                               Snpe_TensorShape_Handle_t stridesHandle,
                                                               Snpe_IUserBuffer_Handle_t userBufferEncodingHandle,
                                                               Snpe_UserBufferSource_Handle_t userBufferSourceHandle);
/**
 * @brief Converts/Quantizes a float32 buffer to an unsigned fixed point 8/16 bit buffer
 *
 * @param[in] inputBuffer Pointer to the float32 buffer that the caller supplies
 *
 * @param[in] inputBufferSizeBytes Input buffer size, in bytes. Must be at least 4 bytes
 *
 * @param[in] isDynamicEncoding When set to false, scale and offset provided (next 2 argumets) will be used
 *            to qunatize. When set to true, scale and offset provided will be ignored and min/max will be computed
 *            from the inputBuffer to derrive the scale and offset for quantization. The computed scale and offset
 *            will be populated back to the caller in the next two arguments
 *
 * @param[in/out] scale Pointer to qunatization encoding scale to be passed in by the caller when isDynamicEncoding is true.
 *                      When isDynamicEncoding is false this argument is ignored and is populated as an outparam.
 *
 * @param[in/out] offset Pointer to qunatization encoding offset to be passed in by the caller when isDynamicEncoding is true.
 *                       When isDynamicEncoding is false this argument is ignored and is populated as an outparam.
 *
 * @param[in] outputBuffer Pointer to the unsigned fixed point 8/16 bit buffer that the caller supplies
 *
 * @param[in] outputBufferSizeBytes Output buffer size, in bytes. Must be large enough for all the inputs
 *
 * @param[in] bitWidth Quantization bitwidth (8 or 16)
 *
 * @note This API can do in-place quantization i.e. when outputBuffer equals inputBuffer pointer. But the output buffer
 *       CANNOT start at an offset from input buffer which will lead to corrupting inputs at sbsequent indexes.
 */
SNPE_API
bool Snpe_Util_Convert_Float32ToTfN(const float* inputBuffer,
                                    size_t inputBufferSizeBytes,
                                    bool isDynamicEncoding,
                                    float* scale,
                                    uint64_t* offset,
                                    void* outputBuffer,
                                    size_t outputBufferSizeBytes,
                                    unsigned bitWidth);
/**
 * @brief Converts/De-Quantizes an unsigned fixed point 8/16 bit buffer to a float32 buffer
 *
 * @param[in] inputBuffer Pointer to the unsigned fixed point 8/16 bit buffer that the caller supplies
 *
 * @param[in] inputBufferSizeBytes Input buffer size, in bytes
 *
 * @param[in/out] scale Qunatization encoding scale to be passed in by the caller
 *
 * @param[in/out] offset Qunatization encoding offset to be passed in by the caller
 *
 * @param[in] outputBuffer Pointer to the float32 buffer that the caller supplies
 *
 * @param[in] outputBufferSizeBytes Output buffer size, in bytes. Must be large enough for all the inputs
 *
 * @param[in] bitWidth Quantization bitwidth (8 or 16)
 *
 * @note This API CANNOT in place de-quantization. The input and output buffers must have no overlap
 */
SNPE_API
bool Snpe_Util_Convert_TfNToFloat32(const void* inputBuffer,
                                    size_t inputBufferSizeBytes,
                                    float scale,
                                    uint64_t offset,
                                    float* outputBuffer,
                                    size_t outputBufferSizeBytes,
                                    unsigned bitWidth);

/**
 * @brief Creates a UserBuffer
 *
 * @param[in] buffer Pointer to the buffer that the caller supplies
 *
 * @param[in] bufSize Buffer size, in bytes
 *
 * @param[in] stridesHandle Total number of bytes between elements in each dimension.
 *          E.g. A tightly packed tensor of floats with dimensions [4, 3, 2] would have strides of [24, 8, 4].
 *
 * @param[in] userBufferEncodingHandle Reference to an UserBufferEncoding object
 *
 * @param[in] userBufferSourceHandle Reference to an UserBufferSource object
 *
 * @note Caller has to ensure that memory pointed to by buffer stays accessible
 *       for the lifetime of the object created
 *
 * @return the created UserBuffer
 *
 */
SNPE_API
Snpe_IUserBuffer_Handle_t Snpe_Util_CreateUserGlBuffer(void *buffer,
                                                       size_t bufSize,
                                                       Snpe_TensorShape_Handle_t stridesHandle,
                                                       Snpe_IUserBuffer_Handle_t userBufferEncodingHandle,
                                                       Snpe_IUserBuffer_Handle_t userBufferSourceHandle);

/**
 * Creates a new ITensor with uninitialized data.
 *
 * ITensor buffer size assumes float32 encoding for each element.
 * (i.e., a tensor with dimensions (2,3) will be represented by (2 * 3) * 4 = 24 bytes in memory)
 *
 * The strides for the tensor will match the tensor dimensions
 * (i.e., the tensor data is contiguous in memory).
 *
 * @param[in] shapeHandle The dimensions for the tensor in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @return The created tensor
 */
SNPE_API
Snpe_ITensor_Handle_t Snpe_Util_CreateITensor(Snpe_TensorShape_Handle_t shapeHandle);

/**
 * Create a new ITensor with specific data.
 * (i.e. the tensor data is contiguous in memory). This tensor is
 * primarily used to create a tensor where tensor size can't be
 * computed directly from dimension. One such example is
 * NV21-formatted image, or any YUV formatted image
 *
 * @param[in] shapeHandle The dimensions for the tensor in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @param[in] data The actual data with which the Tensor object is filled.
 *
 * @param[in] dataSize The size of data
 *
 * @return A handle to the created tensor
 */
SNPE_API
Snpe_ITensor_Handle_t Snpe_Util_CreateITensorDataSize(Snpe_TensorShape_Handle_t shapeHandle, const uint8_t* data, size_t dataSize);

/**
 * Create a new ITensor with specific data.
 * (i.e. the tensor data is contiguous in memory). This tensor is
 * primarily used to create a tensor where tensor size can't be
 * computed directly from dimension. One such example is
 * NV21-formatted image, or any YUV formatted image
 *
 * @param[in] shapeHandle The dimensions for the tensor in which the last
 * element of the vector represents the fastest varying
 * dimension and the zeroth element represents the slowest
 * varying, etc.
 *
 * @param[in] data The actual data with which the Tensor object is filled.
 *
 * @param[in] dataSize The size of data
 *
 * @return the created tensor
 */
SNPE_API
Snpe_ITensor_Handle_t Snpe_Util_CreateITensor_NV21(Snpe_TensorShape_Handle_t shapeHandle, unsigned char *data, size_t dataSize);

/**
 * Indicates whether the supplied runtime is available on the
 * current platform.
 *
 * Note: It is advised to use this function to check DSP availability.
 * If the DSP is not already initialized, it will conduct the initialization - therefore,
 * please ensure that the appropriate arguments are passed, where relevant.
 *
 * @param[in] runtime The target runtime to check.
 *
 * @return Boolean: Non-zero if the supplied runtime is available; 0 otherwise
 *
 */
SNPE_API
int Snpe_Util_IsRuntimeAvailable(Snpe_Runtime_t runtime);

/**
 * Indicates whether the supplied runtime is available on the
 * current platform.
 *
 * Note: It is advised to use this function to check DSP availability.
 * If the DSP is not already initialized, it will conduct the initialization - therefore,
 * please ensure that the appropriate arguments are passed, where relevant.
 *
 * @param[in] runtime The target runtime to check.
 *
 * @param[in] deviceId deviceId in case of multi NSP devices.
 *
 * @return Boolean: Non-zero if the supplied runtime is available; 0 otherwise
 *
 */
SNPE_API
int Snpe_Util_IsRuntimeAvailableForDevice(Snpe_Runtime_t runtime, uint32_t deviceId);

/**
 * Indicates whether the supplied runtime is available on the
 * current platform.
 *
 * Note: It is advised to use this function to check DSP availability.
 * If the DSP is not already initialized, it will conduct the initialization - therefore,
 * please ensure that the appropriate arguments are passed, where relevant.
 *
 * @param[in] runtime The target runtime to check.
 *
 * @param[in] runtimeCheckOption Extent to perform runtime available check.
 *
 * @return Boolean: Non-zero if the supplied runtime is available; 0 otherwise
 *
 */
SNPE_API
int Snpe_Util_IsRuntimeAvailableCheckOption(Snpe_Runtime_t runtime,
                                            Snpe_RuntimeCheckOption_t runtimeCheckOption);

/**
 * Indicates whether the supplied runtime is available on the
 * current platform.
 *
 * Note: It is advised to use this function to check DSP availability.
 * If the DSP is not already initialized, it will conduct the initialization - therefore,
 * please ensure that the appropriate arguments are passed, where relevant.
 *
 * @param[in] runtime The target runtime to check.
 *
 * @param[in] runtimeCheckOption Extent to perform runtime available check.
 *
 * @param[in] deviceId deviceId in case of multi NSP devices
 *
 * @return Boolean: Non-zero if the supplied runtime is available; 0 otherwise
 *
 */
SNPE_API
int Snpe_Util_IsRuntimeAvailableCheckOptionForDevice(Snpe_Runtime_t runtime,
                                            Snpe_RuntimeCheckOption_t runtimeCheckOption,
                                            uint32_t deviceId);


/**
 * Gets the version of the SNPE library.
 *
 * @return Version of the SNPE library.
 *
 */
SNPE_API
Snpe_DlVersion_Handle_t Snpe_Util_GetLibraryVersion();

/**
 * Set the SNPE storage location for all SNPE instances in this
 * process. Note that this may only be called once, and if so
 * must be called before creating any SNPE instances.
 *
 * @param[in] storagePath Absolute path to a directory which SNPE may
 *  use for caching and other storage purposes.
 *
 * @return Boolean: Non-zero if the supplied path was succesfully set as
 *  the SNPE storage location, 0 otherwise.
 *
 */
SNPE_API
int Snpe_Util_SetSNPEStorageLocation(const char* storagePath);

/**
 * @brief Register a user-defined op package with SNPE.
 *
 * @param[in] regLibraryPath Path to the registration library
 *                      that allows clients to register a set of operations that are
 *                      part of the package, and share op info with SNPE
 *
 * @return Boolean: Non-zero if successful, 0 otherwise.
 */
SNPE_API
int Snpe_Util_AddOpPackage(const char* regLibraryPath );

/**
 * Indicates whether the OpenGL and OpenCL interoperability is supported
 * on GPU platform.
 *
 * @return Boolean: Non-zero if the OpenGL and OpenCl interop is supported; 0 otherwise
 *
 */
SNPE_API
int Snpe_Util_IsGLCLInteropSupported();

/**
 * @return A string description of the last error
 */
SNPE_API
const char* Snpe_Util_GetLastError();

/**
 * Initializes logging with the specified log level.
 * initializeLogging with level, is used on Android platforms
 * and after successful initialization, SNPE
 * logs are printed in android logcat logs.
 *
 * It is recommended to initializeLogging before creating any
 * SNPE instances, in order to capture information related to
 * core initialization. If this is called again after first
 * time initialization, subsequent calls are ignored.
 * Also, Logging can be re-initialized after a call to
 * terminateLogging API by calling initializeLogging again.
 *
 * A typical usage of Logging life cycle can be
 * initializeLogging()
 *        any other SNPE API like isRuntimeAvailable()
 * * setLogLevel() - optional - can be called anytime
 *         between initializeLogging & terminateLogging
 *		  SNPE instance creation, inference, destroy
 * terminateLogging().
 *
 * Please note, enabling logging can have performance impact.
 *
 * @param[in] level Log level (LOG_INFO, LOG_WARN, etc.).
 *
 * @return Boolean: non-zero if successful, 0 otherwise.
 */
SNPE_API
int Snpe_Util_InitializeLogging(Snpe_LogLevel_t level);

/**
 * Initializes logging with the specified log level and log path.
 * initializeLogging with level & log path, is used on non Android
 * platforms and after successful initialization, SNPE
 * logs are printed in std output & into log files created in the
 * log path.
 *
 * It is recommended to initializeLogging before creating any
 * SNPE instances, in order to capture information related to
 * core initialization. If this is called again after first
 * time initialization, subsequent calls are ignored.
 * Also, Logging can be re-initialized after a call to
 * terminateLogging API by calling initializeLogging again.
 *
 * A typical usage of Logging life cycle can be
 * initializeLogging()
 *        any other SNPE API like isRuntimeAvailable()
 * * setLogLevel() - optional - can be called anytime
 *         between initializeLogging & terminateLogging
 *		  SNPE instance creation, inference, destroy
 * terminateLogging()
 *
 * Please note, enabling logging can have performance impact
 *
 * @param[in] level Log level (LOG_INFO, LOG_WARN, etc.).
 *
 * @param[in] logPath of directory to store logs.
 *      If path is empty, the default path is "./Log".
 *      For android, the log path is ignored.
 *
 * @return Boolean: non-zero if successful, 0 otherwise.
 */
SNPE_API
int Snpe_Util_InitializeLoggingPath(Snpe_LogLevel_t level, const char* logPath);

/**
 * Updates the current logging level with the specified level.
 * setLogLevel is optional, called anytime after initializeLogging
 * and before terminateLogging, to update the log level set.
 * Log levels can be updated multiple times by calling setLogLevel
 * A call to setLogLevel() is ignored if it is made before
 * initializeLogging() or after terminateLogging()
 *
 * @param[in] level Log level (LOG_INFO, LOG_WARN, etc.).
 *
 * @return Boolean: non-zero if successful, 0 otherwise.
 */
SNPE_API
int Snpe_Util_SetLogLevel(Snpe_LogLevel_t level);

/**
 * Terminates logging.
 *
 * It is recommended to terminateLogging after initializeLogging
 * in order to disable logging information.
 * If this is called before initialization or after first time termination,
 * calls are ignored.
 *
 * @warning Snpe_Util_TerminateLogging() must not be called while another thread is executing.
 * In a multi-threaded use case, the individual threads must have a cooperative life cycle
 * management strategy for the logger.
 *
 * @return Boolean: non-zero if successful, 0 otherwise.
 */
SNPE_API
int Snpe_Util_TerminateLogging();


#ifdef __cplusplus
}  // extern "C"
#endif

#endif // _SNPE_UTIL_H_
