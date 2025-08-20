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

enum ImageFormat {
   JPEG = 64,
   PNG,
   WEBP,
   GIF
};

enum Preprocessor {
   RESNET = 128,
   YOLO,
   OTHER
};

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
   std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {return scores[a] > scores[b]; });
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

// Get available execution provider names
static size_t* get_provider_names(VMContext& context) {
   // Get execution provider names
   std::vector<std::wstring> execution_provider_names;

   auto execution_providers = Ort::GetAvailableProviders();
   for(size_t i = 0; i < execution_providers.size(); ++i) {
      auto execution_provider = execution_providers[i];
      execution_provider_names.push_back(BytesToUnicode(execution_provider));
   }

   // Copy results
   size_t* output_string_array = APITools_MakeIntArray(context, execution_provider_names.size());
   size_t* output_string_array_buffer = output_string_array + 3;
   for(size_t i = 0; i < execution_provider_names.size(); ++i) {
      output_string_array_buffer[i] = (size_t)APITools_CreateStringObject(context, execution_provider_names[i]);
   }

   return output_string_array;
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
