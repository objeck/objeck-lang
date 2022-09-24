#!/bin/bash

rm -f *.tar *.gz *.tgz
rm -rf native 
mkdir native 

pushd ../openssl
./build_linux.sh openssl 
mv openssl.so libobjk_openssl.so
cp *.so ../linux_arm64/native
cd ../sdl
./build_linux.sh sdl
mv sdl.so libobjk_sdl.so
cp *.so ../linux_arm64/native
cd ../diags
./build_linux.sh diags
mv diags.so libobjk_diags.so
cp *.so ../linux_arm64/native
popd

tar -cvf linux_arm64_native.tar native
gzip linux_arm64_native.tar
mv linux_arm64_native.tar.gz linux_arm64_native.tgz

git commit -m "updated rpi4 libs" .
git push
