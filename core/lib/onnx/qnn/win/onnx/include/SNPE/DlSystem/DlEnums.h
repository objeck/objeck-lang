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

#ifndef _DL_ENUMS_H_
#define _DL_ENUMS_H_

#include "DlSystem/SnpeApiExportDefine.h"

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdint.h>
#endif


/**
 * Enumeration of supported target runtimes.
 */
typedef enum
{
  /// Special value indicating the property is unset.
  SNPE_RUNTIME_UNSET = -1,

  /// Run the processing on Snapdragon CPU.
  /// Data: float 32bit
  /// Math: float 32bit
  SNPE_RUNTIME_CPU_FLOAT32  = 0,

  /// Default legacy enum to retain backward compatibility.
  /// CPU = CPU_FLOAT32
  SNPE_RUNTIME_CPU = SNPE_RUNTIME_CPU_FLOAT32,

  /// Run the processing on the Adreno GPU.
  /// Data: float 16bit
  /// Math: float 32bit
  SNPE_RUNTIME_GPU_FLOAT32_16_HYBRID = 1,

  /// Default legacy enum to retain backward compatibility.
  /// GPU = GPU_FLOAT32_16_HYBRID
  SNPE_RUNTIME_GPU = SNPE_RUNTIME_GPU_FLOAT32_16_HYBRID,

  /// Run the processing on the Hexagon DSP.
  /// Data: 8bit fixed point Tensorflow style format
  /// Math: 8bit fixed point Tensorflow style format
  SNPE_RUNTIME_DSP_FIXED8_TF = 2,

  /// Default legacy enum to retain backward compatibility.
  /// DSP = DSP_FIXED8_TF
  SNPE_RUNTIME_DSP = SNPE_RUNTIME_DSP_FIXED8_TF,

  /// Run the processing on the Adreno GPU.
  /// Data: float 16bit
  /// Math: float 16bit
  SNPE_RUNTIME_GPU_FLOAT16 = 3,

  /// Run the processing on Snapdragon AIX+HVX.
  /// Data: 8bit fixed point Tensorflow style format
  /// Math: 8bit fixed point Tensorflow style format
  SNPE_RUNTIME_AIP_FIXED8_TF = 5,

  SNPE_RUNTIME_AIP_FIXED_TF = SNPE_RUNTIME_AIP_FIXED8_TF

} Snpe_Runtime_t;

/**
 * Enumeration of runtime available check options.
 */
typedef enum
{
  /// Perform standard runtime available check
  SNPE_RUNTIME_CHECK_OPTION_DEFAULT = 2,
  /// Perform standard runtime available check
  SNPE_RUNTIME_CHECK_OPTION_NORMAL_CHECK = 0,
  /// Perform basic runtime available check, may be runtime specific
  SNPE_RUNTIME_CHECK_OPTION_BASIC_CHECK = 1,
  /// Perform unsignedPD runtime available check
  SNPE_RUNTIME_CHECK_OPTION_UNSIGNEDPD_CHECK = 2,

} Snpe_RuntimeCheckOption_t;

/**
 * Enumeration of various performance profiles that can be requested.
 */
typedef enum
{
  /// Run in a standard mode.
  /// This mode will be deprecated in the future and replaced with BALANCED.
  SNPE_PERFORMANCE_PROFILE_DEFAULT = 0,

  /// Run in a balanced mode.
  SNPE_PERFORMANCE_PROFILE_BALANCED = 0,

  /// Run in high performance mode
  SNPE_PERFORMANCE_PROFILE_HIGH_PERFORMANCE = 1,

  /// Run in a power sensitive mode, at the expense of performance.
  SNPE_PERFORMANCE_PROFILE_POWER_SAVER = 2,

  /// Use system settings.  SNPE makes no calls to any performance related APIs.
  SNPE_PERFORMANCE_PROFILE_SYSTEM_SETTINGS = 3,

  /// Run in sustained high performance mode
  SNPE_PERFORMANCE_PROFILE_SUSTAINED_HIGH_PERFORMANCE = 4,

  /// Run in burst mode
  SNPE_PERFORMANCE_PROFILE_BURST = 5,

  /// Run in lower clock than POWER_SAVER, at the expense of performance.
  SNPE_PERFORMANCE_PROFILE_LOW_POWER_SAVER = 6,

  /// Run in higher clock and provides better performance than POWER_SAVER.
  SNPE_PERFORMANCE_PROFILE_HIGH_POWER_SAVER = 7,

  /// Run in lower balanced mode
  SNPE_PERFORMANCE_PROFILE_LOW_BALANCED = 8,

  /// Run in lowest clock at the expense of performance
  SNPE_PERFORMANCE_PROFILE_EXTREME_POWER_SAVER = 9,

} Snpe_PerformanceProfile_t;

typedef enum {

  SNPE_DCVS_EXP_VCORNER_DISABLE    = 0,

  SNPE_DCVS_EXP_VCORNER_MIN        = 0x100,

  SNPE_DCVS_EXP_VCORNER_LOW_SVS_D2 = 0x134,

  SNPE_DCVS_EXP_VCORNER_LOW_SVS_D1 = 0x138,

  SNPE_DCVS_EXP_VCORNER_LOW_SVS    = 0x140,

  SNPE_DCVS_EXP_VCORNER_SVS        = 0x180,

  SNPE_DCVS_EXP_VCORNER_SVS_L1     = 0x1C0,

  SNPE_DCVS_EXP_VCORNER_NOM        = 0x200,

  SNPE_DCVS_EXP_VCORNER_NOM_L1     = 0x240,

  SNPE_DCVS_EXP_VCORNER_TUR        = 0x280,

  SNPE_DCVS_EXP_VCORNER_TUR_L1     = 0x2A0,

  SNPE_DCVS_EXP_VCORNER_TUR_L2     = 0x2B0,

  SNPE_DCVS_EXP_VCORNER_TUR_L3     = 0x2C0,

  SNPE_DCVS_EXP_VCORNER_MAX        = 0xFFFF,

} Snpe_DspHmx_ExpVoltageCorner_t;


typedef enum{

 SNPE_HMX_CLK_PERF_HIGH=0,

 SNPE_HMX_CLK_PERF_LOW=1

} Snpe_DspHmx_ClkPerfMode_t;




/**
 * Enumeration of various Perf Voltage Corner that can be requested.
 */
typedef enum
{
  /// Maps to HAP_DCVS_VCORNER_DISABLE.
  /// Disable setting up voltage corner
  SNPE_DCVS_VOLTAGE_CORNER_DISABLE = 0x10,

  /// Maps to HAP_DCVS_VCORNER_SVS2.
  /// Set voltage corner to minimum value supported on platform
  SNPE_DCVS_VOLTAGE_VCORNER_MIN_VOLTAGE_CORNER = 0x20,

  /// Maps to HAP_DCVS_VCORNER_SVS2.
  /// Set voltage corner to SVS2 value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_SVS2 = 0x30,

  /// Maps to HAP_DCVS_VCORNER_SVS.
  /// Set voltage corner to SVS value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_SVS = 0x40,

  /// Maps to HAP_DCVS_VCORNER_SVS_PLUS.
  /// Set voltage corner to SVS_PLUS value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_SVS_PLUS = 0x50,

  /// Maps to HAP_DCVS_VCORNER_NOM.
  /// Set voltage corner to NOMINAL value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_NOM = 0x60,

  /// Maps to HAP_DCVS_VCORNER_NOM_PLUS.
  /// Set voltage corner to NOMINAL_PLUS value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_NOM_PLUS = 0x70,

  /// Maps to HAP_DCVS_VCORNER_TURBO.
  /// Set voltage corner to TURBO value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_TURBO = 0x80,

  /// Maps to HAP_DCVS_VCORNER_TURBO_PLUS.
  /// Set voltage corner to TURBO_PLUS value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_TURBO_PLUS = 0x90,

  //currently the Turbol1 is mapped to Turbo l2 in soc definition
  SNPE_DCVS_VOLTAGE_VCORNER_TURBO_L1 = 0x92,

  /// Set voltage corner to TURBO_L2 value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_TURBO_L2 = SNPE_DCVS_VOLTAGE_VCORNER_TURBO_L1,

  /// Set voltage corner to TURBO_L3 value for the platform
  SNPE_DCVS_VOLTAGE_VCORNER_TURBO_L3 = 0x93,

  /// Maps to HAP_DCVS_VCORNER_MAX.
  /// Set voltage corner to maximum value supported on the platform
  SNPE_DCVS_VOLTAGE_VCORNER_MAX_VOLTAGE_CORNER = 0xA0,

  /// UNKNOWN value that must not be used by client
  SNPE_DCVS_VOLTAGE_VCORNER_UNKNOWN = 0x7fffffff

} Snpe_DspPerf_VoltageCorner_t;

/**
 * Enumeration of various PowerMode that can be requested.
 */
typedef enum
{
  /// Maps to HAP_DCVS_V2_ADJUST_UP_DOWN.
  /// Allows for DCVS to adjust up and down
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_ADJUST_UP_DOWN = 0x1,

  /// Maps to HAP_DCVS_V2_ADJUST_ONLY_UP.
  /// Allows for DCVS to adjust up only
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_ADJUST_ONLY_UP = 0x2,

  /// Maps to HAP_DCVS_V2_POWER_SAVER_MODE.
  /// Higher thresholds for power efficiency
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_POWER_SAVER_MODE = 0x4,

  /// Maps to HAP_DCVS_V2_POWER_SAVER_AGGRESSIVE_MODE.
  /// Higher thresholds for power efficiency with faster ramp down
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_POWER_SAVER_AGGRESSIVE_MODE = 0x8,

  /// Maps to HAP_DCVS_V2_PERFORMANCE_MODE.
  /// Lower thresholds for maximum performance
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_PERFORMANCE_MODE = 0x10,

  /// Maps to HAP_DCVS_V2_DUTY_CYCLE_MODE.
  /// The below value applies only for HVX clients:
  /// - For streaming class clients:
  /// - detects periodicity based on HVX usage
  /// - lowers clocks in the no HVX activity region of each period.
  /// - For compute class clients:
  /// - Lowers clocks on no HVX activity detects and brings clocks up on detecting HVX activity
  ///   again.
  /// - Latency involved in bringing up the clock will be at max 1 to 2 ms.
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_DUTY_CYCLE_MODE = 0x20,

  /// UNKNOWN value that must not be used by client
  SNPE_DSP_PERF_INFRASTRUCTURE_POWERMODE_UNKNOWN = 0x7fffffff

} Snpe_DspPerf_PowerMode_t;

/**
 * Enumeration of various profilngLevels that can be requested.
 */
typedef enum
{
  /// No profiling.
  /// Collects no runtime stats in the DiagLog
  SNPE_PROFILING_LEVEL_OFF = 0,

  /// Basic profiling
  /// Collects some runtime stats in the DiagLog
  SNPE_PROFILING_LEVEL_BASIC = 1,

  /// Detailed profiling
  /// Collects more runtime stats in the DiagLog, including per-layer statistics
  /// Performance may be impacted
  SNPE_PROFILING_LEVEL_DETAILED = 2,

  /// Moderate profiling
  /// Collects more runtime stats in the DiagLog, no per-layer statistics
  SNPE_PROFILING_LEVEL_MODERATE = 3,

  /// Linting profiling
  /// HTP exclusive profiling level that collects in-depth performance metrics
  /// for each op in the graph including main thread execution time and time spent
  /// on parallel background ops
  SNPE_PROFILING_LEVEL_LINTING = 4

} Snpe_ProfilingLevel_t;

/**
 * Enumeration of various execution priority hints.
 */
typedef enum
{
  /// Normal priority
  SNPE_EXECUTION_PRIORITY_NORMAL = 0,

  /// Higher than normal priority
  /// SNPE_EXECUTION_PRIORITY_HIGH usage may be restricted and would
  /// silently be treated as SNPE_EXECUTION_PRIORITY_NORMAL
  SNPE_EXECUTION_PRIORITY_HIGH = 1,

  /// Lower priority
  SNPE_EXECUTION_PRIORITY_LOW = 2,

  /// Between Normal and High priority
  /// SNPE_EXECUTION_PRIORITY_NORMAL_HIGH usage may be restricted and would
  /// silently be treated as SNPE_EXECUTION_PRIORITY_NORMAL
  SNPE_EXECUTION_PRIORITY_NORMAL_HIGH = 3

} Snpe_ExecutionPriorityHint_t;

/**
 * Enumeration that lists the supported image encoding formats.
 */
typedef enum
{
  /// For unknown image type. Also used as a default value for ImageEncoding_t.
  SNPE_IMAGE_ENCODING_UNKNOWN = 0,

  /// The RGB format consists of 3 bytes per pixel: one byte for
  /// Red, one for Green, and one for Blue. The byte ordering is
  /// endian independent and is always in RGB byte order.
  SNPE_IMAGE_ENCODING_RGB = 1,

  /// The ARGB32 format consists of 4 bytes per pixel: one byte for
  /// Red, one for Green, one for Blue, and one for the alpha channel.
  /// The alpha channel is ignored. The byte ordering depends on the
  /// underlying CPU. For little endian CPUs, the byte order is BGRA.
  /// For big endian CPUs, the byte order is ARGB.
  SNPE_IMAGE_ENCODING_ARGB32 = 2,

  /// The RGBA format consists of 4 bytes per pixel: one byte for
  /// Red, one for Green, one for Blue, and one for the alpha channel.
  /// The alpha channel is ignored. The byte ordering is endian independent
  /// and is always in RGBA byte order.
  SNPE_IMAGE_ENCODING_RGBA = 3,

  /// The GRAYSCALE format is for 8-bit grayscale.
  SNPE_IMAGE_ENCODING_GRAYSCALE = 4,

  /// NV21 is the Android version of YUV. The Chrominance is down
  /// sampled and has a subsampling ratio of 4:2:0. Note that this
  /// image format has 3 channels, but the U and V channels
  /// are subsampled. For every four Y pixels there is one U and one V pixel.
  SNPE_IMAGE_ENCODING_NV21 = 5,

  /// The BGR format consists of 3 bytes per pixel: one byte for
  /// Red, one for Green and one for Blue. The byte ordering is
  /// endian independent and is always BGR byte order.
  SNPE_IMAGE_ENCODING_BGR = 6

} Snpe_ImageEncoding_t;

/**
 * Enumeration that lists the supported LogLevels that can be set by users.
 */
typedef enum
{
  /// Enumeration variable to be used by user to set logging level to FATAL.
  SNPE_LOG_LEVEL_FATAL = 0,

  /// Enumeration variable to be used by user to set logging level to ERROR.
  SNPE_LOG_LEVEL_ERROR = 1,

  /// Enumeration variable to be used by user to set logging level to WARN.
  SNPE_LOG_LEVEL_WARN = 2,

  /// Enumeration variable to be used by user to set logging level to INFO.
  SNPE_LOG_LEVEL_INFO = 3,

  /// Enumeration variable to be used by user to set logging level to VERBOSE.
  SNPE_LOG_LEVEL_VERBOSE = 4

} Snpe_LogLevel_t;

/**
 * Enumeration that list the supported data types for buffers
 */
typedef enum
{
  /// Unspecified
  SNPE_IO_BUFFER_DATATYPE_UNSPECIFIED = 0,

  /// 32-bit floating point
  SNPE_IO_BUFFER_DATATYPE_FLOATING_POINT_32 = 1,

  /// 16-bit floating point
  SNPE_IO_BUFFER_DATATYPE_FLOATING_POINT_16 = 2,

  /// 8-bit fixed point
  SNPE_IO_BUFFER_DATATYPE_FIXED_POINT_8 =  3,

  /// 16-bit fixed point
  SNPE_IO_BUFFER_DATATYPE_FIXED_POINT_16 = 4

} Snpe_IOBufferDataType_t;

/**
 * Enumeration that list the mode of checking compatibility
 */
typedef enum {
  /// A binary cache is compatible if it could run on the device. This is the
  /// default.
  SNPE_CACHE_COMPATIBILITY_PERMISSIVE = 0,

  /// A binary cache is compatible if it could run on the device and fully
  /// utilize hardware capability.
  SNPE_CACHE_COMPATIBILITY_STRICT = 1,

  /// A binary cache is always incompatible. SNPE will generate new cache.
  SNPE_CACHE_COMPATIBILITY_ALWAYS_GENERATE_NEW_CACHE = 2

} Snpe_CacheCompatibility_t;

typedef bool Snpe_DspPerf_DcvsEnable_t;

typedef uint32_t Snpe_DspPerf_SleepLatency_t;

typedef uint32_t Snpe_DspPerf_SleepDisable_t;

typedef uint32_t Snpe_DspPerf_RpcPollingTime_t;

typedef uint32_t Snpe_DspPerf_HysteresisTime_t;

typedef bool Snpe_DspPerf_AsyncVoteEnable_t;

typedef bool Snpe_HighPerformanceModeEnabled_t;

typedef bool Snpe_FastInitModeEnabled_t;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // _DL_ENUMS_H_
