# macOS: Building and Testing HTTP/2 + HTTP/3 Support

Applies to Apple Silicon (ARM64) with Homebrew and Xcode 16.3+.

## 1. Pull latest

```bash
git pull
```

## 2. Build the HTTP/2 and HTTP/3 libraries

obr links AWS-LC (TLS 1.3 for QUIC), ngtcp2, nghttp3 and nghttp2 statically, so
nothing from Homebrew ends up in it and the binary runs on a Mac without
Homebrew. Build them once; it takes a few minutes and needs `git` and `cmake`:

```bash
brew install cmake
MACOSX_DEPLOYMENT_TARGET=13.3 bash tools/deps/build_quic_deps.sh
```

They land in `~/objeck-deps/darwin-arm64`, which is where the Xcode project
looks. For another prefix, pass it to the script and pass
`OBJECK_DEPS=<prefix>` to `xcodebuild`. Keep the deployment target equal to the
project's `MACOSX_DEPLOYMENT_TARGET`, or the link warns that the archives target
a newer macOS than obr.

Verify:

```bash
ls ~/objeck-deps/darwin-arm64/lib/libngtcp2_crypto_boringssl.a
```

## 3. Build the VM

```bash
cd core/vm
xcodebuild -project xcode/VM.xcodeproj clean build \
  CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO
cp xcode/build/Release/obr ../../release/deploy/bin/
```

## 4. Build the compiler

```bash
cd ../compiler
xcodebuild -project xcode/Compiler.xcodeproj clean build \
  CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO
cp xcode/build/Release/obc ../release/deploy/bin/
cp ../lib/*.obl ../release/deploy/lib/
cp ../vm/misc/*.pem ../release/deploy/lib/
```

## 5. Compile the HTTP/2 and HTTP/3 libraries

```bash
cd ../release/deploy/bin
export OBJECK_LIB_PATH=../lib

./obc -src ../../../compiler/lib_src/net_h2.obs \
      -lib net,gen_collect,cipher -opt s3 -tar lib \
      -dest ../lib/net_h2.obl

./obc -src ../../../compiler/lib_src/net_quic.obs \
      -lib net,gen_collect,cipher -opt s3 -tar lib \
      -dest ../lib/net_quic.obl
```

## 6. Run the tests

### HTTP/2

```bash
./obc -src ../../../programs/tests/prgm_http2.obs \
      -lib net,net_h2,gen_collect,cipher \
      -dest /tmp/prgm_http2.obe

./obr /tmp/prgm_http2.obe
```

Expected:
```
--- HTTP/2 Client Tests ---
Test 1: HTTP/2 GET /get... PASS (status=200)
Test 2: HTTP/2 POST /post with JSON body... PASS (body echoed back)
Test 3: HTTP/2 custom request header... PASS (status=200)
Test 4: HTTP/2 connection reuse (3 sequential requests)... PASS (all 3 requests succeeded)
Test 5: HTTP/2 QuickGet httpbin.org/get... PASS (status=200)
Test 6: HTTP/2 QuickPost httpbin.org/post... PASS (status=200)
--- Done ---
```

### HTTP/3

```bash
./obc -src ../../../programs/tests/prgm_http3.obs \
      -lib net,net_quic,gen_collect,cipher \
      -dest /tmp/prgm_http3.obe

./obr /tmp/prgm_http3.obe
```

Expected:
```
--- HTTP/3 Client Tests ---
Test 1: HTTP/3 GET quic.nginx.org/... PASS (status=200)
Test 2: HTTP/3 POST quic.nginx.org/... PASS (status=405)
Test 3: HTTP/3 sequential requests on same connection... PASS
Test 4: HTTP/3 QuickGet quic.nginx.org/... PASS (status=200)
Test 5: HTTP/3 QuickPost quic.nginx.org/... PASS (status=405)
--- Done ---
```

## Notes

- `SKIP (no network or ngtcp2 not built)` means the VM was not built with `OBJECK_HAS_NGTCP2`. Check that the Xcode project preprocessor defines include `OBJECK_HAS_NGTCP2` and `OBJECK_HAS_NGHTTP2`.
- `otool -L obr` should list only `/usr/lib` and `/System` libraries. A path under `/opt/homebrew` means the build did not use the static libraries, and `deploy_macos_arm64.sh` fails on it: releases v2026.6.3 through v2026.9.2 shipped such an obr, and it could not start on a Mac without a hand-built ngtcp2 GnuTLS backend.
- HTTP/3 test endpoint is `quic.nginx.org`. Cloudflare's QUIC implementation is incompatible with ngtcp2.
- `OBJECK_LIB_PATH` must point at the `lib/` directory containing `cacert.pem` for TLS certificate verification.
