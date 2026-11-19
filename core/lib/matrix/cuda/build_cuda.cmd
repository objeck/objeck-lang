@echo off
rem Builds libobjk_ml_cuda.dll, the OPTIONAL GPU backend of System.ML.Cnn. Not part of CI, the
rem deploy scripts or a release; run it by hand on a machine with an NVIDIA GPU.
rem
rem   build_cuda.cmd [output-dir]          (default: a "build" folder next to this script)
rem
rem Needs: the CUDA Toolkit 12.x or 13.x (nvcc and cuBLAS; no cuDNN) and a Visual Studio C++ toolset
rem that nvcc accepts. CUDA 13.1 accepts MSVC 2019 and 2022 (_MSC_VER below 1950) and REFUSES the
rem v145 toolset of Visual Studio 2026. That IDE still ships the v143 toolset, so this script selects
rem it (VC_TOOLSET, default 14.4) rather than passing -allow-unsupported-compiler.
rem
rem Environment (all optional): CUDA_PATH (set by the toolkit installer), VCVARSALL (full path of
rem vcvarsall.bat), VC_TOOLSET (vcvars_ver prefix, default 14.4), CUDA_ARCHS (SM numbers, default
rem "75 80 86 89 90 120"; the last one also gets PTX for newer GPUs).
setlocal enabledelayedexpansion
set "HERE=%~dp0"
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%HERE%build"
if "%CUDA_ARCHS%"=="" set "CUDA_ARCHS=75 80 86 89 90 120"
if "%VC_TOOLSET%"=="" set "VC_TOOLSET=14.4"

where nvcc >nul 2>&1
if errorlevel 1 (
  if defined CUDA_PATH set "PATH=%CUDA_PATH%\bin;%PATH%"
)
where nvcc >nul 2>&1
if errorlevel 1 (
  echo nvcc not found: install the CUDA Toolkit and set CUDA_PATH, or put its bin folder on PATH
  exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
  if not defined VCVARSALL (
    for %%E in (Enterprise Professional Community BuildTools) do (
      for %%V in (18 2022) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" if not defined VCVARSALL set "VCVARSALL=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat"
      )
    )
  )
  if not defined VCVARSALL (
    echo No Visual Studio C++ toolset found: set VCVARSALL to the path of vcvarsall.bat
    exit /b 1
  )
  call "!VCVARSALL!" amd64 -vcvars_ver=%VC_TOOLSET% >nul
  if errorlevel 1 (
    echo vcvarsall rejected -vcvars_ver=%VC_TOOLSET%: install the "MSVC v143 build tools" component, or set VC_TOOLSET
    exit /b 1
  )
)

set "GENCODE="
set "LAST="
for %%A in (%CUDA_ARCHS%) do (
  set "GENCODE=!GENCODE! -gencode arch=compute_%%A,code=sm_%%A"
  set "LAST=%%A"
)
set "GENCODE=!GENCODE! -gencode arch=compute_!LAST!,code=compute_!LAST!"

if not exist "%OUT%" mkdir "%OUT%"
nvcc -O3 -shared -o "%OUT%\libobjk_ml_cuda.dll" "%HERE%objk_ml_cuda.cu" !GENCODE! -lcublas
if errorlevel 1 (
  echo build failed
  exit /b 1
)
echo Built %OUT%\libobjk_ml_cuda.dll
echo Copy it next to libobjk_ml.dll, or point the environment variable OBJK_CUDA_LIB at it.
echo At run time cublas64_*.dll must be found: the CUDA bin folder is searched through CUDA_PATH.
endlocal
