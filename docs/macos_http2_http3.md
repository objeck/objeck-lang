# macOS: Building and Testing HTTP/2 + HTTP/3 Support

Applies to Apple Silicon (ARM64) with Homebrew and Xcode 16.3+.

## 1. Pull latest

```bash
git pull
```

## 2. Install dependencies

```bash
brew install nghttp2 libnghttp3 gnutls cmake
```

Homebrew's `libngtcp2` ships only the OpenSSL crypto backend, but the VM
uses GnuTLS.  Build ngtcp2 from source with `ENABLE_GNUTLS=ON`:

```bash
cd /tmp && git clone --depth 1 https://github.com/ngtcp2/ngtcp2.git && cd ngtcp2
git submodule update --init --depth 1
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/opt/homebrew \
      -DENABLE_GNUTLS=ON -DENABLE_OPENSSL=OFF \
      -DCMAKE_PREFIX_PATH="/opt/homebrew;/opt/homebrew/opt/gnutls" \
      -DBUILD_TESTING=OFF ..
make -j$(sysctl -n hw.ncpu)
# Install to a staging area, then copy into Homebrew prefix:
make install DESTDIR=/tmp/ngtcp2-install
cp /tmp/ngtcp2-install/opt/homebrew/lib/libngtcp2* /opt/homebrew/lib/
mkdir -p /opt/homebrew/include/ngtcp2
cp /tmp/ngtcp2-install/opt/homebrew/include/ngtcp2/* /opt/homebrew/include/ngtcp2/
```

Verify:

```bash
ls /opt/homebrew/lib/libngtcp2_crypto_gnutls*
```

If that file is missing, the VM will fail to link — open an issue.

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

Set the library path so the VM can find the ngtcp2 and GnuTLS dylibs at runtime:

```bash
export DYLD_LIBRARY_PATH="/opt/homebrew/lib:$DYLD_LIBRARY_PATH"
```

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
- HTTP/3 test endpoint is `quic.nginx.org`. Cloudflare's QUIC implementation is incompatible with ngtcp2 0.12.x.
- `OBJECK_LIB_PATH` must point at the `lib/` directory containing `cacert.pem` for TLS certificate verification.
