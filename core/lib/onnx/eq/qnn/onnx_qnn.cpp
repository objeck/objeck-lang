#include "../common.h"

#include <onnxruntime_cxx_api.h>

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

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
      env.reset(); // destroys Env after all sessions should be gone
   }

   // List available ONNX execution providers
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_get_provider_names(VMContext& context) {
      get_provider_names(context);
   }

   // create a yolo session
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_new_session(VMContext& context) {
      auto keys = APITools_GetStringsValues(context, 1);
      auto values = APITools_GetStringsValues(context, 2);
      const std::wstring model_path = APITools_GetStringValue(context, 3);

      try {
         // Set DML provider options
         std::unordered_map<std::string, std::string> provider_options;
         provider_options["backend_type"] = "gpu";
         provider_options["qnn_context_cache_enable"] = "1";
         provider_options["qnn_context_cache_path"] = "./qnn_cache"; // persistent & writable
         provider_options["profiling_level"] = "off";

         provider_options["ep.context_enable"] = "1";
         provider_options["ep.context_file_pat"] = "./qnn_cache/model_ctx.onnx";
         provider_options["ep.context_embed_mode"] = "1";

         if(!keys.empty() && keys.size() == values.size()) {
            for(size_t i = 0; i < keys.size(); ++i) {
               provider_options[UnicodeToBytes(keys[i])] = UnicodeToBytes(values[i]);
            }
         }

         // Create session options with DML execution provider
         Ort::SessionOptions session_options;// comment
         session_options.AppendExecutionProvider("QNN", provider_options);



         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

         // Create ONNX session
         const Ort::Session* session = new Ort::Session(*env, model_path.c_str(), session_options);
         APITools_SetIntValue(context, 0, (size_t)session);
      }
      catch(const std::exception& ex) {
         std::wcerr << L"Error creating ONNX session: " << BytesToUnicode(ex.what()) << std::endl;
      }
   }

   // close a yolo session
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_close_session(VMContext& context) {
      close_session(context);
   }

   // Process Yolo image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_yolo_image_inf(VMContext& context) {
      yolo_image_inf(context);
   }

   // Process Resnet image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_resnet_image_inf(VMContext& context) {
      resnet_image_inf(context);
   }

   // Process Deeplab image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_deeplab_image_inf(VMContext& context) {
      deeplab_image_inf(context);
   }

   // Process OpenPose image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_openpose_image_inf(VMContext& context) {
      openpose_image_inf(context);
   }
}
