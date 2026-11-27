# The `using` block

Status: implemented on `feat/using-block`. A new statement node (`Using`), handled by
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

## Exception safety

The resource is closed when an error unwinds past the block, not only on the normal exits. The
emitter lays the statement out like a `try`/`finally` (`EmitUsing`, modelled on `EmitTry`, #1247):

```
w := E; raise := Nil
TRY_START H1; body; TRY_END; JMP NORMAL
H1:  raise := last error
     TRY_START H2; if(w <> Nil) { w->Close(); }; TRY_END; JMP RAISE
H2:  (the error Close() raised is dropped)
RAISE: raise the saved error again
NORMAL: if(w <> Nil) { w->Close(); }
```

(`w` in the closes above is the hidden copy made when the block is entered, `#using_res_N := w`, the
first statement of the body; see Limits.)

and `return`, `break` and `continue` inside the body leave the region (`TRY_END`) and run the same
close, with a returned value kept on the operand stack, through `EmitCriticalUnlocks`.

| Situation | Result |
|---|---|
| body ends, `Close()` succeeds | closed once |
| `return`, `break`, `continue` leaving the block | closed once, return value computed first |
| an error unwinds past the block | closed, then the same error goes on |
| the body ended normally and `Close()` raises | that error propagates to the caller |
| the body raised and `Close()` raises too | the body's error wins; `Close()`'s is dropped |
| the resource is `Nil` | nothing is closed, no fault |

Errors are the recoverable runtime errors a `try` can catch (index, Nil dereference, bad cast, `raise`
of a `System.Error`); an error nothing catches still ends the program as before, and the resource is
then closed before the process exits only if a `try` further up recovers it.

### Cost: a method containing `using` is not JIT-compiled

The body is a `TRY_START` region, and the JIT does not compile a method that contains one (its callees
are compiled as usual). Measured on a Ryzen 9 7950X3D (loaded; medians of 3 runs, `obc -opt s3`):

| Test | with `using` | the same code closing by hand |
|---|---|---|
| hot loop: 20,000,000 calls of a method that opens a trivial object, reads it and closes it | 4.2 to 4.4 s | 0.70 s (JIT) |
| I/O loop: 20,000 file copies (open reader and writer, read a line, write it, close both) | 5.0 s | 4.55 s (+10%) |

So `using` costs about 6 times on a method whose whole cost is a few instructions, and about 10% when the
body does real I/O, which is what a resource is for. Do not use it in an inner loop of arithmetic; put
the block around the I/O and keep the hot work in a callee, which stays compiled. (An earlier version of
this feature closed inline, without a try region, and kept JIT-compilation but did not close on an
error; the all-exits behaviour was chosen over that speed.)

## Limits

* What is closed is the value the block acquired, `E`'s, which the compiler keeps in a hidden local
  when the block is entered. The name is an ordinary variable: the body may assign to it, and the
  block still closes the original, once. `w := Nil;` does not leave the resource open, and after
  `w := other;` the block closes the first value and `other` is the body's own to close (a second
  `using` is the way). A block entered with `Nil` closes nothing, whatever the body assigns. (Until
  v2026.11.4 the close read the name when the block was left, so `w := Nil;` leaked the resource and
  `w := other;` closed `other` in its place.)
* The type of the resource must have a method `Close()` taking no arguments; otherwise the compiler
  says so at the `using` line (after the ordinary "undefined method" error, which names the call).
* `using` does not call a destructor: nothing happens to a resource that is never closed other than
  what the garbage collector does.
* A `Close()` that returns a value is allowed (the result is discarded).

## Tests

* `programs/regression/using_block.obs`: close order and counts for a fall-through, early and last
  `return` (Int, String, Float and literal values), `break` and `continue`, a loop inside the body,
  nested and sequential blocks with the same name, a `return` from a loop in the body, a `leaving`
  block, real `FileWriter`/`FileReader` files, and `using` still usable as a variable and method name.
* `programs/regression/using_block_reassign.obs`: the name set to `Nil` and to another resource in the
  body, then each way out (fall-through, `return`, `break`, `continue`, an error), nested blocks whose
  names are assigned or swapped, a `Nil` resource assigned in the body, an assignment in a loop: the
  value the block acquired is closed, once, and nothing else is.
* `programs/regression/using_block_errors.obs`: an error in the body (closed, error goes on), `Close()` raising after a normal body, both raising (the body's error wins), nested blocks, a `return`, and a `Nil` resource.
* `bad_using_no_close.obs`, `bad_using_equals.obs`, `bad_using_scope.obs`: the compile errors, with
  line numbers.
