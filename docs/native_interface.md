# Writing native libraries for Objeck

Objeck reaches C and C++ through shared libraries it loads at runtime. Every
binding in the standard library works this way — SDL2 and OpenGL, ODBC, OpenCV,
ONNX, mbedTLS, LAME, the diagnostics engine behind the language server. If you
want Objeck to call something the VM does not already know about, this is the
mechanism, and it is the only one.

This document is the whole contract: what the VM guarantees, what your library
must guarantee back, what the collector does with the memory you allocate, and
what a call costs.

---

## The shape of a call

A native library is two halves that meet at a name.

**The Objeck half** loads the library and calls into it by function name,
passing an array of boxed values:

```objeck
use System.API;

class Beep {
	@proxy : static : DllProxy;
	@fn_beep_play : static : String;

	function : Play(hz : Int, ms : Int) ~ Bool {
		if(@proxy = Nil) {
			@proxy := DllProxy->New("libobjk_beep");
		};

		array_args := Base->New[3];
		array_args[0] := IntRef->New();     # the result comes back here
		array_args[1] := IntRef->New(hz);
		array_args[2] := IntRef->New(ms);

		if(@fn_beep_play = Nil) { @fn_beep_play := "beep_play"; };
		@proxy->CallFunction(@fn_beep_play, array_args);

		return array_args[0]->As(IntRef)->Get() <> 0;
	}
}
```

**The C++ half** exports a function of that name taking a `VMContext&`:

```cpp
#include "../../vm/lib_api.h"

extern "C" {
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) { }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() { }

#ifdef _WIN32
  __declspec(dllexport)
#endif
  void beep_play(VMContext& context) {
    const size_t hz = APITools_GetIntValue(context, 1);
    const size_t ms = APITools_GetIntValue(context, 2);

    const bool ok = platform_beep(hz, ms);
    APITools_SetIntValue(context, 0, ok ? 1 : 0);
  }
}
```

There is no header generation, no IDL, no signature checking. The name is the
only thing that ties the halves together, and the argument array is the only
thing that crosses. If the two sides disagree about what index 2 holds, nothing
tells you — you get a wrong number or a crash.

That bluntness is the trade. It also means the whole interface fits in one
header, `core/vm/lib_api.h`, which is worth reading in full at least once.

### What happens between them

1. `DllProxy->New(name)` runs `EXT_LIB_LOAD`, which resolves `name` against the
   library directory, appends the platform's extension, loads it, and calls the
   library's `load_lib`.
2. `CallFunction(name, args)` runs `EXT_LIB_FUNC_CALL`. The VM resolves `name` to
   a symbol, builds a `VMContext` around the argument array, and calls it.
3. Your function reads and writes the argument array in place. It does not
   return a value; there is no return channel other than the array.
4. `Unload()` runs `EXT_LIB_UNLOAD`, which calls `unload_lib` and unloads.

The library directory is `$OBJECK_LIB_PATH/native/`, falling back to
`../lib/native/` relative to the working directory. The name you pass has no
extension — the VM appends `.dll`, `.dylib` or `.so` — and by convention starts
with `libobjk_`, which is what the deploy scripts name the built artifacts.

---

## The C++ side

### The three exports

Every library must export `load_lib(VMContext&)` and `unload_lib()`. The VM
looks them up by name immediately after loading and **exits the process** if
either is missing. Most libraries leave both empty; use them for one-time
global setup and teardown that cannot be done lazily.

Then export one function per entry point, each `void f(VMContext&)`.

All of it goes inside `extern "C"`, and on Windows each export needs
`__declspec(dllexport)`. POSIX builds export by default.

### Reading arguments

`context.data_array` is the Objeck `Base[]` you were passed. Never index it
directly; the header's accessors know the layout.

```cpp
size_t         n   = APITools_GetArgumentCount(context);
bool           b   = APITools_GetBoolValue(context, 1);
size_t         i   = APITools_GetIntValue(context, 1);
double         f   = APITools_GetFloatValue(context, 1);
wchar_t        c   = APITools_GetCharValue(context, 1);
unsigned char  y   = APITools_GetByteValue(context, 1);
const wchar_t* s   = APITools_GetStringValue(context, 1);
size_t*        obj = APITools_GetObjectValue(context, 1);
```

Every one of these is bounds-checked against the argument count and returns a
zero value (or `nullptr`) when the index is out of range or the slot is empty.
That is a guard against a mismatched call, not a licence to skip checking:
`APITools_GetStringValue` returns `nullptr` for a `Nil` string, and dereferencing
it is your bug, not the VM's.

Arrays arrive as a *holder* — an `IntArrayRef`, `FloatArrayRef`, `ByteArrayRef`
and so on — which is an object wrapping the array. So there are two steps:

```cpp
size_t* holder = APITools_GetArray(context, 1);      // the holder
size_t* array  = (size_t*)holder[0];                 // the array itself
size_t  count  = APITools_GetArraySize(array);
double* values = (double*)APITools_GetArray(array);  // past the 3-word header
```

An Objeck array is three header words (`size`, `dimension count`, then one
length per dimension) followed by the elements, which is what
`ARRAY_HEADER_OFFSET` is for. `APITools_GetArray(array)` returns the element
pointer; cast it to the element type. The typed element accessors
(`APITools_GetFloatArrayElement` and friends) do the same work one element at a
time with bounds checks.

`String[]` is the exception with a shortcut: `APITools_GetStringsValues(context, i)`
returns a `std::vector<std::wstring>` directly.

### Returning values

You return by writing into the argument array. By convention index 0 is the
result slot, and the Objeck side has already put a holder there for you.

```cpp
APITools_SetIntValue(context, 0, 42);
APITools_SetFloatValue(context, 0, 3.5);
APITools_SetStringValue(context, 0, L"done");
APITools_SetObjectValue(context, 0, some_object);
```

The primitive setters write *through* the holder the caller supplied — they
mutate the `IntRef` that is already in the slot. `SetStringValue` and
`SetObjectValue` allocate something new and replace the slot's contents.

To hand back a new array, allocate one and store it in the holder:

```cpp
size_t* holder = APITools_GetArray(context, 0);
size_t* result = APITools_MakeByteArray(context, bytes.size());
memcpy(result + ARRAY_HEADER_OFFSET, bytes.data(), bytes.size());
holder[0] = (size_t)result;
```

`APITools_MakeIntArray`, `MakeFloatArray`, `MakeFloatMatrix`, `MakeCharArray`
and `MakeByteArray` cover the cases. `APITools_CreateStringObject` builds a
`System.String`; `APITools_CreateObject` builds an instance of any class by
qualified name.

### Calling back into Objeck

```cpp
APITools_CallMethod(context, instance, L"MyClass:Handler:o.System.String,");
APITools_CallMethod(context, instance, cls_id, mthd_id);
```

The qualified-name form wants the full mangled signature, which is why the
by-id form exists for anything called repeatedly. This is how the SDL bindings
run Objeck event handlers.

---

## Memory and the collector

**Everything a native library allocates lives in the old generation.** That is
the whole rule, and it is worth understanding why, because it is what makes the
plain stores in `lib_api.h` safe.

Objeck's collector is generational. Objects are normally allocated in a nursery
and promoted — which **moves** them — when they survive a collection. Moving is
safe because the collector fixes up every reference afterwards, and it finds
those references two ways: by tracing from roots, and, for references stored
into old-generation objects, through a *write barrier* that records the object
as dirty when the store happens. A minor collection marks an old object without
recursing into it, so an old-to-young reference that the barrier never recorded
is invisible to both the mark phase and the fixup phase.

Now look at how a library returns an object:

```cpp
void APITools_SetObjectValue(VMContext& context, int index, size_t* obj) {
  ...
  data_array[index] = (size_t)obj;      // a plain store, no barrier
}
```

`lib_api.h` is compiled *into your library*, not into the VM. It cannot reach
the collector's write barrier, so it cannot run one. And the argument array is
always an old-generation object, because `AllocateArray` never uses the nursery.

So the VM allocates on your behalf in the old generation instead. Old objects
are never moved, so nothing needs fixing up, and a major collection recurses
into old objects, so your value stays reachable through the argument array. The
barrier is not needed because the hazard is not created.

This is enforced VM-side (`MemoryManager::AllocateObjectNative`), which means it
covers every library already compiled, including yours, without a rebuild. You
do not have to do anything. But you do have to know the rule, because it has one
consequence you can feel: **objects a native library allocates are reclaimed only
by a major collection.** A library that allocates a String per call in a hot loop
grows the old generation. If you are returning many small values, return them
through primitive holders, which allocate nothing at all.

`programs/regression/native_gc_barrier_test.obs` is the test that holds this
down. It fails — with a dangling reference, in practice an "Invalid object cast"
abort — against a VM that allocates native objects in the nursery.

### Arrays are born old, objects are born young

Worth stating separately, because it decides which stores are safe:

- `AllocateArray` **never** uses the nursery. Every Objeck array is
  old-generation from the moment it exists.
- `AllocateObject` does use it — except through the native path above, which is
  the whole point of that change.

No collection can run inside your function, because the API's allocators pass
`collect=false`, so nothing you allocate moves while you are still holding it.

The consequence for you: everything you allocate is old, and the argument array
is old, so every store you make between them is old-to-old and needs no
barrier. That is what makes `lib_api.h`'s plain stores correct.

The one thing to avoid is storing an object **the caller gave you** into an
array. A caller's object may be young, an array is always old, and you have no
way to run the barrier from library code. If you need to hand a caller's object
back, put it in the slot it came from rather than into an array you built.

This is not a hypothetical restriction — the VM's own traps had exactly this
bug. A trap that returned `String[]` allocated the array (old), filled it with
freshly created Strings (young), and ran no barrier; the next minor collection
promoted the elements out from under an array the collector does not look
inside. `Directory->List` followed by enough allocation to fill the nursery
turned two thirds of a 393-entry listing into unreadable memory, and reading
one segfaulted the VM. `programs/regression/trap_array_barrier_test.obs` guards
it now.

### What you still own

The collector manages what it allocated. It knows nothing about anything else.

- Memory you get from `malloc`/`new` is yours to free. Nothing collects it.
- A native handle you stash in an `Int` (a texture id, a socket, a database
  connection) is invisible to the collector. The Objeck side has to release it
  explicitly, which is why so many classes in these bindings have a `Free`.
- The `size_t*` pointers you hold during one call stay valid for that call.
  Native allocations do not trigger a collection and do not park at a safepoint,
  so nothing moves under you between two `APITools_` calls in the same function.
  Do not hold one past the call — store an Objeck-visible reference instead.

---

## Errors

There is no error channel. A native function returns `void`, and the VM has no
notion of a failed call.

**Do not let an exception escape.** The VM calls you through a raw function
pointer, across a module boundary, from a frame that knows nothing about
unwinding. Catch everything at the boundary.

**Do not call `exit()`,** however tempting. Report the failure through the
argument array and let Objeck decide.

The convention that has held up is a status in the result slot plus a
last-error string the caller can ask for:

```cpp
void thing_do(VMContext& context) {
  try {
    ...
    APITools_SetIntValue(context, 0, 1);
  }
  catch(const std::exception& e) {
    last_error = BytesToUnicode(e.what());
    APITools_SetIntValue(context, 0, 0);
  }
}
```

The VM itself is less forgiving than this advice: a missing `load_lib`, a
missing symbol, or a library that fails to load all print a message and **exit
the process**. A typo in a function name is a hard stop at the first call, not
an exception you can catch.

### Threading

Objeck threads call native functions concurrently, and nothing serialises them.
A library with mutable global state needs its own locking. Several of the
bindings here are single-threaded in practice only because their callers happen
to be.

---

## What a call costs

Measured on Windows x64, per call, against a native function that does nothing:

| | ns |
|---|---|
| a call whose name is a string literal | ~1,655 |
| the same call, name held in a field | ~130 |
| a fully hoisted call with a reused argument buffer | ~125 |

The number that surprises people is the first one. **An Objeck string literal is
materialised into a fresh `String` every time it is evaluated** — not once at
load, not interned. A name written at the call site is therefore an allocation
and a copy on every single call, and at ~1,525ns it is 92% of the cost of the
call, spent in code that reads like a constant.

Two rules follow, and together they took a 500-box OpenGL frame from 15.02ms to
3.35ms.

### Rule 1: hold the name in a field

```objeck
# Before -- allocates "sdl_gl_mesh_draw" on every draw.
Proxy->GetDllProxy()->CallFunction("sdl_gl_mesh_draw", array_args);

# After.
@fn_sdl_gl_mesh_draw : static : String;
...
if(@fn_sdl_gl_mesh_draw = Nil) { @fn_sdl_gl_mesh_draw := "sdl_gl_mesh_draw"; };
Proxy->GetDllProxy()->CallFunction(@fn_sdl_gl_mesh_draw, array_args);
```

Static, because the names are class-wide constants and a `function :` cannot
read an instance field. The lazy fill races harmlessly between threads: both
write an equivalent String, one wins, the loser's allocation is discarded, and
no one can observe a half-built value because the store is a pointer store.

`tools/cicd/hoist_native_names.py` applies this across a file, and `--check`
reports any call site that still writes a literal:

```
python tools/cicd/hoist_native_names.py --check core/compiler/lib_src/*.obs
```

Worth knowing: `core/lib/sdl/code_gen` **generates** `sdl2.obs`, and its emitter
writes the literal form. Regenerating it undoes the hoisting for all 353 call
sites, and `--check` is how you find out.

### Rule 2: reuse the argument buffer

The usual shape allocates a `Base[]` and a holder per element on every call:

```objeck
array_args := Base->New[3];
array_args[0] := IntRef->New(mesh);
array_args[1] := IntRef->New(mode);
array_args[2] := IntRef->New(count);
```

Holders are mutable, so on a hot path you can build the buffer once and refill
it:

```objeck
if(@draw_args = Nil) {
	@draw_mesh := IntRef->New(@mesh);
	@draw_mode := IntRef->New(mode);
	@draw_count := IntRef->New(count);
	@draw_args := Base->New[3];
	@draw_args[0] := @draw_mesh;
	@draw_args[1] := @draw_mode;
	@draw_args[2] := @draw_count;
}
else {
	@draw_mesh->Set(@mesh);
	@draw_mode->Set(mode);
	@draw_count->Set(count);
};
Proxy->GetDllProxy()->CallFunction(@draw_name, @draw_args);
```

This is worth 6-8% of a draw-heavy frame, and it introduces exactly one new way
to be wrong: **a buffer that is not refilled sends the previous call's values.**
That failure is invisible to most tests — draw the same thing twice and a stale
buffer produces the correct picture — so a fixture has to pass two *different*
values in succession and check that the second one landed.

There is a second hazard, and it is the reason to be careful about *what* you
park in a reused buffer. A buffer holding a caller's array or object keeps it
reachable for as long as the buffer lives, which is now forever. Release those
slots after the call:

```objeck
Proxy->GetDllProxy()->CallFunction(@m4_name, @m4_args);
@m4_args[1] := Nil;              # do not pin the caller's matrix
@m4_matrix->Set(@m4_idle);
```

Reuse buffers that carry primitives. Be deliberate about buffers that carry
references.

### What is left

After both rules a call is ~125ns, and about 90% of that is spent before your
C++ function is entered — argument-array setup, interpreter dispatch, and the
fact that `CallFunction` contains `EXT_LIB_FUNC_CALL`, which is in neither JIT
whitelist, so the method is always interpreted and every native call from
JIT-compiled code crosses back into the interpreter.

The practical consequence: **batch at the boundary.** One call that draws 500
instances beats 500 calls that draw one, by far more than any tuning of the call
itself.

---

## Building and shipping a library

Naming matters more than it looks. The Objeck side passes an extensionless name
to `DllProxy->New`, the VM appends the platform extension, and the deploy
scripts are what actually produce a file with that name — the build output is
usually named something else and renamed on the way into `lib/native/`.

- **POSIX**: `core/lib/<name>/build_linux.sh <name>` builds `<name>.so`, and
  `core/release/deploy_posix.sh` copies it to `lib/native/libobjk_<name>.so`.
  Note that `matrix.so` ships as `libobjk_ml.so` — the names do not have to
  match, and one place decides.
- **Windows**: an MSVC solution per library under `core/lib/<name>/`, built
  `Release|x64` (ONNX is the exception, `Release-DML|x64`), with the output
  copied to `lib/native/`.
- **macOS**: an Xcode project, same idea.

A new library needs to be added to each deploy script, to the CI build lists,
and — if it has an Objeck-side `.obl` — to the library lists in
`update_version.sh`, `update_version_arm.sh`, `code_doc64.in`, and the CI
"Rebuild libraries" step. `tools/cicd/check_doc_lists.py` guards the doc lists.

One trap worth stating plainly, because it has bitten this repo: **a library
that CI compiles is not necessarily a library that ships.** The `obu` updater
was built and tested by CI for two releases while being packaged in no archive
at all. Check the archive, not the build log.

---

## Traps

Collected from things that have actually gone wrong here.

- **`.obs` files are marked binary in `.gitattributes`.** `git diff` shows you
  nothing. Review by reading the file, and expect that two branches touching the
  same `.obs` cannot be merged in parallel — stack them.
- **A changed `.obs` requires committing the rebuilt `.obl`.** The `.obl` files
  under `core/lib/` are tracked, and a stale one fails on the platforms that do
  not rebuild it — usually as a pile of "undefined method" errors on macOS.
- **Never widen an existing trap's arity.** Append a new trap after `EXIT`
  instead. The bytecode is positional and the loader has its own parser.
- **`String[]` is indexed directly**, without the `[0]` holder indirection the
  other array types need.
- **A native handle in an `Int` is invisible to the collector.** If it needs
  releasing, the Objeck side needs a `Free`.
- **HTTP/1.1 serialises headers in Objeck; HTTP/2 and /3 do it natively.** Any
  validation that must apply to all three belongs on the Objeck side.
- **Silent failure is the default.** A misspelled uniform, an unloaded entry
  point, an oversized texture and a rejected mesh were all silent in the GL
  bindings until each was deliberately given a way to report.

---

## Is this a good design?

It is a good design for what it is: a thin, uniform, dependency-free bridge that
any C or C++ code can be reached through, with one header and no tooling. The VM
stays out of the way, and there is nothing to regenerate when a signature
changes.

The costs are real and worth naming. The name-based dispatch has no type
checking whatsoever, so a mismatch between the two halves is a runtime
corruption rather than a compile error. Errors have no channel and every library
invents its own. Everything crosses as `size_t*`, and correctness rests on the
caller and callee agreeing about a layout that nothing verifies. And per-call
overhead is high enough that the interface rewards batching much more than it
rewards micro-optimisation.

None of that argues for replacing it. It argues for the two habits this document
keeps coming back to: **batch at the boundary**, and **write the fixture that
can tell the difference** — because a native call that returns the previous
call's answer, or a stale buffer that draws the right picture for the wrong
reason, will pass any test that only asks whether something happened.
