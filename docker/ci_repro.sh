#!/bin/bash
# CI reproduction script - runs inside the container
# Mounts: /src = read-only source tree, /build = writable build dir
set -e

# Set UTF-8 locale so mbstowcs() handles Unicode source files correctly
export LANG=C.UTF-8
export LC_ALL=C.UTF-8

SRC=/src
BUILD=/build

echo "=== Setting up build directories ==="
mkdir -p $BUILD/core/shared
mkdir -p $BUILD/core/compiler/make
mkdir -p $BUILD/core/compiler/lib_src
mkdir -p $BUILD/core/vm/make
mkdir -p $BUILD/core/vm/arch
mkdir -p $BUILD/core/vm/misc
mkdir -p $BUILD/core/lib
mkdir -p $BUILD/core/release/deploy-x64/bin
mkdir -p $BUILD/core/release/deploy-x64/lib
mkdir -p $BUILD/programs/regression
mkdir -p $BUILD/programs/tests

echo "=== Copying source files ==="
cp -r $SRC/core/shared/. $BUILD/core/shared/
cp -r $SRC/core/compiler/make/. $BUILD/core/compiler/make/
cp -r $SRC/core/compiler/lib_src/. $BUILD/core/compiler/lib_src/
cp $SRC/core/compiler/*.cpp $BUILD/core/compiler/ 2>/dev/null || true
cp $SRC/core/compiler/*.h $BUILD/core/compiler/ 2>/dev/null || true
cp $SRC/core/compiler/update_version.sh $BUILD/core/compiler/
cp -r $SRC/core/vm/make/. $BUILD/core/vm/make/
cp -r $SRC/core/vm/arch/. $BUILD/core/vm/arch/
cp -r $SRC/core/vm/misc/. $BUILD/core/vm/misc/
cp $SRC/core/vm/*.cpp $BUILD/core/vm/ 2>/dev/null || true
cp $SRC/core/vm/*.h $BUILD/core/vm/ 2>/dev/null || true

# Copy .obl files (pre-built, as fallback)
cp $SRC/core/lib/*.obl $BUILD/core/lib/
cp $SRC/core/lib/*.ini $BUILD/core/lib/ 2>/dev/null || true

# Copy test programs
cp $SRC/programs/regression/mcp_server_test.obs $BUILD/programs/regression/
cp $SRC/programs/regression/select_dispatch_test.obs $BUILD/programs/regression/
cp $SRC/programs/tests/mcp_debug_test.obs $BUILD/programs/tests/
cp $SRC/programs/tests/hash_debug.obs $BUILD/programs/tests/

echo "=== Verifying sys.h in build directory ==="
grep -n "djb2\|5381\|FNV\|14695981" $BUILD/core/shared/sys.h | head -5
echo "---"
md5sum $BUILD/core/shared/sys.h
md5sum $SRC/core/shared/sys.h
echo "---"

echo "=== Running update_version.sh (Bootstrap: sys_obc -> obc -> rebuild .obl) ==="
cd $BUILD/core/compiler
chmod +x update_version.sh

# Run without set -e to continue past individual failures
set +e
./update_version.sh 2>&1
UPDATE_EXIT=$?
set -e

echo "=== Bootstrap complete (exit=$UPDATE_EXIT) ==="
ls -la $BUILD/core/lib/net_server.obl
ls -la $BUILD/core/lib/net.obl

echo "=== Building VM (obr) ==="
cd $BUILD/core/vm
cp make/Makefile.amd64 Makefile
make clean
make -j$(nproc)

echo "=== Setting up deploy directory ==="
cp $BUILD/core/compiler/obc $BUILD/core/release/deploy-x64/bin/
cp $BUILD/core/vm/obr $BUILD/core/release/deploy-x64/bin/
cp $BUILD/core/lib/*.obl $BUILD/core/release/deploy-x64/lib/
cp $BUILD/core/lib/*.ini $BUILD/core/release/deploy-x64/lib/ 2>/dev/null || true

export PATH="$BUILD/core/release/deploy-x64/bin:$PATH"
export OBJECK_LIB_PATH="$BUILD/core/release/deploy-x64/lib"

echo ""
echo "=== Verifying obc binary ==="
ls -la $BUILD/core/release/deploy-x64/bin/obc
md5sum $BUILD/core/release/deploy-x64/bin/obc
strings $BUILD/core/release/deploy-x64/bin/obc 2>/dev/null | grep -iE "djb2|fnv|14695" | head -5 || true
echo "---"

echo "=== Checking compile-time hash computation ==="
# Create a minimal test to check what hash obc computes for label "ping" at compile time
cat > /tmp/hash_check.obs << 'OBSEOF'
class HashCheck {
    function : Main(args : String[]) ~ Nil {
        # If select dispatches correctly, obc hash == HashID hash
        sv := "ping";
        matched := false;
        select(sv) {
            label "ping": { matched := true; }
        };
        if(matched) {
            "compile/runtime hash MATCH for ping"->PrintLine();
        } else {
            "compile/runtime hash MISMATCH for ping"->PrintLine();
        };
    }
}
OBSEOF
cd $BUILD/core/release/deploy-x64/bin
./obc -src /tmp/hash_check.obs -dest /tmp/hash_check.obe 2>&1
./obr /tmp/hash_check.obe 2>&1
echo "---"

echo "=== Compiling select_dispatch_test (s3) ==="
cd $BUILD/core/release/deploy-x64/bin
./obc -src $BUILD/programs/regression/select_dispatch_test.obs \
      -lib cipher,collect,xml,json \
      -opt s3 \
      -dest $BUILD/programs/regression/select_dispatch_test_s3.obe 2>&1

echo "=== Compiling select_dispatch_test (no opt) ==="
./obc -src $BUILD/programs/regression/select_dispatch_test.obs \
      -lib cipher,collect,xml,json \
      -dest $BUILD/programs/regression/select_dispatch_test.obe 2>&1

echo "=== Running select_dispatch_test (no opt) ==="
./obr $BUILD/programs/regression/select_dispatch_test.obe 2>&1
SELECT_EXIT=$?
echo "select_dispatch_test exit code: $SELECT_EXIT"

echo "=== Running select_dispatch_test (s3) ==="
./obr $BUILD/programs/regression/select_dispatch_test_s3.obe 2>&1
SELECT_S3_EXIT=$?
echo "select_dispatch_test s3 exit code: $SELECT_S3_EXIT"

echo ""
echo "=== Compiling hash_debug ==="
./obc -src $BUILD/programs/tests/hash_debug.obs \
      -opt s3 \
      -dest /tmp/hash_debug.obe 2>&1
echo "=== Running hash_debug ==="
./obr /tmp/hash_debug.obe 2>&1

echo ""
echo "=== Compiling mcp_server_test ==="
./obc -src $BUILD/programs/regression/mcp_server_test.obs \
      -lib net,net_server,cipher \
      -opt s3 \
      -dest /tmp/mcp_server_test.obe 2>&1
MCP_COMPILE_EXIT=$?
echo "mcp_server_test compile exit code: $MCP_COMPILE_EXIT"

if [ $MCP_COMPILE_EXIT -eq 0 ]; then
    echo "=== Running mcp_server_test ==="
    ./obr /tmp/mcp_server_test.obe 2>&1
    MCP_EXIT=$?
    echo "mcp_server_test exit code: $MCP_EXIT"
else
    echo "mcp_server_test compilation failed - skipping run"
fi

echo ""
echo "=== Summary ==="
echo "select_dispatch_test: $([ $SELECT_EXIT -eq 0 ] && echo PASS || echo FAIL)"
if [ $MCP_COMPILE_EXIT -eq 0 ]; then
    echo "mcp_server_test: $([ $MCP_EXIT -eq 0 ] && echo PASS || echo FAIL)"
else
    echo "mcp_server_test: COMPILE FAILED (likely due to net_server.obl not rebuilt)"
fi
