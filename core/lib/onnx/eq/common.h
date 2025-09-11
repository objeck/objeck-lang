#pragma once

#include "../../../vm/lib_api.h"

#if _WIN32
#define NOMINMAX
#endif 

#include <iostream>
#include <fstream>
#include <random>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <numeric>

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

std::unique_ptr<Ort::Env> env = nullptr;;

//
// DeepLab utilities
//

// DeepLab model specification
struct ModelSpec {
   bool nchw = true; // NCHW vs NHWC
   bool input_is_float = true; // true: float32, false: uint8
   
   // ImageNet mean/std (used when input_is_float==true)
   float mean[3] = { 0.485f, 0.456f, 0.406f };
   float stdd[3] = { 0.229f, 0.224f, 0.225f };
};

// Overlay color mask on original image
inline cv::Mat overlay_mask(const cv::Mat& orig_bgr, const cv::Mat& mask_bgr, float alpha = 0.5f) {
   CV_Assert(orig_bgr.size() == mask_bgr.size() && orig_bgr.type() == CV_8UC3 && mask_bgr.type() == CV_8UC3);
   cv::Mat blended;
   cv::addWeighted(orig_bgr, 1.0f - alpha, mask_bgr, alpha, 0.0, blended);

   return blended;
}

// simple palette (customize)
static cv::Vec3b class_to_color(int id) {
   static const cv::Vec3b table[] = {
     {  0,  0,  0}, {  0,  0,128}, {  0,128,  0}, {128,  0,  0},
     {128,128,  0}, {  0,128,128}, {128,  0,128}, {128,128,128}
   };

   return table[id % (int)(sizeof(table) / sizeof(table[0]))];
}

// BGR image -> preprocessed float or uint8 tensor
static std::vector<float> deeplab_preprocess(const cv::Mat& img, int height, int width, std::vector<int64_t>& shape) {
   const ModelSpec spec;
   std::vector<float> input_tensor_values;

   cv::Mat resized; 
   cv::resize(img, resized, cv::Size(width, height));

   cv::Mat rgb; 
   cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

   rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);
   shape = spec.nchw ? std::vector<int64_t>{1, 3, height, width} : std::vector<int64_t>{ 1,height,width,3 };
   input_tensor_values.resize((size_t)height * width * 3);

   if(spec.nchw) {
      std::vector<cv::Mat> ch; cv::split(rgb, ch);
      // normalize channel-wise
      for(int c = 0; c < 3; ++c) {
         ch[c] = (ch[c] - spec.mean[c]) / spec.stdd[c];
      }
      size_t cstride = (size_t)height * width;
      std::memcpy(input_tensor_values.data() + 0 * cstride, ch[0].ptr<float>(), cstride * sizeof(float));
      std::memcpy(input_tensor_values.data() + 1 * cstride, ch[1].ptr<float>(), cstride * sizeof(float));
      std::memcpy(input_tensor_values.data() + 2 * cstride, ch[2].ptr<float>(), cstride * sizeof(float));
   }
   else {
      // NHWC normalize in-place
      for(int y = 0; y < height; ++y) {
         float* p = rgb.ptr<float>(y);
         for(int x = 0; x < width; ++x) {
            float r = p[3 * x + 0], g = p[3 * x + 1], b = p[3 * x + 2];
            p[3 * x + 0] = (r - spec.mean[0]) / spec.stdd[0];
            p[3 * x + 1] = (g - spec.mean[1]) / spec.stdd[1];
            p[3 * x + 2] = (b - spec.mean[2]) / spec.stdd[2];
         }
      }

      input_tensor_values.assign((float*)rgb.datastart, (float*)rgb.dataend);
   }

   return input_tensor_values;
}

// BGR image -> preprocessed NCHW float tensor (0..1)
static void openpose_preprocess(const cv::Mat& img, int W, int H, std::vector<float>& out) {
   cv::Mat r; cv::resize(img, r, cv::Size(W, H), 0, 0, cv::INTER_LINEAR);
   cv::Mat rgb; cv::cvtColor(r, rgb, cv::COLOR_BGR2RGB);
   rgb.convertTo(rgb, CV_32F, 1.0f / 255.0f);

   // Normalize to [-1,1] which many OpenPose ONNX exports expect
   rgb = (rgb - 0.5f) / 0.5f;

   std::vector<cv::Mat> ch; cv::split(rgb, ch);
   out.resize((size_t)3 * H * W);
   size_t cs = (size_t)H * W;
   std::memcpy(out.data() + 0 * cs, ch[0].ptr<float>(), cs * sizeof(float));
   std::memcpy(out.data() + 1 * cs, ch[1].ptr<float>(), cs * sizeof(float));
   std::memcpy(out.data() + 2 * cs, ch[2].ptr<float>(), cs * sizeof(float));
}

// logits [num_channel,H,W] -> color mask
static cv::Mat argmax_colorize(const float* logits, int num_channel, int H, int W) {
   cv::Mat mask(H, W, CV_8UC3);
   for(int y = 0; y < H; ++y) {
      for(int x = 0; x < W; ++x) {
         int best = 0; float bestv = logits[0 * H * W + y * W + x];
         for(int c = 1; c < num_channel; ++c) {
            float v = logits[c * H * W + y * W + x];
            if(v > bestv) { bestv = v; best = c; }
         }
         mask.at<cv::Vec3b>(y, x) = class_to_color(best);
      }
   }
   return mask;
}

// argmax in 2D float array
static cv::Point2f argmax2d(const float* m, int H, int W, float& peak) {
   int bx = 0, by = 0; float bv = -1e9f;
   for(int y = 0; y < H; ++y) {
      const float* row = m + y * W;
      for(int x = 0; x < W; ++x) { float v = row[x]; if(v > bv) { bv = v; by = y; bx = x; } }
   }
   peak = bv; 
   
   return { (float)bx,(float)by };
}

//
// Yolo utilities 
//

// Preprocessing metadata used to undo letterbox and map boxes back to the original image.
struct PreprocInfo {
   int in_w = 0, in_h = 0; // network input size
   int img_w = 0, img_h = 0; // original image size
   float scale = 1.0f; // resize scale used during letterbox
   float pad_x = 0.0f, pad_y = 0.0f; // padding applied (left/top)
};

// Aspect-preserving resize with padding (letterbox)
static inline cv::Mat letterbox(const cv::Mat& img, int resize_height, int resize_width, PreprocInfo& info) {
   info.img_w = img.cols; 
   info.img_h = img.rows;
   info.in_w = resize_width; 
   info.in_h = resize_height;

   const float r = std::min((float)resize_width / img.cols, (float)resize_height / img.rows);
   const int new_w = (int)std::round(img.cols * r);
   const int new_h = (int)std::round(img.rows * r);
   info.scale = r;
   info.pad_x = (resize_width - new_w) * 0.5f;
   info.pad_y = (resize_height - new_h) * 0.5f;

   cv::Mat resized; cv::resize(img, resized, cv::Size(new_w, new_h));
   cv::Mat out(resize_height, resize_width, img.type(), cv::Scalar(114, 114, 114));
   resized.copyTo(out(cv::Rect((int)info.pad_x, (int)info.pad_y, new_w, new_h)));

   return out;
}

//  Simple IoU and NMS utilities for YOLO post-processing
static float iou_rect(float x1, float y1, float x2, float y2, float X1, float Y1, float X2, float Y2) {
   float xx1 = std::max(x1, X1), yy1 = std::max(y1, Y1);
   float xx2 = std::min(x2, X2), yy2 = std::min(y2, Y2);
   float w = std::max(0.f, xx2 - xx1), h = std::max(0.f, yy2 - yy1);
   float inter = w * h, uni = (x2 - x1) * (y2 - y1) + (X2 - X1) * (Y2 - Y1) - inter;

   return uni <= 0 ? 0.f : inter / uni;
}

static void nms(std::vector<size_t>& keep_idx, const std::vector<cv::Rect>& boxes, const std::vector<double>& scores, float iou_thres) {
   std::vector<size_t> idx(boxes.size());
   std::iota(idx.begin(), idx.end(), 0);
   std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
      return scores[a] > scores[b]; 
   });

   std::vector<char> sup(boxes.size(), 0);
   for(size_t i = 0; i < idx.size(); ++i) {
      size_t p = idx[i];
      // replace `continue` with if/else
      if(!sup[p]) {
         keep_idx.push_back(p);
         for(size_t j = i + 1; j < idx.size(); ++j) {
            size_t q = idx[j];
            if(!sup[q]) {
               float iou = iou_rect((float)boxes[p].x, (float)boxes[p].y,
                                    (float)(boxes[p].x + boxes[p].width),
                                    (float)(boxes[p].y + boxes[p].height),
                                    (float)boxes[q].x, (float)boxes[q].y,
                                    (float)(boxes[q].x + boxes[q].width),
                                    (float)(boxes[q].y + boxes[q].height));
               if(iou > iou_thres) sup[q] = 1;
            }
            else {
               // suppressed q
            }
         }
      }
      else {
         // suppressed pivot p
      }
   }
}

// Preprocess the image for YOLO (legacy direct-resize kept for compatibility)
static std::vector<float> yolo_preprocess(const cv::Mat& img, int resize_height, int resize_width) {
   cv::Mat resized, float_img, blob;

   // Resize and convert to float RGB
   const cv::Size input_size(resize_width, resize_height);
   cv::resize(img, resized, input_size);
   cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
   resized.convertTo(float_img, CV_32F, 1.0 / 255.0);

   // Convert to blob in NCHW format (1x3xHxW)
   cv::dnn::blobFromImage(float_img, blob, 1.0, input_size, cv::Scalar(), true, false);

   // Copy data from cv::Mat to std::vector<float>
   std::vector<float> input_tensor_values(blob.total());
   std::memcpy(input_tensor_values.data(), blob.ptr<float>(), blob.total() * sizeof(float));

   return input_tensor_values;
}

// YOLO preprocess that uses letterbox (recommended for YOLO11/YOLO12)
static inline std::vector<float> yolo_preprocess_letterbox(const cv::Mat& img, int resize_height, int resize_width, PreprocInfo& info) {
   cv::Mat lb = letterbox(img, resize_height, resize_width, info);
   cv::Mat rgb; cv::cvtColor(lb, rgb, cv::COLOR_BGR2RGB);
   rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);
   
   std::vector<cv::Mat> ch(3); 
   cv::split(rgb, ch);

   std::vector<float> input; input.reserve((size_t)3 * rgb.rows * rgb.cols);
   for(auto& c : ch) {
      input.insert(input.end(), (float*)c.datastart, (float*)c.dataend);
   }

   return input;
}

//
// ResNet utils
//

// Preprocess image for ResNet
static std::vector<float> resnet_preprocess(const cv::Mat& img, int resize_height, int resize_width) {
   cv::Mat resized;
   cv::resize(img, resized, cv::Size(resize_width, resize_height));
   resized.convertTo(resized, CV_32F, 1.0 / 255.0);

   // Split channels
   std::vector<cv::Mat> channels(3);
   cv::split(resized, channels);

   // Normalize using ImageNet mean/std for each channel
   const float mean[3] = { 0.485f, 0.456f, 0.406f };
   const float std[3] = { 0.229f, 0.224f, 0.225f };
   for(int i = 0; i < 3; ++i) {
      channels[i] = (channels[i] - mean[i]) / std[i];
   }

   // Convert to CHW format and flatten
   std::vector<float> input_tensor_values;
   for(const auto& channel : channels) {
      input_tensor_values.insert(input_tensor_values.end(), (float*)channel.datastart, (float*)channel.dataend);
   }

   return input_tensor_values;
}

//
// General utilities
//

// Minimal IEEE-754 float32 -> float16 converter (round-to-nearest-even)
static inline uint16_t f32_to_f16(float f) {
   uint32_t x; std::memcpy(&x, &f, sizeof(x));
   uint32_t sign = (x >> 16) & 0x8000u;
   uint32_t mant = x & 0x007FFFFFu;
   int32_t  exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
   if(exp <= 0) {
      if(exp < -10) return (uint16_t)sign;
      mant = (mant | 0x00800000u) >> (1 - exp);
      return (uint16_t)(sign | ((mant + 0x00001000u) >> 13));
   }
   else if(exp >= 31) {
      return (uint16_t)(sign | 0x7C00u | (mant ? 0x01u : 0u));
   }
   else {
      return (uint16_t)(sign | (exp << 10) | ((mant + 0x00001000u) >> 13));
   }
}

// Build an input tensor in FP32 or FP16 depending on the model input type.
static inline Ort::Value make_tensor_match_input_type(const std::vector<float>& nchw_f32, const std::vector<int64_t>& shape, ONNXTensorElementDataType elem_type) {
   Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
   if(elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
      std::vector<uint16_t> halfbuf(nchw_f32.size());
      for(size_t i = 0; i < nchw_f32.size(); ++i) halfbuf[i] = f32_to_f16(nchw_f32[i]);
      // Use the byte-size overload for FP16
      return Ort::Value::CreateTensor(mem,
                                      halfbuf.data(), halfbuf.size() * sizeof(uint16_t),
                                      shape.data(), (size_t)shape.size(),
                                      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
   }

   // Default: FP32
   return Ort::Value::CreateTensor<float>(mem, const_cast<float*>(nchw_f32.data()), nchw_f32.size(), shape.data(), (size_t)shape.size());
}

// Read OpenCV image from raw data
static cv::Mat opencv_raw_read(size_t* image_obj, VMContext& context) {
   const int type = (int)image_obj[0];
   const int rows = (int)image_obj[2];
   const int cols = (int)image_obj[1];
   size_t* data_array = (size_t*)image_obj[3];

   // get parameters
   const size_t data_size = APITools_GetArraySize(data_array);
   const unsigned char* data = (unsigned char*)APITools_GetArray(data_array);

   cv::Mat image(rows, cols, type);
   memcpy(image.data, data, data_size);

   return image;
}

// Write OpenCV image to raw data
static size_t* opencv_raw_write(cv::Mat& image, VMContext& context) {
   size_t* image_obj = APITools_CreateObject(context, L"API.OpenCV.Image");

   image_obj[0] = image.type(); // type
   image_obj[1] = image.cols; // columns
   image_obj[2] = image.rows; // rows

   const size_t data_size = image.total() * image.elemSize(); // size
   size_t* array = APITools_MakeByteArray(context, data_size);
   unsigned char* byte_array = (unsigned char*)(array + 3);
   memcpy(byte_array, image.data, data_size);
   image_obj[3] = (size_t)array;

   return image_obj;
}

// close a yolo session
static void close_session(VMContext& context) {
   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 0);

   if(session) {
      delete session;
      session = nullptr;
   }

   env.reset();
}

// Get available execution provider names
void get_provider_names(VMContext& context) {
   // Get output parameter
   size_t* output_holder = APITools_GetArray(context, 0);

   // Get execution provider names
   std::vector<std::wstring> execution_provider_names;

   auto execution_providers = Ort::GetAvailableProviders();
   for(size_t i = 0; i < execution_providers.size(); ++i) {
      auto& execution_provider = execution_providers[i];
      execution_provider_names.push_back(BytesToUnicode(execution_provider));
   }

   // Copy results
   size_t* output_string_array = APITools_MakeIntArray(context, execution_provider_names.size());
   size_t* output_string_array_buffer = output_string_array + 3;
   for(size_t i = 0; i < execution_provider_names.size(); ++i) {
      output_string_array_buffer[i] = (size_t)APITools_CreateStringObject(context, execution_provider_names[i]);
   }

   // get provider names and set output holder
   output_holder[0] = (size_t)output_string_array;
}

// Process Yolo image using ONNX model
static void yolo_image_inf(VMContext& context) {
#ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
#endif

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

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
         if(session) {
            delete session;
            session = nullptr;
         }

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

      // Decode YOLO11/YOLO12 fused head: supports [1,num_channel,N] or [1,N,num_channel] and (4+nc) or (4+1+nc)
      const int64_t A = (output_shape.size() >= 2) ? output_shape[1] : 0;
      const int64_t B = (output_shape.size() >= 3) ? output_shape[2] : 0;
      if(output_shape.size() != 3 || output_shape[0] != 1) {
         std::wcerr << L"Unexpected YOLO output shape" << std::endl;
      }

      const int64_t num_channels = std::min<int64_t>(A, B); // channels per candidate  (~84 or 85)
      const int64_t num_candidates = std::max<int64_t>(A, B); // number of candidates    (~8400)
      const bool need_transpose = (A == num_channels); // original was [1,num_channel,N] if A is smaller

      std::vector<float> preds; preds.reserve((size_t)num_candidates * num_channels);
      const float* data_ptr = output_data;
      if(need_transpose) {
         preds.resize((size_t)num_candidates * num_channels);
         // transpose [1,num_channel,N] -> [N,num_channel]
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

         // best class prob, head layout has set: num_channel, idx_cls0, has_obj, idx_obj, labels_size
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

#ifdef _DEBUG
      auto end = std::chrono::high_resolution_clock::now();
      auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"ONNX YOLO inference and processing time: " << duration_ms << L" ms" << std::endl;
#endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

// Process Resnet image using ONNX model
static void resnet_image_inf(VMContext& context) {
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
         if(session) {
            delete session;
            session = nullptr;
         }

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

// Process Deeplab image using ONNX model
static void deeplab_image_inf(VMContext& context) {
#ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
#endif

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

   size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
   const long input_size = ((long)APITools_GetArraySize(input_array));
   const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

   // Validate parameters
   if(!session || !input_bytes) {
      return;
   }

   try {
      std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
      cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
      if(img.empty()) {
         if(session) {
            delete session;
            session = nullptr;
         }

         std::wcerr << L"Failed to read image!" << std::endl;
         return;
      }

      // Preprocess image for Deeplab
      std::vector<int64_t> input_shape;
      std::vector<float> fbuf = deeplab_preprocess(img, 520, 520, input_shape);

      // Create input tensor
      Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
      Ort::Value input_tensor = Ort::Value::CreateTensor<float>(mem_info, fbuf.data(), fbuf.size(), input_shape.data(), input_shape.size());

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

      Ort::Value& output_tensor = output_tensors.front();
      auto info = output_tensor.GetTensorTypeAndShapeInfo();
      auto shape = info.GetShape(); // {1,num_channel,H,W}

      if(shape.size() != 4) {
         if(session) {
            delete session;
            session = nullptr;
         }

         std::wcerr << L"Unexpected output shape!" << std::endl;
         return;
      }

      const int num_channel = (int)shape[1];
      const int H = (int)shape[2];
      const int W = (int)shape[3];
      const float* logits = output_tensor.GetTensorData<float>();

      cv::Mat mask = argmax_colorize(logits, num_channel, H, W);

      cv::Mat masked;
      cv::resize(mask, masked, img.size(), 0, 0, cv::INTER_NEAREST);

      cv::Mat overlaid;
      cv::addWeighted(img, 0.55, masked, 0.45, 0.0, overlaid);

      // Build results
      size_t* deeplab_result_obj = APITools_CreateObject(context, L"API.Onnx.DeepLabResult");

      // masked image
      size_t* maked_image_array = APITools_MakeIntArray(context, 2);
      size_t* masked_image_array_buffer = maked_image_array + 3;
      masked_image_array_buffer[0] = masked.rows;
      masked_image_array_buffer[1] = masked.cols;
      deeplab_result_obj[0] = (size_t)maked_image_array;

      size_t* masked_obj = opencv_raw_write(masked, context);
      deeplab_result_obj[1] = (size_t)masked_obj;

      // overlay image
      size_t* overlay_image_array = APITools_MakeIntArray(context, 2);
      size_t* output_image_array_buffer = overlay_image_array + 3;
      output_image_array_buffer[0] = overlaid.rows;
      output_image_array_buffer[1] = overlaid.cols;
      deeplab_result_obj[2] = (size_t)overlay_image_array;

      size_t* overlay_obj = opencv_raw_write(overlaid, context);
      deeplab_result_obj[3] = (size_t)overlay_obj;

      APITools_SetObjectValue(context, 0, deeplab_result_obj);

#ifdef _DEBUG
      const auto end = std::chrono::high_resolution_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"ONNX Deeplab inference completed in " << duration << L" ms." << std::endl;
#endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

// Process OpenPose image using ONNX model
static void openpose_image_inf(VMContext& context) {
#ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
#endif

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

   size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
   const long input_size = ((long)APITools_GetArraySize(input_array));
   const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

   // Validate parameters
   if(!session || !input_bytes) {
      return;
   }

   try {
      std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
      cv::Mat img = cv::imdecode(image_data, cv::IMREAD_COLOR);
      if(img.empty()) {
         if(session) {
            delete session;
            session = nullptr;
         }

         std::wcerr << L"Failed to read image!" << std::endl;
         return;
      }

      // Preprocess image for OpenPose
      Ort::AllocatorWithDefaultOptions alloc;

      std::string in_name = session->GetInputNameAllocated(0, alloc).get();
      std::vector<std::string> out_names_s;
      size_t output_count = session->GetOutputCount();
      std::vector<const char*> out_names(output_count);
      out_names_s.resize(output_count);
      for(size_t i = 0; i < output_count; ++i) { 
         out_names_s[i] = session->GetOutputNameAllocated(i, alloc).get(); out_names[i] = out_names_s[i].c_str(); 
      }

      // Input shape/type
      auto input_type_shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
      auto input_shape_vec = input_type_shape.GetShape(); // expect [1,3,H,W]
      int input_height = input_shape_vec.size() >= 3 && input_shape_vec[2] > 0 ? (int)input_shape_vec[2] : 368;
      int input_width = input_shape_vec.size() >= 4 && input_shape_vec[3] > 0 ? (int)input_shape_vec[3] : 368;
      auto input_elem = input_type_shape.GetElementType();

      std::vector<float> input_tensor_data; 
      openpose_preprocess(img, input_width, input_height, input_tensor_data);
      std::array<int64_t, 4> input_shape{ 1,3,input_height,input_width };
    
      Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
      Ort::Value input = Ort::Value::CreateTensor<float>(
         mem_info,
         input_tensor_data.data(),
         input_tensor_data.size(),
         input_shape.data(),
         input_shape.size()
      );
      
      // Run
      const char* in_names[] = { in_name.c_str() };
      auto outs = session->Run(
         Ort::RunOptions{ nullptr }, 
         in_names, 
         &input, 
         1, 
         out_names.data(), 
         out_names.size());

      size_t heat_i = SIZE_MAX;
      int num_channel = 0, heatmap_height = 0, heatmap_width = 0;
      int channel_offset = 0; // for 57 = [38 PAFs | 19 heatmaps]
      int total_channels = 0; // actual channels in the tensor

      auto score_channels = [](int c) -> int {
         // prefer exact known sizes, then closeness to 19
         if(c == 19 || c == 26 || c == 18 || c == 25) {
            return 1000;
         }

         if(c == 57) {
            return 900; // HM+PAF concat
         }

         return 100 - std::min(std::abs(c - 19), std::abs(c - 26));
      };

      int best = -1;
      for(size_t i = 0; i < outs.size(); ++i) {
         auto s = outs[i].GetTensorTypeAndShapeInfo().GetShape();
         if(s.size() == 4 && s[0] == 1) {
           int c = (int)s[1], h = (int)s[2], w = (int)s[3];
           if(h >= 8 && w >= 8 && h <= 2048 && w <= 2048) {
              int sc = score_channels(c) + (int)std::log2((double)h * w);
              if(sc > best) {
                 best = sc; heat_i = (size_t)i;
                 total_channels = c;
                 heatmap_height = h; 
                 heatmap_width = w;
              }
           }            
         }
      }
      // bail if truly nothing plausible
      if(heat_i == SIZE_MAX) {
         if(session) {
            delete session;
            session = nullptr;
         }

         std::wcerr << L"No heatmap-like output found (none were 4D with N=1 and reasonable HxW)." << std::endl;
         return;
      }

      // decide how many HM channels and where they start
      if(total_channels == 57) { 
         num_channel = 19; 
         channel_offset = 38; 
      }
      else if(total_channels == 26 || total_channels == 25) { 
         num_channel = total_channels; 
      }
      else if(total_channels == 19 || total_channels == 18) { 
         num_channel = total_channels; 
      }
      else if(total_channels > 19 && total_channels < 128) { 
         num_channel = std::min(total_channels, 26); 
      } // generic fallback
      else { 
         num_channel = std::min(total_channels, 26); 
      }

      const float* base = outs[heat_i].GetTensorData<float>();
      auto atHM = [&](int k, int y, int x) -> float {
         const int kk = k + channel_offset; // skips PAFs if needed
         // NCHW: [1, C_total, H, W]
         size_t idx = (size_t)kk * heatmap_height * heatmap_width + (size_t)y * heatmap_width + x;
         return base[idx];         
      };

      // find peaks (skip background = last channel)
      const int NUM_KP = std::max(0, std::min(num_channel - 1, 25)); // works for 18/19 and 25/26
      std::vector<cv::Point2f> kps(NUM_KP, { -1.f,-1.f });
      for(int k = 0; k < NUM_KP; ++k) {
         float bestv = -1e9f; int bx = 0, by = 0;
         
         for(int y = 0; y < heatmap_height; ++y) {
            for(int x = 0; x < heatmap_width; ++x) {
               float v = atHM(k, y, x); // channel k (background assumed at num_channel-1)
               if(v > bestv) { 
                  bestv = v; by = y; bx = x; 
               }
            }
         }
         
         if(bestv > 0.05f) {
            kps[k] = { 
               bx * (float)img.cols / (float)heatmap_width, by * (float)img.rows / (float)heatmap_height 
            };
         }
      }

      // Draw
      cv::Mat vis = img.clone();
      for(auto& p : kps) {
         if(p.x >= 0) {
            cv::circle(vis, p, 3, { 0,255,0 }, -1, cv::LINE_AA);
         }
      }

      // skeleton pairs (25 keypoints)
      const std::pair<int, int> pairs[] = {
         {1,2},{1,5},{2,3},{3,4},{5,6},{6,7},{1,8},{8,9},{9,10},{1,11},{11,12},{12,13},{2,8},{5,11} 
      };

      // draw skeleton
      for(auto& pr : pairs) {
         const int a = pr.first, b = pr.second;
         if(a >= 0 && a < NUM_KP && b >= 0 && b < NUM_KP && kps[a].x >= 0 && kps[b].x >= 0) {
            cv::line(vis, kps[a], kps[b], { 0,200,255 }, 2, cv::LINE_AA);
         }
      }

      // Build results
      size_t* deeplab_result_obj = APITools_CreateObject(context, L"API.Onnx.OpenPoseResult");

      // masked image
      size_t* maked_image_array = APITools_MakeIntArray(context, 2);
      size_t* pose_image_array_buffer = maked_image_array + 3;
      pose_image_array_buffer[0] = vis.rows;
      pose_image_array_buffer[1] = vis.cols;
      deeplab_result_obj[0] = (size_t)maked_image_array;

      size_t* pose_obj = opencv_raw_write(vis, context);
      deeplab_result_obj[1] = (size_t)pose_obj;

      APITools_SetObjectValue(context, 0, deeplab_result_obj);

#ifdef _DEBUG
      const auto end = std::chrono::high_resolution_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"ONNX Deeplab inference completed in " << duration << L" ms." << std::endl;
#endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}
