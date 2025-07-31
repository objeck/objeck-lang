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
      // Get parameters
      size_t* output_holder = APITools_GetArray(context, 0);

      size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 2);
      const int resize_width = (int)APITools_GetIntValue(context, 3);

      const std::wstring model_path = APITools_GetStringValue(context, 4);

      // Validate parameters
      if(!input_bytes || resize_height < 1 || resize_width < 1 || model_path.empty()) {
         output_holder[0] = 0;
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
            output_holder[0] = 0;
            return;
         }

         /*
         // Start timing
         auto start = std::chrono::high_resolution_clock::now();
         */

/* 
         // Preprocess image and convert HWC -> CHW
         std::vector<float> input_tensor_values = preprocess(img, resize_height, resize_width);
*/        

         // TODO: YOLOv5 preprocessing
         cv::Mat foo = yolo_preprocess(img, resize_height, resize_width);

         // Create input tensor
         std::array<int64_t, 4> input_shape = { 1, 3, resize_height, resize_width };

         // TODO: YOLOv5 preprocessing
         size_t input_tensor_size = 3 * resize_height * resize_width;

         Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
         Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            mem_info,
            // input_tensor_values.data(),
            foo.ptr<float>(),
            
            // input_tensor_values.size(),
            input_tensor_size, 
            
            // input_shape.size()
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

         // Process output (classification scores)
         const float* output_data = output_tensors.front().GetTensorMutableData<float>();
         Ort::Value& output_tensor = output_tensors.front();

         // Get shape info
         Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();

         // Number of elements
         const size_t output_len = shape_info.GetElementCount();

         // Copy results
         size_t* output_double_array = APITools_MakeFloatArray(context, output_len);
         double* output_double_array_buffer = reinterpret_cast<double*>(output_double_array + 3);
         for(size_t i = 0; i < output_len; ++i) {
            output_double_array_buffer[i] = static_cast<double>(output_data[i]);
         }
         output_holder[0] = (size_t)output_double_array;
      }
      catch(const Ort::Exception& e) {
         std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
         output_holder[0] = 0;
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

