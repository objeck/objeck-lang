#include "common.h"

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <unordered_map>

extern "C" {
   // initialize library
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void load_lib(VMContext& context) {
      cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
      if(!env) {
         env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "onnx");
      }
   }

   // release library
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void unload_lib() {
      env.reset();
   }

   // List available ONNX execution providers
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_get_provider_names(VMContext& context) {
      get_provider_names(context);
   }

   // create an ONNX session with the configured execution provider
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_new_session(VMContext& context) {
      std::vector<std::wstring> keys = APITools_GetStringsValues(context, 1);
      std::vector<std::wstring> values = APITools_GetStringsValues(context, 2);

#ifdef _WIN32
      const std::wstring model_path = APITools_GetStringValue(context, 3);
#else
      const std::string model_path = UnicodeToBytes(APITools_GetStringValue(context, 3));
#endif

      try {
         std::unordered_map<std::string, std::string> provider_options;

         // Pull "ep" out BEFORE building provider_options. It is a selector for
         // this layer, not an ORT provider option -- forwarding it to
         // AppendExecutionProvider makes ORT reject the whole option set, and the
         // catch below then returns without setting slot 0, so the caller gets a
         // silently null session. Shipped demos pass ep=cpu and ep=dml today.
         std::string requested_ep;
         if(!keys.empty() && keys.size() == values.size()) {
            for(size_t i = 0; i < keys.size(); ++i) {
               const std::string key = UnicodeToBytes(keys[i]);
               std::string value = UnicodeToBytes(values[i]);
               if(key == "ep") {
                  for(size_t c = 0; c < value.size(); ++c) {
                     value[c] = (char)tolower((unsigned char)value[c]);
                  }
                  requested_ep = value;
               }
               else {
                  provider_options[key] = value;
               }
            }
         }

         // The provider is fixed when this library is compiled, so "ep" can only
         // mean: fall back to CPU, or name the provider that is already built in.
         // Naming any other one is refused rather than silently ignored -- a
         // request for a provider you are not getting must never look like success.
#if defined(ONNX_EP_DML)
         const std::string compiled_ep = "dml";
#elif defined(ONNX_EP_CUDA)
         const std::string compiled_ep = "cuda";
#elif defined(ONNX_EP_QNN)
         const std::string compiled_ep = "qnn";
#elif defined(ONNX_EP_VITIS)
         const std::string compiled_ep = "vitisai";
#elif defined(ONNX_EP_COREML)
         const std::string compiled_ep = "coreml";
#else
         const std::string compiled_ep = "cpu";
#endif

         const bool use_cpu = (requested_ep == "cpu");
         if(!requested_ep.empty() && !use_cpu && requested_ep != compiled_ep) {
            std::wcerr << L">>> ONNX: execution provider '" << BytesToUnicode(requested_ep)
                       << L"' was requested, but this build provides '"
                       << BytesToUnicode(compiled_ep)
                       << L"'. Use ep=" << BytesToUnicode(compiled_ep)
                       << L" or ep=cpu. <<<" << std::endl;
            return;
         }

         Ort::SessionOptions session_options;

#if defined(ONNX_EP_DML)
         if(provider_options.find("device_id") == provider_options.end()) {
            provider_options["device_id"] = "0";
         }
         if(!use_cpu) session_options.AppendExecutionProvider("DML", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
         session_options.DisableMemPattern();
         session_options.SetIntraOpNumThreads(std::thread::hardware_concurrency());

#elif defined(ONNX_EP_CUDA)
         if(!use_cpu) {
            // Two separate traps here, both verified against the vendored
            // runtime on Linux:
            //
            //  1. The GENERIC string form does not accept "CUDA". ORT only
            //     recognises OPENVINO/SNPE/XNNPACK/QNN/WEBNN/AZURE there, so
            //     AppendExecutionProvider("CUDA", ...) always threw -- and its
            //     message does not even mention CUDA, which is why this went
            //     unnoticed. CUDA requires the typed entry point.
            //  2. The provider may not be compiled into the linked runtime at
            //     all. The vendored libonnxruntime.so.1.19.0 reports only
            //     CPUExecutionProvider despite living under cuda/lib.
            //
            // Check availability first so the error names the real problem, and
            // fail rather than fall back: a session that quietly ran on CPU
            // when CUDA was asked for is indistinguishable from success.
            bool cuda_available = false;
            std::string available_list;
            const std::vector<std::string> avail = Ort::GetAvailableProviders();
            for(size_t i = 0; i < avail.size(); ++i) {
               if(avail[i] == "CUDAExecutionProvider") {
                  cuda_available = true;
               }
               if(!available_list.empty()) {
                  available_list += ", ";
               }
               available_list += avail[i];
            }

            if(!cuda_available) {
               std::wcerr << L">>> ONNX: this build requests the CUDA execution provider, but the "
                          << L"linked onnxruntime does not provide it. Available: "
                          << BytesToUnicode(available_list)
                          << L". Rebuild the native library with './build.sh cpu', or link a "
                          << L"CUDA-enabled onnxruntime. <<<" << std::endl;
               return;
            }

            OrtCUDAProviderOptions cuda_options;
            memset(&cuda_options, 0, sizeof(cuda_options));
            cuda_options.device_id = atoi(provider_options["device_id"].c_str());
            session_options.AppendExecutionProvider_CUDA(cuda_options);
         }
         session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
         session_options.DisableMemPattern();

#elif defined(ONNX_EP_QNN)
         if(provider_options.find("backend_type") == provider_options.end()) {
            provider_options["backend_type"] = "gpu";
         }
         if(provider_options.find("qnn_context_cache_enable") == provider_options.end()) {
            provider_options["qnn_context_cache_enable"] = "1";
            provider_options["qnn_context_cache_path"] = "./qnn_cache";
            provider_options["profiling_level"] = "off";
            provider_options["ep.context_enable"] = "1";
            provider_options["ep.context_file_pat"] = "./qnn_cache/model_ctx.onnx";
            provider_options["ep.context_embed_mode"] = "1";
         }
         if(!use_cpu) session_options.AppendExecutionProvider("QNN", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

#elif defined(ONNX_EP_VITIS)
         if(!use_cpu) session_options.AppendExecutionProvider("VitisAI", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

#elif defined(ONNX_EP_COREML)
         session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

#else
         // CPU fallback
         session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
#endif

         if(!env) {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "onnx");
         }

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
         // (CoreML EP has issues with models using external data files)
         const Ort::Session* session = nullptr;
         try {
            if(!use_cpu) session_options.AppendExecutionProvider("CoreML", provider_options);
            session = new Ort::Session(*env, model_path.c_str(), session_options);
         }
         catch(const std::exception& e) {
            std::wcout << L"=> CoreML session failed (" << BytesToUnicode(e.what())
                       << L"), falling back to CPU" << std::endl;
            Ort::SessionOptions cpu_options;
            cpu_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            cpu_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
            session = new Ort::Session(*env, model_path.c_str(), cpu_options);
         }
#else
         const Ort::Session* session = new Ort::Session(*env, model_path.c_str(), session_options);
#endif

         APITools_SetIntValue(context, 0, (size_t)session);
      }
      catch(const std::exception& ex) {
         std::wcerr << L"Error creating ONNX session: " << BytesToUnicode(ex.what()) << std::endl;
      }
   }

   // close session
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_close_session(VMContext& context) {
      close_session(context);
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
