# Performance backlog, measured for v2026.11.4

What a measure-first pass over startup, JSON, strings/collections and compile time found in
November 2026 and did not change, because the fix belongs in the JIT, the collector or the
load-time verifier and those were either in flight elsewhere or too large a design change for the
release. Each item has its numbers, the call path, and a ranked fix with the gain to expect and
the risk. What the pass did change is listed at the end, so the two can be told apart.

## How the numbers were taken

- Machine: the shared Ubuntu 26.04 x64 build box (32 threads), busy throughout, so wall time is
  not used as evidence. The unit is **instructions executed**, counted by
  `valgrind --tool=callgrind` in a container; the count is the same to the instruction from run to
  run. Where a time is given it is user+sys CPU, best of N.
- Suite: `programs/tests/perf/suite/` (`perf_run.py`; the programs it compiles; `prim_bench.obs`
  for the cost of one operation). A per-operation cost is the difference between two loop counts
  divided by the difference in count, with the loop in a `native` function so that it is compiled
  code calling the operation.
- Tree: origin/master at 68637fb7d3 unless a line says otherwise.

## 1. Cost of the primitives (compiled code, instructions per operation)

| Operation | Instructions | Note |
| --- | ---: | --- |
| empty loop iteration, compiled | 15 | |
| empty loop iteration in `Main` | 466 | `Main` is entered once and the VM does not replace a running frame, so a loop in `Main` (or in any method entered once) is interpreted for good, with or without `OBJECK_JIT_THRESHOLD=1` |
| getter call (inlined by obc at s3) | 17 | |
| call of a compiled function from compiled code | 132 | about 117 over the loop; `String->Get(i)` 152, `Char->IsDigit()` 144, `Vector->Get(i)` 153 |
| virtual call | 131 | |
| `GetClassID()` | 303 | a trap through `JitStackCallback` -> `StackCallbackBody` -> `TrapProcessor::ProcessTrap` |
| cast `->As(String)` | 164 | `OBJ_INST_CAST` through the callback into `MemoryManager::ValidObjectCast` |
| new object with a one-field constructor | 301 | the nursery bump is inlined |
| `Char->New[8]` | 977 | `MemoryManager::AllocateArray`: every array goes to the old generation: `calloc`, two mutexes (`free_memory_cache_lock`, `allocated_lock`) and an `unordered_set` insert |
| `String->New()` | 1,465 | an object and its `Char[8]` |
| `String->New(chars, offset, 5)` | 1,943 | |
| a string literal evaluated (`"true"->Size()`) | 3,073 | a literal is a new `String` every time it is evaluated (strings are mutable) |
| `String->ToCharArray()` (4 chars) | 1,426 | array + `Runtime->Copy` |
| `String->Append(Char)` | 180 | |
| `String->Append(String)` of 4 chars | 905 | a `Get(i)` call per character below 32 characters |
| `String->Equals`, two equal 4-char strings | 830 | |
| `String->Compare`, two equal 4-char strings | 1,943 | two `GetClassID`, one cast, a `Get(i)` call per character |
| `IntRef->Compare` | 1,209 | two `GetClassID`, one cast, two `Get` |
| `String->HashID()` (4 chars) | 328 | |
| `Int->ToString()` | 5,082 | |
| `Vector->New()` | 1,419 | |
| `Vector->AddBack` | about 350 | |
| `Map->Insert` into a map of up to 8 `String` keys | 6,200 | two or three `Compare` per insert |
| `Map->Find` in a map of 8 | 5,559 | about three `Compare` |
| `Hash->Insert` (8 keys) / `Hash->Find` | 1,450 / 750 | |
| `Map` insert + find at n = 100,000 `IntRef` keys | 11.4 microseconds | 37.7 with `--jit=off` |

Everything below follows from this table: a JSON object member is a `Map->Insert`, a `Map->Insert`
is `String->Compare`, and `String->Compare` is traps, a cast and calls.

## 2. Backlog, ranked

### 2.1 Load-time verifier: half of a small program's startup (owner: the verifier work)

`Loader::Verify` builds a `RefMap` for every method of the image (`RefMap::Build(program, mthd, true)`,
core/vm/arch/ref_map.h) and deletes it.

| Run | Total instructions | In `ref_map_detail::Builder::Run` |
| --- | ---: | ---: |
| `obr hello.obe` (162 KB image) | 31.7 M | 17.3 M (54%) |
| `obr --mcp mcp_shop.obe` to the answer to `initialize` (641 KB image) | 150.0 M | 69.2 M (46%) |

That is about 107 instructions per byte of image, about 19 ms per MB on this machine. Inside it,
`operator new`/`delete` and `std::vector<int>::operator=` are about half (`Builder::Step`,
`Builder::Flow`, `Builder::NewValue`).

1. Allocation-free builder state (reuse the per-instruction vectors across methods; an arena for
   values). Expected: about half of the verifier, so 20-25% of startup. Risk: low, same results.
2. Keep the map for the JIT instead of deleting it: the JIT builds the same map again for each
   method it compiles (4.6 M of the MCP run). Expected: 3%. Risk: memory held for methods never
   compiled; keep only on request.
3. Verify a method when it is first entered rather than at load. Expected: most of the 46-54%.
   Risk: a refusal moves from load time to first call, which changes what a damaged image does;
   a design decision, not a tuning.

### 2.2 Array allocation: 977 instructions, old generation only (owner: collector)

`MemoryManager::AllocateArray` (core/vm/arch/memory.cpp) never uses the nursery ("promotion/fixup
cannot safely update all root locations including PDA thread operand stacks"). Every `String`
owns a `Char[]`, so every string made pays it, and the array lives until a major collection:
parsing a 5.4 MB JSON document once peaks at 569 MB resident.

1. Thread-local free lists per size class and a per-thread batch for the `old_generation` insert,
   so the common case takes no lock. Expected: 977 -> about 300; 15-25% on JSON parse and string
   building. Risk: medium (collector bookkeeping under STW must drain the batches).
2. Arrays in the nursery. Expected: 977 -> about 60 and far less old-generation growth. Risk:
   high; it is the collector design change the comment in `AllocateArray` describes.

### 2.3 Compiled-to-compiled call: about 117 instructions (owner: JIT)

Measured through the bridge-free path (`JitDirectCall`/`CallCompiled`, jit_common.cpp): frame from
`GetStackFrame`, call-stack push with a fence, `JitRuntime::Execute`, release. A library written as
small methods pays it per character: `String->Compare`, `String->Append(String)` under 32
characters, `String->Find(String)`, the JSON scanner's `IsDigit`/`IsChar` (removed from the scanner
in this pass by writing the comparison out).

1. Inline small leaf methods of the same class at JIT time (`String->Get`, `Size`, `IsDigit`).
   Expected: 5-10x on per-character library loops. Risk: medium.
2. A leaner frame for a callee that makes no calls and cannot fault. Risk: medium-high (GC roots).

### 2.4 `GetClassID` and casts go through the generic callback (owner: JIT)

303 and 164 instructions; both are a load and a compare when inlined (class pointer from the
object header; a cast to the exact class is one compare before the hierarchy walk). Every
`Compare` of `String`, `IntRef`, `FloatRef`, ... starts with two `GetClassID` and one cast, which
is 770 of the 1,943 instructions of a 4-character `String->Compare` and so about 40% of every
`Map` operation and every sort. Expected: 30-40% on `Map`, `CompareVector->Sort`, JSON objects.
Risk: low-medium; both are pure reads.

### 2.5 String literals: 3,073 instructions per evaluation (owner: compiler + VM)

A literal builds a `String` and a `Char[]` and copies the text each time the expression runs.
Strings are mutable, so the object cannot simply be shared. Options: copy-on-write backing array
for literals (the `Char[]` shared until the first mutation), or a compiler pass that hoists a
literal that is only read (`"x"->Size()`, an argument to a parameter never mutated). Expected:
3,073 -> about 400. Risk: medium-high (aliasing rules). Until then library code on a hot path should
append characters rather than short literals; the JSON serializer was changed that way in this pass.

### 2.6 `PatchCallSites` walked the whole program per compiled method (done on perf/startup)

`JitCompiler::PatchCallSites` (core/vm/arch/jit/jit_common.cpp) walked every instruction of every
method once per compiled method: about 0.5 M instructions each for a 641 KB image, 26 M of the
150 M an MCP server runs before its first answer (53 methods compiled), against 5.0 M for
generating the code and 4.6 M for the JIT's own `RefMap`. It now looks the sites up in a table built
once. Kept here because the same shape may exist elsewhere: anything that is O(image size) per
compiled method shows at startup, where 50 to 60 methods are compiled before the first answer.
With `--jit=off` the same startup is 114.7 M instructions, so after this change the JIT still
costs about 11 M (9%) more than it returns before the first answer.

### 2.7 `native` methods compile on first call (owner: JIT policy; no change recommended)

53 methods are compiled before an MCP server's first answer, 61 before the first agent run ends.
With the call-site scan gone the compiles themselves are about 9% of that startup. Compiling them at
the threshold instead would save most of that, but a `native` method entered once with a hot loop
would then be interpreted for good (no on-stack replacement), about 20 times slower. Not
recommended without on-stack replacement.

### 2.8 Loops in a method entered once are never compiled (owner: JIT)

`Main` is compiled when its back-edge counter trips and the compiled code is never used, because
the frame that is running is not replaced: 466 instructions per iteration of an empty loop against
15. A benchmark or a script with its loop in `Main` measures the interpreter. On-stack replacement
at a loop header is the fix; until then the documented advice is to put a hot loop in a method.

### 2.9 Thread virtual size: 72 MB per thread (not worth changing)

An idle VM thread adds 8 MB of stack reservation and a 64 MB glibc malloc arena reservation
(`---p`, not resident): 16 threads 1.5 GB VmSize, 64 threads 5.1 GB, while resident grows by
0.16-0.8 MB per thread. It is address space, not memory. `MALLOC_ARENA_MAX` (or `mallopt` at
start) would cap it at the price of allocator contention between threads, which the old
generation's `calloc` per array would feel. Leave, document `MALLOC_ARENA_MAX` for containers
that limit address space.

### 2.10 The 128 MB nursery (not worth changing on Linux)

Linux: reserved with `mmap` and touched on use: `obr hello.obe` is 227 MB virtual, 8.5 MB resident.
Windows commits the limit up front (see the comment in `MemoryManager::Initialize`, #841);
commit-on-growth needs the inlined bump in generated code to check a commit limit and is its own
change.

### 2.11 obc: the remaining profile after the compression change

For a 790-line program (agent_loop_test.obs): analysis 48%, optimizer 15% (it runs over the linked
library classes too), loading the libraries 13%, freeing everything before exit 10%. Candidates:
skip the teardown in the command-line compiler (leak checkers would need to be told), a per-context
cache in `SearchProgramClasses`/`SearchProgramEnums` (6% + 4%). Each is single digits; none is a
cliff.

Mild super-linear shapes, bounded by existing limits and left alone: a chain of method calls
(`a->M()->M()...`: 240 links +97 ms and 35 MB, refused as too deep near 1,000), an Int array
literal (40,000 elements 0.6 s).

## 3. Changed in this pass

Each on its own branch off master, so each can be taken or dropped alone.

| Branch | Change | Before | After |
| --- | --- | ---: | ---: |
| perf/compile | executables deflated at zlib's default level, libraries still at 9 | `obc hello.obs` 396.4 M instructions; a 790-line program 1,657.6 M; the 977-source regression corpus 168.3 s of CPU | 151.2 M (-62%); 830.4 M (-50%); 70.7 s (-58%); files 6.8% larger; images identical |
| perf/compile | hidden-local names found by bisection, with a compile-time ceiling test | 16,000 interpolated strings in one method: 73.4 s to the error | 0.7 s |
| perf/json | serializer: one pass per string, one walk per object, no string literals; scanner: shared value-less tokens, comparisons written out; text of true/false/null made on demand | 200-byte request: parse 173,338, `ToString` 251,715 instructions | 156,439 (-9.7%); 103,282 (-59%); output byte-identical |
| perf/startup | call-site table instead of a whole-program walk in `PatchCallSites` | `obr --mcp` to first answer 149.96 M; first agent run 113.57 M | 125.86 M (-16.1%); 95.74 M (-15.7%) |

Found and not fixed, outside performance: `obc -debug` writes a different image on every run for
programs/regression/core_string_interp_literal.obs (four line-number fields of the instructions for
a character literal inside an interpolation, `"{$'z'}"`, hold uninitialized memory).
