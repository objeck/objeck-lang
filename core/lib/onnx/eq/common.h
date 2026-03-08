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
#include <iomanip>

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

std::unique_ptr<Ort::Env> env = nullptr;;

//
// FP16 conversion utilities
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

// IEEE 754 half-precision float16 -> float32 converter
static inline float half_to_float_u16(uint16_t h) {
   uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
   uint32_t exp  = (uint32_t)(h & 0x7C00u) >> 10;
   uint32_t mant = (uint32_t)(h & 0x03FFu);

   uint32_t f;
   if(exp == 0) {
      if(mant == 0) {
         f = sign; // zero
      }
      else {
         // subnormal
         exp = 1;
         while((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
         mant &= 0x03FFu;
         uint32_t exp_f = (exp + (127 - 15)) << 23;
         uint32_t mant_f = mant << 13;
         f = sign | exp_f | mant_f;
      }
   }
   else if(exp == 0x1F) {
      // Inf/NaN
      uint32_t exp_f = 0xFFu << 23;
      uint32_t mant_f = mant ? (mant << 13) : 0;
      f = sign | exp_f | mant_f;
   }
   else {
      uint32_t exp_f = (exp + (127 - 15)) << 23;
      uint32_t mant_f = mant << 13;
      f = sign | exp_f | mant_f;
   }

   float out;
   std::memcpy(&out, &f, sizeof(out));
   return out;
}

// IEEE 754 float32 -> half-precision float16 converter
static inline uint16_t float_to_half_u16(float value) {
   uint32_t f;
   std::memcpy(&f, &value, sizeof(f));

   uint32_t sign = (f >> 16) & 0x8000u;
   int32_t exp = ((f >> 23) & 0xFF) - 127 + 15;
   uint32_t mant = (f >> 13) & 0x03FFu;

   if(exp <= 0) {
      if(exp < -10) return (uint16_t)sign;
      mant = (mant | 0x0400u) >> (1 - exp);
      return (uint16_t)(sign | mant);
   }
   else if(exp == 0xFF - 127 + 15) {
      return (uint16_t)(sign | 0x7C00u | mant);
   }
   else if(exp > 30) {
      return (uint16_t)(sign | 0x7C00u);
   }

   return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

//
// DeepLab utilities
//

// model specification
struct DeepLabSpec {
   bool nchw = true; // NCHW vs NHWC
   bool input_is_float = true; // true: float32, false: uint8
   
   // ImageNet mean/std (used when input_is_float==true)
   float mean[3] = { 0.485f, 0.456f, 0.406f };
   float stdd[3] = { 0.229f, 0.224f, 0.225f };
};

// Extract simplified polygons for a specific class id (e.g., road/crosswalk)
static std::vector<std::vector<cv::Point>> extract_polygons(const cv::Mat& class_map_src, int target_class_id, double epsilon_px = 2.0) {
   cv::Mat mask = (class_map_src == target_class_id);
   std::vector<std::vector<cv::Point>> contours;
   std::vector<cv::Vec4i> hierarchy;
   cv::findContours(mask, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

   // simplify
   std::vector<std::vector<cv::Point>> polys;
   polys.reserve(contours.size());
   for(auto& c : contours) {
      std::vector<cv::Point> poly;
      if(c.size() >= 3) {
         cv::approxPolyDP(c, poly, epsilon_px, true);
         if(poly.size() >= 3) polys.push_back(std::move(poly));
      }
   }
   return polys;
}

struct DeepLabSummary {
   cv::Mat class_map_src; // CV_8U at source size, ids in [0..C-1]
   std::vector<size_t> class_ids; // unique ids present (sorted ascending)
   std::vector<double> coverage; // same length as class_ids, fraction 0..1
};

static cv::Vec3b class_to_color(int id) {
   static const cv::Vec3b table[] = {
     {  0,  0,  0}, {  0,  0,128}, {  0,128,  0}, {128,  0,  0},
     {128,128,  0}, {  0,128,128}, {128,  0,128}, {128,128,128}
   };

   return table[id % (int)(sizeof(table) / sizeof(table[0]))];
}

static cv::Mat build_logits_tensor(const float* logits, const std::vector<int64_t>& shape) {
   // Supports common DeepLab outputs:
   //   - NCHW: [1, C, H, W]
   //   - NHWC: [1, H, W, C]
   // Returns CV_8U class-id map (H x W), where each pixel is argmax over classes.

   int C = 0, H = 0, W = 0;
   bool nchw = true;

   if(shape.size() == 4) {
      // pick the smallest dim among {1,2,3} as channels (classes)
      int c_dim = 1;
      int64_t c_min = shape[1];
      for(int i = 2; i <= 3; ++i) {
         if(shape[i] < c_min) { c_min = shape[i]; c_dim = i; }
      }
      nchw = (c_dim == 1);

      if(nchw) {
         C = (int)shape[1]; H = (int)shape[2]; W = (int)shape[3];
      }
      else {
         // treat as NHWC
         H = (int)shape[1]; W = (int)shape[2]; C = (int)shape[3];
      }
   }

   if(C <= 0 || H <= 0 || W <= 0) {
      return cv::Mat();
   }

   cv::Mat cm(H, W, CV_8U);

   if(nchw) {
      const int HW = H * W;
      for(int y = 0; y < H; ++y) {
         uint8_t* row = cm.ptr<uint8_t>(y);
         for(int x = 0; x < W; ++x) {
            int idx = y * W + x;
            int best_c = 0;
            float best_v = logits[0 * HW + idx];
            for(int c = 1; c < C; ++c) {
               float v = logits[c * HW + idx];
               if(v > best_v) { best_v = v; best_c = c; }
            }
            row[x] = static_cast<uint8_t>(best_c);
         }
      }
   }
   else {
      // NHWC contiguous: logits[((y * W + x) * C) + c]
      for(int y = 0; y < H; ++y) {
         uint8_t* row = cm.ptr<uint8_t>(y);
         for(int x = 0; x < W; ++x) {
            const int base = (y * W + x) * C;
            int best_c = 0;
            float best_v = logits[base + 0];
            for(int c = 1; c < C; ++c) {
               float v = logits[base + c];
               if(v > best_v) { best_v = v; best_c = c; }
            }
            row[x] = static_cast<uint8_t>(best_c);
         }
      }
   }

   return cm;
}

static DeepLabSummary summarize_segmentation(const cv::Mat& class_map_src, int C) {
   std::vector<uint64_t> hist(C, 0);
   for(int y = 0; y < class_map_src.rows; ++y) {
      const uint8_t* r = class_map_src.ptr<uint8_t>(y);
      for(int x = 0; x < class_map_src.cols; ++x) {
         uint8_t id = r[x];
         if(id < C) hist[id]++;
      }
   }

   DeepLabSummary out;
   out.class_map_src = class_map_src;
   const double total = static_cast<double>(class_map_src.total());
   for(int cid = 0; cid < C; ++cid) {
      if(hist[cid] > 0) {
         out.class_ids.push_back(cid);
         out.coverage.push_back(hist[cid] / total);
      }
   }
   
   return out;
}

static DeepLabSummary process_deeplab_output(const Ort::Value& out, const cv::Size& src_size) {
   if(!out.IsTensor()) {
      throw std::runtime_error("DeepLab output is not a tensor.");
   }

   const auto info = out.GetTensorTypeAndShapeInfo();
   const auto shape = info.GetShape();
   const auto et = info.GetElementType();

   // Expected logits tensor, typically:
   //   - NCHW: [1, C, H, W]
   //   - NHWC: [1, H, W, C]
   if(shape.size() != 4) {
      throw std::runtime_error("Unexpected DeepLab output rank (expected 4D).");
   }

   // Read logits as float, regardless of FP16/FP32 export.
   std::vector<float> logits_f;
   const float* logits_ptr = nullptr;

   if(et == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      logits_ptr = out.GetTensorData<float>();
   }
   else if(et == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
      // Convert to FP32 for argmax (cheap vs overall inference)
      const uint16_t* h = out.GetTensorData<uint16_t>();
      const size_t n = (size_t)info.GetElementCount();
      logits_f.resize(n);
      for(size_t i = 0; i < n; ++i) {
         logits_f[i] = half_to_float_u16(h[i]);
      }
      logits_ptr = logits_f.data();
   }
   else {
      throw std::runtime_error("DeepLab output dtype must be float or float16.");
   }

   // Build class-id map at model resolution, then upsample to source resolution.
   cv::Mat class_map_model = build_logits_tensor(logits_ptr, shape);
   if(class_map_model.empty()) {
      throw std::runtime_error("Failed to build DeepLab class map (empty).");
   }

   // C is needed for sanitization + summary histogram.
   // For NCHW it's shape[1], for NHWC it's shape[3]. build_logits_tensor picks layout,
   // but we still need a safe upper bound here.
   int C = (int)std::max<int64_t>(shape[1], shape[3]);

   cv::Mat class_map_src;
   cv::resize(class_map_model, class_map_src, src_size, 0, 0, cv::INTER_NEAREST);

   // Safety clamp (should be unnecessary with nearest, but protects against bad data).
   for(int y = 0; y < class_map_src.rows; ++y) {
      uint8_t* r = class_map_src.ptr<uint8_t>(y);
      for(int x = 0; x < class_map_src.cols; ++x) {
         if(r[x] >= C) r[x] = 0;
      }
   }

   return summarize_segmentation(class_map_src, C);
}

static std::vector<float> deeplab_preprocess(const cv::Mat& img, int height, int width, std::vector<int64_t>& shape) {
   const DeepLabSpec spec;
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

//
// OpenPose
//

struct KP { float x, y, conf; };

// BGR image -> preprocessed NCHW float tensor.
// Uses letterbox (preserve aspect ratio) and returns the transform so keypoints
// decoded in model-input space can be mapped back into the original image space.
static void openpose_preprocess(const cv::Mat& img, int W, int H, std::vector<float>& out, float& lb_scale, int& lb_pad_x, int& lb_pad_y) {
   // Letterbox: resize with preserved aspect, then pad to (W,H)
   const float sx = (float)W / (float)img.cols;
   const float sy = (float)H / (float)img.rows;
   lb_scale = std::min(sx, sy);

   const int new_w = (int)std::round(img.cols * lb_scale);
   const int new_h = (int)std::round(img.rows * lb_scale);

   cv::Mat resized;
   cv::resize(img, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

   cv::Mat r(H, W, img.type(), cv::Scalar(0, 0, 0));
   lb_pad_x = (W - new_w) / 2;
   lb_pad_y = (H - new_h) / 2;
   resized.copyTo(r(cv::Rect(lb_pad_x, lb_pad_y, new_w, new_h)));

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
// ResNet utilities
//

// Preprocess image for ResNet
static std::vector<float> resnet_preprocess(const cv::Mat& img, int resize_height, int resize_width) {
   cv::Mat resized;
   cv::resize(img, resized, cv::Size(resize_width, resize_height));

   // Convert BGR -> RGB to match ImageNet mean/std ordering
   cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);

   resized.convertTo(resized, CV_32F, 1.0f / 255.0f);

   // Split channels
   std::vector<cv::Mat> channels(3);
   cv::split(resized, channels);

   // Normalize using ImageNet mean/std for each channel
   const float mean[3] = { 0.485f, 0.456f, 0.406f };
   const float stdd[3] = { 0.229f, 0.224f, 0.225f };
   for(int i = 0; i < 3; ++i) {
      channels[i] = (channels[i] - mean[i]) / stdd[i];
   }

   // Convert to CHW format and flatten
   std::vector<float> input_tensor_values;
   input_tensor_values.reserve((size_t)3 * resize_height * resize_width);
   for(const auto& channel : channels) {
      input_tensor_values.insert(input_tensor_values.end(), (float*)channel.datastart, (float*)channel.dataend);
   }

   return input_tensor_values;
}

//
// General utilities and functions
//

// close a yolo session
static void close_session(VMContext & context) {
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

//
// Reading and writing structs
//

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

//
// Inference
//

// Process Yolo image using ONNX model
static void yolo_image_inf(VMContext& context) {
// #ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
// #endif

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
      cv::Mat buf(1, (int)input_size, CV_8U, (void*)input_bytes);
      cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
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
      std::array<int64_t, 4> input_shape = { 1, 3, resize_height, resize_width };

      // Detect model input type
      auto ti = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
      auto elem = ti.GetElementType(); // ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT or _FLOAT16

      // Build tensor that matches the model input type (FP16 if model expects FP16)
      Ort::Value input_tensor = make_tensor_match_input_type(
         input_tensor_values, { 
            input_shape.begin(), 
            input_shape.end() 
         },
         elem
      );

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

      std::vector<float> preds; 
      preds.reserve((size_t)num_candidates * num_channels);
      const float* data_ptr = output_data;
      if(need_transpose) {
         preds.resize((size_t)num_candidates * num_channels);
         // transpose [1,num_channel,N] -> [N,num_channel]
         for(int64_t c = 0; c < num_channels; ++c) {
            for(int64_t n = 0; n < num_candidates; ++n) {
               preds[(size_t)n * num_channels + c] = output_data[c * num_candidates + n];
            }
         }
         data_ptr = preds.data();
      }

      // head layout
      int c_total = (int)num_channels;
      int nc_hint = (int)labels_size;
      bool has_obj = false;
      int idx_obj = -1;
      int idx_cls0 = 4;

      // determine layout
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
         if(nc <= 0 || nc > max_classes_in_tensor) {
            nc = max_classes_in_tensor;
         }

         auto sigmoid = [](float x) {
            return 1.f / (1.f + std::exp(-x));
         };

         int best = 0; float bestp = 0.f;
         for(int j = 0; j < nc; ++j) {
            float pj = sigmoid(p[idx_cls0 + j]);   // ensure [0,1]
            if(pj > bestp) { 
               bestp = pj; 
               best = j; 
            }
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
      for(size_t i = 0; i < keep.size(); ++i) {
         const size_t j = keep[i];

         const int class_id = classes[j];
         const double confidence = scores[j];
         const cv::Rect& box = boxes[j];

#ifdef _DEBUG
         std::wcout << L"class_id: " << class_id << L", confidence: " << confidence << L", rect: (" << box.x << "," << box.y << L"," << box.width << "," << box.height << ")" << std::endl;
#endif
         size_t* class_result_obj = APITools_CreateObject(context, L"API.Onnx.YoloClassification");
         if(class_result_obj) {
            class_result_obj[0] = class_id;
            if(class_id < labels_size) {
               class_result_obj[1] = labels_objs[class_id];
            }
            *((double*)(&class_result_obj[2])) = confidence;

            size_t* class_rect_obj = APITools_CreateObject(context, L"API.OpenCV.Rect");

            class_rect_obj[0] = box.x;
            class_rect_obj[1] = box.y;
            class_rect_obj[2] = box.width;
            class_rect_obj[3] = box.height;

            class_result_obj[3] = (size_t)class_rect_obj;

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

// #ifdef _DEBUG
      auto end = std::chrono::high_resolution_clock::now();
      auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"=> ONNX YOLO inference and processing time: " << duration_ms << L" ms" << std::endl;
// #endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

// Process Resnet image using ONNX model
static void resnet_image_inf(VMContext& context) {
// #ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
// #endif
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
      cv::Mat buf(1, (int)input_size, CV_8U, (void*)input_bytes);
      cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
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
      std::array<int64_t, 4> input_shape = { 1, 3, resize_height, resize_width };

      auto ti = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
      auto elem = ti.GetElementType();

      Ort::Value input_tensor = make_tensor_match_input_type(
         input_tensor_values,
         { input_shape.begin(), input_shape.end() },
         elem
      );

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

      // Build results
      size_t* resnet_result_obj = APITools_CreateObject(context, L"API.Onnx.ResNetResult");
      resnet_result_obj[0] = (size_t)output_array;

      resnet_result_obj[1] = image_index; // top index
      *((double*)(&resnet_result_obj[2])) = top_confidence; // top confidence

      // copy label name
      if(image_index < labels_size) {
         resnet_result_obj[3] = labels_objs[image_index];
      }

      APITools_SetObjectValue(context, 0, resnet_result_obj);

// #ifdef _DEBUG
      auto end = std::chrono::high_resolution_clock::now();
      auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"=> ONNX ResNet inference and processing time: " << duration_ms << L" ms" << std::endl;
// #endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

// Process Deeplab image using ONNX model
static void deeplab_image_inf(VMContext& context) {
// #ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
// #endif

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

   size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
   const long input_size = ((long)APITools_GetArraySize(input_array));
   const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

   const std::vector<std::wstring>& deeplab_labels = APITools_GetStringsValues(context, 3);

   // Validate parameters
   if(!session || !input_bytes) {
      return;
   }

   try {
      cv::Mat buf(1, (int)input_size, CV_8U, (void*)input_bytes);
      cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
      if(img.empty()) {
         if(session) {
            delete session;
            session = nullptr;
         }

         std::wcerr << L"Failed to read image!" << std::endl;
         return;
      }

      // Detect model input dimensions
      Ort::TypeInfo input_type_info = session->GetInputTypeInfo(0);
      auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
      auto model_input_shape = tensor_info.GetShape();
      auto elem = tensor_info.GetElementType();

      int net_h = 0, net_w = 0;
      // Expected: [1,3,H,W] or similar
      if(model_input_shape.size() == 4) {
         if(model_input_shape[2] > 0 && model_input_shape[3] > 0) {
            net_h = static_cast<int>(model_input_shape[2]);
            net_w = static_cast<int>(model_input_shape[3]);
         }
      }
      if(net_h <= 0 || net_w <= 0) {
         // Fallback: just use source image size
         net_h = img.rows;
         net_w = img.cols;
      }

      // Preprocess image for Deeplab
      std::vector<int64_t> input_shape;
      std::vector<float> preprocessed_input = deeplab_preprocess(img, net_h, net_w, input_shape);

      Ort::Value input_tensor = make_tensor_match_input_type(
         preprocessed_input,          // FP32 buffer from preprocessing
         input_shape,   // already a std::vector<int64_t>
         elem
      );

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

      // set results
      size_t* deeplab_result_obj = APITools_CreateObject(context, L"API.Onnx.DeepLabResult");

      // set images
      const int num_channel = (int)shape[1];
      const int H = (int)shape[2];
      const int W = (int)shape[3];
      const float* logits = output_tensor.GetTensorData<float>();

      cv::Mat mask = argmax_colorize(logits, num_channel, H, W);

      cv::Mat masked;
      cv::resize(mask, masked, img.size(), 0, 0, cv::INTER_NEAREST);

      cv::Mat overlaid;
      cv::addWeighted(img, 0.55, masked, 0.45, 0.0, overlaid);

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

      // set DeepLab metadata
      const cv::Size source_size(img.cols, img.rows);  // from your input image
      const DeepLabSummary summary = process_deeplab_output(output_tensor, source_size);

      std::vector<size_t> class_results;
      class_results.reserve(summary.class_ids.size());

      // build results
      for(size_t i = 0; i < summary.class_ids.size(); ++i) {
         size_t* deeplab_cls_obj = APITools_CreateObject(context, L"API.Onnx.DeepLabClassification");

         // build polygon list for this class
         if(deeplab_cls_obj) {
            const size_t cls_id = summary.class_ids[i];

            std::vector<size_t*> poly_objs;
            auto polys = extract_polygons(summary.class_map_src, (int)cls_id, 2.0);
            for(size_t i = 0; i < polys.size(); ++i) {
               const auto& poly = polys[i];
               for(size_t k = 0; k < poly.size(); ++k) {

                  size_t* x_obj = APITools_CreateObject(context, L"System.IntRef");
                  x_obj[0] = poly[k].x;

                  size_t* y_obj = APITools_CreateObject(context, L"System.IntRef");
                  y_obj[0] = poly[k].y;

                  size_t* pair_obj = APITools_CreateObject(context, L"Collection.Tuple.Pair");
                  pair_obj[0] = (size_t)x_obj;
                  pair_obj[1] = (size_t)y_obj;

                  poly_objs.push_back(pair_obj);
               }
            }

            // copy results
            size_t* polygon_array = APITools_MakeIntArray(context, poly_objs.size());
            size_t* polygon_array_buffer = polygon_array + 3;
            for(size_t i = 0; i < poly_objs.size(); ++i) {
               polygon_array_buffer[i] = (size_t)poly_objs[i];
            }

            // set classification
            deeplab_cls_obj[0] = cls_id;

            const std::wstring& cls_name = (cls_id >= 0 && cls_id < (int)deeplab_labels.size()) ? deeplab_labels[cls_id] : (L"unknown_" + std::to_wstring(cls_id));
            deeplab_cls_obj[1] = (size_t)APITools_CreateStringObject(context, cls_name);

            const double confidence = summary.coverage[i];
            *((double*)(&deeplab_cls_obj[2])) = confidence;

            deeplab_cls_obj[3] = (size_t)polygon_array;

            // add classification to results
            class_results.push_back((size_t)deeplab_cls_obj);
         }
      }
      
      // build results array
      size_t* class_array = APITools_MakeIntArray(context, class_results.size());
      size_t* class_array_ptr = class_array + 3;
      for(size_t i = 0; i < class_results.size(); ++i) {
         class_array_ptr[i] = class_results[i];
      }
      deeplab_result_obj[4] = (size_t)class_array;

      APITools_SetObjectValue(context, 0, deeplab_result_obj);

// #ifdef _DEBUG
      const auto end = std::chrono::high_resolution_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"=> ONNX Deeplab inference completed in " << duration << L" ms." << std::endl;
// #endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

// Process OpenPose image using ONNX model
static void openpose_image_inf(VMContext& context) {
// #ifdef _DEBUG
   auto start = std::chrono::high_resolution_clock::now();
// #endif

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

   size_t* input_array = (size_t*)APITools_GetArray(context, 2)[0];
   const long input_size = ((long)APITools_GetArraySize(input_array));
   const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

   size_t* labels_array = (size_t*)APITools_GetArray(context, 3)[0];
   const long labels_size = ((long)APITools_GetArraySize(labels_array));
   const size_t* labels_objs = APITools_GetArray(labels_array);

   // Validate parameters
   if(!session || !input_bytes || !labels_objs) {
      return;
   }

   try {
      cv::Mat buf(1, (int)input_size, CV_8U, (void*)input_bytes);
      cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
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

      // Preprocess with letterbox (aspect-preserving resize + padding)
      std::vector<float> input_tensor_data;
      float lb_scale = 1.f;
      int lb_pad_x = 0, lb_pad_y = 0;
      openpose_preprocess(img, input_width, input_height, input_tensor_data, lb_scale, lb_pad_x, lb_pad_y);

      // Match FP32 / FP16, same helper as YOLO / DeepLab / ResNet
      std::vector<int64_t> input_shape{ 1, 3, input_height, input_width };
      Ort::Value input = make_tensor_match_input_type(
         input_tensor_data, {
            input_shape.begin(),
            input_shape.end()
         },
         input_elem);

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
      auto atHM = [&](size_t k, size_t y, size_t x) -> float {
         const size_t kk = k + channel_offset; // skips PAFs if needed
         // NCHW: [1, C_total, H, W]
         size_t idx = (size_t)kk * heatmap_height * heatmap_width + (size_t)y * heatmap_width + x;
         return base[idx];         
      };

      // find peaks (skip background = last channel)
      const size_t NUM_KP = std::max(0, std::min(num_channel - 1, 25)); // works for 18/19 and 25/26
      const float PEAK_THRESH = 0.02f;
      const float KP_BOX_THRESH = 0.02f;
      const int MIN_VALID_KP = 4;

      std::vector<KP> kps(NUM_KP, { -1.f, -1.f, 0.f });

      float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
      int valid = 0;

      for(size_t k = 0; k < NUM_KP; ++k) {
         float bestv = -1e9f; int bx = -1, by = -1;
         for(int y = 0; y < heatmap_height; ++y) {
            for(int x = 0; x < heatmap_width; ++x) {
               float v = atHM(k, (size_t)y, (size_t)x);
               if(v > bestv) { bestv = v; by = y; bx = x; }
            }
         }
         if(bestv >= PEAK_THRESH && bx >= 0 && by >= 0) {
            // Keypoints are decoded in model-input (letterboxed) pixel space.
            // Map them back into the original image (crop) space by undoing
            // the letterbox padding and scale.
            float px = bx * (float)input_width / (float)heatmap_width;
            float py = by * (float)input_height / (float)heatmap_height;

            px = (px - (float)lb_pad_x) / lb_scale;
            py = (py - (float)lb_pad_y) / lb_scale;
            px = std::max(0.0f, std::min(px, (float)(img.cols - 1)));
            py = std::max(0.0f, std::min(py, (float)(img.rows - 1)));

            kps[k] = { px, py, bestv };

            if(bestv >= KP_BOX_THRESH) {
               minx = std::min(minx, px); miny = std::min(miny, py);
               maxx = std::max(maxx, px); maxy = std::max(maxy, py);
               ++valid;
            }
         }
      }

      cv::Rect person_bb;
      if(valid >= MIN_VALID_KP) {
         person_bb = cv::Rect(
            (int)std::max(0.f, minx), (int)std::max(0.f, miny),
            (int)std::max(1.f, maxx - minx), (int)std::max(1.f, maxy - miny)
         ) & cv::Rect(0, 0, img.cols, img.rows);
      }

      // Build results
      size_t* openpose_result_obj = APITools_CreateObject(context, L"API.Onnx.OpenPoseResult");

      // Normalize bbox to [0,1]
      double xn = person_bb.x / (double)img.cols;
      double yn = person_bb.y / (double)img.rows;
      double wn = person_bb.width / (double)img.cols;
      double hn = person_bb.height / (double)img.rows;

      *((double*)(&openpose_result_obj[0])) = xn;
      *((double*)(&openpose_result_obj[1])) = yn;
      *((double*)(&openpose_result_obj[2])) = wn;
      *((double*)(&openpose_result_obj[3])) = hn;
      
      std::vector<size_t> class_results;
      class_results.reserve(NUM_KP);

      for(int i = 0; i < NUM_KP; ++i) {
         size_t* openpose_class_obj = APITools_CreateObject(context, L"API.Onnx.OpenPoseClassification");
         if(openpose_class_obj) {
            const KP& p = kps[i];
            double x_norm = (p.conf > 0.f) ? (p.x / (double)img.cols) : -1.0;
            double y_norm = (p.conf > 0.f) ? (p.y / (double)img.rows) : -1.0;

            openpose_class_obj[0] = i; // id
            if(i < labels_size) {
               openpose_class_obj[1] = labels_objs[i]; // name
            }
            *((double*)(&openpose_class_obj[2])) = x_norm; // normalized x
            *((double*)(&openpose_class_obj[3])) = y_norm; // normalized y
            *((double*)(&openpose_class_obj[4])) = p.conf; // confidence

            class_results.push_back((size_t)openpose_class_obj);
         }
      }

      // Create class results array
      size_t* class_array = APITools_MakeIntArray(context, class_results.size());
      size_t* class_array_ptr = class_array + 3;
      for(size_t i = 0; i < class_results.size(); ++i) {
         class_array_ptr[i] = class_results[i];
      }
      openpose_result_obj[6] = (size_t)class_array;

      // Draw keypoints and skeleton
      cv::Mat vis = img.clone();
      for(auto& p : kps) {
         if(p.conf > PEAK_THRESH && p.x >= 0) {
            cv::Point2f pp(p.x, p.y);
            cv::circle(vis, pp, 3, { 0, 255, 255 }, -1, cv::LINE_AA);
         }
      }

      // COCO-18 skeleton pairs
      const std::pair<int, int> pairs[] = {
         {1,2},{2,3},{3,4},     // neck -> right arm
         {1,5},{5,6},{6,7},     // neck -> left arm
         {1,8},{8,9},{9,10},    // neck -> right leg
         {1,11},{11,12},{12,13},// neck -> left leg
         {1,0},                 // neck -> nose
         {0,14},{14,16},        // nose -> right eye -> right ear
         {0,15},{15,17}         // nose -> left eye -> left ear
      };

      // draw skeleton
      for(auto& pr : pairs) {
         const int a = pr.first, b = pr.second;
         if(a >= 0 && (size_t)a < NUM_KP && b >= 0 && (size_t)b < NUM_KP &&
            kps[a].conf > PEAK_THRESH && kps[b].conf > PEAK_THRESH &&
            kps[a].x >= 0 && kps[b].x >= 0) {
            cv::Point2f aa(kps[a].x, kps[a].y); cv::Point2f bb(kps[b].x, kps[b].y);
            cv::line(vis, aa, bb, { 0, 255, 0 }, 2, cv::LINE_AA);
         }
      }

      // masked image
      size_t* maked_image_array = APITools_MakeIntArray(context, 2);
      size_t* pose_image_array_buffer = maked_image_array + 3;
      pose_image_array_buffer[0] = vis.rows;
      pose_image_array_buffer[1] = vis.cols;
      openpose_result_obj[4] = (size_t)maked_image_array;

      size_t* pose_obj = opencv_raw_write(vis, context);
      openpose_result_obj[5] = (size_t)pose_obj;

      APITools_SetObjectValue(context, 0, openpose_result_obj);

// #ifdef _DEBUG
      const auto end = std::chrono::high_resolution_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::wcout << L"=> ONNX OpenPose inference completed in " << duration << L" ms." << std::endl;
// #endif
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

//
// Phi-3 / SLM text generation
//

struct SLMModelInfo {
   std::string input_ids_name;
   std::string position_ids_name;
   std::string attention_mask_name;
   std::vector<std::string> past_key_names;
   std::vector<std::string> past_value_names;

   std::string logits_name;
   std::vector<std::string> present_key_names;
   std::vector<std::string> present_value_names;

   int num_layers = 0;
   int num_kv_heads = 0;
   int head_dim = 0;
   ONNXTensorElementDataType kv_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
};

static SLMModelInfo discover_slm_model(Ort::Session* session) {
   SLMModelInfo info;
   Ort::AllocatorWithDefaultOptions alloc;

   size_t num_inputs = session->GetInputCount();
   for(size_t i = 0; i < num_inputs; ++i) {
      std::string name = session->GetInputNameAllocated(i, alloc).get();

      if(name == "input_ids") {
         info.input_ids_name = name;
      }
      else if(name == "position_ids") {
         info.position_ids_name = name;
      }
      else if(name == "attention_mask") {
         info.attention_mask_name = name;
      }
      else if(name.length() > 4 && name.substr(name.length() - 4) == ".key") {
         info.past_key_names.push_back(name);

         // Get KV shape info from first key tensor
         if(info.past_key_names.size() == 1) {
            auto ti = session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
            auto shape = ti.GetShape();
            info.kv_type = ti.GetElementType();
            // Shape: [batch, num_kv_heads, past_seq, head_dim]
            if(shape.size() >= 4) {
               info.num_kv_heads = (shape[1] > 0) ? (int)shape[1] : 32;
               info.head_dim = (shape[3] > 0) ? (int)shape[3] : 96;
            }
         }
      }
      else if(name.length() > 6 && name.substr(name.length() - 6) == ".value") {
         info.past_value_names.push_back(name);
      }
   }

   size_t num_outputs = session->GetOutputCount();
   for(size_t i = 0; i < num_outputs; ++i) {
      std::string name = session->GetOutputNameAllocated(i, alloc).get();

      if(name == "logits") {
         info.logits_name = name;
      }
      else if(name.length() > 4 && name.substr(name.length() - 4) == ".key") {
         info.present_key_names.push_back(name);
      }
      else if(name.length() > 6 && name.substr(name.length() - 6) == ".value") {
         info.present_value_names.push_back(name);
      }
   }

   info.num_layers = (int)info.past_key_names.size();

   // Sort for consistent ordering
   std::sort(info.past_key_names.begin(), info.past_key_names.end());
   std::sort(info.past_value_names.begin(), info.past_value_names.end());
   std::sort(info.present_key_names.begin(), info.present_key_names.end());
   std::sort(info.present_value_names.begin(), info.present_value_names.end());

   return info;
}

// Sample next token from logits
static int64_t sample_token(const float* logits, int vocab_size, double temperature) {
   if(temperature < 1e-6) {
      // Greedy
      return (int64_t)std::distance(logits,
         std::max_element(logits, logits + vocab_size));
   }

   // Temperature sampling
   std::vector<float> probs(vocab_size);
   float max_logit = *std::max_element(logits, logits + vocab_size);
   float sum = 0.f;
   for(int j = 0; j < vocab_size; ++j) {
      probs[j] = std::exp((logits[j] - max_logit) / (float)temperature);
      sum += probs[j];
   }
   for(int j = 0; j < vocab_size; ++j) {
      probs[j] /= sum;
   }

   std::random_device rd;
   std::mt19937 gen(rd());
   std::discrete_distribution<int> dist(probs.begin(), probs.end());
   return (int64_t)dist(gen);
}

// Extract float logits from tensor (handles FP32 and FP16)
static std::vector<float> extract_last_logits(Ort::Value& logits_tensor, int& vocab_size) {
   auto shape_info = logits_tensor.GetTensorTypeAndShapeInfo();
   auto shape = shape_info.GetShape();
   auto elem_type = shape_info.GetElementType();

   // logits shape: [1, seq_len, vocab_size]
   int seq_len = (int)shape[1];
   vocab_size = (int)shape[2];

   std::vector<float> last_logits(vocab_size);

   if(elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      const float* data = logits_tensor.GetTensorData<float>();
      const float* last = data + (seq_len - 1) * vocab_size;
      std::copy(last, last + vocab_size, last_logits.begin());
   }
   else if(elem_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
      const uint16_t* data = logits_tensor.GetTensorData<uint16_t>();
      const uint16_t* last = data + (seq_len - 1) * vocab_size;
      for(int j = 0; j < vocab_size; ++j) {
         last_logits[j] = half_to_float_u16(last[j]);
      }
   }

   return last_logits;
}

// Phi-3 / SLM text generation inference
static void phi3_text_inf(VMContext& context) {
   auto start = std::chrono::high_resolution_clock::now();

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);

   // Get input token IDs
   size_t* token_array = (size_t*)APITools_GetArray(context, 2)[0];
   const long token_count = (long)APITools_GetArraySize(token_array);
   const size_t* token_data = APITools_GetArray(token_array);

   const int max_new_tokens = (int)APITools_GetIntValue(context, 3);
   const double temperature = APITools_GetFloatValue(context, 4);

   // Get EOS token IDs
   size_t* eos_array = (size_t*)APITools_GetArray(context, 5)[0];
   const long eos_count = (long)APITools_GetArraySize(eos_array);
   const size_t* eos_data = APITools_GetArray(eos_array);

   if(!session || !token_data || token_count < 1 || max_new_tokens < 1) {
      return;
   }

   // Collect EOS tokens
   std::vector<int64_t> eos_tokens;
   for(long i = 0; i < eos_count; ++i) {
      eos_tokens.push_back((int64_t)eos_data[i]);
   }

   try {
      // Convert prompt token IDs
      std::vector<int64_t> prompt_tokens(token_count);
      for(long i = 0; i < token_count; ++i) {
         prompt_tokens[i] = (int64_t)token_data[i];
      }

      // Discover model I/O
      SLMModelInfo model_info = discover_slm_model(session);

      if(model_info.input_ids_name.empty() || model_info.logits_name.empty()) {
         std::wcerr << L"Could not identify SLM model input/output tensor names." << std::endl;
         return;
      }

      std::wcout << L"=> SLM model: " << model_info.num_layers << L" layers, "
                 << model_info.num_kv_heads << L" kv_heads, head_dim=" << model_info.head_dim << std::endl;

      Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

      std::vector<int64_t> generated_tokens;
      generated_tokens.reserve(max_new_tokens);

      // Full sequence (prompt + generated so far) - rebuilt each step
      std::vector<int64_t> full_sequence = prompt_tokens;
      bool has_kv = (model_info.num_layers > 0);

      // Build input/output name vectors
      std::vector<std::string> in_name_strs;
      in_name_strs.push_back(model_info.input_ids_name);
      if(!model_info.position_ids_name.empty()) {
         in_name_strs.push_back(model_info.position_ids_name);
      }
      if(!model_info.attention_mask_name.empty()) {
         in_name_strs.push_back(model_info.attention_mask_name);
      }
      if(has_kv) {
         for(int i = 0; i < model_info.num_layers; ++i) {
            in_name_strs.push_back(model_info.past_key_names[i]);
            in_name_strs.push_back(model_info.past_value_names[i]);
         }
      }

      // Only request logits output (no KV cache reuse for DML compatibility)
      std::vector<std::string> out_name_strs;
      out_name_strs.push_back(model_info.logits_name);

      std::vector<const char*> in_names(in_name_strs.size());
      for(size_t i = 0; i < in_name_strs.size(); ++i) {
         in_names[i] = in_name_strs[i].c_str();
      }
      std::vector<const char*> out_names(out_name_strs.size());
      for(size_t i = 0; i < out_name_strs.size(); ++i) {
         out_names[i] = out_name_strs[i].c_str();
      }

      // Generation loop (full re-encode each step for DML compatibility)
      for(int step = 0; step < max_new_tokens; ++step) {
         std::vector<Ort::Value> inputs;
         int64_t seq_len = (int64_t)full_sequence.size();

         // input_ids: [1, seq_len] - full sequence each step
         std::vector<int64_t> ids_shape = {1, seq_len};
         inputs.push_back(Ort::Value::CreateTensor<int64_t>(
            mem_info, full_sequence.data(), full_sequence.size(),
            ids_shape.data(), ids_shape.size()));

         // position_ids: [1, seq_len]
         std::vector<int64_t> pos_ids(seq_len);
         std::vector<int64_t> pos_shape = {1, seq_len};
         if(!model_info.position_ids_name.empty()) {
            for(int64_t p = 0; p < seq_len; ++p) {
               pos_ids[p] = p;
            }
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(
               mem_info, pos_ids.data(), pos_ids.size(),
               pos_shape.data(), pos_shape.size()));
         }

         // attention_mask: [1, seq_len]
         std::vector<int64_t> mask(seq_len, 1);
         std::vector<int64_t> mask_shape = {1, seq_len};
         if(!model_info.attention_mask_name.empty()) {
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(
               mem_info, mask.data(), mask.size(),
               mask_shape.data(), mask_shape.size()));
         }

         // Empty KV cache each step (no reuse)
         if(has_kv) {
            for(int i = 0; i < model_info.num_layers; ++i) {
               std::vector<int64_t> kv_shape = {1, (int64_t)model_info.num_kv_heads, 0, (int64_t)model_info.head_dim};
               inputs.push_back(Ort::Value::CreateTensor(
                  mem_info, (float*)nullptr, 0,
                  kv_shape.data(), kv_shape.size(),
                  model_info.kv_type));
               inputs.push_back(Ort::Value::CreateTensor(
                  mem_info, (float*)nullptr, 0,
                  kv_shape.data(), kv_shape.size(),
                  model_info.kv_type));
            }
         }

         // Run inference
         auto outputs = session->Run(
            Ort::RunOptions{nullptr},
            in_names.data(),
            inputs.data(),
            inputs.size(),
            out_names.data(),
            out_names.size());

         // Extract logits and sample
         int vocab_size = 0;
         std::vector<float> last_logits = extract_last_logits(outputs[0], vocab_size);
         int64_t next_token = sample_token(last_logits.data(), vocab_size, temperature);

         // Check EOS
         bool is_eos = false;
         for(auto eos : eos_tokens) {
            if(next_token == eos) { is_eos = true; break; }
         }
         if(is_eos) break;

         generated_tokens.push_back(next_token);
         full_sequence.push_back(next_token);
      }

      // Build Objeck result
      size_t* phi3_result_obj = APITools_CreateObject(context, L"API.Onnx.Phi3Result");

      // Copy generated tokens
      size_t* gen_token_array = APITools_MakeIntArray(context, generated_tokens.size());
      size_t* gen_token_buffer = gen_token_array + 3;
      for(size_t i = 0; i < generated_tokens.size(); ++i) {
         gen_token_buffer[i] = (size_t)generated_tokens[i];
      }
      phi3_result_obj[0] = (size_t)gen_token_array;

      APITools_SetObjectValue(context, 0, phi3_result_obj);

      auto end = std::chrono::high_resolution_clock::now();
      auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      double tps = (duration_ms > 0) ? (generated_tokens.size() * 1000.0 / duration_ms) : 0;
      std::wcout << L"=> SLM generation: " << generated_tokens.size() << L" tokens in "
                 << duration_ms << L" ms (" << std::fixed << std::setprecision(1) << tps << L" tok/s)" << std::endl;
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error: " << e.what() << std::endl;
   }
}

//
// Phi-3 Vision (multimodal) inference
//

// CLIP normalization constants
static const float CLIP_MEAN[] = {0.48145466f, 0.4578275f, 0.40821073f};
static const float CLIP_STD[]  = {0.26862954f, 0.26130258f, 0.27577711f};

// HD transform: calculate output size for an image
static std::pair<int,int> calc_hd_transform_size(int width, int height, int hd_num = 16) {
   bool transposed = false;
   if(width < height) {
      std::swap(width, height);
      transposed = true;
   }
   double ratio = (double)width / height;
   int scale = 1;
   while(scale * (int)std::ceil((double)scale / ratio) <= hd_num) {
      scale++;
   }
   scale--;

   int new_w = scale * 336;
   int new_h = (int)(new_w / ratio);

   // Pad height to multiple of 336
   int padded_h = (int)(std::ceil((double)new_h / 336.0) * 336);
   int padded_w = new_w;

   if(transposed) {
      std::swap(padded_w, padded_h);
   }
   return {padded_w, padded_h};
}

// Calculate number of image tokens for given image dimensions
static int calc_num_image_tokens(int width, int height, int hd_num = 16) {
   auto [pw, ph] = calc_hd_transform_size(width, height, hd_num);
   int h_crops = ph / 336;
   int w_crops = pw / 336;
   return (h_crops * w_crops + 1) * 144 + 1 + (h_crops + 1) * 12;
}

// Preprocess image for Phi-3 Vision: HD transform + CLIP normalization + crop splitting
// Returns pixel_values [1, num_crops+1, 3, 336, 336] and image_sizes [1, 2]
static bool phi3v_preprocess_image(
   const std::vector<uint8_t>& jpeg_bytes,
   int num_crops,
   std::vector<float>& pixel_values,
   int& actual_num_crops,
   int& padded_h, int& padded_w,
   int& num_img_tokens)
{
   // Decode image
   cv::Mat img = cv::imdecode(cv::Mat(jpeg_bytes), cv::IMREAD_COLOR);
   if(img.empty()) return false;
   cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

   int orig_w = img.cols, orig_h = img.rows;

   // HD transform: determine resize dimensions
   bool transposed = false;
   int work_w = orig_w, work_h = orig_h;
   if(work_w < work_h) {
      std::swap(work_w, work_h);
      transposed = true;
   }

   double ratio = (double)work_w / work_h;
   int scale = 1;
   while(scale * (int)std::ceil((double)scale / ratio) <= num_crops) {
      scale++;
   }
   scale--;

   int new_w = scale * 336;
   int new_h = (int)(new_w / ratio);

   // Resize (in landscape orientation)
   cv::Mat resized;
   if(transposed) {
      cv::resize(img, resized, cv::Size(new_h, new_w), 0, 0, cv::INTER_CUBIC);
   } else {
      cv::resize(img, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_CUBIC);
   }

   // Pad to multiple of 336 with white (255)
   int rh = resized.rows, rw = resized.cols;
   int tar_h = (int)(std::ceil((double)rh / 336.0) * 336);
   int tar_w = (int)(std::ceil((double)rw / 336.0) * 336);
   int top_pad = (tar_h - rh) / 2;
   int bot_pad = tar_h - rh - top_pad;
   int left_pad = (tar_w - rw) / 2;
   int right_pad = tar_w - rw - left_pad;

   cv::Mat padded;
   cv::copyMakeBorder(resized, padded, top_pad, bot_pad, left_pad, right_pad,
                      cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));

   padded_h = padded.rows;
   padded_w = padded.cols;
   int h_crops = padded_h / 336;
   int w_crops = padded_w / 336;
   int local_crops = h_crops * w_crops;
   actual_num_crops = local_crops + 1; // +1 for global
   num_img_tokens = (local_crops + 1) * 144 + 1 + (h_crops + 1) * 12;

   // Convert to float [0,1] and normalize with CLIP stats
   cv::Mat float_img;
   padded.convertTo(float_img, CV_32FC3, 1.0 / 255.0);

   // Create global image: bicubic resize to 336x336
   cv::Mat global_img;
   cv::resize(float_img, global_img, cv::Size(336, 336), 0, 0, cv::INTER_CUBIC);

   // Allocate output: (num_crops+1) x 3 x 336 x 336
   int total_crops = num_crops + 1; // padded to max
   pixel_values.resize(total_crops * 3 * 336 * 336, 0.0f);

   // Helper: write a 336x336 crop to pixel_values at given crop index (CHW format, normalized)
   auto write_crop = [&](const cv::Mat& crop, int crop_idx) {
      for(int c = 0; c < 3; ++c) {
         for(int y = 0; y < 336; ++y) {
            for(int x = 0; x < 336; ++x) {
               float val = crop.at<cv::Vec3f>(y, x)[c];
               val = (val - CLIP_MEAN[c]) / CLIP_STD[c];
               int offset = crop_idx * (3 * 336 * 336) + c * (336 * 336) + y * 336 + x;
               pixel_values[offset] = val;
            }
         }
      }
   };

   // Crop 0: global image
   write_crop(global_img, 0);

   // Crops 1..local_crops: local 336x336 patches from HD image
   int crop_idx = 1;
   for(int r = 0; r < h_crops; ++r) {
      for(int col = 0; col < w_crops; ++col) {
         cv::Rect roi(col * 336, r * 336, 336, 336);
         cv::Mat patch = float_img(roi);
         write_crop(patch, crop_idx++);
      }
   }

   return true;
}

// Phi-3 Vision multimodal text generation
// Context layout: [0]=result, [1]=vision_session, [2]=embed_session, [3]=decoder_session,
//                 [4]=image_bytes, [5]=prefix_tokens, [6]=suffix_tokens,
//                 [7]=max_tokens, [8]=temperature, [9]=eos_tokens
static void phi3_vision_inf(VMContext& context) {
   auto start = std::chrono::high_resolution_clock::now();

   Ort::Session* vision_session = (Ort::Session*)APITools_GetIntValue(context, 1);
   Ort::Session* embed_session = (Ort::Session*)APITools_GetIntValue(context, 2);
   Ort::Session* decoder_session = (Ort::Session*)APITools_GetIntValue(context, 3);

   // Image bytes
   size_t* img_array = (size_t*)APITools_GetArray(context, 4)[0];
   const long img_size = (long)APITools_GetArraySize(img_array);
   const unsigned char* img_data = (unsigned char*)APITools_GetArray(img_array);

   // Prefix tokens (before image placeholder)
   size_t* prefix_array = (size_t*)APITools_GetArray(context, 5)[0];
   const long prefix_count = (long)APITools_GetArraySize(prefix_array);
   const size_t* prefix_data = APITools_GetArray(prefix_array);

   // Suffix tokens (after image placeholder)
   size_t* suffix_array = (size_t*)APITools_GetArray(context, 6)[0];
   const long suffix_count = (long)APITools_GetArraySize(suffix_array);
   const size_t* suffix_data = APITools_GetArray(suffix_array);

   const int max_new_tokens = (int)APITools_GetIntValue(context, 7);
   const double temperature = APITools_GetFloatValue(context, 8);

   // EOS tokens
   size_t* eos_array = (size_t*)APITools_GetArray(context, 9)[0];
   const long eos_count = (long)APITools_GetArraySize(eos_array);
   const size_t* eos_data = APITools_GetArray(eos_array);

   if(!vision_session || !embed_session || !decoder_session || img_size < 1) {
      return;
   }

   std::vector<int64_t> eos_tokens;
   for(long i = 0; i < eos_count; ++i) {
      eos_tokens.push_back((int64_t)eos_data[i]);
   }

   try {
      // Step 1: Image preprocessing
      std::vector<uint8_t> jpeg_bytes(img_data, img_data + img_size);

      const int NUM_CROPS = 16;
      std::vector<float> pixel_values;
      int actual_crops, pad_h, pad_w, num_img_tokens;
      if(!phi3v_preprocess_image(jpeg_bytes, NUM_CROPS, pixel_values, actual_crops, pad_h, pad_w, num_img_tokens)) {
         std::wcerr << L"Failed to preprocess image" << std::endl;
         return;
      }

      std::wcout << L"=> Vision: " << pad_w << L"x" << pad_h
                 << L", crops=" << actual_crops << L", img_tokens=" << num_img_tokens << std::endl;

      Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

      // Step 2: Run vision model
      // Detect expected input type for pixel_values
      ONNXTensorElementDataType pv_expected_type;
      {
         Ort::AllocatorWithDefaultOptions alloc;
         auto type_info = vision_session->GetInputTypeInfo(0);
         pv_expected_type = type_info.GetTensorTypeAndShapeInfo().GetElementType();
      }

      // pixel_values: [1, num_crops+1, 3, 336, 336] - padded to max
      std::vector<int64_t> pv_shape = {1, (int64_t)(NUM_CROPS + 1), 3, 336, 336};
      std::vector<uint16_t> pixel_values_fp16;
      Ort::Value pv_tensor{nullptr};

      if(pv_expected_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
         pixel_values_fp16.resize(pixel_values.size());
         for(size_t i = 0; i < pixel_values.size(); ++i) {
            pixel_values_fp16[i] = float_to_half_u16(pixel_values[i]);
         }
         pv_tensor = Ort::Value::CreateTensor(
            mem_info, pixel_values_fp16.data(), pixel_values_fp16.size() * sizeof(uint16_t),
            pv_shape.data(), pv_shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
      }
      else {
         pv_tensor = Ort::Value::CreateTensor<float>(
            mem_info, pixel_values.data(), pixel_values.size(),
            pv_shape.data(), pv_shape.size());
      }

      // image_sizes: [1, 2] = {padded_h, padded_w}
      std::vector<int64_t> img_sizes_data = {(int64_t)pad_h, (int64_t)pad_w};
      std::vector<int64_t> img_sizes_shape = {1, 2};
      Ort::Value img_sizes_tensor = Ort::Value::CreateTensor<int64_t>(
         mem_info, img_sizes_data.data(), img_sizes_data.size(),
         img_sizes_shape.data(), img_sizes_shape.size());

      const char* vision_in_names[] = {"pixel_values", "image_sizes"};
      const char* vision_out_names[] = {"visual_features"};
      std::vector<Ort::Value> vision_inputs;
      vision_inputs.push_back(std::move(pv_tensor));
      vision_inputs.push_back(std::move(img_sizes_tensor));

      auto vision_outputs = vision_session->Run(
         Ort::RunOptions{nullptr},
         vision_in_names, vision_inputs.data(), vision_inputs.size(),
         vision_out_names, 1);

      // visual_features shape
      auto vf_info = vision_outputs[0].GetTensorTypeAndShapeInfo();
      auto vf_shape = vf_info.GetShape();
      auto vf_type = vf_info.GetElementType();
      int64_t vf_tokens = vf_shape[1];
      int64_t hidden_size = vf_shape[2];
      std::wcout << L"=> Visual features: [" << vf_shape[0] << L"," << vf_tokens
                 << L"," << hidden_size << L"] type=" << vf_type << std::endl;

      // Step 3: Build input_ids with image placeholder tokens
      const int64_t IMG_PLACEHOLDER = -1;
      std::vector<int64_t> input_ids;
      for(long i = 0; i < prefix_count; ++i) {
         input_ids.push_back((int64_t)prefix_data[i]);
      }
      int64_t img_start_pos = (int64_t)input_ids.size();
      for(int64_t i = 0; i < vf_tokens; ++i) {
         input_ids.push_back(IMG_PLACEHOLDER);
      }
      for(long i = 0; i < suffix_count; ++i) {
         input_ids.push_back((int64_t)suffix_data[i]);
      }

      // Step 4: Run embedding model on input_ids
      std::vector<int64_t> embed_ids = input_ids;
      for(auto& id : embed_ids) {
         if(id == IMG_PLACEHOLDER) id = 0;
      }

      int64_t seq_len = (int64_t)embed_ids.size();
      std::vector<int64_t> embed_ids_shape = {1, seq_len};

      const char* embed_in_names[] = {"input_ids"};
      const char* embed_out_names[] = {"inputs_embeds"};
      std::vector<Ort::Value> embed_inputs;
      embed_inputs.push_back(Ort::Value::CreateTensor<int64_t>(
         mem_info, embed_ids.data(), embed_ids.size(),
         embed_ids_shape.data(), embed_ids_shape.size()));

      auto embed_outputs = embed_session->Run(
         Ort::RunOptions{nullptr},
         embed_in_names, embed_inputs.data(), embed_inputs.size(),
         embed_out_names, 1);

      // inputs_embeds: [1, seq_len, hidden_size]
      auto embed_type = embed_outputs[0].GetTensorTypeAndShapeInfo().GetElementType();

      // Step 5: Fuse visual features into embeddings at placeholder positions
      if(embed_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16 && vf_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
         uint16_t* embed_data = embed_outputs[0].GetTensorMutableData<uint16_t>();
         const uint16_t* vf_data = vision_outputs[0].GetTensorData<uint16_t>();
         for(int64_t i = 0; i < vf_tokens; ++i) {
            std::memcpy(
               embed_data + (img_start_pos + i) * hidden_size,
               vf_data + i * hidden_size,
               hidden_size * sizeof(uint16_t));
         }
      }
      else if(embed_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
         float* embed_data = embed_outputs[0].GetTensorMutableData<float>();
         if(vf_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            const float* vf_data = vision_outputs[0].GetTensorData<float>();
            for(int64_t i = 0; i < vf_tokens; ++i) {
               std::memcpy(
                  embed_data + (img_start_pos + i) * hidden_size,
                  vf_data + i * hidden_size,
                  hidden_size * sizeof(float));
            }
         }
         else if(vf_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            const uint16_t* vf_data = vision_outputs[0].GetTensorData<uint16_t>();
            for(int64_t i = 0; i < vf_tokens; ++i) {
               for(int64_t j = 0; j < hidden_size; ++j) {
                  embed_data[(img_start_pos + i) * hidden_size + j] =
                     half_to_float_u16(vf_data[i * hidden_size + j]);
               }
            }
         }
      }

      // Step 6: Discover decoder model I/O
      SLMModelInfo decoder_info = discover_slm_model(decoder_session);

      std::string embeds_input_name;
      {
         Ort::AllocatorWithDefaultOptions alloc;
         size_t ni = decoder_session->GetInputCount();
         for(size_t i = 0; i < ni; ++i) {
            std::string name = decoder_session->GetInputNameAllocated(i, alloc).get();
            if(name == "inputs_embeds") {
               embeds_input_name = name;
               break;
            }
         }
      }

      if(embeds_input_name.empty()) {
         std::wcerr << L"Decoder model does not have inputs_embeds input" << std::endl;
         return;
      }

      // Step 7: Build decoder input/output names
      std::vector<std::string> dec_out_strs;
      dec_out_strs.push_back(decoder_info.logits_name);
      std::vector<const char*> dec_out_names(dec_out_strs.size());
      for(size_t i = 0; i < dec_out_strs.size(); ++i) {
         dec_out_names[i] = dec_out_strs[i].c_str();
      }

      std::vector<std::string> dec_in_strs;
      dec_in_strs.push_back(embeds_input_name);
      if(!decoder_info.position_ids_name.empty()) {
         dec_in_strs.push_back(decoder_info.position_ids_name);
      }
      if(!decoder_info.attention_mask_name.empty()) {
         dec_in_strs.push_back(decoder_info.attention_mask_name);
      }
      bool has_kv = (decoder_info.num_layers > 0);
      if(has_kv) {
         for(int i = 0; i < decoder_info.num_layers; ++i) {
            dec_in_strs.push_back(decoder_info.past_key_names[i]);
            dec_in_strs.push_back(decoder_info.past_value_names[i]);
         }
      }
      std::vector<const char*> dec_in_names(dec_in_strs.size());
      for(size_t i = 0; i < dec_in_strs.size(); ++i) {
         dec_in_names[i] = dec_in_strs[i].c_str();
      }

      // Step 8: First decoder pass with fused embeddings
      std::vector<int64_t> generated_tokens;
      generated_tokens.reserve(max_new_tokens);

      std::vector<int64_t> full_token_ids = input_ids;
      int64_t total_seq = seq_len;

      {
         std::vector<Ort::Value> dec_inputs;
         dec_inputs.push_back(std::move(embed_outputs[0]));

         std::vector<int64_t> pos_ids(total_seq);
         std::vector<int64_t> pos_shape = {1, total_seq};
         if(!decoder_info.position_ids_name.empty()) {
            for(int64_t p = 0; p < total_seq; ++p) pos_ids[p] = p;
            dec_inputs.push_back(Ort::Value::CreateTensor<int64_t>(
               mem_info, pos_ids.data(), pos_ids.size(),
               pos_shape.data(), pos_shape.size()));
         }

         std::vector<int64_t> mask(total_seq, 1);
         std::vector<int64_t> mask_shape = {1, total_seq};
         if(!decoder_info.attention_mask_name.empty()) {
            dec_inputs.push_back(Ort::Value::CreateTensor<int64_t>(
               mem_info, mask.data(), mask.size(),
               mask_shape.data(), mask_shape.size()));
         }

         if(has_kv) {
            for(int i = 0; i < decoder_info.num_layers; ++i) {
               std::vector<int64_t> kv_shape = {1, (int64_t)decoder_info.num_kv_heads, 0, (int64_t)decoder_info.head_dim};
               dec_inputs.push_back(Ort::Value::CreateTensor(
                  mem_info, (float*)nullptr, 0,
                  kv_shape.data(), kv_shape.size(),
                  decoder_info.kv_type));
               dec_inputs.push_back(Ort::Value::CreateTensor(
                  mem_info, (float*)nullptr, 0,
                  kv_shape.data(), kv_shape.size(),
                  decoder_info.kv_type));
            }
         }

         auto dec_outputs = decoder_session->Run(
            Ort::RunOptions{nullptr},
            dec_in_names.data(), dec_inputs.data(), dec_inputs.size(),
            dec_out_names.data(), dec_out_names.size());

         int vocab_size = 0;
         std::vector<float> last_logits = extract_last_logits(dec_outputs[0], vocab_size);
         int64_t next_token = sample_token(last_logits.data(), vocab_size, temperature);

         bool is_eos = false;
         for(auto eos : eos_tokens) {
            if(next_token == eos) { is_eos = true; break; }
         }
         if(!is_eos) {
            generated_tokens.push_back(next_token);
            full_token_ids.push_back(next_token);
            total_seq++;
         }
         else {
            goto vision_done;
         }
      }

      // Step 9: Subsequent steps - re-encode full sequence
      for(int step = 1; step < max_new_tokens; ++step) {
         std::vector<int64_t> step_ids = full_token_ids;
         for(auto& id : step_ids) {
            if(id == IMG_PLACEHOLDER) id = 0;
         }
         int64_t step_seq = (int64_t)step_ids.size();
         std::vector<int64_t> step_shape = {1, step_seq};

         std::vector<Ort::Value> step_embed_inputs;
         step_embed_inputs.push_back(Ort::Value::CreateTensor<int64_t>(
            mem_info, step_ids.data(), step_ids.size(),
            step_shape.data(), step_shape.size()));

         auto step_embed_out = embed_session->Run(
            Ort::RunOptions{nullptr},
            embed_in_names, step_embed_inputs.data(), step_embed_inputs.size(),
            embed_out_names, 1);

         // Re-fuse visual features
         auto se_type = step_embed_out[0].GetTensorTypeAndShapeInfo().GetElementType();
         if(se_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16 && vf_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            uint16_t* ed = step_embed_out[0].GetTensorMutableData<uint16_t>();
            const uint16_t* vd = vision_outputs[0].GetTensorData<uint16_t>();
            for(int64_t i = 0; i < vf_tokens; ++i) {
               std::memcpy(ed + (img_start_pos + i) * hidden_size, vd + i * hidden_size, hidden_size * sizeof(uint16_t));
            }
         }
         else if(se_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            float* ed = step_embed_out[0].GetTensorMutableData<float>();
            if(vf_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
               const float* vd = vision_outputs[0].GetTensorData<float>();
               for(int64_t i = 0; i < vf_tokens; ++i) {
                  std::memcpy(ed + (img_start_pos + i) * hidden_size, vd + i * hidden_size, hidden_size * sizeof(float));
               }
            } else {
               const uint16_t* vd = vision_outputs[0].GetTensorData<uint16_t>();
               for(int64_t i = 0; i < vf_tokens; ++i) {
                  for(int64_t j = 0; j < hidden_size; ++j) {
                     ed[(img_start_pos + i) * hidden_size + j] = half_to_float_u16(vd[i * hidden_size + j]);
                  }
               }
            }
         }

         // Run decoder
         std::vector<Ort::Value> dec_inputs;
         dec_inputs.push_back(std::move(step_embed_out[0]));

         std::vector<int64_t> pos_ids(step_seq);
         std::vector<int64_t> pos_shape = {1, step_seq};
         if(!decoder_info.position_ids_name.empty()) {
            for(int64_t p = 0; p < step_seq; ++p) pos_ids[p] = p;
            dec_inputs.push_back(Ort::Value::CreateTensor<int64_t>(
               mem_info, pos_ids.data(), pos_ids.size(),
               pos_shape.data(), pos_shape.size()));
         }

         std::vector<int64_t> mask(step_seq, 1);
         std::vector<int64_t> mask_shape = {1, step_seq};
         if(!decoder_info.attention_mask_name.empty()) {
            dec_inputs.push_back(Ort::Value::CreateTensor<int64_t>(
               mem_info, mask.data(), mask.size(),
               mask_shape.data(), mask_shape.size()));
         }

         if(has_kv) {
            for(int i = 0; i < decoder_info.num_layers; ++i) {
               std::vector<int64_t> kv_shape = {1, (int64_t)decoder_info.num_kv_heads, 0, (int64_t)decoder_info.head_dim};
               dec_inputs.push_back(Ort::Value::CreateTensor(
                  mem_info, (float*)nullptr, 0,
                  kv_shape.data(), kv_shape.size(),
                  decoder_info.kv_type));
               dec_inputs.push_back(Ort::Value::CreateTensor(
                  mem_info, (float*)nullptr, 0,
                  kv_shape.data(), kv_shape.size(),
                  decoder_info.kv_type));
            }
         }

         auto dec_outputs = decoder_session->Run(
            Ort::RunOptions{nullptr},
            dec_in_names.data(), dec_inputs.data(), dec_inputs.size(),
            dec_out_names.data(), dec_out_names.size());

         int vocab_size = 0;
         std::vector<float> last_logits = extract_last_logits(dec_outputs[0], vocab_size);
         int64_t next_token = sample_token(last_logits.data(), vocab_size, temperature);

         bool is_eos = false;
         for(auto eos : eos_tokens) {
            if(next_token == eos) { is_eos = true; break; }
         }
         if(is_eos) break;

         generated_tokens.push_back(next_token);
         full_token_ids.push_back(next_token);
         total_seq++;
      }

      vision_done:
      // Build Objeck result
      size_t* result_obj = APITools_CreateObject(context, L"API.Onnx.Phi3Result");
      size_t* gen_array = APITools_MakeIntArray(context, generated_tokens.size());
      size_t* gen_buf = gen_array + 3;
      for(size_t i = 0; i < generated_tokens.size(); ++i) {
         gen_buf[i] = (size_t)generated_tokens[i];
      }
      result_obj[0] = (size_t)gen_array;
      APITools_SetObjectValue(context, 0, result_obj);

      auto end = std::chrono::high_resolution_clock::now();
      auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      double tps = (duration_ms > 0) ? (generated_tokens.size() * 1000.0 / duration_ms) : 0;
      std::wcout << L"=> Vision generation: " << generated_tokens.size() << L" tokens in "
                 << duration_ms << L" ms (" << std::fixed << std::setprecision(1) << tps << L" tok/s)" << std::endl;
   }
   catch(const Ort::Exception& e) {
      std::wcerr << L"ONNX Runtime Error (vision): " << e.what() << std::endl;
   }
}
