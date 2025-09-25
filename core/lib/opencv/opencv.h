#pragma once

#include <iostream>
#include <fstream>
#include <random>
#include <filesystem>

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../../vm/lib_api.h"

enum ImageFormat {
  JPEG = 64,
  PNG ,
  WEBP,
  GIF
};

enum Preprocessor {
   RESNET = 128,
   YOLO,
   OTHER
};

//
// Common supporting functions
//

// Convert OpenCV Mat to image format
std::vector<unsigned char> convert_image_bytes(cv::Mat &image, size_t output_format)
{
  std::string output_ext; std::vector<int> encode_params;
  switch(output_format) {
    // JPEG
  case ImageFormat::JPEG:
    output_ext = ".jpg";
    encode_params = { cv::IMWRITE_JPEG_QUALITY, 95 }; // image quality
    break;

    // PNG
  case ImageFormat::PNG:
    output_ext = ".png";
    encode_params = { cv::IMWRITE_PNG_COMPRESSION, 3 }; // PNG compression level
    break;

    // WEBP
  case ImageFormat::WEBP:
    output_ext = ".webp";
    encode_params = { cv::IMWRITE_WEBP_QUALITY, 95 }; // image quality
    break;

#ifdef _WIN32
    // GIF
  case ImageFormat::GIF:
    output_ext = ".gif";
    encode_params = { cv::IMWRITE_GIF_QUALITY, 95 }; // image quality
    break;
#endif

  default:
    return std::vector<unsigned char>();
  }  

  // Special handling for HDR JPEG (tone map to 8-bit)
  if(output_format == ImageFormat::JPEG && image.depth() == CV_32F) {
    image.convertTo(image, CV_8UC3, 255.0);  // tone mapping
  }

  // Encode to target format
  std::vector<unsigned char> output_bytes;
  if(!cv::imencode(output_ext, image, output_bytes, encode_params)) {
    return std::vector<unsigned char>();
  }

  return output_bytes;
}

// Preprocess the image for YOLO
std::vector<float> yolo_preprocess(const cv::Mat& img, int resize_height, int resize_width) {
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

// Preprocess image for ResNet
std::vector<float> resnet_preprocess(const cv::Mat& img, int resize_height, int resize_width) {
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

// Read OpenCV image from raw data
cv::Mat opencv_raw_read(size_t* image_obj, VMContext& context) {
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
size_t* opencv_raw_write(cv::Mat& image, VMContext& context) {
   if(!image.data) {
      return nullptr;
   }

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

