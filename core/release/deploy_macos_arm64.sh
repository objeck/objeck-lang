#!/bin/sh

# In CI, use the installed certificate; locally, use default keychain
SIGN_FLAGS=""
if [ "${CI}" = "true" ]; then
	KEYCHAIN_PATH="$RUNNER_TEMP/app-signing.keychain-db"
	if [ -f "$KEYCHAIN_PATH" ]; then
		# Xcode projects are configured for "Mac Development" signing.
		# Check if that specific identity exists in the CI keychain.
		HAS_MAC_DEV=$(security find-identity -v -p codesigning "$KEYCHAIN_PATH" 2>/dev/null | grep -c "Mac Development" || true)
		if [ "$HAS_MAC_DEV" -gt 0 ]; then
			echo "Found Mac Development identity - using keychain signing"
			SIGN_FLAGS="OTHER_CODE_SIGN_FLAGS=--keychain=$KEYCHAIN_PATH"
		else
			echo "No Mac Development identity in keychain - using ad-hoc signing"
			SIGN_FLAGS="CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO"
		fi
	else
		SIGN_FLAGS="CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO"
	fi
fi

# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/lib
mkdir deploy/lib/sdl
mkdir deploy/lib/sdl/fonts
mkdir deploy/lib/native
mkdir deploy/lib/native/misc
mkdir deploy/app
mkdir deploy/doc

# build compiler
cd ../compiler
xcodebuild -project xcode/Compiler.xcodeproj clean build $SIGN_FLAGS
cp xcode/build/Release/obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../lib/*.ini ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib

# build VM
cd ../vm
# The static QUIC libraries from tools/deps/build_quic_deps.sh, passed explicitly
# rather than left to the project's $HOME default.
OBJECK_DEPS="${OBJECK_DEPS:-$HOME/objeck-deps/darwin-arm64}"
if [ ! -f "$OBJECK_DEPS/lib/libngtcp2.a" ]; then
	echo "ERROR: QUIC libraries not found in $OBJECK_DEPS; run tools/deps/build_quic_deps.sh"
	exit 1
fi
xcodebuild -project xcode/VM.xcodeproj clean build $SIGN_FLAGS OBJECK_DEPS="$OBJECK_DEPS"
cp xcode/build/Release/obr ../release/deploy/bin
# obr must start on a Mac with nothing installed. v2026.6.3 through v2026.9.2
# linked Homebrew's GnuTLS, nghttp2, nghttp3 and ngtcp2 plus a hand-built ngtcp2
# backend, and aborted at launch on every Mac but the one that built it.
if otool -L ../release/deploy/bin/obr | tail -n +2 | awk '{print $1}' | grep -qE '^(/opt/homebrew|/usr/local)/'; then
	echo "ERROR: obr links libraries a user's Mac does not have:"
	otool -L ../release/deploy/bin/obr | grep -E '/opt/homebrew|/usr/local'
	exit 1
fi

# build debugger
cd ../debugger
xcodebuild -project xcode/Debugger.xcodeproj clean build $SIGN_FLAGS
cp xcode/build/Release/obd ../release/deploy/bin

# build module library
cd ../module
xcodebuild -project xcode/module.xcodeproj clean build $SIGN_FLAGS

# build repl
cd ../repl
xcodebuild -project xcode/repl.xcodeproj clean build $SIGN_FLAGS
cp xcode/build/Release/obi ../release/deploy/bin

# build native launcher
cd ../utils/launcher
xcodebuild -project "xcode/Native Launcher.xcodeproj" -target obb clean build $SIGN_FLAGS
cp xcode/build/Release/obb ../../release/deploy/bin

xcodebuild -project "xcode/Native Launcher.xcodeproj" -target obn clean build $SIGN_FLAGS
cp xcode/build/Release/obn ../../release/deploy/lib/native/misc
cp ../../vm/misc/config.prop ../../release/deploy/lib/native/misc

# build updater
# obu has no Xcode project, so it builds from its portable arm64 makefile. That
# leaves it signed exactly as the Xcode-built binaries are here: in CI the
# keychain holds Developer ID, not "Mac Development", so deploy falls back to
# ad-hoc signing for all of them. The .pkg is signed as a whole by productsign.
cd ../updater
make -f make/Makefile.arm64 clean; make -f make/Makefile.arm64 -j3
cp obu ../../release/deploy/bin
# No 'set -e' here, so a failed make or cp would silently yield a tree with no
# obu -- how v2026.8.0 shipped without it. Fail loudly instead.
if [ ! -f ../../release/deploy/bin/obu ]; then
	echo "ERROR: obu was not built or copied - aborting deploy"
	exit 1
fi
cd ../launcher

# build libraries
cd ../../lib/crypto
xcodebuild -project macos/xcode/objk_crypto.xcodeproj clean build $SIGN_FLAGS
cp macos/xcode/build/Release/libobjk_crypto.dylib ../../release/deploy/lib/native/libobjk_crypto.dylib

cd ../sdl
xcodebuild -project macos/xcode/sdl.xcodeproj build $SIGN_FLAGS
cp macos/xcode/build/Release/libxcode.dylib ../../release/deploy/lib/native/libobjk_sdl.dylib
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

# Ship SDL2 INSIDE the distribution, the way Windows ships its DLLs beside the
# binaries, instead of handing the user a tarball to install by hand.
#
# Why this dance is needed: the vendored dylibs were built with an absolute
# install name (LC_ID_DYLIB = /usr/local/lib/libSDL2-2.0.0.dylib), so anything
# linking them records that absolute path and dyld looks nowhere else. That is
# the only reason the old README told macOS users to untar sdl2_arm64.tgz and
# copy SDL2 into /usr/local/lib -- a sudo-level system install that also
# collides with a Homebrew SDL2. Rewriting the install names to @rpath and
# giving libobjk_sdl.dylib an rpath into the distro removes the step entirely.
# -P: the unversioned names (libSDL2.dylib, libSDL2_image.dylib, ...) are symlinks
# in the repo. A plain cp dereferenced them into four duplicate files that got
# their own install names, were loaded by nothing, and could not resolve their
# own @rpath/libSDL2-2.0.0.dylib dependency.
cp -P macos/arm64/lib/libSDL2*.dylib ../../release/deploy/lib/sdl

SDL_DEPLOY="../../release/deploy/lib/sdl"
OBJK_SDL="../../release/deploy/lib/native/libobjk_sdl.dylib"

# install_name_tool invalidates the code signature, and on Apple Silicon dyld
# refuses to load a Mach-O whose signature does not match. Re-sign ad-hoc after
# every rewrite; that is sufficient for a locally distributed dylib, and the
# release build re-signs properly afterwards when an identity is available.
resign() {
	codesign --force --sign - "$1" 2>/dev/null || \
		echo "  warning: could not re-sign $1"
}

for dylib in "$SDL_DEPLOY"/libSDL2*.dylib; do
	[ -f "$dylib" ] || continue
	# skip the version symlinks; only real Mach-O files need rewriting
	if [ -L "$dylib" ]; then
		continue
	fi
	base=$(basename "$dylib")
	install_name_tool -id "@rpath/$base" "$dylib" 2>/dev/null

	# the satellite libraries (image/mixer/ttf) link libSDL2 by that same
	# absolute path, so repoint those too
	for dep in $(otool -L "$dylib" | awk '/\/usr\/local\/lib\/libSDL2/ {print $1}'); do
		install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$dylib" 2>/dev/null
	done
	resign "$dylib"
done

# The Xcode target still produces "libxcode.dylib", so the copy we ship carries
# an install name of /usr/local/lib/libxcode.dylib -- a path that does not exist
# and never will. Nothing resolves through it today (the VM dlopens this library
# by absolute path, which ignores LC_ID_DYLIB), but it makes otool output read
# as though the tree still depends on /usr/local/lib. Correct it so the only
# absolute paths left in the distribution are real system ones.
install_name_tool -id "@rpath/libobjk_sdl.dylib" "$OBJK_SDL" 2>/dev/null

# libobjk_sdl.dylib lives in lib/native, so lib/sdl is one directory across
for dep in $(otool -L "$OBJK_SDL" | awk '/\/usr\/local\/lib\/libSDL2/ {print $1}'); do
	install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$OBJK_SDL" 2>/dev/null
done
install_name_tool -add_rpath "@loader_path/../sdl" "$OBJK_SDL" 2>/dev/null
resign "$OBJK_SDL"

# Fail loudly rather than shipping a tree that cannot load SDL. Any remaining
# /usr/local/lib reference means the user would still need a manual install.
if otool -L "$OBJK_SDL" | grep -q "/usr/local/lib/libSDL2"; then
	echo "ERROR: libobjk_sdl.dylib still references /usr/local/lib for SDL2;"
	echo "       the distribution would need a manual SDL2 install."
	exit 1
fi

# The absence of absolute paths is not the same as the libraries being present.
# Check that every @rpath dependency actually resolves inside the tree, or the
# distribution still fails at load time on a machine without Homebrew SDL2 --
# which is exactly the machine we cannot test on here.
MISSING=""
for dep in $(otool -L "$OBJK_SDL" | awk '/@rpath\/libSDL2/ {print $1}'); do
	base=$(basename "$dep")
	if [ ! -f "$SDL_DEPLOY/$base" ]; then
		MISSING="$MISSING $base"
	fi
done
if [ -n "$MISSING" ]; then
	echo "ERROR: libobjk_sdl.dylib needs these via @rpath but they are not in"
	echo "       lib/sdl:$MISSING"
	exit 1
fi
echo "SDL2 bundled into lib/sdl with @rpath install names, all deps resolve in-tree"

cd ../odbc
xcodebuild -project macos/xcode/ODBC.xcodeproj clean build $SIGN_FLAGS
cp macos/xcode/build/Release/libobjk_odbc.dylib ../../release/deploy/lib/native/libobjk_odbc.dylib

cd ../lame
xcodebuild -project macos/lame.xcodeproj clean build $SIGN_FLAGS
cp macos/build/Release/libobjk_lame.dylib ../../release/deploy/lib/native/libobjk_lame.dylib

cd ../opencv
xcodebuild -project macos/objk_opencv.xcodeproj -target objk_opencv clean build $SIGN_FLAGS
cp macos/build/Release/libobjk_opencv.dylib ../../release/deploy/lib/native/libobjk_opencv.dylib

cd ../matrix
xcodebuild -project macos/xcode/matrix.xcodeproj clean build $SIGN_FLAGS
cp macos/xcode/build/Release/libxcode.dylib ../../release/deploy/lib/native/libobjk_ml.dylib

cd ../diags
xcodebuild -project macos/xcode/objk_diags.xcodeproj clean build $SIGN_FLAGS
cp macos/xcode/build/Release/libobjk_diags.dylib ../../release/deploy/lib/native/libobjk_diags.dylib

cd ../onnx/eq
./build.sh coreml
cp libobjk_onnx.dylib ../../../release/deploy/lib/native/libobjk_onnx.dylib

# Every native library this script builds must be present (and, on Linux,
# resolvable) before the tree is packaged. Without this a failed build was
# dropped from the release with a green exit -- the obu incident, again. The
# result has to be acted on: this script has no 'set -e', and before the
# '|| exit 1' below v2026.9.1 printed "2 native-library verification failure(s)"
# (onnx, opencv) and shipped its .pkg, .zip and .tgz without them. All eight are
# required on macOS; see verify_native_libs.sh.
sh ../../../release/verify_native_libs.sh ../../../release/deploy/lib/native dylib crypto diags lame ml odbc onnx opencv sdl || exit 1

# Every binding gets the same install name, @rpath/<its file name>, and the
# deploy fails if one does not. Each came out of its build with whatever its
# project happened to say: libobjk_opencv and libobjk_lame carried the CI
# runner's build directory (/Users/runner/work/objeck-lang/...), libobjk_ml the
# Xcode template's /usr/local/lib/libxcode.dylib, crypto, diags and odbc
# /usr/local/lib/<name>, and onnx a bare name. v2026.9.2 shipped all of them:
# only libobjk_sdl was ever corrected (above), and nothing checked the rest.
# The VM dlopens a binding by absolute path, which ignores LC_ID_DYLIB, so
# loading never broke; the release leaked build paths, and otool read as though
# the tree depended on /usr/local/lib. Rewriting an install name invalidates the
# signature, so each binding is re-signed ad-hoc and its signature verified, as
# the SDL2 block does; the release build signs the whole tree afterwards.
NATIVE_DEPLOY="../../../release/deploy/lib/native"
BAD_NATIVE=""
for name in crypto diags lame ml odbc onnx opencv sdl; do
	base="libobjk_$name.dylib"
	dylib="$NATIVE_DEPLOY/$base"
	install_name_tool -id "@rpath/$base" "$dylib" 2>/dev/null
	resign "$dylib"
	id=$(otool -D "$dylib" 2>/dev/null | sed -n '2p')
	if [ "$id" != "@rpath/$base" ]; then
		BAD_NATIVE="$BAD_NATIVE
       $base: install name ${id:-(none)}"
	elif ! codesign --verify --strict "$dylib" 2>/dev/null; then
		BAD_NATIVE="$BAD_NATIVE
       $base: signature does not verify after the rewrite"
	fi
done
if [ -n "$BAD_NATIVE" ]; then
	echo "ERROR: native libraries not left at @rpath/<name> with a valid signature:$BAD_NATIVE"
	exit 1
fi
echo "Native libraries carry @rpath install names and verify: crypto diags lame ml odbc onnx opencv sdl"
cd ..

# build macOS app launcher (.app bundle)
cd ../../utils/MacApp
swiftc -O -o AppLauncher AppLauncher.swift -framework AppKit

APP_BUNDLE=../../release/deploy/app/Objeck.app
mkdir -p "$APP_BUNDLE/Contents/MacOS"
mkdir -p "$APP_BUNDLE/Contents/Resources"
cp AppLauncher "$APP_BUNDLE/Contents/MacOS/"
cp Info.plist "$APP_BUNDLE/Contents/"
cp ../../../docs/images/Gear.icns "$APP_BUNDLE/Contents/Resources/"
rm -f AppLauncher

# copy docs
cd ../../../
cp -R docs/syntax core/release/deploy/doc/syntax
cp docs/readme.html core/release/deploy
cp docs/style/readme.css core/release/deploy/doc
cp LICENSE core/release/deploy

# Ship the dependency installer INSIDE the distribution. Linux links
# libobjk_sdl.so against the system SDL2 and libGL and ships neither, so the
# person who needs this script is precisely the person who downloaded a tarball
# and never cloned the repo.
cp tools/install_deps.sh core/release/deploy
chmod +x core/release/deploy/install_deps.sh
unzip docs/api.zip -d core/release/deploy/doc

# copy examples
mkdir core/release/deploy/examples
mkdir core/release/deploy/examples/media
cp programs/deploy/*.obs core/release/deploy/examples
cp programs/deploy/README.md core/release/deploy/examples
# The OpenGL examples live in programs/examples, which nothing copied, so no
# distribution ever carried them.
mkdir -p core/release/deploy/examples/opengl
cp programs/examples/gl_*.obs core/release/deploy/examples/opengl
cp programs/examples/cube_gl.obs core/release/deploy/examples/opengl
cp programs/examples/gl_crystal.obj core/release/deploy/examples/opengl
# The data files two examples read. Windows has copied these since forever
# (deploy_windows.cmd), but neither POSIX script did, so json_stream_23.obs
# ("data/weather.json") and neural_21.obs ("data/gender.csv") failed with a
# file-not-found on every Linux and macOS distribution.
mkdir -p core/release/deploy/examples/data
cp programs/deploy/data/* core/release/deploy/examples/data
cp programs/deploy/media/*.png core/release/deploy/examples/media
cp programs/deploy/media/*.wav core/release/deploy/examples/media
sh core/release/verify_example_assets.sh core/release/deploy/examples || exit 1

# Every Mach-O file in the finished tree, the app launcher included, must load
# its libraries from the tree or macOS alone. The checks above read obr's
# absolute links and the bindings' install names; this one follows LC_RPATH and
# @rpath links, the way v2026.9.2's obr reached a library only its build machine
# had. It runs once the tree is assembled and before anything signs or archives
# it, so it sees what ships. The OpenCV, ONNX and LAME bindings link Homebrew
# until they are bundled, and are allowed by name until then, as in the install
# tests in ci-build.yml and release-build.yml.
ALLOW_EXTERNAL="libobjk_opencv.dylib libobjk_onnx.dylib libobjk_lame.dylib" \
	bash tools/cicd/check_macos_tree_links.sh core/release/deploy || exit 1

cd core/release

# deploy
if [ ! -z "$1" ] && [ "$1" = "deploy" ]; then
	# Sign before archiving. The .tgz is built HERE, and until now the .pkg was
	# the only artifact anything ever signed -- so tarball users got a tree of
	# ad-hoc binaries that macOS quarantines and then refuses to run, silently.
	# A no-op when no Developer ID is present, so local builds are unaffected.
	SIGN_TREE="$(cd ../.. && pwd)/tools/cicd/sign_macos_tree.sh"
	if [ -x "$SIGN_TREE" ]; then
		SKIP_OK=1 "$SIGN_TREE" deploy || \
			echo "warning: deploy tree is not fully signed; the .tgz may be blocked by Gatekeeper"
	fi

	mkdir -p ~/Desktop
	rm -rf ~/Desktop/objeck-lang
	cp -rf deploy ~/Desktop/objeck-lang
	cd ~/Desktop

	# The .zip is the artifact macOS users should get: notarytool accepts an
	# archive Apple recognises (.zip, .pkg, .dmg) and a .tgz is not submittable,
	# so a tarball can never be notarized and its contents stay quarantined --
	# which on macOS means the tools are killed outright, with no message.
	#
	# ditto, not zip: it preserves the code signatures, symlinks and extended
	# attributes that a plain `zip` mangles, and a mangled signature fails
	# notarization for a reason that reads as though the binary was tampered with.
	rm -f objeck-macos-arm64_0.0.0.zip
	ditto -c -k --keepParent --sequesterRsrc objeck-lang objeck-macos-arm64_0.0.0.zip

	# The .tgz stays for one release only, so that macOS copies of obu built
	# before this change -- which look for OBU_ASSET_SUFFIX ".tgz" and would
	# otherwise fail to find any asset -- can still self-update. Once a release
	# has shipped with obu asking for .zip, delete these four lines.
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar
	mv objeck.tar.gz objeck-macos-arm64_0.0.0.tgz
fi;
