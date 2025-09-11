#include "opencv.h"

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

   // Load image from memory
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_load_image_bytes(VMContext& context) {
      // get parameters
      size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
      const long input_size = (long)APITools_GetArraySize(input_array);
      const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

      std::vector<uchar> image_data(input_bytes, input_bytes + input_size);
      cv::Mat image = cv::imdecode(image_data, cv::IMREAD_UNCHANGED);
      if(image.empty()) {
         APITools_SetIntValue(context, 0, 0);
         return;
      }

      size_t* image_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_obj);
   }

   // Load image from file
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_load_image_path(VMContext& context) {
      const std::wstring w_image_path = APITools_GetStringValue(context, 1);
      const std::string image_path = UnicodeToBytes(w_image_path);

      cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      size_t* image_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_obj);
   }

   // Display image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_show_image(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 0);

      const std::wstring w_title = APITools_GetStringValue(context, 1);
      const std::string title = UnicodeToBytes(w_title);

      const double scaling = APITools_GetFloatValue(context, 2);

      cv::Mat image = opencv_raw_read(image_obj, context);

      // resize
      cv::Mat resized_image;
      cv::resize(image, resized_image, cv::Size(), scaling, scaling, cv::INTER_LINEAR);

      cv::imshow(title, resized_image);
      cv::waitKey(0);
   }

   // Draw a rectangle on image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_draw_rect(VMContext& context) {
      size_t* image_in_obj = APITools_GetObjectValue(context, 1);
      size_t* rect_obj = APITools_GetObjectValue(context, 2);
      size_t* color_obj = APITools_GetObjectValue(context, 3);
      const long thickness = (long)APITools_GetIntValue(context, 4);
      const long type = (long)APITools_GetIntValue(context, 5);

      if(!image_in_obj || !rect_obj || !color_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_in_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      const long left = (long)rect_obj[0];
      const long top = (long)rect_obj[1];
      const long width = (long)rect_obj[2];
      const long height = (long)rect_obj[3];

      const double r = *((double*)&color_obj[0]);
      const double g = *((double*)&color_obj[1]);
      const double b = *((double*)&color_obj[2]);
      const double a = *((double*)&color_obj[3]);

      cv::rectangle(image, cv::Rect(left, top, width, height), cv::Scalar(r, g, b, a), thickness, type);

      size_t* image_out_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_out_obj);
   }

   // Draw a circle on image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_draw_circle(VMContext& context) {
      size_t* image_in_obj = APITools_GetObjectValue(context, 1);
      size_t* pt_obj = APITools_GetObjectValue(context, 2);
      const long radius = (long)APITools_GetIntValue(context, 3);
      size_t* color_obj = APITools_GetObjectValue(context, 4);
      const long thickness = (long)APITools_GetIntValue(context, 5);
      const long type = (long)APITools_GetIntValue(context, 6);

      if(!image_in_obj || !pt_obj || !color_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_in_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      const long x = (long)pt_obj[0];
      const long y = (long)pt_obj[1];

      const double r = *((double*)&color_obj[0]);
      const double g = *((double*)&color_obj[1]);
      const double b = *((double*)&color_obj[2]);
      const double a = *((double*)&color_obj[3]);

      cv::circle(image, cv::Point(x, y), radius, cv::Scalar(r, g, b, a), thickness, type);

      size_t* image_out_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_out_obj);
   }

   // Draw text on image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_draw_text(VMContext& context) {
      size_t* image_in_obj = APITools_GetObjectValue(context, 1);

      const std::wstring w_text = APITools_GetStringValue(context, 2);
      const std::string text = UnicodeToBytes(w_text);

      size_t* pt_obj = APITools_GetObjectValue(context, 3);
      const long font_face = (long)APITools_GetIntValue(context, 4);
      const double scale = APITools_GetFloatValue(context, 5);
      size_t* color_obj = APITools_GetObjectValue(context, 6);
      const long thickness = (long)APITools_GetIntValue(context, 7);
      const long type = (long)APITools_GetIntValue(context, 8);

      if(!image_in_obj || !pt_obj || !color_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_in_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      const long x = (long)pt_obj[0];
      const long y = (long)pt_obj[1];

      const double r = *((double*)&color_obj[0]);
      const double g = *((double*)&color_obj[1]);
      const double b = *((double*)&color_obj[2]);
      const double a = *((double*)&color_obj[3]);

      cv::putText(image, text, cv::Point(x, y), font_face, scale, cv::Scalar(r, g, b, a), thickness, type);

      size_t* image_out_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_out_obj);
   }

   // Resize an image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_resize_image(VMContext& context) {
      size_t* image_in_obj = APITools_GetObjectValue(context, 1);
      size_t* size_obj = APITools_GetObjectValue(context, 2);
      const double fx = APITools_GetFloatValue(context, 3);
      const double fy = APITools_GetFloatValue(context, 4);
      const long interpolation = (long)APITools_GetIntValue(context, 5);

      if(!image_in_obj || !size_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_in_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      const long width = (long)size_obj[0];
      const long height = (long)size_obj[1];

      cv::resize(image, image, cv::Size(width, height), fx, fy, interpolation);

      size_t* image_out_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_out_obj);
   }

   // Convert image to a given format
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_convert_image_format(VMContext& context) {
      size_t* output_holder = APITools_GetArray(context, 0);

      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const size_t output_format = APITools_GetIntValue(context, 2);

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         output_holder[0] = 0;
         return;
      }

      std::vector<unsigned char> ouput_bytes = convert_image_bytes(image, output_format);

      // copy output
      size_t* output_byte_array = APITools_MakeByteArray(context, ouput_bytes.size());
      unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
      memcpy(output_byte_array_buffer, ouput_bytes.data(), ouput_bytes.size());
      output_holder[0] = (size_t)output_byte_array;
   }
}