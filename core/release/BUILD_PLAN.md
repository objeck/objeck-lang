# Build Plan: Fix Bootstrap, Establish Baseline, Re-enable Minor GC

## Current State (as of 2026-02-16)

### Code Changes ALREADY APPLIED in working tree:

1. **TrimNameValue fix** — `core/compiler/linker.cpp` line 547
   - DONE: Replaced unsafe `do { } while(!done)` loops with bounds-checked `while` loops
   - Fixes `std::out_of_range` crash when parsing `configobjk.ini` with `\r\n` line endings on WSL2

2. **GC: Guard remembered set clearing** — `core/vm/arch/memory.cpp` line 854
   - DONE: `remembered_set.clear()` wrapped in `if(!minor_gc_mode)`, with conservative re-add of all old objects after major GC

3. **GC: Two-phase CollectMinor** — `core/vm/arch/memory.cpp` line 1518
   - DONE: Phase 1 traces remembered set with `minor_gc_mode=false` (full recursion), Phase 2 marks from roots with `minor_gc_mode=true`

4. **GC: Optimized minor/major sweep** — `core/vm/arch/memory.cpp` line 719
   - DONE: Minor GC iterates `young_generation`, major GC iterates `allocated_memory`; clears mark bits on old objects after young sweep

5. **GC: Write barrier in ProcessStoreFunctionVar** — `core/vm/interpreter.cpp` line 1470
   - DONE: `MemoryManager::WriteBarrier(cls_inst_mem)` after instance field store

6. **JIT field-store rejection** — both JIT backends
   - DONE: `core/vm/arch/jit/amd64/jit_amd_lp64.cpp` lines 5267-5273
   - DONE: `core/vm/arch/jit/arm64/jit_arm_a64.cpp` lines 4640-4646
   - Pre-scan rejects methods with `STOR_CLS_INST_INT_VAR`, `COPY_CLS_INST_INT_VAR`, `STOR_INT_ARY_ELM`

### Git status of modified files:
```
M  core/vm/arch/jit/amd64/jit_amd_lp64.cpp   (staged)
 M core/vm/arch/jit/arm64/jit_arm_a64.cpp     (unstaged)
 M core/vm/arch/memory.cpp                     (unstaged)
 M core/vm/interpreter.cpp                     (unstaged)
 M core/compiler/linker.cpp                    (unstaged - the TrimNameValue fix)
```

---

## Step 2: Bootstrap Toolchain via WSL2

### Why this is needed
Commit `64f6f6c28` ("instr reordering") moved `MTHD_CALL_JIT` and `DYN_MTHD_CALL_JIT` from end of `InstructionType` enum to middle, shifting opcode values from `JMP` onwards. The existing obc/obr binaries and .obl files are out of sync. Must rebuild everything.

### Commands (run each manually — the shell script has \r\n issues)

```bash
# Navigate to compiler directory
cd /mnt/c/Users/objec/Documents/Code/objeck-lang/core/compiler

# 1. Build sys_obc (minimal compiler, no lib dependencies)
make -f make/Makefile.sys.amd64 clean
make -f make/Makefile.sys.amd64

# 2. Build lang.obl using sys_obc
./sys_obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl -strict

# 3. Build full obc (links against lang.obl)
make -f make/Makefile.amd64 clean
make -f make/Makefile.amd64

# 4. Build all libraries using full obc
./obc -src lib_src/gen_collect.obs -lib ../lib/lang -tar lib -opt s3 -dest ../lib/gen_collect.obl -strict
./obc -src lib_src/json_stream.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json_stream.obl
./obc -src lib_src/cipher.obs -tar lib -opt s3 -dest ../lib/cipher.obl
./obc -src lib_src/json.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/json.obl
./obc -src lib_src/opencv.obs -lib cipher,json -tar lib -opt s3 -dest ../lib/opencv.obl
./obc -src lib_src/onnx.obs -lib opencv,cipher,json -tar lib -opt s3 -dest ../lib/onnx.obl
./obc -src lib_src/lame.obs -tar lib -opt s3 -dest ../lib/lame.obl
./obc -src lib_src/diags.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/diags.obl
./obc -src lib_src/xml.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/xml.obl
./obc -src lib_src/regex.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/regex.obl
./obc -src lib_src/csv.obs -tar lib -lib gen_collect -opt s3 -dest ../lib/csv.obl
./obc -src lib_src/ml.obs -lib gen_collect,csv -tar lib -opt s3 -dest ../lib/ml.obl
./obc -src lib_src/nlp.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/nlp.obl
./obc -src lib_src/net_common.obs,lib_src/net.obs,lib_src/net_secure.obs -tar lib -lib gen_collect,cipher -opt s3 -dest ../lib/net.obl
./obc -src lib_src/net_server.obs -tar lib -lib net,json,gen_collect,cipher -opt s3 -dest ../lib/net_server.obl
./obc -src lib_src/json_rpc.obs -tar lib -lib json,net -opt s3 -dest ../lib/json_rpc.obl
./obc -src lib_src/misc.obs -lib gen_collect,net,json -tar lib -opt s3 -dest ../lib/misc.obl
./obc -src lib_src/rss.obs -tar lib -lib xml,gen_collect,net,cipher -opt s3 -dest ../lib/rss.obl
./obc -src lib_src/query.obs -tar lib -lib net,regex,csv,xml,json,misc -opt s3 -dest ../lib/query.obl
./obc -src lib_src/odbc.obs -lib gen_collect -tar lib -opt s3 -dest ../lib/odbc.obl
./obc -src lib_src/openai.obs -lib json,net,net_server,cipher,misc -tar lib -opt s3 -dest ../lib/openai.obl
./obc -src lib_src/gemini.obs -lib misc,json,net,net_server,cipher -tar lib -opt s3 -dest ../lib/gemini.obl
./obc -src lib_src/ollama.obs -lib net,json,cipher,misc -tar lib -opt s3 -dest ../lib/ollama.obl
./obc -src lib_src/sdl2.obs -tar lib -dest ../lib/sdl2.obl
./obc -src lib_src/sdl_game.obs -lib gen_collect,json,sdl2 -tar lib -dest ../lib/sdl_game.obl

# 5. Quick verification: compile and run hello world
echo 'class Hello { function : Main(args : String[]) ~ Nil { "Hello World"->PrintLine(); } }' > /tmp/hello.obs
./obc -src /tmp/hello.obs -dest /tmp/hello.obe
cd /mnt/c/Users/objec/Documents/Code/objeck-lang/core/release
# (need to also build obr via make, or use existing Linux obr)
```

### Alternative: fix update_version.sh line endings and run it
```bash
cd /mnt/c/Users/objec/Documents/Code/objeck-lang/core/compiler
sed -i 's/\r$//' update_version.sh
bash update_version.sh
```

---

## Step 3: Build and Verify on Windows

### Commands (from VS Developer Command Prompt)
```cmd
cd C:\Users\objec\Documents\Code\objeck-lang\core\release
deploy_windows.cmd x64
```

### Verification checklist:
1. Hello World runs and produces output
2. Regression tests produce **non-empty** output files
3. code_doc64 generation succeeds

```cmd
REM Hello world test
obc -src hello.obs -dest hello.obe
obr hello.obe

REM Regression tests
cd ..\..\programs\regression
REM run tests, then check output files are non-empty

REM code_doc64 (original GC crash test)
cd ..\..\core\release
code_doc64
```

---

## Step 5: Verify with Minor GC Enabled

After Windows build completes:
1. Rebuild MSVC x64 Release
2. Run Hello World
3. Run regression tests (verify non-empty output)
4. Run code_doc64 generation (the original crash test for GC)
5. Run CLBG benchmarks: `binarytrees` (GC-heavy), `spectralnorm`

---

## Key Files Reference
| File | What changed |
|------|-------------|
| `core/compiler/linker.cpp:547` | TrimNameValue bounds-check fix |
| `core/vm/arch/memory.cpp:854` | remembered_set guard + re-add after major GC |
| `core/vm/arch/memory.cpp:719-768` | Minor/major sweep split |
| `core/vm/arch/memory.cpp:1518-1562` | Two-phase CollectMinor |
| `core/vm/interpreter.cpp:1470` | WriteBarrier in ProcessStoreFunctionVar |
| `core/vm/arch/jit/amd64/jit_amd_lp64.cpp:5267-5273` | JIT field-store rejection (x64) |
| `core/vm/arch/jit/arm64/jit_arm_a64.cpp:4640-4646` | JIT field-store rejection (ARM64) |
| `core/shared/instrs.h` | InstructionType enum (the root cause of opcode shift) |

## Background: Why Regression Tests Were Falsely Passing
Windows `cmd.exe` doesn't set `ERRORLEVEL` on access violations/segfaults. Programs were crashing but the test harness saw exit code 0. After bootstrap, verify output files are **non-empty** to confirm real success.
