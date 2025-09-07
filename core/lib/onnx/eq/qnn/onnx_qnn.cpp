#include "../common.h"

#include <onnxruntime_cxx_api.h>

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

extern "C" {
   std::unique_ptr<Ort::Env> env;

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
      // Get output parameter
      size_t* output_holder = APITools_GetArray(context, 0);

      // get provider names and set output holder
      output_holder[0] = (size_t)get_provider_names(context);
   }

   // create a yolo session and return available execution providers
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_yolo_session(VMContext& context) {
      const std::wstring w_provider = APITools_GetStringValue(context, 1);
      const std::string provider = UnicodeToBytes(w_provider);

      size_t* keys_array = (size_t*)APITools_GetArray(context, 2)[1];
      const long keys_size = ((long)APITools_GetArraySize(keys_array));
      const size_t* keys_ptrs = APITools_GetArray(keys_array);

      size_t* values_array = (size_t*)APITools_GetArray(context, 3)[1];
      const long values_size = ((long)APITools_GetArraySize(keys_array));
      const size_t* values_ptrs = APITools_GetArray(keys_array);

      const std::wstring model_path = APITools_GetStringValue(context, 4);

      try {
         // Set QNN provider options
         std::unordered_map<std::string, std::string> provider_options;
         provider_options["backend_type"] = "htp";

         // Create session options with QNN execution provider
         Ort::SessionOptions session_options;
         session_options.AppendExecutionProvider("QNN", provider_options);
         session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

         // Create ONNX session
         Ort::Session* session = new Ort::Session(*env, model_path.c_str(), session_options);
         APITools_SetIntValue(context, 0, (size_t)session);
      }
      catch(const std::exception& ex) {
         std::wcerr << L"Error creating ONNX session: " << BytesToUnicode(ex.what()) << std::endl;
      }
   }

   // Process Yolo image using ONNX model
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_yolo_image_inf(VMContext& context) {
      Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

      /*
      Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING);

      // Set QNN provider options
      std::unordered_map<std::string, std::string> provider_options;
      provider_options["backend_type"] = "htp";

      // Create session options with QNN execution provider
      Ort::SessionOptions session_options;
      session_options.AppendExecutionProvider("QNN", provider_options);
      session_options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

      // Create ONNX session
      Ort::Session* session = new Ort::Session(env, L"data/yolo11x.onnx", session_options);
      */

      size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 3);
      const int resize_width = (int)APITools_GetIntValue(context, 4);

      const double conf_threshold = APITools_GetFloatValue(context, 5);
      const double iou_threshold = 0.45; //  default NMS IoU

      size_t* labels_array = (size_t*)APITools_GetArray(context, 6)[0];
      const long labels_size = ((long)APITools_GetArraySize(labels_array));
      const size_t* labels_objs = APITools_GetArray(labels_array);

      // Validate parameters
      if(!session || !input_bytes || !labels_objs || conf_threshold < 0.0 || resize_height < 1 || resize_width < 1 || labels_size < 1) {
         return;
      }

      try {
         std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
         cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
         if(img.empty()) {
            std::wcerr << L"Failed to read image!" << std::endl;
            return;
         }

         // Preprocess image using letterbox (YOLO11/12 compatible)
         PreprocInfo pp;
         std::vector<float> input_tensor_values = yolo_preprocess_letterbox(img, resize_height, resize_width, pp);
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

         Ort::AllocatedStringPtr input_name_ptr = session->GetInputNameAllocated(0, allocator);
         std::string input_name_str(input_name_ptr.get());
         std::vector<const char*> input_names = { input_name_str.c_str() };

         Ort::AllocatedStringPtr output_name_ptr = session->GetOutputNameAllocated(0, allocator);
         std::string output_name_str(output_name_ptr.get());
         std::vector<const char*> output_names = { output_name_str.c_str() };

         // Run inference
         auto output_tensors = session->Run(
            Ort::RunOptions{ nullptr },
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            1);

         // Process output and shape information
         Ort::Value& output_tensor = output_tensors.front();
         const float* output_data = output_tensor.GetTensorMutableData<float>();
         const Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();
         const size_t output_len = shape_info.GetElementCount();

         // Build results
         size_t* yolo_result_obj = APITools_CreateObject(context, L"API.Onnx.YoloResult");

         // Copy raw output for debugging/compat
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

         // Copy original image size
         const int rows = img.rows;
         const int cols = img.cols;

         size_t* output_image_array = APITools_MakeIntArray(context, 2);
         size_t* output_image_array_buffer = output_image_array + 3;
         output_image_array_buffer[0] = rows;
         output_image_array_buffer[1] = cols;
         yolo_result_obj[2] = (size_t)output_image_array;

         // Decode YOLO11/YOLO12 fused head: supports [1,C,N] or [1,N,C] and (4+nc) or (4+1+nc)
         const int64_t A = (output_shape.size() >= 2) ? output_shape[1] : 0;
         const int64_t B = (output_shape.size() >= 3) ? output_shape[2] : 0;
         if(output_shape.size() != 3 || output_shape[0] != 1) {
            std::wcerr << L"Unexpected YOLO output shape" << std::endl;
         }

         const int64_t num_channels = std::min<int64_t>(A, B); // channels per candidate  (~84 or 85)
         const int64_t num_candidates = std::max<int64_t>(A, B); // number of candidates    (~8400)
         const bool need_transpose = (A == num_channels); // original was [1,C,N] if A is smaller

         std::vector<float> preds; preds.reserve((size_t)num_candidates * num_channels);
         const float* data_ptr = output_data;
         if(need_transpose) {
            preds.resize((size_t)num_candidates * num_channels);
            // transpose [1,C,N] -> [N,C]
            for(int64_t c = 0; c < num_channels; ++c)
               for(int64_t n = 0; n < num_candidates; ++n)
                  preds[(size_t)n * num_channels + c] = output_data[c * num_candidates + n];
            data_ptr = preds.data();
         }

         // head layout
         int c_total = (int)num_channels;
         int nc_hint = (int)labels_size;
         bool has_obj = false;
         int idx_obj = -1;
         int idx_cls0 = 4;

         if(c_total == 4 + nc_hint) {
            has_obj = false; idx_cls0 = 4;
         }
         else if(c_total == 5 + nc_hint) {
            has_obj = true; idx_obj = 4; idx_cls0 = 5;
         }
         else if(c_total > 5) {
            has_obj = false; idx_cls0 = 4; nc_hint = c_total - 4;
         }
         else {
            std::wcerr << L"Unexpected channels per candidate: " << c_total << std::endl;
         }

         // collect candidates
         std::vector<cv::Rect> boxes;
         boxes.reserve((size_t)num_candidates);
         std::vector<double> scores;
         scores.reserve((size_t)num_candidates);
         std::vector<int> classes;
         classes.reserve((size_t)num_candidates);

         for(int64_t i = 0; i < num_candidates; ++i) {
            const float* p = data_ptr + i * num_channels;
            float cx = p[0], cy = p[1], w = p[2], h = p[3];

            // best class prob, head layout has set: C, idx_cls0, has_obj, idx_obj, labels_size
            int max_classes_in_tensor = (int)(num_channels - idx_cls0);
            int nc = (int)labels_size;
            if(nc <= 0 || nc > max_classes_in_tensor) nc = max_classes_in_tensor;

            auto sigmoid = [](float x) {
               return 1.f / (1.f + std::exp(-x));
               };

            int best = 0; float bestp = 0.f;
            for(int j = 0; j < nc; ++j) {
               float pj = sigmoid(p[idx_cls0 + j]);   // ensure [0,1]
               if(pj > bestp) { bestp = pj; best = j; }
            }
            float obj = has_obj ? sigmoid(p[idx_obj]) : 1.f;
            float score = obj * bestp;
            // if/else instead of continue on confidence
            if(score >= (float)conf_threshold) {

               // xywh (pixel space at network input) -> xyxy
               float x1 = cx - w * 0.5f;
               float y1 = cy - h * 0.5f;
               float x2 = cx + w * 0.5f;
               float y2 = cy + h * 0.5f;

               // undo letterbox
               x1 = (x1 - pp.pad_x) / pp.scale;
               y1 = (y1 - pp.pad_y) / pp.scale;
               x2 = (x2 - pp.pad_x) / pp.scale;
               y2 = (y2 - pp.pad_y) / pp.scale;

               // clip to image
               x1 = std::min(std::max(x1, 0.f), (float)cols - 1);
               y1 = std::min(std::max(y1, 0.f), (float)rows - 1);
               x2 = std::min(std::max(x2, 0.f), (float)cols - 1);
               y2 = std::min(std::max(y2, 0.f), (float)rows - 1);

               const int left = (int)std::round(x1);
               const int top = (int)std::round(y1);
               const int width = (int)std::round(x2 - x1);
               const int height = (int)std::round(y2 - y1);
               if(width > 0 && height > 0) {
                  boxes.emplace_back(left, top, width, height);
                  scores.push_back((double)score);
                  classes.push_back(best);
               }
            }
         }

         // NMS (class-wise)
         std::vector<size_t> keep;
         nms(keep, boxes, scores, (float)iou_threshold);

         // Build class result objects
         std::vector<size_t> class_results;
         class_results.reserve(keep.size());
         for(size_t k = 0; k < keep.size(); ++k) {
            const size_t i = keep[k];

            const int class_id = classes[i];
            const double confidence = scores[i];
            const cv::Rect& r = boxes[i];

#ifdef _DEBUG
            std::wcout << L"class_id: " << class_id << L", confidence: " << confidence << L", rect: (" << r.x
               << "," << r.y << L"," << r.width << "," << r.height << ")" << std::endl;
#endif

            size_t* class_result_obj = APITools_CreateObject(context, L"API.Onnx.YoloClassification");
            class_result_obj[0] = class_id;
            if(class_id < labels_size) {
               class_result_obj[1] = labels_objs[class_id];
            }
            *((double*)(&class_result_obj[2])) = confidence;

            size_t* class_rect_obj = APITools_CreateObject(context, L"API.OpenCV.Rect");
            class_rect_obj[0] = r.x; class_rect_obj[1] = r.y;
            class_rect_obj[2] = r.width; class_rect_obj[3] = r.height;
            class_result_obj[3] = (size_t)class_rect_obj;

            class_results.push_back((size_t)class_result_obj);
         }

         // Create class results array
         size_t* class_array = APITools_MakeIntArray(context, class_results.size());
         size_t* class_array_ptr = class_array + 3;
         for(size_t i = 0; i < class_results.size(); ++i) {
            class_array_ptr[i] = class_results[i];
         }
         yolo_result_obj[3] = (size_t)class_array;

         APITools_SetObjectValue(context, 0, yolo_result_obj);
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
      Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

      size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 3);
      const int resize_width = (int)APITools_GetIntValue(context, 4);

      size_t* labels_array = (size_t*)APITools_GetArray(context, 5)[0];
      const long labels_size = (long)APITools_GetArraySize(labels_array);
      const size_t* labels_objs = APITools_GetArray(labels_array);

      // Validate parameters
      if(!session || !input_bytes || !labels_objs || resize_height < 1 || resize_width < 1 || labels_size < 1) {
         return;
      }

      try {
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

         Ort::AllocatedStringPtr input_name_ptr = session->GetInputNameAllocated(0, allocator);
         std::string input_name_str = input_name_ptr.get();
         std::vector<const char*> input_names = { input_name_str.c_str() };

         Ort::AllocatedStringPtr output_name_ptr = session->GetOutputNameAllocated(0, allocator);
         std::string output_name_str = output_name_ptr.get();
         std::vector<const char*> output_names = { output_name_str.c_str() };

         // Run inference
         auto output_tensors = session->Run(
            Ort::RunOptions{ nullptr },
            input_names.data(),
            &input_tensor,
            1,
            output_names.data(),
            1);

         // Process output and shape information
         Ort::Value& output_tensor = output_tensors.front();
         const float* output_data = output_tensor.GetTensorMutableData<float>();
         const Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();
         const size_t output_len = shape_info.GetElementCount();

         // Copy output
         size_t* output_array = APITools_MakeFloatArray(context, output_len);
         double* output_array_buffer = reinterpret_cast<double*>(output_array + 3);

         // Find max for numerical stability
         double max_logit = output_data[0];
         for(size_t j = 1; j < output_len; j++) {
            if(output_data[j] > max_logit) {
               max_logit = output_data[j];
            }
         }

         // Compute exp(logit - max_logit)
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
         double top_confidence = output_data[0];
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

         resnet_result_obj[3] = image_index; // top index
         *((double*)(&resnet_result_obj[4])) = top_confidence; // top confidence

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
