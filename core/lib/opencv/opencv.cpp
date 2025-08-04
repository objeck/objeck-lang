#include "opencv.h"

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
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
  // Process image using OpenCV
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_process_image(VMContext& context) {
  }

  //
// Load image from memory
//
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_load_image_bytes(VMContext& context) {
    // get parameters
    size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long input_size = ((long)APITools_GetArraySize(input_array));
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

  //
  // Load image from file
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_load_image_path(VMContext& context) {
    const std::wstring w_image_path = APITools_GetStringValue(context, 1);
    const std::string image_path = UnicodeToBytes(w_image_path);

    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if(image.empty()) {
      APITools_SetIntValue(context, 0, 0);
      return;
		}

    size_t* image_obj = opencv_raw_write(image, context);
    APITools_SetObjectValue(context, 0, image_obj);
  }

  //
  // Display image
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_show_image(VMContext& context) {
    size_t* image_obj = APITools_GetObjectValue(context, 0);

    const std::wstring w_title = APITools_GetStringValue(context, 1);
    const std::string title = UnicodeToBytes(w_title);

    cv::Mat image = opencv_raw_read(image_obj, context);

    cv::imshow(title, image);
    cv::waitKey(0);
  }

  //
  // Draw a rectangle on image
  //
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
      return;
    }

    cv::Mat image = opencv_raw_read(image_in_obj, context);
    if(image.empty()) {
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

  //
  // Convert an image from one format to another
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_convert_image(VMContext& context) {
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

