//=============================================================================
//
//  Copyright (c) 2023 - 2024 Qualcomm Technologies, Inc.
//  All Rights Reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//=============================================================================
#pragma once
#include "Wrapper.hpp"
#include <string>
#include <cstdint>

#include "Options.hpp"
#include "DlSystem/String.hpp"

#include "DiagLog/IDiagLog.h"


namespace DiagLog{
class IDiagLog :
public Wrapper<IDiagLog, Snpe_IDiagLog_Handle_t>
{
  friend BaseType;
  // Use this to get free move Ctor and move assignment operator, provided this class does not specify
  // as copy assignment operator or copy Ctor
  using BaseType::BaseType;

  static Snpe_ErrorCode_t InvalidDeleteCall(Snpe_IDiagLog_Handle_t ){
    return SNPE_ERRORCODE_CAPI_DELETE_FAILURE;
  }

  static constexpr DeleteFunctionType DeleteFunction{InvalidDeleteCall};
  class OptionsInternal :
  public Wrapper<OptionsInternal, Snpe_Options_Handle_t>
  {
    friend BaseType;
    // Use this to get free move Ctor and move assignment operator, provided this class does not specify
    // as copy assignment operator or copy Ctor
    using BaseType::BaseType;

    static constexpr DeleteFunctionType DeleteFunction{Snpe_Options_Delete};
  public:
    OptionsInternal()
      : BaseType(Snpe_Options_Create())
    {  }

    explicit OptionsInternal(const Options& options)
      : BaseType(Snpe_Options_Create())
    {
      setDiagLogMask(options.DiagLogMask.c_str());
      setLogFileDirectory(options.LogFileDirectory.c_str());
      setLogFileName(options.LogFileName.c_str());
      setLogFileRotateCount(options.LogFileRotateCount);
      setLogFileReplace(options.LogFileReplace);
    }

/**
 * @brief Gets DiagLogMask
 * diagLogMask: Enables diag logging only on the specified area mask
 *
 * @return diagLogMask as a const char*
 */

    const char* getDiagLogMask() const{
      return Snpe_Options_GetDiagLogMask(handle());
    }

/**
 * @brief Sets DiagLogMask
 * diagLogMask: Enables diag logging only on the specified area mask
 *
 * @param[in] handle : Handle to access Options object
 * @param[in] diagLogMask : specific area where logging needs to be enabed
 */
    void  setDiagLogMask(const char* diagLogMask){
      Snpe_Options_SetDiagLogMask(handle(), diagLogMask);
    }

/**
 * @brief Gets logFileDirectory
 * logFileDirectory: The path to the directory where log files will be written.
 * The path may be relative or absolute. Relative paths are interpreted
 *
 * @return logFileDirectory as a const char*
 */
    const char* getLogFileDirectory() const{
      return Snpe_Options_GetLogFileDirectory(handle());
    }

/**
 * @brief Sets logFileDirectory
 * logFileDirectory: The path to the directory where log files will be written.
 * The path may be relative or absolute. Relative paths are interpreted
 *
 * @param[in] logFileDirectory : path for saving the log files
 */    void  setLogFileDirectory(const char* logFileDirectory){
      Snpe_Options_SetLogFileDirectory(handle(), logFileDirectory);
    }

/**
 * @brief Gets logFileName
 * logFileName: The name used for log files. If this value is empty then BaseName will be
 * used as the default file name.
 *
 * @return logFileName as a const char*
 */
    const char* getLogFileName() const{
      return Snpe_Options_GetLogFileName(handle());
    }

/**
 * @brief Sets logFileName
 * logFileName: The name used for log files. If this value is empty then BaseName will be
 * used as the default file name.
 *
 * @param[in] logFileName : name of log file
 */
    void setLogFileName(const char* logFileName){
      Snpe_Options_SetLogFileName(handle(), logFileName);
    }

/**
 * @brief Gets the maximum number of log files to create. If set to 0 no log rotation
 * will be used and the log file name specified will be used each time, overwriting
 * any existing log file that may exist.
 *
 * @return max log files to create
 */
    uint32_t getLogFileRotateCount() const{
      return Snpe_Options_GetLogFileRotateCount(handle());
    }

/**
 * @brief Sets the maximum number of log files to create. If set to 0 no log rotation
 * will be used and the log file name specified will be used each time, overwriting
 * any existing log file that may exist.
 *
 * @param[in] logFileRotateCount : max log files to create
 */
    void setLogFileRotateCount(uint32_t logFileRotateCount){
      Snpe_Options_SetLogFileRotateCount(handle(), logFileRotateCount);
    }

/**
 * @brief If the log file already exists, control whether it will be replaced
 *
 * @return 1 if log file will be replaced, 0 otherwise
 */
    bool getLogFileReplace() const{
      return Snpe_Options_GetLogFileReplace(handle());
    }

/**
 * @brief If the log file already exists, control whether it will be replaced
 *
 * @param[in] logFileReplace : 1 if log file to be replaced, 0 otherwise
 */
    void setLogFileReplace(bool logFileReplace){
      Snpe_Options_SetLogFileReplace(handle(), logFileReplace);
    }

    explicit operator Options() const{
      return {
        getDiagLogMask(),
        getLogFileDirectory(),
        getLogFileName(),
        getLogFileRotateCount(),
        getLogFileReplace()
      };
    }

  };



public:
/**
 * @brief Sets the options after initialization occurs.
 *
 * @param[in] loggingOptions : The options to set up diagnostic logging.
 *
 * @return Error code if the options could not be set. Ensure logging is not started/
 *         SNPE_SUCCESS otherwise
 */
  bool setOptions(const Options& loggingOptions){
    OptionsInternal optionsInternal(loggingOptions);
    return SNPE_SUCCESS == Snpe_IDiagLog_SetOptions(handle(), getHandle(optionsInternal));
  }

/**
 * @brief Gets the curent options for the diag logger.
 *
 * @return Handle to access DiagLog options.
 */
  Options getOptions() const{
    OptionsInternal optionsInternal(moveHandle(Snpe_IDiagLog_GetOptions(handle())));
    return Options(optionsInternal);
  }

/**
 * @brief Allows for setting the log mask once diag logging has started
 *
 * @param[in] mask : DiagLogMask
 *
 * @return SNPE_SUCCESS if the level was set successfully.
 */
  bool setDiagLogMask(const std::string& mask){
    return SNPE_SUCCESS == Snpe_IDiagLog_SetDiagLogMask(handle(), mask.c_str());
  }

/**
 * @brief Allows for setting the log mask once diag logging has started
 *
 * @param[in] mask : DiagLogMask
 *
 * @return SNPE_SUCCESS if the level was set successfully.
 */
  bool setDiagLogMask(const DlSystem::String& mask){
    return setDiagLogMask(static_cast<const std::string&>(mask));
  }

/**
 * @brief Enables logging. Logging should be started prior to the instantiation of
 * other SNPE_APIs to ensure all events are captured.
 *
 * @return SNPE_SUCCESS if diagnostic logging started successfully.
 */
  bool start(void){
    return SNPE_SUCCESS == Snpe_IDiagLog_Start(handle());
  }

/**
 * @brief Disables logging.
 *
 * @return SNPE_SUCCESS if logging stopped successfully. Error code otherwise.
 */
  bool stop(void){
    return SNPE_SUCCESS == Snpe_IDiagLog_Stop(handle());
  }

};

} // ns DiagLog

ALIAS_IN_ZDL_NAMESPACE(DiagLog, IDiagLog)