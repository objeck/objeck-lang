#include "common.h"
#include "../../../shared/cpu_count.h"

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <unordered_map>
#include <thread>
#include <memory>

// IO binding: tensors that stay in provider memory between runs
#include "device_tensor.h"
#include "float16_entries.h"

// Session options, the CUDA and TensorRT option sets
#include "session_options.h"

//
// Execution provider selection. Every provider this library can request, as the
// short name a program passes in "ep" and as ONNX Runtime spells it in
// Ort::GetAvailableProviders(). One table, so the session code, the provider
// query and the error messages can never disagree about a name.
//
struct EpName {
   const char* name;
   const char* ort_name;
};

static const EpName EP_NAMES[] = {
   { "cpu", "CPUExecutionProvider" },
   { "cuda", "CUDAExecutionProvider" },
   { "tensorrt", "TensorrtExecutionProvider" },
   { "dml", "DmlExecutionProvider" },
   { "qnn", "QNNExecutionProvider" },
   { "vitisai", "VitisAIExecutionProvider" },
   { "coreml", "CoreMLExecutionProvider" }
};

// The provider a build uses when the program names none. Only the default is a
// compile-time choice now; "ep" overrides it per session.
#if defined(ONNX_EP_DML)
static const char* const COMPILED_DEFAULT_EP = "dml";
#elif defined(ONNX_EP_CUDA)
static const char* const COMPILED_DEFAULT_EP = "cuda";
#elif defined(ONNX_EP_QNN)
static const char* const COMPILED_DEFAULT_EP = "qnn";
#elif defined(ONNX_EP_VITIS)
static const char* const COMPILED_DEFAULT_EP = "vitisai";
#elif defined(ONNX_EP_COREML)
static const char* const COMPILED_DEFAULT_EP = "coreml";
#else
static const char* const COMPILED_DEFAULT_EP = "cpu";
#endif

// "CUDA", " cuda ", "directml" and ORT's own "CUDAExecutionProvider" all name a provider.
static std::string normalize_ep(std::string value) {
   const size_t first = value.find_first_not_of(" 	");
   const size_t last = value.find_last_not_of(" 	");
   value = (first == std::string::npos) ? std::string() : value.substr(first, last - first + 1);
   for(size_t i = 0; i < value.size(); ++i) {
      value[i] = (char)tolower((unsigned char)value[i]);
   }

   static const std::string suffix = "executionprovider";
   if(value.size() > suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
      value.erase(value.size() - suffix.size());
   }

   if(value == "directml") {
      return "dml";
   }
   if(value == "vitis") {
      return "vitisai";
   }
   if(value == "trt") {
      return "tensorrt";
   }
   return value;
}

// ORT's name for a short provider name, or nullptr when it is not one we know.
static const char* ort_provider_name(const std::string& ep) {
   for(size_t i = 0; i < sizeof(EP_NAMES) / sizeof(EP_NAMES[0]); ++i) {
      if(ep == EP_NAMES[i].name) {
         return EP_NAMES[i].ort_name;
      }
   }
   return nullptr;
}

static std::string known_ep_names() {
   std::string list;
   for(size_t i = 0; i < sizeof(EP_NAMES) / sizeof(EP_NAMES[0]); ++i) {
      if(!list.empty()) {
         list += ", ";
      }
      list += EP_NAMES[i].name;
   }
   return list;
}

// What the loaded ONNX Runtime reports, comma separated, for messages.
static std::string available_ep_list() {
   std::string list;
   const std::vector<std::string> available = Ort::GetAvailableProviders();
   for(size_t i = 0; i < available.size(); ++i) {
      if(!list.empty()) {
         list += ", ";
      }
      list += available[i];
   }
   return list;
}

// True when the loaded ONNX Runtime was built with this provider. The CPU
// provider is always there.
static bool ep_is_available(const std::string& ep) {
   if(ep == "cpu") {
      return true;
   }
   const char* ort_name = ort_provider_name(ep);
   if(!ort_name) {
      return false;
   }
   const std::vector<std::string> available = Ort::GetAvailableProviders();
   for(size_t i = 0; i < available.size(); ++i) {
      if(available[i] == ort_name) {
         return true;
      }
   }
   return false;
}

// Reads a String[] argument. Unlike APITools_GetStringsValues, which answers a Nil
// element with an EMPTY vector -- indistinguishable from "no options" -- this says so,
// so a caller can refuse instead of quietly dropping every option it was given.
static bool read_string_array(VMContext& context, size_t index, std::vector<std::wstring>& out) {
   out.clear();
   size_t* array = APITools_GetArrayAddress(APITools_GetObjectValue(context, index));
   const size_t size = APITools_GetArraySize(array);
   for(size_t i = 0; i < size; ++i) {
      const wchar_t* value = APITools_GetStringValue(array, i);
      if(!value) {
         out.clear();
         return false;
      }
      out.push_back(value);
   }
   return true;
}

// Per-user cache directory for QNN compiled contexts. It used to be "./qnn_cache",
// i.e. relative to whatever the working directory happened to be, which scatters
// caches and fails in read-only directories. Falls back to the old relative path
// only if no per-user location can be found or created.
static std::string qnn_cache_dir() {
   std::string base;
#ifdef _WIN32
   // getenv_s: the Windows ARM64 project does not suppress C4996, an error in this build
   char local[1024] = {0};
   size_t local_len = 0;
   if(getenv_s(&local_len, local, sizeof(local), "LOCALAPPDATA") == 0 && local_len > 1) {
      base = std::string(local) + "/objeck-onnx/qnn_cache";
   }
#elif defined(__APPLE__)
   const char* home = getenv("HOME");
   if(home && *home) {
      base = std::string(home) + "/Library/Caches/objeck-onnx/qnn_cache";
   }
#else
   const char* xdg = getenv("XDG_CACHE_HOME");
   const char* home = getenv("HOME");
   if(xdg && *xdg) {
      base = std::string(xdg) + "/objeck-onnx/qnn_cache";
   }
   else if(home && *home) {
      base = std::string(home) + "/.cache/objeck-onnx/qnn_cache";
   }
#endif
   if(!base.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(base, ec);
      if(!ec) {
         return base;
      }
   }
   return "./qnn_cache";
}

extern "C" {
   // initialize library
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void load_lib(VMContext& context) {
      cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
      get_env();
   }

   // release library
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void unload_lib() {
      // Deliberately NOT freeing the Ort::Env: sessions created from it can still be
      // alive in the VM (they are owned by Objeck objects), and deleting the Env under
      // them is a use-after-free. It is a one-per-process allocation that the OS
      // reclaims at exit; a later load_lib reuses it via get_env().
   }

   // List available ONNX execution providers
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_get_provider_names(VMContext& context) {
      get_provider_names(context);
   }

   // The provider a session gets when the program names none (a short name:
   // "cpu", "cuda", "dml", "qnn", "vitisai" or "coreml")
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_get_default_provider(VMContext& context) {
      APITools_SetStringValue(context, 0, BytesToUnicode(COMPILED_DEFAULT_EP));
   }

   // Does the loaded ONNX Runtime provide this provider? Takes the short names
   // the "ep" option takes. Slot 0 is set to 1 or 0; an unknown name is 0.
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_has_provider(VMContext& context) {
      const wchar_t* name = APITools_GetStringValue(context, 1);
      if(!name) {
         // a Nil name: no provider, rather than a null pointer into UnicodeToBytes
         APITools_SetIntValue(context, 0, 0);
         return;
      }
      APITools_SetIntValue(context, 0, ep_is_available(normalize_ep(UnicodeToBytes(name))) ? 1 : 0);
   }

   // create an ONNX session with the selected execution provider
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_new_session(VMContext& context) {
      // A Nil key or value, or keys and values of different lengths, is a malformed
      // option set. Dropping it would drop "ep" with it and open the session on the
      // default provider -- the quiet substitution this layer refuses everywhere else.
      std::vector<std::wstring> keys;
      std::vector<std::wstring> values;
      const bool keys_ok = read_string_array(context, 1, keys);
      const bool values_ok = read_string_array(context, 2, values);
      if(!keys_ok || !values_ok || keys.size() != values.size()) {
         std::wcerr << L">>> ONNX: the session options are malformed (a Nil key or value, or keys "
                    << L"and values of different lengths); no session was created. <<<" << std::endl;
         APITools_SetIntValue(context, 0, 0);
         return;
      }

      const wchar_t* model_arg = APITools_GetStringValue(context, 3);
      if(!model_arg) {
         std::wcerr << L">>> ONNX: the model path is Nil; no session was created. <<<" << std::endl;
         APITools_SetIntValue(context, 0, 0);
         return;
      }

#ifdef _WIN32
      const std::wstring model_path = model_arg;
#else
      const std::string model_path = UnicodeToBytes(model_arg);
#endif

      try {
         std::unordered_map<std::string, std::string> provider_options;

         // Pull "ep" out BEFORE building provider_options. It is a selector for
         // this layer, not an ORT provider option -- forwarding it to
         // AppendExecutionProvider makes ORT reject the whole option set, and the
         // catch below then returns without setting slot 0, so the caller gets a
         // silently null session. Shipped demos pass ep=cpu and ep=dml today. The
         // key is matched without regard to case, so "EP" selects too instead of
         // travelling to the provider as an option it would ignore.
         std::string requested_ep;
         for(size_t i = 0; i < keys.size(); ++i) {
            std::string key = UnicodeToBytes(keys[i]);
            std::string value = UnicodeToBytes(values[i]);
            std::string folded_key = key;
            for(size_t c = 0; c < folded_key.size(); ++c) {
               folded_key[c] = (char)tolower((unsigned char)folded_key[c]);
            }
            if(folded_key == "ep") {
               requested_ep = value;
            }
            else {
               provider_options[key] = value;
            }
         }

         // Session-level keys (intra_op_threads, graph_optimization, session_config.*, ...) are
         // options of the session, not of a provider: take them out now so they are neither
         // forwarded to a provider that would refuse them nor counted as a CPU option.
         SessionTuning tuning_options;
         if(!extract_session_options(provider_options, tuning_options)) {
            APITools_SetIntValue(context, 0, 0);
            return;
         }

         // Pick the execution provider. The ONNX_EP_* define this library was
         // compiled with is only the DEFAULT, used when the caller does not name
         // one; "ep" selects at session creation, so one libobjk_onnx can serve
         // cpu, cuda, dml, ... as long as the ONNX Runtime beside it carries the
         // provider. What the runtime really carries is asked of ORT
         // (Ort::GetAvailableProviders), never assumed, and a provider that is not
         // there is refused with the list of what is -- never replaced by CPU.
         const std::string default_ep = COMPILED_DEFAULT_EP;
         const bool explicit_ep = !requested_ep.empty();
         const std::string ep = explicit_ep ? normalize_ep(requested_ep) : default_ep;

         if(ort_provider_name(ep) == nullptr) {
            std::wcerr << L">>> ONNX: unknown execution provider '" << BytesToUnicode(requested_ep)
                       << L"'. Known names: " << BytesToUnicode(known_ep_names())
                       << L". This ONNX Runtime provides: " << BytesToUnicode(available_ep_list())
                       << L". <<<" << std::endl;
            // Write the refusal into the return slot. #1028 fixed the catch below but not this
            // path, which left slot 0 as the caller found it -- reading as failure only
            // because IntRef->New() defaults to 0.
            APITools_SetIntValue(context, 0, 0);
            return;
         }

         // The shipped defaults for qnn, vitisai and coreml are not pre-checked: their
         // behaviour is exactly what it was before "ep" became a runtime choice, and
         // ORT's own error already names them. Everything else is checked here so the
         // message can list what is available.
         const bool is_plain_default = !explicit_ep || ep == default_ep;
         const bool skip_availability_check = is_plain_default &&
            (ep == "qnn" || ep == "vitisai" || ep == "coreml");
         if(!skip_availability_check && !ep_is_available(ep)) {
            std::wcerr << L">>> ONNX: execution provider '" << BytesToUnicode(ep)
                       << L"' was requested, but the ONNX Runtime this library loaded does not provide it. Available: "
                       << BytesToUnicode(available_ep_list())
                       << L". Use ep=cpu, or put an ONNX Runtime that carries '"
                       << BytesToUnicode(ep) << L"' beside libobjk_onnx. <<<" << std::endl;
            APITools_SetIntValue(context, 0, 0);
            return;
         }

         const bool use_cpu = (ep == "cpu");

         // The CPU provider takes no options. Say so rather than let a program
         // believe device_id or a memory limit applied: a wrong provider name is
         // refused, but a setting that is silently dropped looks like success.
         if(use_cpu && !provider_options.empty()) {
            std::wcerr << L">>> ONNX: the cpu provider takes no options; ignoring:";
            for(const auto& option : provider_options) {
               std::wcerr << L" " << BytesToUnicode(option.first);
            }
            std::wcerr << L" <<<" << std::endl;
         }

         Ort::SessionOptions session_options;

         // Append the provider that was asked for.
         if(ep == "dml") {
            if(provider_options.find("device_id") == provider_options.end()) {
               provider_options["device_id"] = "0";
            }
            session_options.AppendExecutionProvider("DML", provider_options);
         }
         else if(ep == "cuda") {
            // The GENERIC string form does not accept "CUDA" (see append_cuda): CUDA needs
            // its own entry point, which takes string options and refuses an unknown one.
            append_cuda(session_options, provider_options);
         }
         else if(ep == "tensorrt") {
            // TensorRT first, then CUDA for what TensorRT cannot take (see append_tensorrt)
            bool used_cuda_fallback = false;
            std::string cache_note;
            append_tensorrt(session_options, provider_options, ep_is_available("cuda"), used_cuda_fallback, cache_note);
            if(!cache_note.empty()) {
               std::wcerr << L">>> ONNX: " << BytesToUnicode(cache_note) << L". <<<" << std::endl;
            }
         }
         else if(ep == "qnn") {
            if(provider_options.find("backend_type") == provider_options.end()) {
               provider_options["backend_type"] = "gpu";
            }
            if(provider_options.find("qnn_context_cache_enable") == provider_options.end()) {
               provider_options["qnn_context_cache_enable"] = "1";
               const std::string qnn_dir = qnn_cache_dir();
               provider_options["qnn_context_cache_path"] = qnn_dir;
               provider_options["profiling_level"] = "off";
               provider_options["ep.context_enable"] = "1";
               provider_options["ep.context_file_path"] = qnn_dir + "/model_ctx.onnx";
               provider_options["ep.context_embed_mode"] = "1";
            }
            session_options.AppendExecutionProvider("QNN", provider_options);
         }
         else if(ep == "vitisai") {
            session_options.AppendExecutionProvider("VitisAI", provider_options);
         }
         // "coreml" is appended further down, where its CPU fallback lives; "cpu" appends nothing.

         // Session tuning follows the provider, except for CPU, which keeps the tuning of
         // the build it runs in -- so a DirectML build asked for ep=cpu still behaves as
         // it always did, and a CPU-only build is untouched.
         const std::string tuning = use_cpu ? default_ep : ep;
         if(tuning == "dml") {
            session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
            session_options.DisableMemPattern();
            session_options.SetIntraOpNumThreads((int)UsableCpuCount());
         }
         else if(tuning == "cuda") {
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
            session_options.DisableMemPattern();
         }
         else if(tuning == "tensorrt") {
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
         }
         else if(tuning == "qnn" || tuning == "vitisai") {
            session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
         }
         else {
            // cpu, coreml
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
         }

         // What the program asked for wins over the defaults chosen above
         apply_session_options(session_options, tuning_options);

         Ort::Env* ort_env = get_env();

#if defined(ONNX_EP_COREML)
         // Persist the compiled CoreML model across runs. Without
         // ModelCacheDirectory the EP compiles the ONNX graph into a temp dir and
         // DELETES it when the session closes, so every process start pays the
         // full CoreML compile (measured: 7.6s for DeepLabV3 vs 67ms cached).
         // Users can override with their own path or pass an empty string to
         // disable. Note (per the ORT header): the cache key hashes the model
         // path — replacing a model file in place requires clearing the cache.
         auto cache_opt = provider_options.find("ModelCacheDirectory");
         if(cache_opt == provider_options.end()) {
            const char* home = getenv("HOME");
            if(home) {
               const std::string cache_dir = std::string(home) + "/Library/Caches/objeck-onnx";
               std::error_code ec;
               std::filesystem::create_directories(cache_dir, ec);
               if(!ec) {
                  provider_options["ModelCacheDirectory"] = cache_dir;
               }
            }
         }
         else if(cache_opt->second.empty()) {
            provider_options.erase(cache_opt);
         }

         // Try CoreML first, fall back to CPU if it fails
         // (CoreML EP has issues with models using external data files).
         // The fallback is the macOS default and is announced; a caller that NAMED
         // ep=coreml asked for CoreML specifically and gets the error instead.
         const Ort::Session* session = nullptr;
         try {
            if(ep == "coreml") session_options.AppendExecutionProvider("CoreML", provider_options);
            session = new Ort::Session(*ort_env, model_path.c_str(), session_options);
         }
         catch(const std::exception& e) {
            if(explicit_ep && ep == "coreml") {
               throw;
            }
            std::wcerr << L"=> CoreML session failed (" << BytesToUnicode(e.what())
                       << L"), falling back to CPU" << std::endl;
            Ort::SessionOptions cpu_options;
            cpu_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            cpu_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
            session = new Ort::Session(*ort_env, model_path.c_str(), cpu_options);
         }
#else
         const Ort::Session* session = new Ort::Session(*ort_env, model_path.c_str(), session_options);
#endif

         // Remember what this session runs on: device tensors and IO bindings need the
         // provider and device, and keep the session alive until they are gone.
         int device_id = 0;
         const auto device_option = provider_options.find("device_id");
         if(device_option != provider_options.end()) {
            device_id = atoi(device_option->second.c_str());
         }
         registry_add(session, ep, device_id);

         APITools_SetIntValue(context, 0, (size_t)session);
      }
      catch(const std::exception& ex) {
         // Write the failure into the return slot rather than leaving it as the
         // caller found it. The slot is only set on the success path above, so a
         // failed session used to return whatever was already there; it worked
         // out as 0 because IntRef->New() defaults to 0, which made a real
         // failure signal depend on the caller's initialisation. Objeck reads 0
         // as "never opened" (API.Onnx.*Session->IsOpen) -- #1028.
         APITools_SetIntValue(context, 0, 0);
         std::wcerr << L"Error creating ONNX session: " << BytesToUnicode(ex.what())
                    << L" (this ONNX Runtime provides: " << BytesToUnicode(available_ep_list()) << L")" << std::endl;
      }
   }

   // close session
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_close_session(VMContext& context) {
      close_session(context);
   }

   // Describe a model: the names, element types and shapes of its inputs and
   // outputs. A symbolic dimension is reported as -1 -- the caller chooses it.
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_model_info(VMContext& context) {
      model_info(context);
   }

   // Run a model on named tensors, returning its outputs as named tensors. The
   // chokepoint the eight per-family functions below never had: no preprocessing
   // and no decoding, so a decoder written once in Objeck can serve any of them.
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_run(VMContext& context) {
      generic_run(context);
   }

   // Device tensors and IO binding (device_tensor.h): tensors that stay in the
   // execution provider's memory between runs.
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_new(VMContext& context) {
      device_new(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_free(VMContext& context) {
      device_free(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_upload(VMContext& context) {
      device_upload(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_download(VMContext& context) {
      device_download(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_meta(VMContext& context) {
      device_meta(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_bound_new(VMContext& context) {
      bound_new(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_bound_free(VMContext& context) {
      bound_free(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_bound_bind(VMContext& context) {
      bound_bind(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_bound_clear(VMContext& context) {
      bound_clear(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_bound_run(VMContext& context) {
      bound_run(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_bound_output(VMContext& context) {
      bound_output(context);
   }

   // Raw element patterns, and float16 / bfloat16 conversion (float16_entries.h)
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_upload_bits(VMContext& context) {
      device_upload_bits(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_device_download_bits(VMContext& context) {
      device_download_bits(context);
   }

#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_convert_float16(VMContext& context) {
      convert_float16(context);
   }

   // YOLO inference
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_yolo_image_inf(VMContext& context) {
      yolo_image_inf(context);
   }

   // ResNet inference
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_resnet_image_inf(VMContext& context) {
      resnet_image_inf(context);
   }

   // DeepLab inference
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_deeplab_image_inf(VMContext& context) {
      deeplab_image_inf(context);
   }

   // OpenPose inference
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_openpose_image_inf(VMContext& context) {
      openpose_image_inf(context);
   }

   // Phi-3 text inference
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_phi3_text_inf(VMContext& context) {
      phi3_text_inf(context);
   }

   // Phi-3 Vision inference
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_phi3_vision_inf(VMContext& context) {
      phi3_vision_inf(context);
   }

   // Detect faces using SCRFD model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_face_detect_inf(VMContext& context) {
      face_detect_inf(context);
   }

   // Detect faces and extract ArcFace embeddings
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_face_recognize_inf(VMContext& context) {
      face_recognize_inf(context);
   }
}
