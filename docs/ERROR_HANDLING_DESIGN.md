# Error Types, Typed Recovery, `finally` and Re-raise — Design

Issue: [#1247](https://github.com/objeck/objeck-lang/issues/1247), the rest of it.
[RECOVERED_ERRORS_DESIGN.md](RECOVERED_ERRORS_DESIGN.md) shipped the first step: after a `?->`
recovers, `Runtime->GetErrorKind()` and `Runtime->GetError()` say what went wrong. This
document covers what the issue left open:
- user-defined error types and raising one;
- typed recovery (catching one kind and letting another through);
- `finally`;
- re-raising.

## Why

Research code raises and recovers constantly: a malformed record, a parse failure, a model
that did not converge, a timeout. Today a program can only recover from *runtime* faults
(Nil, index, divide by zero, cast). To signal its own failure it must provoke one on purpose.
`rosetta_exceptions.obs` divides by zero after leaving a note in a shared array. A handler
cannot let one error through and keep another, and there is no cleanup tied to a guarded
region.

## What the VM already has

Measured, not assumed (file references as of 2026-10-20):

- **Regions, not just calls.** `TRY_START handler` / `TRY_END` push and pop a per-thread
  handler (IP, operand-stack depth, call-stack depth; `interpreter.h:151-168`).
  `TryErrorRecovery` unwinds frames down to the saved depth and resumes at the handler. The
  compiler only ever wraps one call chain in a region (`EmitTryIntrinsic`), but nothing in
  the VM requires that. A region around statements in the same method sees that method's
  locals, so a handler needs no closure. Lambdas capture by value, so they could not do this.
- **The error record.** `RecoveredErrorState` (kind + message, per thread) is written on every
  recovery.
- **The JIT.** A method containing `TRY_START` is never compiled; its callees are. A fault in
  compiled code travels outward as a negative status to the interpreted frame that owns the
  handler (statuses −1 to −6, TRY_IN_JIT_DESIGN.md). A new kind of failure needs a new status
  on that path and nothing else.

Two defects found while reading (fixed in phase 1, below):
1. `PushTryHandler` silently drops a push when 16 handlers are active, but the matching
   `TRY_END` still pops, which removes an *outer* handler. Seventeen nested `?->` regions
   (recursion through a guarded call is enough) leave the program unguarded where it thinks it
   is guarded.
2. A failing trap goes straight to `VmExit(1)` (`Handle_TRAP`), never through
   `TryErrorRecovery`, although RECOVERED_ERRORS_DESIGN.md says `OTHER` covers traps.

## Decisions (proposed)

### 1. Errors are objects: `System.Error`

```ruby
class Error {
  New(message : String);
  method : public : GetMessage() ~ String;
  method : public : GetKind() ~ Runtime->ErrorKind;   # USER for a raised error
}
class ParseError from Error { ... }                    # user-defined: just a subclass
```

The VM's own faults surface as built-in subclasses, so one `on` clause can name them:
`NilError`, `IndexError`, `DivideByZeroError`, `CastError`, `RuntimeError` (for `OTHER`).
They are **built lazily**: recovery still records only a kind and a message (so it allocates
nothing, as now), and `Runtime->GetLastError()` materializes the object when it is asked for.

### 2. Raising: `Runtime->Raise(e : Error)`

A library function, not a keyword. It is a trap that never returns.
- Under a handler, it records the error (now the object, kept as a per-thread GC root) and
  recovers like any fault. `?->` recovers from it too, so existing code keeps working.
- With no handler, it prints `Unhandled ParseError: <message>` plus the stack trace, and the
  program exits, as an unguarded runtime fault does.
- From JIT-compiled code, the trap callback returns a new status (−7, raised), which travels
  outward like −1 to −6.

Re-raising is the same call: `Runtime->Raise(e)` in a handler passes the same object on.

### 3. Typed recovery: a block form

```ruby
try {
  rows := Load(path);
  model := Fit(rows);
}
on(e : ParseError) {
  "bad row: {$(e->GetMessage())}"->ErrorLine();
}
on(e : DivideByZeroError) {
  model := Nil;
}
finally {
  log->Close();
};
```

- `try { }` compiles to a region in the same method: `TRY_START h; body; TRY_END`.
- At `h`, the compiler binds `e := Runtime->GetLastError()` and tests each `on` clause in
  order (`TypeOf`, the same test `->As` uses). An `on(e : Error)` catches everything.
- **No clause matches → re-raise** (after `finally`). So a typed `try` never swallows an error
  it did not name. This is the difference from `?->`, which swallows every error, deliberately.
- `try`, `on` and `finally` become keywords. A scan of lib_src and programs found them only in
  comments.

### 4. `finally`

- It runs after the body completes, after a matching handler, and before an unmatched error is
  re-raised.
- A `return`, `break` or `continue` that leaves a `try` body or handler emits `TRY_END` (body
  only) and the `finally` statements first, as `leaving { }` does for `return`.
- `leaving { }` stays as it is (method scope, runs on return). It does **not** run when an
  error unwinds its method's frame; that stays documented. `finally` is the region-scoped tool.

### 5. Threads, cost, JIT

- **Threads:** errors are per thread and never cross. An unhandled error in a spawned thread
  ends that thread's run as a runtime fault does today.
- **Cost on the no-error path:** a `try` block costs one handler push and pop. Raising costs
  an allocation, which the program itself makes (`Error->New`).
- **JIT:** a method with a `try` block stays interpreted, as a method with `?->` does now (the
  JIT report names it). Its callees compile, and a raise inside them reaches the handler
  through the status path.

## Phases

1. **Errors and Raise** (shipped).
   - `System.Error` and the built-in subclasses; `Runtime->Raise`, `Runtime->GetLastError`.
   - The −7 status on both JIT backends.
   - Fixes for the two defects above. An overflowing handler stack becomes a runtime error,
     not a silent drop.
   - Tests: raise and recover through `?->`, interpreted and from compiled callees, three modes.
   - `rosetta_exceptions.obs` loses its divide-by-zero workaround (left for phase 4, with the
     Rosetta rewrite in the new form).

   As built:
   - `Runtime->Raise(e)` calls `RaiseNative(e, e->GetMessage())`. The VM never reads the
     object's fields, so a subclass may override `GetMessage`.
   - The recorded kind is `ErrorKind->RAISED`. The object sits in a per-thread slot that is
     a GC root (`AddPendingThreadRoot`) until the next recovery replaces it or `ClearError`
     clears it. A runtime fault's object is built on the first `GetLastError` and kept
     in the same slot.
   - A failing trap records its message for a handler (`TrapError` in common.cpp) instead
     of printing it. `StackInterpreter::ProcessTrap` recovers, and from compiled code the
     callback returns `JIT_STATUS_TRAP_FAILED` (−7).
   - The handler stack is a growing vector, so any depth of nesting recovers at the
     innermost handler.
   - Found on the way: an error under `?->` in a method whose tail call the optimizer made a
     loop crashes the JIT (#1269, on master before this work).
   - Tests: `error_raise.obs` (21 checks, three modes) and `bad_error_raise_unhandled.obs`.
2. **`try` / `on`** (shipped, with phase 3). Parser, analysis (clause types must be `Error` subclasses; a later clause
   shadowed by an earlier one is an error), emission, and unmatched re-raise. Tests cover
   nesting, a raise from three frames down, a raise from JIT code, and an unmatched error
   reaching an outer `try`.
3. **`finally`** (shipped), with `return`/`break`/`continue` leaving a region, and the interaction with
   `leaving`.
4. **Docs and Rosetta** (shipped): FEATURES.md, and the Exceptions tasks rewritten in the new form.

## Phases 2 to 4, as built

- **Contextual words.** `on` is a parameter name in the standard library
  (`SetQuitOnEscape(on : Bool)`), and named arguments made parameter names public API, so
  none of the three is reserved. `try` is a keyword only when `{` follows it, `on` only when
  `(` follows it after a `try` block or another clause, and `finally` only when `{` follows
  it there.
- **Parse.** A `Try` node holds the body, the clauses (type, a typed local and statements),
  `finally` and two hidden `System.Error` locals: the error being handled and one to raise
  again.
- **Analysis.** A clause's type must be `System.Error` or a subclass, and a clause that an
  earlier one covers is an error. A `try` with neither a clause nor `finally` is an error.
  `return` anywhere in `finally`, and `break`/`continue` that would leave it, are errors. A
  `try` returns on every path when its body and all its clauses do.
- **Emission** (`EmitTry`):

  ```
  raise := Nil
  TRY_START H1; body; TRY_END; JMP AFTER
  H1: error := GetLastError(); TRY_START H2
      per clause: error TypeOf T? e := error; statements; JMP DONE
      raise := error                      -- no clause matched
  DONE: TRY_END; JMP AFTER
  H2: raise := GetLastError()             -- a clause raised
  AFTER: finally; raise <> Nil? Raise(raise)
  ```

  The clauses run in a region of their own, so `finally` runs before an error raised in one
  of them goes on. A `try` sits on the emitter's exit stack beside `critical` sections. A
  `return`, `break` or `continue` that leaves it closes the region still open, then emits
  the `finally` statements, innermost first, as `leaving` does for a return.
- **Found on the way:**
  - The emitter counts string literals to decide when two must be swapped. A returned
    literal was still counted when the first literal of a `leaving` block (or of `finally`)
    was emitted after it. The two swapped, and the method returned the leaving block's
    literal while printing its return value. The count is saved and reset around both.
  - A program links only the library classes it names, so `GetLastError` could not build a
    `DivideByZeroError` for a program that never named it. It falls back to `System.Error`,
    since such a program cannot test for the subclass anyway.
- **Rosetta:** `rosetta_exceptions.obs` and
  `rosetta_exceptions_catch_an_exception_thrown_in_a_nested_call.obs` now use exception
  classes, `try`/`on`/`finally` and rethrow. Their shared-note workarounds are gone.
- **Tests:** `try_blocks.obs` (13 checks, three modes) and five `bad_try_*.obs`.

## Alternatives considered

- **A typed `?->`** (`x?<ParseError>->M()`): it keeps one construct, but cannot hold
  statements, cleanup or several handlers, and its syntax reads badly.
- **Handlers as lambdas** (`Runtime->Try(\() => {...}, \(e) => {...})`): no new syntax, but
  closures capture by value, so the body could not assign the caller's locals. That is the
  common case (`model := Fit(rows)`).
- **Checked errors (a `raises` clause):** heavy for research code, and nothing in the
  libraries would declare them. Not proposed.

## Not in scope

- Errors crossing threads (a joined thread re-raising in its joiner).
- Recovering from a native C-stack overflow (TRY_IN_JIT_DESIGN.md leaves it unrecoverable).
