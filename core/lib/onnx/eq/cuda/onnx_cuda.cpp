#include "../common.h"

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
      std::vector<std::wstring> keys = APITools_GetStringsValues(context, 1);
      std::vector<std::wstring> values = APITools_GetStringsValues(context, 2);
      const std::string model_path = UnicodeToBytes(APITools_GetStringValue(context, 3));
      
      try {
         // Set DML provider options
         std::unordered_map<std::string, std::string> provider_options;
         provider_options["device_id"] = "0";

         if(!keys.empty() && keys.size() == values.size()) {
            for(size_t i = 0; i < keys.size(); ++i) {
               std::string key = UnicodeToBytes(keys[i]);
               std::string value = UnicodeToBytes(values[i]);
               provider_options[key] = value;
            }
         }

         // Create session options with DML execution provider
         Ort::SessionOptions session_options;// comment
         session_options.AppendExecutionProvider("DML", provider_options);
         
         session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

         session_options.DisableMemPattern();

         // Create ONNX session
         if(!env) {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "onnx");
         }
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
