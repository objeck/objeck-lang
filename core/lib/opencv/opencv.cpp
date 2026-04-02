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
         const bool result = capture->read(image);
         
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
}