SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-gcc.exe")
SET(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/arm-linux-gnueabihf-g++.exe")
SET(CMAKE_SYSROOT "$ENV{TOOLCHAIN_ROOT}/arm-linux-gnueabihf/sysroot")
