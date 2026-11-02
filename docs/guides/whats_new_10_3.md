# What is new in v2026.10.3

> v2026.10.3 is in development. This page describes what is on `master`; the
> [CHANGELOG](../../CHANGELOG.md) is the complete list and says when the release is published.
> The examples on this page were compiled and run with a toolchain built from `master`.

v2026.10.3 is a hardening release. It adds almost no new syntax; it makes existing things
safer, and one of them, the HTTP client, behaves differently enough to read about.

## Upgrading: recompile your programs and libraries

The `.obe` (program) and `.obl` (library) file formats changed, because an `Int[]` and an object
array are now different kinds of heap object. A program or library built by an earlier version
is refused with a message that says so. Recompile every `.obe` from source with the new `obc`
and rebuild every `.obl` of your own.

This is what the new `obr` printed for a program compiled by the previous toolchain:

```text
Cannot run 'hello_old.obe': it was compiled before an Int[] and an object array were allocated with distinct opcodes,
	so this runtime would read its object arrays as Int[]. Recompile its source with this toolchain's obc.
```

and for a program compiled by the new toolchain, run by the previous `obr`:

```text
Unknown file type for 'hello_new.obe'.
```

Two smaller notes:

- A malformed `.obe` is refused by a verifier with a message before anything runs. A valid program
  is never refused.
- Code that stopped one server and started another on the same port within four seconds, and
  relied on every request getting a fresh connection, should wait for the old server to exit (see
  the next section).

## HttpClient reuses connections

`HttpClient` used to open a connection for each request and close it. On Windows that could run
the machine out of ports: a process making about 130 requests a second used all 16,384 and every
connect then failed. Now plain-HTTP connections wait in a pool when the server allows it, and a
`GET` or `DELETE` to the same host and port reuses one. `POST`, `PUT` and `PATCH` never do.
`HttpClient->CloseIdleConnections()` closes the waiting ones, and `WebServer->Stop()` calls it.

In the program from the [HTTP guide](http.md), twenty `GET`s now use one connection:

```text
GET:  20 of 20 answered, 1 connection(s) used
```

where the same program on a toolchain from before the change used 20. Details and the full program:
**[HTTP client and web server](http.md)**.

## Libraries stop dereferencing Nil

A failed request, an empty reply, a missing file or a partial document no longer ends the program
in the libraries that used to:

- OpenAI, Gemini, misc and net (a refused connection answers with status `0`, not `Nil`);
- `JsonParser` and `XmlParser` built from `Nil` text, and `XmlParser->FindElements` on text that
  did not parse;
- `RegEx` with a `Nil` pattern or `Nil` text;
- RSS, JSON-RPC and the web server's multipart parser.

`RegEx` repeat counts are bounded: `a{99999999999}` used to fill the heap while compiling and
never return. A count is now at most 1000, the low bound may not exceed the high one, and a
pattern that expands past 100,000 instructions is not OK and matches nothing. The JSON-RPC client
sends a fresh request id each call, `WebDownloader` no longer skips URLs when given seven or
more, and sorting a vector of `Collection.Pair` no longer ends in `Invalid object cast`.

See **[JSON, XML and regular expressions](data_and_text.md)** for a program that feeds each of
them bad input and shows the answers, and **[Collections](collections.md)** for the pair sort.

## The language server and debugger

Edits analyze faster (a hover after an edit in a small workspace went from 165 ms to 53 ms on
Windows), a library that fails to load no longer takes the server down, and the debug adapter
keeps output whole, orders its messages, can pause and refuses requests it does not understand.
See **[Editor setup and the language server](language_server.md)**.

## Under the hood

- Compiled code now tells the garbage collector exactly which stack words are references on x64
  and ARM64, so an `Int` that happens to equal an object's address is never rewritten by a
  collection ([design](../GC_ROOTS_DESIGN.md)).
- The loader was fuzzed for hundreds of millions of executions, which fixed a crash on a repeated
  class or method id, allocations of up to 16 GB from a corrupt count and a read past the end of a
  short `.obe`; errors are returned instead of calling `exit(1)`
  ([trust model](../SECURITY_MODEL.md)).
- The native library ABI is versioned, so a library can tell it was built for a different VM.

## A new example: checkers with ten AI levels

[`programs/examples/checkers`](../../programs/examples/checkers/) is an SDL2 game with ten
opponents built from `System.AI`'s `Minimax` and `MonteCarloTreeSearch` and the example's own
search, a self-play learner that tunes the evaluation, several piece sets, and music generated while
you play. Its move generator is checked against the published
perft counts. See **[Game AI with System.AI](ai.md)** for a small version of the same idea you can
read in one sitting.

## New and updated documents

- Getting-started guides (this set): [Query.Structured](query.md),
  [HTTP](http.md), [JSON, XML and RegEx](data_and_text.md), [Collections](collections.md),
  [Game AI](ai.md), [Editor setup](language_server.md).
- [`docs/COLLECTIONS_GUIDE.md`](../COLLECTIONS_GUIDE.md): which collection to use and what each
  operation costs.
- [`docs/ERROR_CONVENTIONS.md`](../ERROR_CONVENTIONS.md) and
  [`docs/ADDING_AN_INTRINSIC.md`](../ADDING_AN_INTRINSIC.md).
- [`docs/ARCHITECTURE_ASSESSMENT_2026_11.md`](../ARCHITECTURE_ASSESSMENT_2026_11.md) and
  [`docs/SECURITY_MODEL.md`](../SECURITY_MODEL.md) (an `.obe` is native code).
