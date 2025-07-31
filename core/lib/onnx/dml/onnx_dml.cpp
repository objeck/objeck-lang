#include "../onnx_common.h"

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

extern "C" {
   //
   // initialize library
   //
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void load_lib(VMContext& context) {
   }

   //
   // release library
   //
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void unload_lib() {
   }

   //
   // Process image using ONNX model
   //
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_process_image(VMContext& context) {
      size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 2);
      const int resize_width = (int)APITools_GetIntValue(context, 3);

      const std::wstring model_path = APITools_GetStringValue(context, 4);
      
      const Preprocessor input_preprocessor = (Preprocessor)APITools_GetIntValue(context, 5);

      // Validate parameters
      if(!input_bytes || resize_height < 1 || resize_width < 1 || model_path.empty()) {
         return;
      }

      try {
         Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING);

         // Set DML provider options
         std::unordered_map<std::string, std::string> provider_options;
         provider_options["device_id"] = "0";

         // Create session options with QNN execution provider
         Ort::SessionOptions session_options;
         session_options.AppendExecutionProvider("DmlExecutionProvider", provider_options);
         // session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

         // Create ONNX session
         Ort::Session session(env, model_path.c_str(), session_options);

         std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
         cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
         if(img.empty()) {
            std::wcerr << L"Failed to load  image!" << std::endl;
            return;
         }

         /*
         // Start timing
         auto start = std::chrono::high_resolution_clock::now();
         */
         
			// Check model format and preprocess image
         std::vector<float> input_tensor_values;
         size_t input_tensor_size = 0;
         
         switch(input_preprocessor) {
         case Preprocessor::RESNET:
            // Preprocess image for ResNet
            input_tensor_values = resnet_preprocess(img, resize_height, resize_width);
            input_tensor_size = input_tensor_values.size();
            break;
         
         case Preprocessor::YOLO:
            // Preprocess image for YOLO
            input_tensor_values = yolo_preprocess(img, resize_height, resize_width);
            input_tensor_size = 3 * resize_height * resize_width;
            break;

         default:
            std::wcerr << L"Unsupported preprocessor type!" << std::endl;
            return;
			}

         // Create input tensor
         std::array<int64_t, 4> input_shape = { 1, 3, resize_height, resize_width };

         Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
         Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            mem_info,
            input_tensor_values.data(),
            input_tensor_size, 
            input_shape.data(),
            4);

         // Get input/output names
         Ort::AllocatorWithDefaultOptions allocator;

         Ort::AllocatedStringPtr input_name_ptr = session.GetInputNameAllocated(0, allocator);
         std::string input_name_str = input_name_ptr.get();
         std::vector<const char*> input_names = { input_name_str.c_str() };

         Ort::AllocatedStringPtr output_name_ptr = session.GetOutputNameAllocated(0, allocator);
         std::string output_name_str = output_name_ptr.get();
         std::vector<const char*> output_names = { output_name_str.c_str() };

         // Run inference
         auto output_tensors = session.Run(
            Ort::RunOptions { nullptr },
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            1);

         /*
         // Calculate duration in milliseconds
         auto end = std::chrono::high_resolution_clock::now();
         auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
         */

			// Process output and shape infomoration
         Ort::Value& output_tensor = output_tensors.front();
         const float* output_data = output_tensor.GetTensorMutableData<float>();
         const Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();
         const size_t output_len = shape_info.GetElementCount();

			// Build results
         size_t* result_obj = APITools_CreateObject(context, L"API.Onnx.ImageResult");

         // Copy output
         size_t* output_array = APITools_MakeFloatArray(context, output_len);
         double* output_array_buffer = reinterpret_cast<double*>(output_array + 3);
         for(size_t i = 0; i < output_len; ++i) {
            output_array_buffer[i] = static_cast<double>(output_data[i]);
         }

         result_obj[0] = (size_t)output_array;

         // Copy output shape
         auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
         
         size_t* output_shape_array = APITools_MakeIntArray(context, output_shape.size());
         size_t* output_shape_array_buffer = output_shape_array + 3;
         for(size_t i = 0; i < output_shape.size(); ++i) {
            output_shape_array_buffer[i] = output_shape[i];
         }
         result_obj[1] = (size_t)output_shape_array;

         size_t* output_image_array = APITools_MakeIntArray(context, 2);
         size_t* output_image_array_buffer = output_image_array + 3;
         output_image_array_buffer[0] = img.rows;
         output_image_array_buffer[1] = img.cols;
         result_obj[2] = (size_t)output_image_array;
         
         APITools_SetObjectValue(context, 0, result_obj);
      }
      catch(const Ort::Exception& e) {
         std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
      }
   }

   //
   // List available ONNX execution providers
   //
#ifdef _WIN32
   __declspec(dllexport)
#endif
     void onnx_get_provider_names(VMContext& context) {
     // Get output parameter
     size_t* output_holder = APITools_GetArray(context, 0);

     // get provider names and set output holder
     output_holder[0] = (size_t)get_provider_names(context);
   }

   //
   // Convert an image from one format to another
   //
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_convert_image(VMContext& context) {
     // Get parameters
     size_t* output_holder = APITools_GetArray(context, 0);

     size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
     const long input_size = ((long)APITools_GetArraySize(input_array));
     const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

     const int output_format = (int)APITools_GetIntValue(context, 2);

     // convert image
     std::vector<unsigned char> output_image_bytes = convert_image_bytes(context, input_bytes, input_size, output_format);

     // Copy results
     size_t* output_byte_buffer = APITools_MakeByteArray(context, output_image_bytes.size());
     unsigned char* output_byte_array_buffer = (unsigned char*)(output_byte_buffer + 3);

     for(size_t i = 0; i < output_image_bytes.size(); ++i) {
       output_byte_array_buffer[i] = output_image_bytes[i];
     }

     output_holder[0] = (size_t)output_byte_buffer;
   }
}

