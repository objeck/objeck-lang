# Making `AddHeader()` send headers on HTTP/2 and HTTP/3 — plan A′

> **Revision 2.** Rewritten after three review passes and after reading how
> HTTP/1.1 already does this. Revision 1 proposed changing the existing trap's
> arity; that is now rejected — see "Why not plain Option A".
>
> **Validation is already done and shipped** (#616 native, #617 Objeck). What
> remains is purely plumbing headers to the native side for two protocols.

## What HTTP/1.1 taught us

`HttpClient->AddHeader` **works**, and looking at why is what settles this design.

`net.obs` builds the entire request as text, in Objeck:

```objeck
oper->Append(request_key); oper->Append(": ");
oper->Append(request_value); oper->Append("\r\n");
```

So headers need no native plumbing at all — they are serialized before the
socket ever sees them. HTTP/2 and HTTP/3 are the opposite: `net_h2.obs` and
`net_quic.obs` hand the request to a native trap whose signature is
`(body, content_type, path, method, instance)`. There is no headers argument,
so `@request_headers` is populated and then dropped on the floor.

Three consequences, all now confirmed:

1. **The gap is exactly two protocols.** HTTP/1.1 and HTTPS are unaffected —
   they read their map (`net.obs:816,985,1201`,
   `net_secure.obs:568,726,1130,1347`).
2. **HTTP/2 has the identical dead map to HTTP/3.** `common.cpp:6809` iterates
   a `request_headers` that nothing writes, same as `:7416`. Fix both in one
   change or pay the artifact rebuild twice.
3. **Validation belongs in Objeck, and it is already there.** Because HTTP/1.1
   never reaches native code, a trap-side check could never have covered it —
   which is why `Web.HTTP.HeaderCheck` (#617) guards all six `AddHeader`
   methods instead. That is the single choke point for every client. The
   native validators (#616) stay as defence in depth for the two protocols
   that do cross into C++.

**This removes the largest and riskiest part of revision 1.** No octet rules,
no pseudo-header policy, no `AddHeader` semantics to design. It is done.

## The remaining work

Flatten `@request_headers` into a `String[]` in `net_h2.obs` / `net_quic.obs`,
pass it through a **new** trap, and populate `ctx->request_headers` — the map
both backends already consume correctly.

### Why the flattening cannot live in `lang.obs`

`update_version.sh:5` builds `lang.obl` with **`-strict` and no `-lib`**, so
`lang.obs` compiles against *zero* libraries. `Hash<K,V>` is declared in
`gen_collect.obs:1665` → `gen_collect.obl`. **`lang.obs` cannot name
`Hash<String,String>` at all** — it would not compile.

`net_quic.obs` builds with `-lib net,gen_collect,cipher` and already declares
the field. So the flatten happens there; `lang.obs` only ever sees a
`String[]`, a shape it uses in ~20 places already.

### Why not plain Option A (change the existing trap's arity)

Revision 1 proposed adding a parameter to `HTTP3_REQUEST`. Three findings kill it:

- **CI does not rebuild `lang.obl`.** Only linux-x64, linux-arm64 and
  macos-arm64 bootstrap it; **windows-x64 consumes the committed artifact**.
  Revision 1 claimed this was "mitigated by both being compiled together" —
  that was simply false.
- **A stale `lang.obl` is undetectable.** `VER_NUM` is a build stamp derived
  from the version string, not a content hash, so two `lang.obl` with
  different contents and the same `VER_NUM` are indistinguishable to the
  linker. There is no slot-count verifier.
- **The failure is silent, and JIT-only.** The interpreter ignores the `TRAP`
  operand entirely; only the JIT reads it (`jit_amd_lp64.cpp:835`,
  `jit_arm_a64.cpp:786`). An arity mismatch therefore corrupts the operand
  stack **only once the JIT threshold trips** — the same shape as the
  `TRAP *_ARY_LEN` over-count that crashed JIT compiles in `916b93187`.

Revision 1 also contradicted itself — "pushed before the existing arguments"
and "pops the new argument first" cannot both hold. Push order is reversed on
pop. **Push last, pop first.**

### Option A′ — a new trap, leaving the existing one untouched

Append `HTTP3_REQUEST_HDRS` / `HTTP2_REQUEST_HDRS` **after `EXIT`** in
`traps.h`, and add an overloaded 5-argument `Request` to `lang.obs`. The
existing 4-argument trap and method keep working, unchanged.

Why this is strictly safer: a stale `lang.obl` cannot desync the stack,
because no existing arity changes. Worst case it emits the old call and
`AddHeader` stays a no-op — today's behaviour — and the new test **fails
loudly on windows-x64** instead of silently corrupting the operand stack.
Old `net_h2.obl` / `net_quic.obl` still link, because the 4-argument method
still exists.

Cost: about six extra lines. Overloading is already used in `lang.obs`
(`Split(delim : String)` vs `Split(delim : Char)`).

**`traps.h`'s enum is unnumbered and the ordinal is the wire id** — inserting
anywhere but the end renumbers ~50 traps. Append after `EXIT`. No new
*opcode*, so `linker.cpp`'s `ReadStatement()` binary parser is untouched.

## Files, in lockstep

| # | file | change |
| --- | --- | --- |
| 1 | `core/shared/traps.h` | append 2 ids **after `EXIT`** |
| 2 | `core/compiler/scanner.h` / `scanner.cpp` | token + `ident_map` |
| 3 | `core/compiler/parser.cpp` | two duplicate token→instruction switches |
| 4 | `core/compiler/intermediate.cpp` | new case, `TRAP 7L`, headers as the last `LOAD_INT_VAR` |
| 5 | `core/vm/common.cpp` | new handler; pop headers **first**; populate `ctx->request_headers` **before** the `#ifdef` fork so all three backends get it |
| 6 | `core/compiler/lib_src/lang.obs` | overloaded 5-arg `Request` (H2 + H3) |
| 7 | `net_h2.obs` / `net_quic.obs` | flatten the Hash, call the overload |
| 8 | `core/lib/lang.obl`, `net_h2.obl`, `net_quic.obl` | rebuilt, **same commit** |

The `PopInt` block sits *outside* the backend `#ifdef`, so one edit serves
ngtcp2, WinHTTP and the no-engine stub. `obd` and the module build need no
change beyond a rebuild.

## Read-path requirements (non-negotiable)

The existing `String[]` walker in `lib_api.h:601-616` has live bugs — it
bounds-checks against `[0]` while iterating to `[2]`, and dereferences
elements without a null check. Do not copy it. Follow `diags.cpp:195-210`:

1. Null-check the array **before** any `[0]` indirection.
2. Count from `[2]`, not `[0]`.
3. Assert `dim == 1`; data offset is `arr + arr[1] + 2`.
4. Reject an odd element count (or use two parallel arrays and check lengths
   are equal — structurally safer).
5. Null-check each element **and** its inner char array.
6. **Copy into `std::string` immediately.** The array is popped off the op
   stack and is no longer a GC root, and the trap later calls
   `AllocateArray`/`CreateStringObject`, either of which can move the young
   generation. The body is already copied for exactly this reason.
7. Cap header count and total size.

## Testing

**The planned test in revision 1 could not have failed.** The HTTP/2 and
HTTP/3 network tests run only on linux-x64 under `continue-on-error` — a
header-echo test added there can never redden the build. That is the same
defect this branch already hit twice.

So, two tests:

- **Gating**, in `programs/regression/`: assert the *flattened `String[]`* is
  what we expect, offline. No socket, deterministic, and it can fail. Must
  call `Runtime->Exit(1)` — the runners grade on exit code and never read
  output.
- **Non-gating**, in `programs/tests/`: live header echo asserting the
  **echoed value**, not a status code.

Write the gating one first and watch it fail.

## Sequencing

1. Land validation — **done** (#616, #617).
2. Write the failing gating test.
3. Implement in table order, 1 → 7.
4. Rebuild artifacts: `_SYSTEM` bootstrap compiler for `lang.obl`
   (`$env:CL="/D_SYSTEM"`, `-t:Rebuild`, then **restore a normal `obc`**),
   then replay `update_version.sh:10-39`. Verify on disk with
   `find core/lib -maxdepth 1 -name "*.obl" -mmin +15` — anything listed did
   not rebuild.
5. **One commit**: sources + every changed `.obl`. Splitting them is what
   turned master red on #595→#596.
6. Verify on Windows **and** WSL2 before pushing. windows-x64 is the leg that
   fails alone, and it is the one most users are on.

## What must not happen

- `lang.obs` changed without its rebuilt `lang.obl` in the same commit.
- A test that passes whether or not headers are sent.
- A VM pointer retained across an allocation inside the trap.
- Weakening the WinHTTP downgrade assertion or the header validators to make
  a test pass.
