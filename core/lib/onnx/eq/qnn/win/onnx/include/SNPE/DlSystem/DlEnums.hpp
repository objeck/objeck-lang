//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================
#pragma once
#include "Wrapper.hpp"

#include <cstdint>

namespace DlSystem {
/** @addtogroup c_plus_plus_apis C++
@{ */

/**
 * Enumeration of supported target runtimes.
 */
enum class Runtime_t
{
  /// Special value indicating the property is unset.
  UNSET = -1,

  /// Run the processing on Snapdragon CPU.
  /// Data: float 32bit
  /// Math: float 32bit
  CPU_FLOAT32  = 0,

  /// Default legacy enum to retain backward compatibility.
  /// CPU = CPU_FLOAT32
  CPU = CPU_FLOAT32,

  /// Run the processing on the Adreno GPU.
  /// Data: float 16bit
  /// Math: float 32bit
  GPU_FLOAT32_16_HYBRID = 1,

  /// Default legacy enum to retain backward compatibility.
  /// GPU = GPU_FLOAT32_16_HYBRID
  GPU = GPU_FLOAT32_16_HYBRID,

  /// Run the processing on the Hexagon DSP.
  /// Data: 8bit fixed point Tensorflow style format
  /// Math: 8bit fixed point Tensorflow style format
  DSP_FIXED8_TF = 2,

  /// Default legacy enum to retain backward compatibility.
  /// DSP = DSP_FIXED8_TF
  DSP = DSP_FIXED8_TF,

  /// Run the processing on the Adreno GPU.
  /// Data: float 16bit
  /// Math: float 16bit
  GPU_FLOAT16 = 3,

  /// Run the processing on Snapdragon AIX+HVX.
  /// Data: 8bit fixed point Tensorflow style format
  /// Math: 8bit fixed point Tensorflow style format
  AIP_FIXED8_TF = 5,

  AIP_FIXED_TF = AIP_FIXED8_TF,

  /// Any new enums should be added above this line
  NUM_RUNTIME_TARGETS
};

/**
 * Enumeration of runtime available check options.
 */
enum class RuntimeCheckOption_t
{
  /// Perform standard runtime available check
  NORMAL_CHECK = 0,

  /// Perform basic runtime available check, may be runtime specific
  BASIC_CHECK = 1,

  /// Perform unsignedPD runtime available check
  UNSIGNEDPD_CHECK = 2,

  /// Perform standard runtime available check
  DEFAULT = 2,

  /// Any new enums should be added above this line
  NUM_RUNTIMECHECK_OPTIONS
};

/**
 * Enumeration of various performance profiles that can be requested.
 */
enum class PerformanceProfile_t
{
  /// Run in a standard mode.
  /// This mode will be deprecated in the future and replaced with BALANCED.
  DEFAULT = 0,
  /// Run in a balanced mode.
  BALANCED = 0,

  /// Run in high performance mode
  HIGH_PERFORMANCE = 1,

  /// Run in a power sensitive mode, at the expense of performance.
  POWER_SAVER = 2,

  /// Use system settings.  SNPE makes no calls to any performance related APIs.
  SYSTEM_SETTINGS = 3,

  /// Run in sustained high performance mode
  SUSTAINED_HIGH_PERFORMANCE = 4,

  /// Run in burst mode
  BURST = 5,

  /// Run in lower clock than POWER_SAVER, at the expense of performance.
  LOW_POWER_SAVER = 6,

  /// Run in higher clock and provides better performance than POWER_SAVER.
  HIGH_POWER_SAVER = 7,

  /// Run in lower balanced mode
  LOW_BALANCED = 8,

  /// Run in lowest clock at the expense of performance
  EXTREME_POWER_SAVER = 9,

  /// Any new enums should be added above this line
  NUM_PERF_PROFILES
};


enum class  DspPerfVoltageCorner_t
{
  /// Maps to HAP_DCVS_VCORNER_DISABLE.
  /// Disable setting up voltage corner
  DCVS_VOLTAGE_CORNER_DISABLE = 0x10,

  /// Maps to HAP_DCVS_VCORNER_SVS2.
  /// Set voltage corner to minimum value supported on platform
  DCVS_VOLTAGE_VCORNER_MIN_VOLTAGE_CORNER = 0x20,

  /// Maps to HAP_DCVS_VCORNER_SVS2.
  /// Set voltage corner to SVS2 value for the platform
  DCVS_VOLTAGE_VCORNER_SVS2 = 0x30,

  /// Maps to HAP_DCVS_VCORNER_SVS.
  /// Set voltage corner to SVS value for the platform
  DCVS_VOLTAGE_VCORNER_SVS = 0x40,

  /// Maps to HAP_DCVS_VCORNER_SVS_PLUS.
  /// Set voltage corner to SVS_PLUS value for the platform
  DCVS_VOLTAGE_VCORNER_SVS_PLUS = 0x50,

  /// Maps to HAP_DCVS_VCORNER_NOM.
  /// Set voltage corner to NOMINAL value for the platform
  DCVS_VOLTAGE_VCORNER_NOM = 0x60,

  /// Maps to HAP_DCVS_VCORNER_NOM_PLUS.
  /// Set voltage corner to NOMINAL_PLUS value for the platform
  DCVS_VOLTAGE_VCORNER_NOM_PLUS = 0x70,

  /// Maps to HAP_DCVS_VCORNER_TURBO.
  /// Set voltage corner to TURBO value for the platform
  DCVS_VOLTAGE_VCORNER_TURBO = 0x80,

  /// Maps to HAP_DCVS_VCORNER_TURBO_PLUS.
  /// Set voltage corner to TURBO_PLUS value for the platform
  DCVS_VOLTAGE_VCORNER_TURBO_PLUS = 0x90,

  //currently the Turbol1 is mapped to Turbo l2 in soc definition
  DCVS_VOLTAGE_VCORNER_TURBO_L1 = 0x92,
  /// Set voltage corner to TURBO_L2 value for the platform
  DCVS_VOLTAGE_VCORNER_TURBO_L2 = DCVS_VOLTAGE_VCORNER_TURBO_L1,
  /// Set voltage corner to TURBO_L3 value for the platform
  DCVS_VOLTAGE_VCORNER_TURBO_L3 = 0x93,

  /// Maps to HAP_DCVS_VCORNER_MAX.
  /// Set voltage corner to maximum value supported on the platform
  DCVS_VOLTAGE_VCORNER_MAX_VOLTAGE_CORNER = 0xA0,

  /// UNKNOWN value that must not be used by client
  DCVS_VOLTAGE_VCORNER_UNKNOWN = 0x7fffffff
};

enum class DspPerfPowerMode_t
{
  /// Maps to HAP_DCVS_V2_ADJUST_UP_DOWN.
  /// Allows for DCVS to adjust up and down
  DSP_PERF_INFRASTRUCTURE_POWERMODE_ADJUST_UP_DOWN = 0x1,

  /// Maps to HAP_DCVS_V2_ADJUST_ONLY_UP.
  /// Allows for DCVS to adjust up only
  DSP_PERF_INFRASTRUCTURE_POWERMODE_ADJUST_ONLY_UP = 0x2,

  /// Maps to HAP_DCVS_V2_POWER_SAVER_MODE.
  /// Higher thresholds for power efficiency
  DSP_PERF_INFRASTRUCTURE_POWERMODE_POWER_SAVER_MODE = 0x4,

  /// Maps to HAP_DCVS_V2_POWER_SAVER_AGGRESSIVE_MODE.
  /// Higher thresholds for power efficiency with faster ramp down
  DSP_PERF_INFRASTRUCTURE_POWERMODE_POWER_SAVER_AGGRESSIVE_MODE = 0x8,

  /// Maps to HAP_DCVS_V2_PERFORMANCE_MODE.
  /// Lower thresholds for maximum performance
  DSP_PERF_INFRASTRUCTURE_POWERMODE_PERFORMANCE_MODE = 0x10,

  /// Maps to HAP_DCVS_V2_DUTY_CYCLE_MODE.
  /// The below value applies only for HVX clients:
  ///  - For streaming class clients:
  ///    - detects periodicity based on HVX usage
  ///    - lowers clocks in the no HVX activity region of each period.
  ///  - For compute class clients:
  ///    - Lowers clocks on no HVX activity detects and brings clocks up on detecting
  ///      HVX activity again.
  ///    - Latency involved in bringing up the clock will be at max 1 to 2 ms.
  DSP_PERF_INFRASTRUCTURE_POWERMODE_DUTY_CYCLE_MODE = 0x20,

  /// UNKNOWN value that must not be used by client
  DSP_PERF_INFRASTRUCTURE_POWERMODE_UNKNOWN = 0x7fffffff
};

/**
 * Enumeration of various profilngLevels that can be requested.
 */
enum class ProfilingLevel_t
{
  /// No profiling.
  /// Collects no runtime stats in the DiagLog
  OFF = 0,

  /// Basic profiling
  /// Collects some runtime stats in the DiagLog
  BASIC = 1,

  /// Detailed profiling
  /// Collects more runtime stats in the DiagLog, including per-layer statistics
  /// Performance may be impacted
  DETAILED = 2,

  /// Moderate profiling
  /// Collects more runtime stats in the DiagLog, no per-layer statistics
  MODERATE = 3,

  /// Linting profiling
  /// HTP exclusive profiling level that collects in-depth performance metrics
  /// for each op in the graph including main thread execution time and time spent
  /// on parallel background ops
  LINTING = 4,

  /// Any new enums should be added above this line
  NUM_PROFILING_LEVELS
};

/**
 * Enumeration of various execution priority hints.
 */
enum class ExecutionPriorityHint_t
{
  /// Normal priority
  NORMAL = 0,

  /// Higher than normal priority
  /// HIGH usage may be restricted and would silently be treated as NORMAL
  HIGH = 1,

  /// Lower priority
  LOW = 2,

  /// Between Normal and High priority
  /// NORMAL_HIGH usage may be restricted and would silently be treated as NORMAL
  NORMAL_HIGH = 3,

  /// Any new enums should be added above this line
  NUM_EXECUTION_PRIORITY_HINTS
};

/** @} */ /* end_addtogroup c_plus_plus_apis C++*/

/**
 * Enumeration that lists the supported image encoding formats.
 */
enum class ImageEncoding_t
{
  /// For unknown image type. Also used as a default value for ImageEncoding_t.
  UNKNOWN = 0,

  /// The RGB format consists of 3 bytes per pixel: one byte for
  /// Red, one for Green, and one for Blue. The byte ordering is
  /// endian independent and is always in RGB byte order.
  RGB = 1,

  /// The ARGB32 format consists of 4 bytes per pixel: one byte for
  /// Red, one for Green, one for Blue, and one for the alpha channel.
  /// The alpha channel is ignored. The byte ordering depends on the
  /// underlying CPU. For little endian CPUs, the byte order is BGRA.
  /// For big endian CPUs, the byte order is ARGB.
  ARGB32 = 2,

  /// The RGBA format consists of 4 bytes per pixel: one byte for
  /// Red, one for Green, one for Blue, and one for the alpha channel.
  /// The alpha channel is ignored. The byte ordering is endian independent
  /// and is always in RGBA byte order.
  RGBA = 3,

  /// The GRAYSCALE format is for 8-bit grayscale.
  GRAYSCALE = 4,

  /// NV21 is the Android version of YUV. The Chrominance is down
  /// sampled and has a subsampling ratio of 4:2:0. Note that this
  /// image format has 3 channels, but the U and V channels
  /// are subsampled. For every four Y pixels there is one U and one V pixel.
  NV21 = 5,

  /// The BGR format consists of 3 bytes per pixel: one byte for
  /// Red, one for Green and one for Blue. The byte ordering is
  /// endian independent and is always BGR byte order.
  BGR = 6
};

/**
 * Enumeration that lists the supported LogLevels that can be set by users.
 */
enum class LogLevel_t
{
  /// Enumeration variable to be used by user to set logging level to FATAL.
  LOG_FATAL = 0,

  /// Enumeration variable to be used by user to set logging level to ERROR.
  LOG_ERROR = 1,

  /// Enumeration variable to be used by user to set logging level to WARN.
  LOG_WARN = 2,

  /// Enumeration variable to be used by user to set logging level to INFO.
  LOG_INFO = 3,

  /// Enumeration variable to be used by user to set logging level to VERBOSE.
  LOG_VERBOSE = 4,

  /// Any new enums should be added above this line
  NUM_LOG_LEVELS
};

enum class IOBufferDataType_t : int
{
  /// Unspecified
  UNSPECIFIED = 0,

  /// 32-bit floating point
  FLOATING_POINT_32 = 1,

  /// 16-bit floating point
  FLOATING_POINT_16 = 2,

  /// 8-bit fixed point
  FIXED_POINT_8 =  3,

  /// 16-bit fixed point
  FIXED_POINT_16 = 4,

  /// int32
  INT_32 = 5,

  ///unsigned int32
  UINT_32 = 6,

  /// int8
  INT_8 =  7,

  /// uint8
  UINT_8 = 8,

  /// int16
  INT_16 = 9,

  /// uint16
  UINT_16 = 10,

  /// boolean
  BOOL_8 = 11,

  /// int64
  INT_64 = 12,

  /// uint64
  UINT_64 = 13
};

enum class CacheCompatibility_t : int
{
  /// A binary cache is compatible if it could run on the device. This is the
  /// default.
  CACHE_COMPATIBILITY_PERMISSIVE = 0,
  /// A binary cache is compatible if it could run on the device and fully
  /// utilize hardware capability.
  CACHE_COMPATIBILITY_STRICT = 1,
  /// A binary cache is always incompatible. SNPE will generate new cache.
  CACHE_COMPATIBILITY_ALWAYS_GENERATE_NEW_CACHE = 2
};

enum class DspHmx_ExpVoltageCorner_t
{
  DCVS_EXP_VCORNER_DISABLE    = 0,

  DCVS_EXP_VCORNER_MIN        = 0x100,

  DCVS_EXP_VCORNER_LOW_SVS_D2 = 0x134,

  DCVS_EXP_VCORNER_LOW_SVS_D1 = 0x138,

  DCVS_EXP_VCORNER_LOW_SVS    = 0x140,

  DCVS_EXP_VCORNER_SVS        = 0x180,

  DCVS_EXP_VCORNER_SVS_L1     = 0x1C0,

  DCVS_EXP_VCORNER_NOM        = 0x200,

  DCVS_EXP_VCORNER_NOM_L1     = 0x240,

  DCVS_EXP_VCORNER_TUR        = 0x280,

  DCVS_EXP_VCORNER_TUR_L1     = 0x2A0,

  DCVS_EXP_VCORNER_TUR_L2     = 0x2B0,

  DCVS_EXP_VCORNER_TUR_L3     = 0x2C0,

  DCVS_EXP_VCORNER_MAX        = 0xFFFF,

};

enum class DspHmx_ClkPerfMode_t
{
  HMX_CLK_PERF_HIGH=0,

  HMX_CLK_PERF_LOW=1

};

typedef bool DspPerfDcvsEnable_t;

typedef uint32_t DspPerfSleepLatency_t;

typedef uint32_t DspPerfSleepDisable_t;

typedef uint32_t DspPerfRpcPollingTime_t;

typedef uint32_t DspPerfHysteresisTime_t;

typedef bool  DspPerfAsyncVoteEnable_t;

typedef bool HighPerformanceModeEnabled_t;
typedef bool FastInitModeEnabled_t;

} // ns DlSystem


ALIAS_IN_ZDL_NAMESPACE(DlSystem, Runtime_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, RuntimeCheckOption_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, PerformanceProfile_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, ProfilingLevel_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, ExecutionPriorityHint_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, ImageEncoding_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, LogLevel_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, IOBufferDataType_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, CacheCompatibility_t)

ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfVoltageCorner_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfPowerMode_t)

ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfDcvsEnable_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfSleepLatency_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfSleepDisable_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfRpcPollingTime_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfHysteresisTime_t)
ALIAS_IN_ZDL_NAMESPACE(DlSystem, DspPerfAsyncVoteEnable_t)




