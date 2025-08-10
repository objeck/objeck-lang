#include "../common.h"

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
   }

   // release library
#ifdef _WIN32
   __declspec(dllexport)
#endif
      void unload_lib() {
   }

   // List available ONNX execution providers
#ifdef _WIN32
   __declspec(dllexport)
#endif
      void onnx_get_provider_names(VMContext& context) {
      // Get output parameter
      size_t* output_holder = APITools_GetArray(context, 0);

      // get provider names and set output holder
      output_holder[0] = (size_t)get_provider_names(context);
   }

   // Process Yolo image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_yolo_image_inf(VMContext& context) {
      size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 2);
      const int resize_width = (int)APITools_GetIntValue(context, 3);

      const std::wstring model_path = APITools_GetStringValue(context, 4);

      size_t* labels_array = (size_t*)APITools_GetArray(context, 5)[0];
      const long labels_size = ((long)APITools_GetArraySize(labels_array));
      const size_t* labels_objs = APITools_GetArray(labels_array);

      // Validate parameters
      if(!input_bytes || !labels_objs || resize_height < 1 || resize_width < 1 || labels_size < 1 || model_path.empty()) {
         return;
      }

      try {
         /*
         // Start timing
         auto start = std::chrono::high_resolution_clock::now();
         */

         Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING);

         // Set DML provider options
         std::unordered_map<std::string, std::string> provider_options;
         provider_options["device_id"] = "0";

         // Create session options with QNN execution provider
         Ort::SessionOptions session_options;
         session_options.AppendExecutionProvider("DmlExecutionProvider", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

         // Create ONNX session
         Ort::Session session(env, model_path.c_str(), session_options);

         std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
         cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
         if(img.empty()) {
            std::wcerr << L"Failed to read image!" << std::endl;
            return;
         }

         // Preprocess image for YOLO
         std::vector<float> input_tensor_values = yolo_preprocess(img, resize_height, resize_width);
         size_t input_tensor_size = input_tensor_size = 3 * resize_height * resize_width;

         /*
         // Preprocess image for ResNet
         input_tensor_values = resnet_preprocess(img, resize_height, resize_width);
         input_tensor_size = input_tensor_values.size();
         */

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
            Ort::RunOptions{ nullptr },
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            1);

         // Process output and shape infomoration
         Ort::Value& output_tensor = output_tensors.front();
         const float* output_data = output_tensor.GetTensorMutableData<float>();
         const Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();
         const size_t output_len = shape_info.GetElementCount();

         // Build results
         size_t* yolo_result_obj = APITools_CreateObject(context, L"API.Onnx.YoloResult");

         // Copy output
         size_t* output_array = APITools_MakeFloatArray(context, output_len);
         double* output_array_buffer = reinterpret_cast<double*>(output_array + 3);
         for(size_t i = 0; i < output_len; ++i) {
            output_array_buffer[i] = static_cast<double>(output_data[i]);
         }

         yolo_result_obj[0] = (size_t)output_array;

         // Copy output shape
         auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
         size_t* output_shape_array = APITools_MakeIntArray(context, output_shape.size());
         size_t* output_shape_array_buffer = output_shape_array + 3;
         for(size_t i = 0; i < output_shape.size(); ++i) {
            output_shape_array_buffer[i] = output_shape[i];
         }
         yolo_result_obj[1] = (size_t)output_shape_array;

         // Copy image size
         const int rows = img.rows;
         const int cols = img.cols;

         size_t* output_image_array = APITools_MakeIntArray(context, 2);
         size_t* output_image_array_buffer = output_image_array + 3;
         output_image_array_buffer[0] = rows;
         output_image_array_buffer[1] = cols;
         yolo_result_obj[2] = (size_t)output_image_array;

         // Process results
         std::vector<size_t> class_results;

         const int result_count = (int)output_shape[1];
         for(int i = 0; i < result_count; ++i) {
            const int base = i * (labels_size + 5);
            const double x = output_data[base + 0];
            const double y = output_data[base + 1];
            const double w = output_data[base + 2];
            const double h = output_data[base + 3];
            const double conf = output_data[base + 4];

            if(conf >= 0.7) {
               int left = static_cast<int>(((x - w / 2.0) * cols) / resize_width);
               int top = static_cast<int>(((y - h / 2.0) * rows) / resize_height);
               int width = static_cast<int>((w * cols) / resize_width);
               int height = static_cast<int>((h * rows) / resize_height);

               // Find the class with the highest probability
               int start = base + 5;
               int end = start + labels_size;
               int class_id = 0;
               double max_score = output_data[start];

               for(int j = 1; j < labels_size; ++j) {
                  double score = output_data[start + j];
                  if(score > max_score) {
                     max_score = score;
                     class_id = j;
                  }
               }

               // class ID
               size_t* class_result_obj = APITools_CreateObject(context, L"API.Onnx.YoloClassification");
               class_result_obj[0] = class_id;

               // copy label name
               if(class_id < labels_size) {
                  class_result_obj[1] = labels_objs[class_id];
               }

               // confidence
               memcpy(&class_result_obj[2], &conf, sizeof(conf));

               // copy rectangle
               size_t* class_rect_obj = APITools_CreateObject(context, L"API.OpenCV.Rect");
               class_rect_obj[0] = left;
               class_rect_obj[1] = top;
               class_rect_obj[2] = width;
               class_rect_obj[3] = height;
               class_result_obj[3] = (size_t)class_rect_obj;

               /*
               // Add classification
               std::wcout << L"Detected object: Class ID = " << class_id << L", Confidence = "
                   << conf << L", Bounding Box = [" << left << L", " << top << L", "
                   << width << L", " << height << L"]" << std::endl;
               */

               class_results.push_back((size_t)class_result_obj);
            }
         }

         // Create class results array
         size_t* class_array = APITools_MakeIntArray(context, class_results.size());
         size_t* class_array_ptr = class_array + 3;
         for(size_t i = 0; i < class_results.size(); ++i) {
            class_array_ptr[i] = class_results[i];
         }
         yolo_result_obj[3] = (size_t)class_array;

         APITools_SetObjectValue(context, 0, yolo_result_obj);

         /*
         // Calculate duration in milliseconds
         auto end = std::chrono::high_resolution_clock::now();
         auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
         std::wcout << L"ONNX inference completed in " << duration << L" ms." << std::endl;
         */
      }
      catch(const Ort::Exception& e) {
         std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
      }
   }

   // Process Resnet image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_resnet_image_inf(VMContext& context) {
      size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 2);
      const int resize_width = (int)APITools_GetIntValue(context, 3);

      const std::wstring model_path = APITools_GetStringValue(context, 4);

      size_t* labels_array = (size_t*)APITools_GetArray(context, 5)[0];
      const long labels_size = ((long)APITools_GetArraySize(labels_array));
      const size_t* labels_objs = APITools_GetArray(labels_array);

      // Validate parameters
      if(!input_bytes || !labels_objs || resize_height < 1 || resize_width < 1 || labels_size < 1 || model_path.empty()) {
         return;
      }

      try {
         /*
         // Start timing
         auto start = std::chrono::high_resolution_clock::now();
         */

         Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING);

         // Set DML provider options
         std::unordered_map<std::string, std::string> provider_options;
         provider_options["device_id"] = "0";

         // Create session options with QNN execution provider
         Ort::SessionOptions session_options;
         session_options.AppendExecutionProvider("DmlExecutionProvider", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

         // Create ONNX session
         Ort::Session session(env, model_path.c_str(), session_options);

         std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
         cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
         if(img.empty()) {
            std::wcerr << L"Failed to read image!" << std::endl;
            return;
         }

         // Preprocess image for YOLO
         std::vector<float> input_tensor_values = resnet_preprocess(img, resize_height, resize_width);
         size_t input_tensor_size = input_tensor_values.size();

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
            Ort::RunOptions{ nullptr },
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            1);

         // Process output and shape infomoration
         Ort::Value& output_tensor = output_tensors.front();
         const float* output_data = output_tensor.GetTensorMutableData<float>();
         const Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();
         const size_t output_len = shape_info.GetElementCount();

         // Copy output
         size_t* output_array = APITools_MakeFloatArray(context, output_len);
         double* output_array_buffer = reinterpret_cast<double*>(output_array + 3);
         
         // find max for numerical stability
         double max_logit = output_data[0];
         for(size_t j = 1; j < output_len; j++) {
            if(output_data[j] > max_logit) {
               max_logit = output_data[j];
            }
         }

         // compute exp(logit - max_logit)
         double sum_exp = 0.0;
         for(size_t j = 0; j < output_len; j++) {
            output_array_buffer[j] = std::exp(output_data[j] - max_logit);
            sum_exp += output_array_buffer[j];
         }

         // normalize
         for(size_t j = 0; j < output_len; j++) {
            output_array_buffer[j] /= sum_exp;
         }

         // find the top confidence
         size_t image_index = 0;
         double top_confidence= output_data[0];
         for(size_t j = 1; j < output_len; j++) {
            if(output_array_buffer[j] > top_confidence) {
               top_confidence = output_array_buffer[j];
               image_index = j;
            }
         }

         // std::wcout << L"Predicted class ID: " << image_index << L", confidence: " << top_confidence; 

         // Build results
         size_t* resnet_result_obj = APITools_CreateObject(context, L"API.Onnx.ResNetResult");
         resnet_result_obj[0] = (size_t)output_array;

         // Copy output shape
         auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
         size_t* output_shape_array = APITools_MakeIntArray(context, output_shape.size());
         size_t* output_shape_array_buffer = output_shape_array + 3;
         for(size_t i = 0; i < output_shape.size(); ++i) {
            output_shape_array_buffer[i] = output_shape[i];
         }
         resnet_result_obj[1] = (size_t)output_shape_array;

         // Copy image size
         const int rows = img.rows;
         const int cols = img.cols;

         size_t* output_image_array = APITools_MakeIntArray(context, 2);
         size_t* output_image_array_buffer = output_image_array + 3;
         output_image_array_buffer[0] = rows;
         output_image_array_buffer[1] = cols;
         resnet_result_obj[2] = (size_t)output_image_array;

         resnet_result_obj[3] = image_index; // top idex
         memcpy(&resnet_result_obj[4], &top_confidence, sizeof(top_confidence)); // set top confidence

         // copy label name
         if(image_index < labels_size) {
            resnet_result_obj[5] = labels_objs[image_index];
         }

         APITools_SetObjectValue(context, 0, resnet_result_obj);

         /*
         // Calculate duration in milliseconds
         auto end = std::chrono::high_resolution_clock::now();
         auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
         std::wcout << L"ONNX inference completed in " << duration << L" ms." << std::endl;
         */
      }
      catch(const Ort::Exception& e) {
         std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
      }
   }
}
