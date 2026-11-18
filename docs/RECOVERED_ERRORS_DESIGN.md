# Reading a Recovered Error — Design

Issue: [#1247](https://github.com/objeck/objeck-lang/issues/1247) (typed catch, finally,
rethrow, and reading the error in a `?->` handler), split out of #1186. This covers the
issue's **smallest useful step**: letting the code after a `?->` read what went wrong.

**Status:** implemented (PR #1267, `0f13a433a7`, v2026.10.1). It is the first step of #1247; the rest (user-defined error types, `Runtime->Raise`, typed `try`/`on`/`finally`, re-raise) shipped afterwards and is in [ERROR_HANDLING_DESIGN.md](ERROR_HANDLING_DESIGN.md). Since then `ErrorKind` gained `RAISED`, and the recorded error is also available as an object through `Runtime->GetLastError()`.

## Why

`?->` (`->Try()->`) turns any runtime error raised below a guarded call into Nil (or the `??`
default), and execution continues. Every recovered error looks the same: the runtime prints
the message only when nothing is guarding. Code that must react differently to a missing key
and a divide by zero had to record a note in shared state before raising, and the test
`rosetta_exceptions.obs` documents that workaround. Research code recovers from errors
constantly (a bad sample, a failed parse, a timeout), and it needs to log *which* error and
*why*.

```ruby
value := table->Get(key)?->Parse() ?? 0;
if(Runtime->HasError()) {
  kind := Runtime->GetErrorKind();      # Runtime->ErrorKind->INVALID_CAST, ...
  "skipped {$key}: {$(Runtime->GetError())}"->ErrorLine();
  Runtime->ClearError();
};
```

## Decisions

- **The last recovered error, per thread.** When a `?->` region recovers, the VM records the
  error's kind and message in the thread's state. It stays there until the next recovery
  replaces it or `Runtime->ClearError()` clears it. It is per thread because try handlers are
  already per thread. "Last" rather than a stack, because recovery is not nested from the
  program's point of view: the region that recovered is the one the code just ran.
- **The kind is a small enum**, matching the classes the JIT already reports as statuses:

  | `Runtime->ErrorKind` | raised by |
  |---|---|
  | `NONE` | no recovered error, or cleared |
  | `NIL_DEREFERENCE` | a method call or field access on Nil |
  | `INDEX_OUT_OF_BOUNDS` | an array index outside the array |
  | `DIVIDE_BY_ZERO` | integer or float division or modulo by zero |
  | `INVALID_CAST` | `->As(T)` on an object that is not a `T` |
  | `OTHER` | any other runtime error (a trap or native call) |
  | `RAISED` | `Runtime->Raise` (ERROR_HANDLING_DESIGN.md) |

- **The message is the text the runtime prints** for an unguarded error, without the
  `>>> <<<` markers. One difference by mode: an index error raised in JIT-compiled code is
  reported by a guard stub that has only the status, so its message is "Index out of bounds"
  without the index and the size the interpreter includes. A cast error carries both class
  names in either mode, because the bridge callback records it while they are in hand.
- **A Nil receiver is not an error.** `x?->M()` with `x` Nil short-circuits to Nil without
  calling anything; that is the nil-safe operator's normal case, so nothing is recorded. A Nil
  dereference *inside* the call is recorded.
- **Nothing is recorded when no handler exists.** The program ends as before.
- **Cost on the no-error path: none.** Recording happens only inside recovery.

### API (`System.Runtime`, appended traps)

```ruby
function : HasError() ~ Bool         # a recovered error is recorded
function : GetError() ~ String        # its message, or Nil
function : GetErrorKind() ~ Runtime->ErrorKind
function : ClearError() ~ Nil
```

The three new traps (`RUNTIME_GET_ERROR`, `RUNTIME_GET_ERROR_KIND`, `RUNTIME_CLEAR_ERROR`) are **appended after the last existing trap at the time**; `HasError` is not a trap but `GetErrorKindNative() <> 0` in `lang.obs`. The rule is (the memory rule: a
trap's number and arity are never changed).

## Not in this step (all since shipped)

These were the rest of #1247 when this step was written. They are now in
[ERROR_HANDLING_DESIGN.md](ERROR_HANDLING_DESIGN.md): user-defined error types and
`Runtime->Raise` (there is no `throw` keyword), typed catch (`try`/`on`), `finally`, and
re-raise.

## Implementation

1. **VM:** a `RecoveredErrorState` (kind and message) next to the per-thread `TryHandlerState`.
   - `TryErrorRecovery(stack_pos, kind, message)` records it when it recovers. Its call
     sites already raise one error each, so each passes its own kind and text.
   - The bridge's status path maps the JIT statuses (−1 to −5 here; −6 and −7 were added by #1247) to kinds when it recovers
     for a compiled frame.
2. **Traps:** `RUNTIME_HAS_ERROR`, `RUNTIME_GET_ERROR`, `RUNTIME_GET_ERROR_KIND` and
   `RUNTIME_CLEAR_ERROR`, added to `traps.h`. Their handlers go in the trap processor, and
   `linker.cpp` needs nothing because traps are operands, not opcodes.
3. **lang.obs:** the four functions and the `ErrorKind` enum on `Runtime`. lang.obl is
   rebuilt.

## Tests

`recovered_errors.obs` covers each kind:
- raised interpreted, and raised inside JIT-compiled code (three modes);
- the message text;
- `ClearError`;
- a later recovery replacing an earlier one;
- no record after a call that succeeds.

`rosetta_exceptions.obs` was later rewritten with `try`/`on`/`finally`, and its shared-note workaround is gone.

## How to check

`obc -src programs/regression/recovered_errors.obs -dest t.obe && obr t.obe`, then with `obr --jit=off t.obe` and `obr --jit=1 t.obe` (interpreted and JIT-compiled raises).
The traps are in `core/shared/traps.h`; `Runtime->ErrorKind` is in `core/compiler/lib_src/lang.obs`.
