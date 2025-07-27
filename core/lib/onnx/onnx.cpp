#include <iostream>
#include <fstream>
#include <random>
#include <filesystem>

#include <opencv2/opencv.hpp>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

#include "../../vm/lib_api.h"

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

// Helper to preprocess input for ResNet
cv::Mat preprocess(const cv::Mat& img) {
   cv::Mat resized;
   cv::resize(img, resized, cv::Size(224, 224)); // ResNet input size
   resized.convertTo(resized, CV_32F, 1.0 / 255);

   // Normalize (mean/std from ImageNet)
   cv::Mat channels[3];
   cv::split(resized, channels);
   for(int i = 0; i < 3; ++i) {
      channels[i] = (channels[i] - 0.485f) / 0.229f;
   }
   cv::merge(channels, 3, resized);

   return resized;
}

// 2017 Train/Val/Test: http://images.cocodataset.org/zips/train2017.zip
std::vector<std::string> load_labels(const std::string name) {
   std::vector<std::string> labels;

   std::ifstream file_in(name);

   std::string label;
   while(std::getline(file_in, label)) {
      labels.push_back(label);
   }

   file_in.close();

   return labels;
}

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
   // covert PCM to MP3 audio
   //
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void onnx_process_image(VMContext& context) {
      // get parameters
      size_t* output_holder = APITools_GetArray(context, 0);

      size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
      const long input_size = ((long)APITools_GetArraySize(input_array));
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      const int resize_height = (int)APITools_GetIntValue(context, 2);
      const int resize_width = (int)APITools_GetIntValue(context, 3);

      const std::wstring model_path = APITools_GetStringValue(context, 4);

      // check parameters
      if(!input_bytes || resize_height < 1 || resize_width < 1 || model_path.empty()) {
         output_holder[0] = 0;
         return;
      }

      Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "ONNXRuntime_QNN_Example");
      auto label_names = load_labels("C:/Users/objec/Documents/Temp/onnx/data/test2017/labels.txt");
      
      // Set up QNN options
      std::unordered_map<std::string, std::string> qnn_options;
		qnn_options["backend_type"] = "htp";

      // Create session options with QNN execution provider
      Ort::SessionOptions session_options;
      session_options.AppendExecutionProvider("QNN", qnn_options);
      session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      session_options.DisableMemPattern();

      // Load model
      Ort::Session session(env, model_path.c_str(), session_options);

      // look for NPU device
      auto execution_providers = Ort::GetAvailableProviders();
      for(size_t i = 0; i < execution_providers.size(); ++i) {
         auto provider_name = execution_providers[i];
         std::cout << "Provider: id=" << i << ", name='" << provider_name << "'" << std::endl;
      }
      
      std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
      cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
      if(img.empty()) {
         std::cerr << "Failed to load image!" << std::endl;
         return;
      }

      // Start timing
      auto start = std::chrono::high_resolution_clock::now();

      // Preprocess image
      cv::Mat input = preprocess(img);

      // Convert HWC -> CHW
      std::vector<float> input_tensor_values;
      input_tensor_values.reserve(3 * 224 * 224);
      for(int c = 0; c < 3; c++) {
         for(int y = 0; y < 224; y++) {
            for(int x = 0; x < 224; x++) {
               input_tensor_values.push_back(input.at<cv::Vec3f>(y, x)[c]);
            }
         }
      }

      // Create input tensor
      std::array<int64_t, 4> input_shape = { 1, 3, 224, 224 };
      Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
         OrtDeviceAllocator, OrtMemTypeCPU);

      Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
         mem_info, input_tensor_values.data(), input_tensor_values.size(),
         input_shape.data(), input_shape.size());

      // Get input/output names
      Ort::AllocatorWithDefaultOptions allocator;
      const char* input_name = session.GetInputNameAllocated(0, allocator).get();
      const char* output_name = session.GetOutputNameAllocated(0, allocator).get();

      // Run inference
      std::vector<const char*> input_names = { input_name };
      std::vector<const char*> output_names = { output_name };
      auto output_tensors = session.Run(
         Ort::RunOptions { nullptr }, input_names.data(), &input_tensor, 1,
         output_names.data(), 1);

      // Calculate duration in milliseconds
      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

      // Process output (classification scores)
      float* raw_output = output_tensors.front().GetTensorMutableData<float>();
      float* output_data = output_tensors.front().GetTensorMutableData<float>();
      Ort::Value& output_tensor = output_tensors.front();

      // Get shape info
      Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();

      // Number of elements
      const size_t output_len = shape_info.GetElementCount();

      /*
      for (size_t j = 0; j < output_len; ++j) {
         std::cout << "Run: " << j << " output: " << output_data[j] << std::endl;
      }
      */

      std::vector<float> probs(output_len);
      float max_logit = output_data[0];

      // find max for numerical stability
      for(size_t j = 1; j < output_len; j++) {
         if(output_data[j] > max_logit) {
            max_logit = output_data[j];
         }
      }

      // compute exp(logit - max_logit)
      float sum_exp = 0.0f;
      for(size_t j = 0; j < output_len; j++) {
         probs[j] = std::exp(output_data[j] - max_logit);
         sum_exp += probs[j];
      }

      // normalize
      for(size_t j = 0; j < output_len; j++) {
         probs[j] /= sum_exp;
      }

      size_t image_index = 0;
      float top_confidence = probs[0];

      for(size_t j = 1; j < output_len; j++) {
         if(probs[j] > top_confidence) {
            top_confidence = probs[j];
            image_index = j;
         }
      }

      // only print high confidence results
      if(top_confidence > 0.7f) {
         std::cout << duration << "," << top_confidence << "," << image_index
            << ", size=" << input_size << "," << label_names[image_index] << "\n";
      }
      
      // copy output
      size_t* output_double_array = APITools_MakeFloatArray(context, output_len);
      double* output_double_array_buffer = reinterpret_cast<double*>(output_double_array + 3);
      
      // memcpy(output_double_array_buffer, output, SHA256_DIGEST_LENGTH * sizeof(unsigned char));
      for(size_t j = 1; j < output_len; j++) {
         output_double_array_buffer[j] = output_data[j];
      }

      output_holder[0] = (size_t)output_double_array;

      std::cout << "Fin." << std::endl;
   }
}

