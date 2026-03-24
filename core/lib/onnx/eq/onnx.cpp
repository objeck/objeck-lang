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

         // apply user-provided key/value options
         if(!keys.empty() && keys.size() == values.size()) {
            for(size_t i = 0; i < keys.size(); ++i) {
               provider_options[UnicodeToBytes(keys[i])] = UnicodeToBytes(values[i]);
            }
         }

         Ort::SessionOptions session_options;

#if defined(ONNX_EP_DML)
         if(provider_options.find("device_id") == provider_options.end()) {
            provider_options["device_id"] = "0";
         }
         session_options.AppendExecutionProvider("DML", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
         session_options.DisableMemPattern();
         session_options.SetIntraOpNumThreads(std::thread::hardware_concurrency());

#elif defined(ONNX_EP_CUDA)
         if(provider_options.find("device_id") == provider_options.end()) {
            provider_options["device_id"] = "0";
         }
         session_options.AppendExecutionProvider("CUDA", provider_options);
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
         session_options.AppendExecutionProvider("QNN", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

#elif defined(ONNX_EP_VITIS)
         session_options.AppendExecutionProvider("VitisAI", provider_options);
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
         // Try with CoreML first, fall back to CPU if it fails
         // (CoreML EP has issues with models using external data files)
         const Ort::Session* session = nullptr;
         try {
            Ort::SessionOptions coreml_options;
            coreml_options.AppendExecutionProvider("CoreML", provider_options);
            coreml_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            coreml_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
            session = new Ort::Session(*env, model_path.c_str(), coreml_options);
         }
         catch(...) {
            std::wcout << L"=> CoreML session failed, falling back to CPU" << std::endl;
            session = new Ort::Session(*env, model_path.c_str(), session_options);
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
}
