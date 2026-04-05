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

   // Get video property
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_get(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         const int prop_id = (int)APITools_GetIntValue(context, 2);
         APITools_SetFloatValue(context, 0, capture->get(prop_id));
      }
   }

   // Opens a stream
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_open_id_pref(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         const int device_id = (int)APITools_GetIntValue(context, 2);
         const int pref_id = (int)APITools_GetIntValue(context, 3);

         APITools_SetIntValue(context, 0, capture->open(device_id, pref_id));
      }
   }

   // Opens a stream
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_open_id(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         const int device_id = (int)APITools_GetIntValue(context, 2);
         APITools_SetIntValue(context, 0, capture->open(device_id));
      }
   }

      // Opens a stream
#ifdef _WIN32
      __declspec(dllexport)
#endif
   void opencv_open_name(VMContext & context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         const std::wstring w_name = APITools_GetStringValue(context, 2);
         const std::string name = UnicodeToBytes(w_name);

         APITools_SetIntValue(context, 0, capture->open(name.c_str()));
      }
   }
   
   // Read in image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_read(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         cv::Mat image;
         capture->read(image);
         
         size_t* image_obj = opencv_raw_write(image, context);                  
         APITools_SetObjectValue(context, 0, image_obj);
      }
   }

   // Set video property
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_set(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         const int prop_id = (int)APITools_GetIntValue(context, 2);
         const double value = APITools_GetFloatValue(context, 3);

         APITools_SetFloatValue(context, 0, capture->set(prop_id, value));
      }
   }

   // Checks if video is opened
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_is_open(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 1);
      if(capture) {
         APITools_SetIntValue(context, 0, capture->isOpened());
      }
   }

   // Release video
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_release_video_capture(VMContext& context) {
      cv::VideoCapture* capture = (cv::VideoCapture*)APITools_GetIntValue(context, 0);
      if(capture) {
         capture->release();

         delete capture;
         capture = nullptr;
      }
   }

   // Load video
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_new_video_capture(VMContext& context) {
      const std::wstring w_video_path = APITools_GetStringValue(context, 1);
      const std::string video_path = UnicodeToBytes(w_video_path);

      cv::VideoCapture* capture = new cv::VideoCapture(video_path);
      if(capture->isOpened()) {
         APITools_SetIntValue(context, 0, (size_t)capture);
      }
   }

   // Load video
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_new_video_capture_prop(VMContext& context) {
      const std::wstring w_video_path = APITools_GetStringValue(context, 1);
      const std::string video_path = UnicodeToBytes(w_video_path);

      const int prop_id = (int)APITools_GetIntValue(context, 2);

      cv::VideoCapture* capture = new cv::VideoCapture(video_path, prop_id);
      APITools_SetIntValue(context, 0, (size_t)capture);
   }

   // Turn on device
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_new_video_capture_id(VMContext& context) {
      const int dev_id = (int)APITools_GetIntValue(context, 1);

      cv::VideoCapture* capture = new cv::VideoCapture(dev_id);
      if(capture->isOpened()) {
         APITools_SetIntValue(context, 0, (size_t)capture);
      }
   }

   // Turn on device
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_new_video_capture_prop_id(VMContext& context) {
      const int dev_id = (int)APITools_GetIntValue(context, 1);
      const int prop_id = (int)APITools_GetIntValue(context, 2);

      cv::VideoCapture* capture = new cv::VideoCapture(dev_id, prop_id);
      if(capture->isOpened()) {
         APITools_SetIntValue(context, 0, (size_t)capture);
      }
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

   // Determines if an image is empty
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_is_empty_image(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      
      cv::Mat image = opencv_raw_read(image_obj, context);
      APITools_SetIntValue(context, 0, image.empty());
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
      cv::waitKey(250);
   }

   // Display image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_show_image_ms(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 0);

      const std::wstring w_title = APITools_GetStringValue(context, 1);
      const std::string title = UnicodeToBytes(w_title);

      const double scaling = APITools_GetFloatValue(context, 2);

      const int wait = (int)APITools_GetIntValue(context, 3);

      cv::Mat image = opencv_raw_read(image_obj, context);

      // resize
      cv::Mat resized_image;
      cv::resize(image, resized_image, cv::Size(), scaling, scaling, cv::INTER_LINEAR);

      cv::imshow(title, resized_image);
      cv::waitKey(wait);
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

      if(!image_in_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_in_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      if(size_obj) {
         const long width = (long)size_obj[0];
         const long height = (long)size_obj[1];

         cv::resize(image, image, cv::Size(width, height), fx, fy, interpolation);
      }
      else {
         cv::resize(image, image, cv::Size(), fx, fy, interpolation);
      }

      size_t* image_out_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_out_obj);
   }

   //
   // --- Color Conversion ---
   //

   // Convert image color space
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_cvt_color(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int code = (int)APITools_GetIntValue(context, 2);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::cvtColor(image, result, code);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   //
   // --- Filtering ---
   //

   // Apply Gaussian blur to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_gaussian_blur(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int ksize_w = (int)APITools_GetIntValue(context, 2);
      const int ksize_h = (int)APITools_GetIntValue(context, 3);
      const double sigmaX = APITools_GetFloatValue(context, 4);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::GaussianBlur(image, result, cv::Size(ksize_w, ksize_h), sigmaX);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply median blur to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_median_blur(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int ksize = (int)APITools_GetIntValue(context, 2);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::medianBlur(image, result, ksize);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply bilateral filter to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_bilateral_filter(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int d = (int)APITools_GetIntValue(context, 2);
      const double sigmaColor = APITools_GetFloatValue(context, 3);
      const double sigmaSpace = APITools_GetFloatValue(context, 4);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::bilateralFilter(image, result, d, sigmaColor, sigmaSpace);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   //
   // --- Edge Detection ---
   //

   // Apply Canny edge detection
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_canny(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const double threshold1 = APITools_GetFloatValue(context, 2);
      const double threshold2 = APITools_GetFloatValue(context, 3);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::Canny(image, result, threshold1, threshold2);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply Sobel edge detection
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_sobel(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int ddepth = (int)APITools_GetIntValue(context, 2);
      const int dx = (int)APITools_GetIntValue(context, 3);
      const int dy = (int)APITools_GetIntValue(context, 4);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::Sobel(image, result, ddepth, dx, dy);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   //
   // --- Threshold ---
   //

   // Apply threshold to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_threshold(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const double thresh = APITools_GetFloatValue(context, 2);
      const double maxval = APITools_GetFloatValue(context, 3);
      const int type = (int)APITools_GetIntValue(context, 4);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::threshold(image, result, thresh, maxval, type);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply adaptive threshold to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_adaptive_threshold(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const double maxval = APITools_GetFloatValue(context, 2);
      const int adaptive_method = (int)APITools_GetIntValue(context, 3);
      const int threshold_type = (int)APITools_GetIntValue(context, 4);
      const int block_size = (int)APITools_GetIntValue(context, 5);
      const double C = APITools_GetFloatValue(context, 6);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::adaptiveThreshold(image, result, maxval, adaptive_method, threshold_type, block_size, C);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   //
   // --- Morphology ---
   //

   // Apply erosion to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_erode(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int shape = (int)APITools_GetIntValue(context, 2);
      const int ksize_w = (int)APITools_GetIntValue(context, 3);
      const int ksize_h = (int)APITools_GetIntValue(context, 4);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(ksize_w, ksize_h));
      cv::Mat result;
      cv::erode(image, result, kernel);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply dilation to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_dilate(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int shape = (int)APITools_GetIntValue(context, 2);
      const int ksize_w = (int)APITools_GetIntValue(context, 3);
      const int ksize_h = (int)APITools_GetIntValue(context, 4);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(ksize_w, ksize_h));
      cv::Mat result;
      cv::dilate(image, result, kernel);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply morphological operation to image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_morphology_ex(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int op = (int)APITools_GetIntValue(context, 2);
      const int shape = (int)APITools_GetIntValue(context, 3);
      const int ksize_w = (int)APITools_GetIntValue(context, 4);
      const int ksize_h = (int)APITools_GetIntValue(context, 5);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(ksize_w, ksize_h));
      cv::Mat result;
      cv::morphologyEx(image, result, op, kernel);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   //
   // --- Utility ---
   //

   // Save image to file
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_save_image(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const std::wstring w_path = APITools_GetStringValue(context, 2);
      const std::string path = UnicodeToBytes(w_path);

      if(!image_obj) {
         APITools_SetIntValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetIntValue(context, 0, 0);
         return;
      }

      const bool success = cv::imwrite(path, image);
      APITools_SetIntValue(context, 0, success ? 1 : 0);
   }

   // Flip image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_flip(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int flip_code = (int)APITools_GetIntValue(context, 2);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result;
      cv::flip(image, result, flip_code);

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Crop image using ROI
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_crop(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      size_t* rect_obj = APITools_GetObjectValue(context, 2);

      if(!image_obj || !rect_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      const long x = (long)rect_obj[0];
      const long y = (long)rect_obj[1];
      const long width = (long)rect_obj[2];
      const long height = (long)rect_obj[3];

      cv::Rect roi(x, y, width, height);
      cv::Mat result = image(roi).clone();

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Clone image (deep copy)
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_clone(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);

      if(!image_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat result = image.clone();

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
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

   //
   // --- Contour Detection ---
   //

   // Find contours in image
   // Returns Int[] with format: [count, size0, size1, ..., sizeN-1, x0, y0, x1, y1, ...]
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_find_contours(VMContext& context) {
      size_t* output_holder = APITools_GetArray(context, 0);

      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const int mode = (int)APITools_GetIntValue(context, 2);
      const int method = (int)APITools_GetIntValue(context, 3);

      if(!image_obj) {
         output_holder[0] = 0;
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         output_holder[0] = 0;
         return;
      }

      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(image, contours, mode, method);

      // Count total points
      size_t total_points = 0;
      for(const auto& contour : contours) {
         total_points += contour.size();
      }

      // Format: [count, size0, size1, ..., sizeN-1, x0, y0, x1, y1, ...]
      const size_t array_size = 1 + contours.size() + total_points * 2;
      size_t* int_array = APITools_MakeIntArray(context, array_size);

      // Set count
      APITools_SetIntArrayElement(int_array, 0, (long)contours.size());

      // Set sizes
      for(size_t i = 0; i < contours.size(); ++i) {
         APITools_SetIntArrayElement(int_array, 1 + i, (long)contours[i].size());
      }

      // Set flattened points
      size_t idx = 1 + contours.size();
      for(const auto& contour : contours) {
         for(const auto& pt : contour) {
            APITools_SetIntArrayElement(int_array, idx++, (long)pt.x);
            APITools_SetIntArrayElement(int_array, idx++, (long)pt.y);
         }
      }

      output_holder[0] = (size_t)int_array;
   }

   // Draw contours on image
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_draw_contours(VMContext& context) {
      size_t* image_in_obj = APITools_GetObjectValue(context, 1);
      size_t* contours_array = (size_t*)APITools_GetArray(context, 2)[0];
      const int contour_idx = (int)APITools_GetIntValue(context, 3);
      size_t* color_obj = APITools_GetObjectValue(context, 4);
      const int thickness = (int)APITools_GetIntValue(context, 5);

      if(!image_in_obj || !contours_array || !color_obj) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_in_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      // Deserialize contours from Int[]
      const long count = APITools_GetIntArrayElement(contours_array, 0);

      std::vector<std::vector<cv::Point>> contours(count);
      size_t idx = 1 + count;
      for(long i = 0; i < count; ++i) {
         const long sz = APITools_GetIntArrayElement(contours_array, 1 + i);
         contours[i].resize(sz);
         for(long j = 0; j < sz; ++j) {
            const long x = APITools_GetIntArrayElement(contours_array, idx++);
            const long y = APITools_GetIntArrayElement(contours_array, idx++);
            contours[i][j] = cv::Point(x, y);
         }
      }

      const double r = *((double*)&color_obj[0]);
      const double g = *((double*)&color_obj[1]);
      const double b = *((double*)&color_obj[2]);
      const double a = *((double*)&color_obj[3]);

      cv::drawContours(image, contours, contour_idx, cv::Scalar(r, g, b, a), thickness);

      size_t* image_out_obj = opencv_raw_write(image, context);
      APITools_SetObjectValue(context, 0, image_out_obj);
   }

   // Compute contour area
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_contour_area(VMContext& context) {
      size_t* contours_array = (size_t*)APITools_GetArray(context, 1)[0];
      const int index = (int)APITools_GetIntValue(context, 2);

      if(!contours_array) {
         APITools_SetFloatValue(context, 0, 0.0);
         return;
      }

      const long count = APITools_GetIntArrayElement(contours_array, 0);
      if(index < 0 || index >= count) {
         APITools_SetFloatValue(context, 0, 0.0);
         return;
      }

      // Skip to the target contour
      size_t idx = 1 + count;
      for(int i = 0; i < index; ++i) {
         const long sz = APITools_GetIntArrayElement(contours_array, 1 + i);
         idx += sz * 2;
      }

      const long sz = APITools_GetIntArrayElement(contours_array, 1 + index);
      std::vector<cv::Point> contour(sz);
      for(long j = 0; j < sz; ++j) {
         const long x = APITools_GetIntArrayElement(contours_array, idx++);
         const long y = APITools_GetIntArrayElement(contours_array, idx++);
         contour[j] = cv::Point(x, y);
      }

      const double area = cv::contourArea(contour);
      APITools_SetFloatValue(context, 0, area);
   }

   // Compute bounding rectangle for a contour
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_bounding_rect(VMContext& context) {
      size_t* contours_array = (size_t*)APITools_GetArray(context, 1)[0];
      const int index = (int)APITools_GetIntValue(context, 2);

      if(!contours_array) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      const long count = APITools_GetIntArrayElement(contours_array, 0);
      if(index < 0 || index >= count) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      // Skip to the target contour
      size_t idx = 1 + count;
      for(int i = 0; i < index; ++i) {
         const long sz = APITools_GetIntArrayElement(contours_array, 1 + i);
         idx += sz * 2;
      }

      const long sz = APITools_GetIntArrayElement(contours_array, 1 + index);
      std::vector<cv::Point> contour(sz);
      for(long j = 0; j < sz; ++j) {
         const long x = APITools_GetIntArrayElement(contours_array, idx++);
         const long y = APITools_GetIntArrayElement(contours_array, idx++);
         contour[j] = cv::Point(x, y);
      }

      cv::Rect rect = cv::boundingRect(contour);

      size_t* rect_obj = APITools_CreateObject(context, L"API.OpenCV.Rect");
      rect_obj[0] = rect.x;
      rect_obj[1] = rect.y;
      rect_obj[2] = rect.width;
      rect_obj[3] = rect.height;

      APITools_SetObjectValue(context, 0, rect_obj);
   }

   //
   // --- VideoWriter ---
   //

   // Create a new VideoWriter
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_new_video_writer(VMContext& context) {
      const std::wstring w_filename = APITools_GetStringValue(context, 1);
      const std::string filename = UnicodeToBytes(w_filename);
      const int fourcc = (int)APITools_GetIntValue(context, 2);
      const double fps = APITools_GetFloatValue(context, 3);
      const int width = (int)APITools_GetIntValue(context, 4);
      const int height = (int)APITools_GetIntValue(context, 5);

      cv::VideoWriter* writer = new cv::VideoWriter(filename, fourcc, fps, cv::Size(width, height));
      if(writer->isOpened()) {
         APITools_SetIntValue(context, 0, (size_t)writer);
      }
      else {
         delete writer;
         APITools_SetIntValue(context, 0, 0);
      }
   }

   // Write a frame to VideoWriter
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_writer_write(VMContext& context) {
      cv::VideoWriter* writer = (cv::VideoWriter*)APITools_GetIntValue(context, 0);
      size_t* image_obj = APITools_GetObjectValue(context, 1);

      if(writer && image_obj) {
         cv::Mat image = opencv_raw_read(image_obj, context);
         if(!image.empty()) {
            writer->write(image);
         }
      }
   }

   // Check if VideoWriter is opened
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_writer_is_open(VMContext& context) {
      cv::VideoWriter* writer = (cv::VideoWriter*)APITools_GetIntValue(context, 1);
      if(writer) {
         APITools_SetIntValue(context, 0, writer->isOpened() ? 1 : 0);
      }
      else {
         APITools_SetIntValue(context, 0, 0);
      }
   }

   // Release VideoWriter
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_writer_release(VMContext& context) {
      cv::VideoWriter* writer = (cv::VideoWriter*)APITools_GetIntValue(context, 0);
      if(writer) {
         writer->release();
         delete writer;
         writer = nullptr;
      }
   }

   // Compute FourCC code
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_video_writer_fourcc(VMContext& context) {
      const int c1 = (int)APITools_GetIntValue(context, 1);
      const int c2 = (int)APITools_GetIntValue(context, 2);
      const int c3 = (int)APITools_GetIntValue(context, 3);
      const int c4 = (int)APITools_GetIntValue(context, 4);

      const int fourcc = cv::VideoWriter::fourcc((char)c1, (char)c2, (char)c3, (char)c4);
      APITools_SetIntValue(context, 0, fourcc);
   }

   //
   // --- Geometric Transforms ---
   //

   // Get 2D rotation matrix
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_get_rotation_matrix_2d(VMContext& context) {
      size_t* output_holder = APITools_GetArray(context, 0);

      const double center_x = APITools_GetFloatValue(context, 1);
      const double center_y = APITools_GetFloatValue(context, 2);
      const double angle = APITools_GetFloatValue(context, 3);
      const double scale = APITools_GetFloatValue(context, 4);

      cv::Mat mat = cv::getRotationMatrix2D(cv::Point2f((float)center_x, (float)center_y), angle, scale);

      // Return 6 doubles (2x3 matrix flattened)
      size_t* float_array = APITools_MakeFloatArray(context, 6);
      double* float_array_buffer = reinterpret_cast<double*>(float_array + 3);
      for(int r = 0; r < 2; ++r) {
         for(int c = 0; c < 3; ++c) {
            float_array_buffer[r * 3 + c] = mat.at<double>(r, c);
         }
      }

      output_holder[0] = (size_t)float_array;
   }

   // Apply affine transformation
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_warp_affine(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      size_t* matrix_array = (size_t*)APITools_GetArray(context, 2)[0];
      const int width = (int)APITools_GetIntValue(context, 3);
      const int height = (int)APITools_GetIntValue(context, 4);

      if(!image_obj || !matrix_array) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      // Reconstruct 2x3 matrix from Float[6]
      cv::Mat mat(2, 3, CV_64F);
      for(int r = 0; r < 2; ++r) {
         for(int c = 0; c < 3; ++c) {
            mat.at<double>(r, c) = APITools_GetFloatArrayElement(matrix_array, r * 3 + c);
         }
      }

      cv::Mat result;
      cv::warpAffine(image, result, mat, cv::Size(width, height));

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   // Apply perspective transformation
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_warp_perspective(VMContext& context) {
      size_t* image_obj = APITools_GetObjectValue(context, 1);
      size_t* matrix_array = (size_t*)APITools_GetArray(context, 2)[0];
      const int width = (int)APITools_GetIntValue(context, 3);
      const int height = (int)APITools_GetIntValue(context, 4);

      if(!image_obj || !matrix_array) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         APITools_SetObjectValue(context, 0, 0);
         return;
      }

      // Reconstruct 3x3 matrix from Float[9]
      cv::Mat mat(3, 3, CV_64F);
      for(int r = 0; r < 3; ++r) {
         for(int c = 0; c < 3; ++c) {
            mat.at<double>(r, c) = APITools_GetFloatArrayElement(matrix_array, r * 3 + c);
         }
      }

      cv::Mat result;
      cv::warpPerspective(image, result, mat, cv::Size(width, height));

      size_t* out_obj = opencv_raw_write(result, context);
      APITools_SetObjectValue(context, 0, out_obj);
   }

   //
   // --- Image Normalization ---
   //

   // Normalize image for neural network preprocessing
#ifdef _WIN32
   __declspec(dllexport)
#endif
   void opencv_normalize(VMContext& context) {
      size_t* output_holder = APITools_GetArray(context, 0);

      size_t* image_obj = APITools_GetObjectValue(context, 1);
      const double mean_r = APITools_GetFloatValue(context, 2);
      const double mean_g = APITools_GetFloatValue(context, 3);
      const double mean_b = APITools_GetFloatValue(context, 4);
      const double std_r = APITools_GetFloatValue(context, 5);
      const double std_g = APITools_GetFloatValue(context, 6);
      const double std_b = APITools_GetFloatValue(context, 7);

      if(!image_obj) {
         output_holder[0] = 0;
         return;
      }

      cv::Mat image = opencv_raw_read(image_obj, context);
      if(image.empty()) {
         output_holder[0] = 0;
         return;
      }

      // Convert to float and normalize to [0, 1]
      cv::Mat float_img;
      image.convertTo(float_img, CV_32F, 1.0 / 255.0);

      // Convert BGR to RGB
      cv::cvtColor(float_img, float_img, cv::COLOR_BGR2RGB);

      // Split channels
      std::vector<cv::Mat> channels(3);
      cv::split(float_img, channels);

      // Normalize: (channel - mean) / std
      channels[0] = (channels[0] - (float)mean_r) / (float)std_r;
      channels[1] = (channels[1] - (float)mean_g) / (float)std_g;
      channels[2] = (channels[2] - (float)mean_b) / (float)std_b;

      // Flatten to CHW format
      const size_t total = float_img.rows * float_img.cols * 3;
      size_t* float_array = APITools_MakeFloatArray(context, total);
      double* float_array_buffer = reinterpret_cast<double*>(float_array + 3);

      size_t offset = 0;
      for(const auto& channel : channels) {
         for(int r = 0; r < channel.rows; ++r) {
            for(int c = 0; c < channel.cols; ++c) {
               float_array_buffer[offset++] = (double)channel.at<float>(r, c);
            }
         }
      }

      output_holder[0] = (size_t)float_array;
   }
}