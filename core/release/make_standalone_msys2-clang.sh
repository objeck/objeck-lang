#!/bin/sh

# copy dlls
cp /clang64/bin/libc++.dll deploy-msys2-clang/bin
cp /clang64/bin/libunwind.dll deploy-msys2-clang/bin
cp /clang64/bin/libwinpthread-1.dll deploy-msys2-clang/bin
cp /clang64/bin/zlib1.dll deploy-msys2-clang/bin
cp /clang64/bin/libmbedtls.dll deploy-msys2-clang/bin
cp /clang64/bin/libmbedx509.dll deploy-msys2-clang/bin
cp /clang64/bin/libmbedcrypto.dll deploy-msys2-clang/bin
cp /clang64/bin/libodbc-2.dll deploy-msys2-clang/bin
cp /clang64/bin/libiconv-2.dll deploy-msys2-clang/bin
cp /clang64/bin/libltdl-7.dll deploy-msys2-clang/bin
cp /clang64/bin/SDL2.dll deploy-msys2-clang/bin
cp /clang64/bin/SDL2_image.dll deploy-msys2-clang/bin
cp /clang64/bin/SDL2_mixer.dll deploy-msys2-clang/bin
cp /clang64/bin/SDL2_ttf.dll deploy-msys2-clang/bin
cp /clang64/bin/libjpeg-8.dll deploy-msys2-clang/bin
cp /clang64/bin/libjxl.dll deploy-msys2-clang/bin
cp /clang64/bin/libpng16-16.dll deploy-msys2-clang/bin
cp /clang64/bin/libtiff-6.dll deploy-msys2-clang/bin
cp /clang64/bin/libwebp-7.dll deploy-msys2-clang/bin
cp /clang64/bin/libmpg123-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libopusfile-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libfreetype-6.dll deploy-msys2-clang/bin
cp /clang64/bin/libharfbuzz-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libbrotlidec.dll deploy-msys2-clang/bin
cp /clang64/bin/libbrotlienc.dll deploy-msys2-clang/bin
cp /clang64/bin/libhwy.dll deploy-msys2-clang/bin
cp /clang64/bin/liblcms2-2.dll deploy-msys2-clang/bin
cp /clang64/bin/libdeflate.dll deploy-msys2-clang/bin
cp /clang64/bin/libjbig-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libLerc.dll deploy-msys2-clang/bin
cp /clang64/bin/liblzma-5.dll deploy-msys2-clang/bin
cp /clang64/bin/libzstd.dll deploy-msys2-clang/bin
cp /clang64/bin/libsharpyuv-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libogg-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libopus-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libbz2-1.dll deploy-msys2-clang/bin
cp /clang64/bin/libglib-2.0-0.dll deploy-msys2-clang/bin
cp /clang64/bin/libgraphite2.dll deploy-msys2-clang/bin
cp /clang64/bin/libintl-8.dll deploy-msys2-clang/bin
cp /clang64/bin/libbrotlicommon.dll deploy-msys2-clang/bin
cp /clang64/bin/libpcre2-8-0.dll deploy-msys2-clang/bin

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-msys2-clang ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-utils-msys2-x64_0.0.0.tgz
fi
