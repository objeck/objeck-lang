# Web.Server: reimplementation over Web.HTTP.Server

Status: design, 2026-08-30. Supersedes the native-bridge design for issue #654.

## Why this exists

`Web.Server` shipped as `web_server.obl` in every release and could not be used
by anyone. Three independently fatal reasons:

1. **No implementation exists.** Its 13 native entry points appear in exactly one
   file in this repository -- `core/compiler/lib_src/web_server.obs`, the binding
   itself. Verified with `rg --no-ignore` across the whole tree. There is no
   `.cpp`, no build target, and no `libobjk_web_server` in any deploy, on any
   platform.
2. **Nothing sets `OBJECK_LIB_WEB_SERVER`**, which is the property the `DllProxy`
   is constructed from.
3. **`Request` and `Response` declare no constructor.** Every method reads an
   `@request`/`@response` Int handle that only a native host can supply, so a
   program cannot obtain an instance. Confirmed by compiling one: it fails.

## Why "just write the native library" was not the fix

The design is a **per-host bridge**, not a missing file. `DllProxy->New(...)`
takes the library name from a runtime property, and the doc comment names the
intended hosts: Nginx, IIS and Apache. The opaque Int handle is a pointer into
whichever host owns the request, and those hosts have entirely different request
structures (`ngx_http_request_t`, `IHttpContext`, `request_rec`).

So the design requires **one bridge library per host**, each compiled against
that host's SDK. A single generic `libobjk_web_server` is not a thing that can
exist. That is why none was ever written.

Meanwhile `Web.HTTP.Server` in `net_server.obs` already provides a complete,
working, tested `Request`/`Response` -- params, cookies, headers, content,
compression, forwarding, status codes -- in pure Objeck, exercised by the
regression suite today. `Web.Server` was a strictly smaller, non-functional
parallel API to it.

## Decision

Reimplement `Web.Server` in pure Objeck as an adapter over `Web.HTTP.Server`.

- keeps the `Web.Server` bundle, class names and method signatures, so existing
  source against the published API still compiles
- deletes the native indirection entirely: no native library, no new build
  targets, no new CI legs, no release risk
- makes the API genuinely usable and testable end to end against real HTTP

Rejected: shipping a mock native library (verifies the binding, not any
behaviour, and still adds a native artifact to five platform legs); shipping a
real host bridge (delivers the original intent but needs a host SDK and cannot
be tested in CI without running the host); retiring the API outright.

## Gap analysis

The adapter is a delegation for most of the surface. Three methods have no data
source in the underlying server today, established by reading the code:

| Method | Source | Resolution |
|---|---|---|
| `GetHeader`, `ReadBody`, `SetContentType`, `WriteBody` x2, `Redirect`, `SetHeader`, `RemoveHeader`, `ClearAll` | present on `Web.HTTP.Server.Request`/`Response` | direct delegation |
| `GetMethod` | **absent.** The verb is parsed in `ServeOne` and used to dispatch to `ProcessGet`/`ProcessPost`, but never stored: `Request` holds only the path | thread the verb into `Request` at its four construction sites, via a new overloaded constructor |
| `GetRemoteAddress` | **available but discarded.** `@client->GetAddress()` is read at `net_server.obs:2015` only inside `if(@is_debug)`. `SockTcpAccept` (`common.cpp:4537`) sets `sock_obj[1]` to the client address and `sock_obj[2]` to the client port, so on an accepted socket these are genuinely the peer | thread into `Request` alongside the verb |
| `GetLocalAddress` | **no source.** `TCPSocket` exposes `GetAddress()`/`GetPort()`, which on an accepted socket are the *remote* end. There is no `getsockname` accessor | see below |

### GetLocalAddress

The honest options are:

1. add a `getsockname` trap and a `TCPSocket` accessor -- correct, but
   `TCPSocket` lives in `lang.obs`, so it drags in the `lang.obl` bootstrap
   asymmetry that has reddened master before. **Not during a release.**
2. thread the bound port from `WebServer->Serve(callback, port, ...)`, which is
   authoritative for the port but has no host part
3. derive from the `Host` request header -- zero plumbing, but client-controlled
   and therefore not trustworthy as the server's own identity

**Chosen: 2, falling back to 3 for the host part**, and documented precisely as
"the address this server is bound to" rather than implying a socket-level
answer. Option 1 is the correct long-term fix and should follow as its own PR
after the release, where a red windows-x64 leg is diagnosable in isolation.

## Phases

1. **Plumbing in `net_server.obs`** (additive, backward compatible): carry the
   request verb and peer address into `Web.HTTP.Server.Request` through a new
   overloaded constructor; existing constructors keep working unchanged. Add
   `GetMethod()` and `GetRemoteAddress()` accessors.
2. **Rewrite `web_server.obs`** as delegating adapters. Remove the `Proxy`
   class, the 13 `@fn_*` name fields and every `CallFunction`. Add constructors
   taking the underlying `Web.HTTP.Server` objects.
3. **A handler adapter** so a `Web.Server`-style handler can be served by the
   existing `WebServer`, making the API reachable without any new server.
4. **Regression test** driving real HTTP through the adapter and asserting on
   every method, replacing 0-of-13 coverage.
5. **Docs**: update the bundle doc comment, and note the `Web.HTTP.Server`
   relationship so the two APIs are not mistaken for competitors.

## Compatibility

`web_server.obl` keeps its name, bundle and method signatures. Source written
against the published API continues to compile. Nothing that worked before
breaks, because nothing worked before.

## Release impact

None by construction. No native artifact, no build-system change, no
bootstrap-library change. Lands on its own branch and gates on its own CI.

## Implementation findings

Four things surfaced while building this that were not visible from reading, and
each cost a wrong assumption first.

**1. `virtual` in Objeck is a declaration, not a modifier.** `Handler.Process`
was first written as a concrete method returning false, for subclasses to
override. It is bound statically from `ProcessGet`, so the override never ran:
every response came back 200 with an empty body and the handler was never
entered. Declaring it `method : public : virtual : Process(...) ~ Bool;` with no
body is what produces dynamic dispatch. Every assertion in the test failed until
this changed, which is why the test asserts on values rather than on completion.

**2. The server only serialises a full response for code 200.**
`ProcessResponse` builds headers and a body under `if(response->GetCode() = 200)`
and otherwise falls to a select that writes a bodyless status line. The
underlying `Response` defaults to **400**, so an adapter that sets no code
produces `400 Bad Request` with `Content-Length: 0` no matter what it wrote. The
adapter now sets 200 in its constructor.

**3. A 302 takes its `Location` from `GetReason()`, not from a header.** The 302
branch writes `Location: {reason}` by hand and emits no application headers, so
`SetHeader("Location", url)` is silently dropped. `Redirect` uses `SetReason`.

**4. `GetHeader` does a raw lookup against lower-cased keys.** Request header
names are stored lower-cased at parse time, so `GetHeader("Content-Type")` would
miss. The adapter normalises.

## Two pre-existing defects found, not caused by this work

**A server program segfaults on exit.** Any program that starts
`WebServer->Serve` on a thread and then returns from `Main` dies with SIGSEGV
during teardown, with the server thread parked in `Accept`. Reproduced on
**pristine master** with an unmodified `net_server.obs` and no `Web.Server`
involved, 3 runs of 3. No in-tree test caught this because no test both starts
the server and exits normally. The regression test calls `Runtime->Exit(0)` to
avoid scoring a passing test as a crash, and the crash is filed separately.

**The accepted socket class can be dropped by the linker.** `SockTcpAccept`
allocates the client socket natively through `GetSocketObjectId()`, so there is
no static reference for the linker to follow. A program that uses the server
without otherwise mentioning `TCPSocket` fails at runtime with
`unable to find class: System.IO.Net.TCPSocket` -- and in one arrangement
crashed outright rather than reporting that. Filed with the above.
