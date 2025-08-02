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
  // Convert an image from one format to another
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_load_image_path(VMContext& context) {
    const std::wstring w_image_path = APITools_GetStringValue(context, 1);
    const  std::string image_path = UnicodeToBytes(w_image_path);

    cv::Mat image = cv::imread(image_path, cv::IMREAD_COLOR);
    if(image.empty()) {
      APITools_SetIntValue(context, 0, 0);
      return;
		}

    std::vector<unsigned char*> image_bytes(image.total() * image.elemSize());
    std::memcpy(image_bytes.data(), image.data, image_bytes.size());

    // Copy results
    size_t* output_byte_buffer = APITools_MakeByteArray(context, output_image_bytes.size());
    unsigned char* output_byte_array_buffer = (unsigned char*)(output_byte_buffer + 3);

    for(size_t i = 0; i < output_image_bytes.size(); ++i) {
      output_byte_array_buffer[i] = output_image_bytes[i];
    }

    output_holder[0] = (size_t)output_byte_buffer;
  }

  //
  // Convert an image from one format to another
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void opencv_convert_image(VMContext& context) {
    // Get parameters
    size_t* output_holder = APITools_GetArray(context, 0);

    size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long input_size = ((long)APITools_GetArraySize(input_array));
    const unsigned char* input_bytes = (unsigned char*)APITools_GetArray(input_array);

    const int output_format = (int)APITools_GetIntValue(context, 2);

    // convert image
    std::vector<unsigned char> output_image_bytes = convert_image_bytes(context, input_bytes, input_size, output_format);

    // Copy results
    size_t* output_byte_buffer = APITools_MakeByteArray(context, output_image_bytes.size());
    unsigned char* output_byte_array_buffer = (unsigned char*)(output_byte_buffer + 3);

    for(size_t i = 0; i < output_image_bytes.size(); ++i) {
      output_byte_array_buffer[i] = output_image_bytes[i];
    }

    output_holder[0] = (size_t)output_byte_buffer;
  }
}

