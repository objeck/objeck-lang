//=============================================================================
//
//  Copyright (c) 2023 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once
#include "Wrapper.hpp"
#include "DlSystem/DlEnums.hpp"
#include "DlSystem/DlVersion.hpp"
#include "DlSystem/ITensorFactory.hpp"
#include "DlSystem/IUserBufferFactory.hpp"


#include "SNPE/SNPEUtil.h"
#include "DlSystem/DlEnums.h"

namespace SNPE {

/**
 * The factory class for creating SNPE objects.
 *
 */
class SNPEFactory {
public:

/**
 * @brief Indicates whether the supplied runtime is available on the current platform.
 *
 * Note: It is advised to use this function to check DSP availability.
 * If the DSP is not already initialized, it will conduct the initialization - therefore,
 * please ensure that the appropriate arguments are passed, where relevant.
 *
 * @param[in] runtime The target runtime to check.
 *
 * @param[in] deviceId deviceId in case of multi NSP devices, 0 by default.
 *
 * @return True if the supplied runtime is available; false,
 *         otherwise.
 */
  static bool isRuntimeAvailable(DlSystem::Runtime_t runtime, uint32_t deviceId = 0){
    return Snpe_Util_IsRuntimeAvailableForDevice(static_cast<Snpe_Runtime_t>(runtime), deviceId);
  }

/**
 * @brief Indicates whether the supplied runtime is available on the current platform.
 *
 * Note: It is advised to use this function to check DSP availability.
 * If the DSP is not already initialized, it will conduct the initialization - therefore,
 * please ensure that the appropriate arguments are passed, where relevant.
 *
 * @param[in] runtime The target runtime to check.
 *
 * @param[in] option Extent to perform runtime available check.
 *
 * @param[in] deviceId deviceId in case of multi NSP devices, 0 by default.
 *
 * @return True if the supplied runtime is available; false,
 *         otherwise.
 */
  static bool isRuntimeAvailable(DlSystem::Runtime_t runtime, DlSystem::RuntimeCheckOption_t option,
                                                              uint32_t deviceId = 0){
    return Snpe_Util_IsRuntimeAvailableCheckOptionForDevice(static_cast<Snpe_Runtime_t>(runtime),
                                                   static_cast<Snpe_RuntimeCheckOption_t>(option),
                                                   deviceId);
  }

/**
 * @brief Gets a reference to the tensor factory.
 *
 * @return A reference to the tensor factory.
 */
  static DlSystem::ITensorFactory& getTensorFactory(){
    static DlSystem::ITensorFactory iTensorFactory;
    return iTensorFactory;
  }

/**
 * @brief Gets a reference to the UserBuffer factory.
 *
 * @return A reference to the UserBuffer factory.
 */
  static DlSystem::IUserBufferFactory& getUserBufferFactory(){
    static DlSystem::IUserBufferFactory iUserBufferFactory;
    return iUserBufferFactory;
  }

/**
 * @brief Gets the version of the SNPE library.
 *
 * @return Version of the SNPE library.
 *
 */
  static DlSystem::Version_t getLibraryVersion(){
    return WrapperDetail::moveHandle(Snpe_Util_GetLibraryVersion());
  }

/**
 * @brief Set the SNPE storage location for all SNPE instances in this
 * process. Note that this may only be called once, and if so
 * must be called before creating any SNPE instances.
 *
 * @param[in] storagePath Absolute path to a directory which SNPE may
 *  use for caching and other storage purposes.
 *
 * @return True if the supplied path was succesfully set as
 *  the SNPE storage location, false otherwise.
 */
  static bool setSNPEStorageLocation(const char* storagePath){
    return SNPE_SUCCESS == Snpe_Util_SetSNPEStorageLocation(storagePath);
  }

/**
 * @brief Register a user-defined op package with SNPE.
 *
 * @param[in] regLibraryPath Path to the registration library
 *                      that allows clients to register a set of operations that are
 *                      part of the package, and share op info with SNPE
 *
 * @return True if successful, False otherwise.
 */
  static bool addOpPackage(const std::string& regLibraryPath){
    return SNPE_SUCCESS == Snpe_Util_AddOpPackage(regLibraryPath.c_str());
  }

/**
 * @brief Indicates whether the OpenGL and OpenCL interoperability is supported
 * on GPU platform.
 *
 * @return True if the OpenGL and OpenCl interop is supported; false,
 *         otherwise.
 */
  static bool isGLCLInteropSupported(){
    return Snpe_Util_IsGLCLInteropSupported();
  }

/**
 * @brief Returns last error code
 *
 * @return Error code
 */
  static const char* getLastError(){
    return Snpe_Util_GetLastError();
  }

/**
 * @brief Initializes logging with the specified log level.
 * InitializeLogging with level is used on Android platforms
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
 *     SNPE instance creation, inference, destroy
 * terminateLogging().
 *
 * Please note, enabling logging can have performance impact.
 *
 * @param[in] LogLevel_t Log level (LOG_INFO, LOG_WARN, etc.).
 *
 * @return True if successful, False otherwise.
 */
  static bool initializeLogging(const DlSystem::LogLevel_t& level){
    return Snpe_Util_InitializeLogging(static_cast<Snpe_LogLevel_t>(level));
  }

/**
 * @brief Initializes logging with the specified log level and log path.
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
 *     SNPE instance creation, inference, destroy
 * terminateLogging()
 *
 * Please note, enabling logging can have performance impact
 *
 * @param[in] LogLevel_t Log level (LOG_INFO, LOG_WARN, etc.).
 *
 * @param[in] Path of directory to store logs.
 *      If path is empty, the default path is "./Log".
 *      For android, the log path is ignored.
 *
 * @return True if successful, False otherwise.
 */
  static bool initializeLogging(const DlSystem::LogLevel_t& level, const std::string& logPath){
    return Snpe_Util_InitializeLoggingPath(static_cast<Snpe_LogLevel_t>(level), logPath.c_str());
  }

/**
 * @brief Updates the current logging level with the specified level.
 * setLogLevel is optional, called anytime after initializeLogging
 * and before terminateLogging, to update the log level set.
 * Log levels can be updated multiple times by calling setLogLevel
 * A call to setLogLevel() is ignored if it is made before
 * initializeLogging() or after terminateLogging()
 *
 * @param[in] LogLevel_t Log level (LOG_INFO, LOG_WARN, etc.).
 *
 * @return True if successful, False otherwise.
 */
  static bool setLogLevel(const DlSystem::LogLevel_t& level){
    return Snpe_Util_SetLogLevel(static_cast<Snpe_LogLevel_t>(level));
  }

/**
 * @brief Terminates logging. It is recommended to terminateLogging after initializeLogging
 * in order to disable logging information. If this is called before initialization or after
 * first time termination, calls are ignored.
 *
 * @warning terminateLogging() must not be called while another thread is executing.
 * In a multi-threaded use case, the individual threads must have a cooperative life cycle
 * management strategy for the logger.
 *
 * @return True if successful, False otherwise.
 */
  static bool terminateLogging(){
    return Snpe_Util_TerminateLogging();
  }
};

} // ns SNPE

ALIAS_IN_ZDL_NAMESPACE(SNPE, SNPEFactory)
