#pragma once

//
// Session options and the GPU providers' option sets.
//
// The config map a program hands a Session is one flat String-to-String map. Before this
// file, every key but "ep" went to the execution provider as one of its options, and a
// session had no way to say how many threads the CPU side may use or how aggressively the
// graph is optimized. Now the keys fall into four groups:
//
//   ep                              which provider (consumed by onnx.cpp)
//   intra_op_threads, inter_op_threads, graph_optimization, execution_mode,
//   mem_pattern, cpu_mem_arena      options of the SESSION, whichever provider runs it
//   session_config.<key>            a raw ONNX Runtime session config entry, <key> verbatim
//                                   (session_config.session.disable_prepacking, ...)
//   everything else                 an option of the provider chosen by "ep"
//
// A session option or a raw entry is checked HERE and refused with a message when its
// value is not one it understands, exactly as a provider ONNX Runtime does not know is
// refused; a provider option ONNX Runtime does not know is refused by ONNX Runtime
// ("Unknown provider option"), never dropped.
//
// ep=tensorrt is described in append_tensorrt below.
//
// Plain C++11; compiles and behaves the same with no CUDA or TensorRT present.
//

#include <unordered_map>
#include <string>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <memory>

struct SessionTuning {
   bool has_intra;
   int intra;
   bool has_inter;
   int inter;
   int graph_optimization;     // -1 unset, 0 disable, 1 basic, 2 extended, 3 all
   int execution_mode;         // -1 unset, 0 sequential, 1 parallel
   int mem_pattern;            // -1 unset, 0 off, 1 on
   int cpu_mem_arena;          // -1 unset, 0 off, 1 on
   std::vector<std::pair<std::string, std::string> > config_entries;

   SessionTuning() : has_intra(false), intra(0), has_inter(false), inter(0), graph_optimization(-1),
                     execution_mode(-1), mem_pattern(-1), cpu_mem_arena(-1) {}
};

static std::string fold_key(const std::string& key) {
   std::string folded = key;
   for(size_t i = 0; i < folded.size(); ++i) {
      folded[i] = (char)tolower((unsigned char)folded[i]);
   }
   return folded;
}

// "0", "1", "true", "false", "on", "off", "yes", "no"
static bool parse_flag(const std::string& value, int& out) {
   const std::string v = fold_key(value);
   if(v == "1" || v == "true" || v == "on" || v == "yes") {
      out = 1;
      return true;
   }
   if(v == "0" || v == "false" || v == "off" || v == "no") {
      out = 0;
      return true;
   }
   return false;
}

static bool parse_count(const std::string& value, int& out) {
   if(value.empty() || value.size() > 6) {
      return false;
   }
   int total = 0;
   for(size_t i = 0; i < value.size(); ++i) {
      if(value[i] < '0' || value[i] > '9') {
         return false;
      }
      total = total * 10 + (value[i] - '0');
   }
   out = total;
   return true;
}

static void refuse_option(const std::string& key, const std::string& value, const char* wanted) {
   std::wcerr << L">>> ONNX: the session option '" << BytesToUnicode(key) << L"' cannot be '"
              << BytesToUnicode(value) << L"': " << BytesToUnicode(wanted) << L". No session was created. <<<" << std::endl;
}

// Moves the session-level keys out of the option map into tuning. False, after saying why,
// when one of them has a value it does not understand: the session is then refused rather
// than opened with an option quietly dropped.
static bool extract_session_options(std::unordered_map<std::string, std::string>& options, SessionTuning& tuning) {
   static const char* const PREFIX = "session_config.";
   const size_t prefix_size = std::string(PREFIX).size();

   std::vector<std::string> consumed;
   for(const auto& option : options) {
      const std::string& key = option.first;
      const std::string& value = option.second;
      const std::string folded = fold_key(key);

      if(key.compare(0, prefix_size, PREFIX) == 0) {
         if(key.size() == prefix_size) {
            refuse_option(key, value, "name the ONNX Runtime key after session_config., for example session_config.session.disable_prepacking");
            return false;
         }
         tuning.config_entries.push_back(std::make_pair(key.substr(prefix_size), value));
         consumed.push_back(key);
      }
      else if(folded == "intra_op_threads") {
         if(!parse_count(value, tuning.intra)) {
            refuse_option(key, value, "a whole number of threads, 0 for ONNX Runtime's own choice");
            return false;
         }
         tuning.has_intra = true;
         consumed.push_back(key);
      }
      else if(folded == "inter_op_threads") {
         if(!parse_count(value, tuning.inter)) {
            refuse_option(key, value, "a whole number of threads, 0 for ONNX Runtime's own choice");
            return false;
         }
         tuning.has_inter = true;
         consumed.push_back(key);
      }
      else if(folded == "graph_optimization") {
         const std::string v = fold_key(value);
         if(v == "disable" || v == "none" || v == "off") {
            tuning.graph_optimization = 0;
         }
         else if(v == "basic") {
            tuning.graph_optimization = 1;
         }
         else if(v == "extended") {
            tuning.graph_optimization = 2;
         }
         else if(v == "all" || v == "on") {
            tuning.graph_optimization = 3;
         }
         else {
            refuse_option(key, value, "one of disable, basic, extended, all");
            return false;
         }
         consumed.push_back(key);
      }
      else if(folded == "execution_mode") {
         const std::string v = fold_key(value);
         if(v == "sequential") {
            tuning.execution_mode = 0;
         }
         else if(v == "parallel") {
            tuning.execution_mode = 1;
         }
         else {
            refuse_option(key, value, "sequential or parallel");
            return false;
         }
         consumed.push_back(key);
      }
      else if(folded == "mem_pattern") {
         if(!parse_flag(value, tuning.mem_pattern)) {
            refuse_option(key, value, "0 or 1");
            return false;
         }
         consumed.push_back(key);
      }
      else if(folded == "cpu_mem_arena") {
         if(!parse_flag(value, tuning.cpu_mem_arena)) {
            refuse_option(key, value, "0 or 1");
            return false;
         }
         consumed.push_back(key);
      }
   }

   for(size_t i = 0; i < consumed.size(); ++i) {
      options.erase(consumed[i]);
   }
   return true;
}

// Applied AFTER the provider's own defaults, so what a program asked for wins.
static void apply_session_options(Ort::SessionOptions& session_options, const SessionTuning& tuning) {
   if(tuning.has_intra) {
      session_options.SetIntraOpNumThreads(tuning.intra);
   }
   if(tuning.has_inter) {
      session_options.SetInterOpNumThreads(tuning.inter);
   }
   if(tuning.graph_optimization == 0) {
      session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
   }
   else if(tuning.graph_optimization == 1) {
      session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
   }
   else if(tuning.graph_optimization == 2) {
      session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
   }
   else if(tuning.graph_optimization == 3) {
      session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
   }
   if(tuning.execution_mode == 0) {
      session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
   }
   else if(tuning.execution_mode == 1) {
      session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
   }
   if(tuning.mem_pattern == 1) {
      session_options.EnableMemPattern();
   }
   else if(tuning.mem_pattern == 0) {
      session_options.DisableMemPattern();
   }
   if(tuning.cpu_mem_arena == 1) {
      session_options.EnableCpuMemArena();
   }
   else if(tuning.cpu_mem_arena == 0) {
      session_options.DisableCpuMemArena();
   }
   for(size_t i = 0; i < tuning.config_entries.size(); ++i) {
      session_options.AddConfigEntry(tuning.config_entries[i].first.c_str(), tuning.config_entries[i].second.c_str());
   }
}

//
// The CUDA provider, from its option map. The GENERIC string form of AppendExecutionProvider
// does not accept "CUDA", so this is the V2 entry point; its UpdateCUDAProviderOptions
// refuses a key it does not know. Options worth knowing: device_id, gpu_mem_limit (bytes),
// arena_extend_strategy (kNextPowerOfTwo, kSameAsRequested), cudnn_conv_algo_search
// (EXHAUSTIVE, HEURISTIC, DEFAULT), cudnn_conv_use_max_workspace, use_tf32, enable_cuda_graph,
// do_copy_in_default_stream, tunable_op_enable.
//
static void append_cuda(Ort::SessionOptions& session_options, std::unordered_map<std::string, std::string> options) {
   if(options.find("device_id") == options.end()) {
      options["device_id"] = "0";
   }
   const OrtApi& ort_api = Ort::GetApi();
   OrtCUDAProviderOptionsV2* raw_cuda_options = nullptr;
   Ort::ThrowOnError(ort_api.CreateCUDAProviderOptions(&raw_cuda_options));
   std::unique_ptr<OrtCUDAProviderOptionsV2, decltype(ort_api.ReleaseCUDAProviderOptions)>
      cuda_options(raw_cuda_options, ort_api.ReleaseCUDAProviderOptions);
   std::vector<const char*> cuda_keys, cuda_values;
   for(const auto& option : options) {
      cuda_keys.push_back(option.first.c_str());
      cuda_values.push_back(option.second.c_str());
   }
   Ort::ThrowOnError(ort_api.UpdateCUDAProviderOptions(cuda_options.get(), cuda_keys.data(), cuda_values.data(), cuda_keys.size()));
   session_options.AppendExecutionProvider_CUDA_V2(*cuda_options);
}

// A per-user directory for TensorRT engines, created on demand; empty when none can be made.
static std::string tensorrt_cache_dir() {
   std::string base;
#ifdef _WIN32
   char local[1024] = {0};
   size_t local_len = 0;
   if(getenv_s(&local_len, local, sizeof(local), "LOCALAPPDATA") == 0 && local_len > 1) {
      base = std::string(local) + "/objeck-onnx/tensorrt_cache";
   }
#elif defined(__APPLE__)
   const char* home = getenv("HOME");
   if(home && *home) {
      base = std::string(home) + "/Library/Caches/objeck-onnx/tensorrt_cache";
   }
#else
   const char* xdg = getenv("XDG_CACHE_HOME");
   const char* home = getenv("HOME");
   if(xdg && *xdg) {
      base = std::string(xdg) + "/objeck-onnx/tensorrt_cache";
   }
   else if(home && *home) {
      base = std::string(home) + "/.cache/objeck-onnx/tensorrt_cache";
   }
#endif
   if(!base.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(base, ec);
      if(!ec) {
         return base;
      }
   }
   return std::string();
}

//
// ep=tensorrt.
//
// TensorRT compiles the model, or the parts of it it supports, into an engine for this GPU,
// which takes from seconds to minutes the first time and is then reused: so the engine
// cache is ON by default, in a per-user directory, and the timing cache with it. A program
// that does not want it passes engine_cache=0.
//
// Short names for the options a program reaches for, then anything beginning trt_ goes to
// ONNX Runtime unchanged (the full list is in the ONNX Runtime TensorRT provider documentation):
//
//   fp16=1               trt_fp16_enable            half precision kernels where TensorRT can
//   int8=1               trt_int8_enable            8-bit kernels; the model must be QDQ-quantized
//                                                   (or trt_int8_calibration_table_name given)
//   engine_cache=0|1     trt_engine_cache_enable    default 1
//   engine_cache_path=   trt_engine_cache_path      default: a per-user cache directory
//   timing_cache=0|1     trt_timing_cache_enable    default 1 when the engine cache is on
//   workspace_mb=n       trt_max_workspace_size     in megabytes
//   opt_level=0..5       trt_builder_optimization_level
//   cuda_graph=1         trt_cuda_graph_enable      replay the captured kernels; bind tensors with
//                                                   BoundSession and keep their addresses
//   device_id=n          device_id
//
// TensorRT takes the parts of the model it supports; the rest would fall to the CPU. So the
// CUDA provider is appended after it (cuda_fallback=0 to leave it out) and runs what
// TensorRT cannot, on the same GPU; keys beginning cuda. are that provider's options
// (cuda.gpu_mem_limit=...). Only a node neither provider has runs on the CPU.
//
static void append_tensorrt(Ort::SessionOptions& session_options, const std::unordered_map<std::string, std::string>& given,
                            bool cuda_available, bool& used_cuda_fallback, std::string& cache_note) {
   static const struct { const char* alias; const char* name; } ALIASES[] = {
      { "fp16", "trt_fp16_enable" },
      { "int8", "trt_int8_enable" },
      { "engine_cache", "trt_engine_cache_enable" },
      { "engine_cache_path", "trt_engine_cache_path" },
      { "timing_cache", "trt_timing_cache_enable" },
      { "workspace_mb", "trt_max_workspace_size" },
      { "opt_level", "trt_builder_optimization_level" },
      { "cuda_graph", "trt_cuda_graph_enable" }
   };

   std::unordered_map<std::string, std::string> trt;
   std::unordered_map<std::string, std::string> cuda;
   bool want_fallback = true;

   for(const auto& option : given) {
      const std::string key = fold_key(option.first);
      const std::string& value = option.second;

      if(key == "cuda_fallback") {
         int flag = 1;
         if(!parse_flag(value, flag)) {
            throw std::runtime_error("cuda_fallback must be 0 or 1");
         }
         want_fallback = flag != 0;
         continue;
      }
      if(key.compare(0, 5, "cuda.") == 0) {
         cuda[option.first.substr(5)] = value;
         continue;
      }

      std::string name = option.first;
      for(size_t i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); ++i) {
         if(key == ALIASES[i].alias) {
            name = ALIASES[i].name;
            break;
         }
      }
      if(name == "trt_max_workspace_size" && key == "workspace_mb") {
         int megabytes = 0;
         if(!parse_count(value, megabytes)) {
            throw std::runtime_error("workspace_mb must be a whole number of megabytes");
         }
         trt[name] = std::to_string((unsigned long long)megabytes * 1024ULL * 1024ULL);
      }
      else {
         trt[name] = value;
      }
   }

   if(trt.find("device_id") == trt.end()) {
      trt["device_id"] = "0";
   }
   if(cuda.find("device_id") == cuda.end()) {
      cuda["device_id"] = trt["device_id"];
   }

   // the engine cache: on, in a per-user directory, unless the program said otherwise
   int cache_on = 1;
   const auto cache_flag = trt.find("trt_engine_cache_enable");
   if(cache_flag != trt.end() && !parse_flag(cache_flag->second, cache_on)) {
      throw std::runtime_error("engine_cache must be 0 or 1");
   }
   if(cache_flag == trt.end()) {
      trt["trt_engine_cache_enable"] = "1";
   }
   if(cache_on) {
      if(trt.find("trt_engine_cache_path") == trt.end()) {
         // One subdirectory per precision setting: an engine built for FP32 must never be
         // found and reused for fp16=1 or int8=1 (ONNX Runtime keys its files on the model
         // and the GPU, not on those flags).
         std::string dir = tensorrt_cache_dir();
         if(!dir.empty()) {
            std::string precision = "fp16-";
            precision += (trt.find("trt_fp16_enable") != trt.end() && trt["trt_fp16_enable"] == "1") ? "1" : "0";
            precision += "_int8-";
            precision += (trt.find("trt_int8_enable") != trt.end() && trt["trt_int8_enable"] == "1") ? "1" : "0";
            if(trt.find("trt_builder_optimization_level") != trt.end()) {
               precision += "_opt-" + trt["trt_builder_optimization_level"];
            }
            std::error_code ec;
            std::filesystem::create_directories(dir + "/" + precision, ec);
            dir = ec ? std::string() : dir + "/" + precision;
         }
         if(dir.empty()) {
            trt["trt_engine_cache_enable"] = "0";
            cache_note = "no per-user cache directory could be created, so the TensorRT engine cache is off (pass engine_cache_path)";
         }
         else {
            trt["trt_engine_cache_path"] = dir;
         }
      }
      if(trt["trt_engine_cache_enable"] == "1") {
         if(trt.find("trt_timing_cache_enable") == trt.end()) {
            trt["trt_timing_cache_enable"] = "1";
         }
         if(trt.find("trt_timing_cache_path") == trt.end() && trt.find("trt_engine_cache_path") != trt.end()) {
            trt["trt_timing_cache_path"] = trt["trt_engine_cache_path"];
         }
      }
   }

   const OrtApi& ort_api = Ort::GetApi();
   OrtTensorRTProviderOptionsV2* raw_options = nullptr;
   Ort::ThrowOnError(ort_api.CreateTensorRTProviderOptions(&raw_options));
   std::unique_ptr<OrtTensorRTProviderOptionsV2, decltype(ort_api.ReleaseTensorRTProviderOptions)>
      options(raw_options, ort_api.ReleaseTensorRTProviderOptions);
   std::vector<const char*> keys, values;
   for(const auto& option : trt) {
      keys.push_back(option.first.c_str());
      values.push_back(option.second.c_str());
   }
   Ort::ThrowOnError(ort_api.UpdateTensorRTProviderOptions(options.get(), keys.data(), values.data(), keys.size()));
   session_options.AppendExecutionProvider_TensorRT_V2(*options);

   used_cuda_fallback = false;
   if(want_fallback && cuda_available) {
      append_cuda(session_options, cuda);
      used_cuda_fallback = true;
   }
}
