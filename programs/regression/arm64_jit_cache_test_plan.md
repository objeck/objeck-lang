# ARM64 JIT Register Cache - Test Plan

## 1. Build

```bash
cd core/compiler
./update_version_arm.sh
```

This rebuilds the compiler and all libraries on ARM64.

## 2. Run full regression suite

```bash
cd programs/regression
chmod +x run_regression.sh
./run_regression.sh arm64
```

All tests should show PASS. Any FAIL here is a regression from the cache changes.

## 3. Run debugger tests

```bash
cd programs/regression
chmod +x run_debugger_tests.sh
./run_debugger_tests.sh arm64
```

## 4. Targeted JIT stress tests

These specifically exercise the register pressure scenarios the cache improves:

```bash
cd core/release/deploy-arm64/bin

# JIT-heavy tests (native keyword forces JIT)
./obc -src ../../../../programs/regression/jit_native_math.obs -dest /tmp/jit_math.obe
./obr /tmp/jit_math.obe

./obc -src ../../../../programs/regression/jit_native_cls_fields.obs -dest /tmp/jit_cls.obe
./obr /tmp/jit_cls.obe

./obc -src ../../../../programs/regression/jit_native_float_array.obs -dest /tmp/jit_float.obe
./obr /tmp/jit_float.obe

./obc -src ../../../../programs/regression/jit_native_func_ref.obs -dest /tmp/jit_func.obe
./obr /tmp/jit_func.obe
```

Compare output against `programs/regression/results/*_output.txt`.

## 5. Deploy smoke tests

```bash
cd core/release/deploy-arm64/bin
./obc -lib cipher,net,xml,json -src ../../../../programs/deploy/hello_0.obs
./obr ../../../../programs/deploy/hello_0.obe

./obc -lib cipher,net,xml,json -src ../../../../programs/deploy/functions_5.obs
./obr ../../../../programs/deploy/functions_5.obe

./obc -lib cipher,net,xml,json -src ../../../../programs/deploy/json_3.obs
./obr ../../../../programs/deploy/json_3.obe
```

## 6. What to watch for

- **Segfaults** - would indicate a cached register is used after its value was clobbered (wrong invalidation)
- **Wrong output** - would indicate a stale cache entry was used instead of the updated memory value
- **"No general registers available"** - should appear *less often* than before (fewer methods falling back to interpreter)
