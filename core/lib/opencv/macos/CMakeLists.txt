# macOS build of the two OpenCV-based native libraries, libobjk_opencv and
# libobjk_onnx, against the static OpenCV, the prebuilt ONNX Runtime and the
# mbedTLS headers that tools/deps/build_macos_deps.sh puts in
# <OBJECK_DEPS>/macos-bundle. Nothing is found through Homebrew: before
# v2026.9.3 these libraries linked Homebrew's opencv@4 (.414 dylibs, 57 of them
# for the ONNX binding) and onnxruntime by absolute path, and did not load on a
# Mac without those formulas.
#
#   cmake -S core/lib/opencv/macos -B <build> -DOBJECK_DEPS=<prefix> -DCMAKE_BUILD_TYPE=Release
#   cmake --build <build>
#
# Produces libobjk_opencv.dylib (links OpenCV statically) and libobjk_onnx.dylib
# (links OpenCV statically, loads @rpath/libonnxruntime.1.dylib from beside itself).
cmake_minimum_required(VERSION 3.20)
project(objk_opencv_bindings CXX)

set(CMAKE_OSX_ARCHITECTURES arm64)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3)
endif()
if(NOT OBJECK_DEPS)
  message(FATAL_ERROR "Pass -DOBJECK_DEPS=<prefix> (the tools/deps/build_macos_deps.sh prefix)")
endif()

set(BUNDLE "${OBJECK_DEPS}/macos-bundle")
set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
foreach(required "${BUNDLE}/opencv/lib/cmake/opencv4/OpenCVConfig.cmake"
                 "${BUNDLE}/onnxruntime/lib/libonnxruntime.1.dylib"
                 "${BUNDLE}/mbedtls/include/mbedtls/ssl.h")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "${required} not found; run tools/deps/build_macos_deps.sh")
  endif()
endforeach()

set(OpenCV_STATIC ON)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui calib3d dnn
  PATHS "${BUNDLE}/opencv/lib/cmake/opencv4" NO_DEFAULT_PATH)

# Both sources include lib_api.h -> vm/common.h, which includes mbedTLS headers:
# they must be the headers of the mbedTLS the VM links.
set(MBEDTLS_INCLUDE "${BUNDLE}/mbedtls/include")

add_library(objk_opencv SHARED "${REPO}/core/lib/opencv/opencv.cpp")
set_target_properties(objk_opencv PROPERTIES
  CXX_STANDARD 20 CXX_EXTENSIONS ON
  PREFIX "lib" SUFFIX ".dylib"
  INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR ON SKIP_BUILD_RPATH ON)
target_include_directories(objk_opencv PRIVATE "${MBEDTLS_INCLUDE}")
target_link_libraries(objk_opencv PRIVATE ${OpenCV_LIBS})

add_library(objk_onnx SHARED "${REPO}/core/lib/onnx/eq/onnx.cpp")
set_target_properties(objk_onnx PROPERTIES
  CXX_STANDARD 17
  PREFIX "lib" SUFFIX ".dylib"
  INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR ON
  BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@loader_path")
target_compile_definitions(objk_onnx PRIVATE ONNX_EP_COREML)
target_compile_options(objk_onnx PRIVATE -Wno-unused-function -Wno-deprecated-declarations)
target_include_directories(objk_onnx PRIVATE "${BUNDLE}/onnxruntime/include" "${MBEDTLS_INCLUDE}")
target_link_libraries(objk_onnx PRIVATE ${OpenCV_LIBS} "${BUNDLE}/onnxruntime/lib/libonnxruntime.1.dylib")
