# ONNX and OpenCV Support

##
Build 

### Windows
Build with Visual Studio 2022

11. Enable Onnx support via VS Studio Nuget (Tools -> Package Manager -> Settings)
1. Add local Nuget package and include ``Microsoft.ML.OnnxRuntime.DirectML.1.22.1.nupkg``
1. Got to the project ``Manage Nuget Packages`` and ensure the following packages are included
![alt text](../../../docs/images/vs-nuget-pckgs.png "Nuget build options")

1. Download OpenCV ``https://opencv.org/releases/`` and CMake to build it ``https://cmake.org/download/``
1. Unzip the source and use the CMake GUI to set the source and build directories, and click Configure
1. Change the following configuration options and then click Generate
  2. CPU_BASELINE = FP16
  2. CPU_DISPATH = FP16
  2. NEON_INTRINSICS = checked
  3. PNG_ARM_NEON = off
1. Compile the build target (modules -> opencv_world) and build 
1. Build output will be the libraries and runtime DLLs, copy to the appropriate locations 