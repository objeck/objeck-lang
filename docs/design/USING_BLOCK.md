# The `using` block

Status: implemented on `feat/using-block` (draft PR). A new statement node (`Using`), handled by
the parser, the context analyzer and the emitter; no VM, JIT or library change.

## What it is for

A file, socket or statement that is opened in a method has to be closed on every way out of it, and
the closes are easy to leave off a `return` that was added later:

```
r := FileReader->New(path);
line := r->ReadLine();
if(line = Nil) {
  r->Close();           // forgotten in the first version of this method
  return "";
};
r->Close();
return line;
```

```
using(r := FileReader->New(path)) {
  line := r->ReadLine();
  if(line = Nil) { return ""; };
  return line;
};
```

## Syntax

```
using(<name> := <expression>) { <statements> }
```

* `name` is a new local, visible in the block only. Two blocks in one method may both name theirs
  `f`.
* `using` is a *contextual* word, as `try` is (#1247): it opens a block only where an identifier
  `using` is followed by a `(` whose matching `)` is followed by `{`, which a call statement never
  is. So `using := 5;`, `using->Foo();` and `using(x);` (a call to a method named `using`) are
  unchanged.
* Only the `name := expression` form: one resource per block. Nest blocks for several; they close
  innermost first.

## Meaning

`using(w := E) { B }` is `w := E`, then `B`, then `w->Close()`, where the close also runs

* before every `return` inside `B` (after the returned value has been computed, so
  `return r->ReadLine();` reads, then closes, then returns the string),
* before every `break` or `continue` inside `B` that leaves `B` (one in a loop that is itself inside
  `B` does not close anything),
* in `leaving` order: a method's `leaving` block runs first, then the resources close.

Nested blocks close innermost first at every exit.

## Closeable interface or `Close()` by name?

Decided on the library: **`Close()` is looked up by name**; there is no interface.

| Class | `Close` |
|---|---|
| `FileReader`, `FileWriter`, `FileReaderWriter`, `Pipe`, `TCPSocket`, `TCPSecureSocket`, `UDPSocket`, `DTLSSocket` | `method : public : Close() ~ Nil`, and each implements `InputStream` and/or `OutputStream`, the only interfaces that declare `Close()` |
| `TCPSocketServer`, `TCPSecureSocketServer`, `UDPSocketServer`, `DTLSSocketServer`, `Http2Session`, `Http3Session`, and the ODBC, ONNX, CNN, SDL, OpenCV and query handles | the same `Close() ~ Nil`, but implement neither stream interface |
| the OpenAI client (`openai.obs`) | `Close() ~ Bool` |

About 56 library methods are named `Close()`, almost all returning `Nil` (one returns `Bool`). The stream
interfaces cover the files, pipes and client sockets but not the servers, the HTTP sessions or any
handle, so an interface-based `using` would refuse most of the things a program wants to close; a new
`Closeable` interface would have to be added to every one of those classes, in a library set that is
rebuilt and re-signed as a whole, and every user class with its own `Close()` would have to opt in
before it worked with `using`, for a check the analyzer can already make by compiling the call.
Calling the method by name needs no library change and reports a missing `Close()` at the `using`
statement.

The close is compiled as the ordinary call `w->Close()`, so a `Close()` that returns a value works
(the result is discarded), and so does one inherited from a parent class.

## Why not `try`/`finally`

`try { } finally { }` (#1247) already closes on every exit, including an error, but the method that
holds it is not JIT-compiled (the body is a `TRY_START` region). A loop that opens a file per
iteration, or a handler that runs per request, would drop from native code to the interpreter. `using`
is emitted inline instead:

* the close is a straight-line call (no label), emitted once after the body and once before each
  `return`, `break` and `continue` that leaves it, through the same exit hook `critical` and `try`
  use (`EmitCriticalUnlocks`);
* a returned value is on the operand stack while the close runs. A call leaves it untouched, and the
  absence of a label keeps the JIT's "empty working stack at a label" rule intact;
* the method is compiled by the JIT exactly like one without `using` (checked with
  `OBJECK_JIT_THRESHOLD=1`).

The price is stated plainly: **an error that unwinds past the block does not close the resource.**
Objeck runtime errors end the program unless a `try` recovers them, so this only matters when the
`using` is inside a method called from a `try` block that recovers an error raised in the body. If
that matters, write `try { ... } finally { r->Close(); }` for that block. (The `AgentScope` library
(#1837) uses a real `finally` for the same reason.)

## Limits

* The resource must not be `Nil`: `Close()` on `Nil` is the usual Nil dereference, at the close. The
  constructors of the library's closeable classes return an object even when the open failed (check
  `IsOpen()`), so `using(f := FileReader->New(p))` is safe.
* Assigning to the resource variable inside the block closes the new value, not the old one. Do not.
* The type of the resource must have a method `Close()` taking no arguments; otherwise the compiler
  says so at the `using` line (after the ordinary "undefined method" error, which names the call).
* `using` does not call a destructor: nothing happens to a resource that is never closed other than
  what the garbage collector does.

## Tests

* `programs/regression/using_block.obs`: close order and counts for a fall-through, early and last
  `return` (Int, String, Float and literal values), `break` and `continue`, a loop inside the body,
  nested and sequential blocks with the same name, a `return` from a loop in the body, a `leaving`
  block, real `FileWriter`/`FileReader` files, and `using` still usable as a variable and method name.
* `bad_using_no_close.obs`, `bad_using_equals.obs`, `bad_using_scope.obs`: the compile errors, with
  line numbers.
