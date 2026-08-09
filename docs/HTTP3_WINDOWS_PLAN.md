# Enabling HTTP/3 on Windows — implementation plan

> **OUTCOME (2026-08-08): shipped, but NOT by the plan below.**
>
> HTTP/3 now works on Windows via **WinHTTP over MsQuic** (Windows 11 /
> Server 2022+) — see `core/vm/win_http3.h`, guarded by
> `OBJECK_HAS_WINHTTP_H3`. The POSIX ngtcp2 path was left untouched; the three
> traps gained `#elif` arms. No vendored QUIC library, no second TLS backend,
> no new DLL to deploy (`winhttp.lib` ships with Windows).
>
> **The plan below proposed vendoring ngtcp2 + GnuTLS through vcpkg. Three
> review passes rejected it before any code was written** — that rejection,
> and the reasoning in the review sections at the end, is why this document is
> worth keeping. The replacement was proven in a single throwaway probe:
> `status=200 protocol=HTTP/3`, zero new dependencies.
>
> Two facts found the hard way, not in any doc at the time:
> - `WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL` must be set on the **session**
>   handle *before* the request is created. Setting it later is accepted and
>   silently ignored.
> - WinHTTP treats HTTP/3 as a **preference** and downgrades transparently, so
>   a 200 proves nothing. Every request must assert
>   `WINHTTP_OPTION_HTTP_PROTOCOL_USED` or it will silently speak HTTP/2 —
>   which would have re-created the exact false promise being fixed.
>
> Still open: the msys2 Windows builds have no engine (they correctly report
> `runtime.feature.http3 == 0`), and `debugger.vcxproj` / `module.vcxproj`
> compile `common.cpp` with no protocol macros at all, so `obd` ships with
> HTTP/2 and HTTP/3 dead — the same class of gap, one level up.
>
> Read what follows as the historical plan and its review, not as work to do.

## The problem

`Web.HTTP.Http3Client` is documented and shipped in `net_quic.obl`, but on
Windows it has **no engine behind it**. The native HTTP/3 implementation in
`core/vm/common.cpp` sits in three `#ifdef OBJECK_HAS_NGTCP2` blocks
(~lines 6887-7452, roughly 700 lines) and `core/vm/vs/vm.vcxproj` never defines
that macro. POSIX does: `core/vm/make/Makefile.amd64` defines
`-DOBJECK_HAS_NGTCP2` and links
`-lngtcp2 -lngtcp2_crypto_gnutls -lnghttp3 -lgnutls`.

Most Objeck users are on Windows, so a documented feature that silently does
nothing on the majority platform is the worst kind of gap: it is not a crash,
it is a promise that quietly is not kept.

## Why this is not a build-flag fix

Two independent blockers, which is why the work is phased rather than a
one-line `vm.vcxproj` edit.

**1. The TLS layer is bound to GnuTLS.** Windows is an mbedTLS toolchain
(`vm.vcxproj` links `mbedtls.lib`; CI vcpkg installs `mbedtls` + `nghttp2`
only). There is no GnuTLS anywhere in the Windows build.

**2. The socket/time layer uses POSIX APIs.** Inside the h3 blocks:

| POSIX API | count | Windows equivalent |
|---|---|---|
| `clock_gettime(CLOCK_MONOTONIC)` | 3 | `std::chrono::steady_clock` (portable, no `#ifdef` needed) |
| `socklen_t` | 3 | `int` (Winsock) |
| `fcntl(..., O_NONBLOCK)` | 2 | `ioctlsocket(s, FIONBIO, &one)` |
| `poll` / `struct pollfd` | 1 / 1 | `WSAPoll` (Vista+; identical struct layout) |
| `errno` / `EAGAIN` / `EWOULDBLOCK` | 4 / 2 / 2 | `WSAGetLastError()` / `WSAEWOULDBLOCK` |
| socket close | — | `closesocket` |

**The good news, and it is decisive:** the datagram I/O uses `sendto`/`recvfrom`,
**not** `sendmsg`/`recvmsg`. Winsock2 provides both directly, so no
`msghdr`/`iovec` emulation is needed — that would have been the expensive part.
`getaddrinfo`/`freeaddrinfo`/`SOCK_DGRAM` are likewise available on Winsock2.

**Also decisive:** the ngtcp2 crypto *callbacks* are backend-agnostic.
`ngtcp2_crypto_client_initial_cb`, `..._encrypt_cb`, `..._decrypt_cb`,
`..._hp_mask_cb`, `..._update_key_cb`, `..._recv_retry_cb` and friends have the
same names and signatures in **every** `libngtcp2_crypto_X`. Only three things
are backend-specific:

- `gnutls_rnd(...)` — 3 call sites, needs cryptographically secure random bytes
- the TLS session setup block — ~15 lines (credentials, `gnutls_init`, SNI,
  peer verification, TLS 1.3 priority, ALPN `h3`, session pointer)
- `ngtcp2_crypto_gnutls_configure_client_session(session)` — 1 call site

So the TLS port surface is roughly **20 lines**, not 700.

## Chosen approach

**Backend: quictls/OpenSSL (`ngtcp2_crypto_quictls`), not GnuTLS-on-Windows.**

Rationale:

- vcpkg's GnuTLS support on MSVC is historically painful (drags in
  nettle/gmp/libtasn1), and even macOS CI has to build ngtcp2 **from source**
  with `-DENABLE_GNUTLS=ON` because packaged builds default to OpenSSL. Fighting
  that on Windows optimises for "one code path" at the cost of a fragile build
  on the platform most users are on — the wrong trade.
- ngtcp2 upstream treats quictls as the first-class backend; it is what
  packaged/prebuilt ngtcp2 targets.
- OpenSSL is already a well-trodden vcpkg dependency on Windows.

**Cost of the choice:** a second TLS code path. That is real and must be
contained — hence the abstraction below, which is the whole point of Phase 2.
POSIX keeps GnuTLS (do not touch a working path), Windows gets quictls, and
both go through one narrow interface.

## Phases

Each phase is independently shippable and independently verifiable. **CI is the
verifier** — HTTP/3 cannot be linked or run on the current dev box, so
"it compiles and links on the Windows CI leg" is the gate for Phase 3, and a
live handshake test is the gate for Phase 4.

### Phase 1 — portability layer (no behaviour change, no new dependency)

Replace the POSIX-only APIs inside the h3 blocks with portable or
`#ifdef _WIN32`-guarded equivalents, per the table above. Prefer *portable*
over *guarded* where possible:

- `h3_timestamp()` → `std::chrono::steady_clock` arithmetic (drops
  `clock_gettime` entirely, no `#ifdef`)
- a small `h3_set_nonblocking(sock)` / `h3_close_socket(sock)` /
  `h3_last_error_would_block()` set of helpers carrying the `#ifdef`
- `socklen_t` → a typedef already used elsewhere in `common.cpp`, or `int` under
  `_WIN32`

**Verification:** POSIX CI (linux-x64, linux-arm64, macos-arm64) must stay green
— this phase changes only *how* the existing POSIX build expresses the same
calls. The Windows build is unaffected because the code is still compiled out.
This phase is safe to merge alone.

### Phase 2 — TLS backend abstraction

Introduce a minimal internal interface with two implementations, chosen by
`#ifdef`:

```
h3_tls_init(ctx, host)   -> bool     // credentials, session, SNI, verify, ALPN, conn_ref
h3_tls_free(ctx)                     // teardown
h3_tls_rand(dest, len) -> bool       // CSPRNG bytes; EVERY call site must check
```

- **GnuTLS impl** — the existing code moved behind the interface **and hardened**.
  It must NOT move verbatim: the setup calls at `common.cpp:7232-7256`
  (`gnutls_certificate_allocate_credentials`, `..._set_x509_system_trust`,
  `gnutls_credentials_set`, `gnutls_server_name_set`, `gnutls_priority_set_direct`,
  `gnutls_alpn_set_protocols`, `ngtcp2_crypto_gnutls_configure_client_session`)
  currently have **unchecked return values**; `h3_tls_init` must check each and
  return false on any failure. Set `GNUTLS_ALPN_MANDATORY` — line 7250 passes
  flags `0`, so a server selecting no protocol does not abort the handshake.
- **quictls impl** — `SSL_CTX_new(TLS_client_method())`, then:
  - **Trust anchors: the shipped `cacert.pem`, NOT `SSL_CTX_set_default_verify_paths`.**
    OpenSSL does not read the Windows certificate store; the default verify paths
    point at a build-time `OPENSSLDIR` that does not exist on a user's machine, so
    *every* connection would fail with `X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY`
    — and the shortest path to "working" would be disabling verification. Use
    `SSL_CTX_load_verify_locations(ctx, GetLibraryPath() + CACERT_PEM_FILE, nullptr)`,
    the same bundle every other Windows TLS path uses (`common.h:131`,
    `win32.h:602/670/901`). Must return 1, else fail closed.
  - `SSL_set_tlsext_host_name` (SNI) **in addition to, never instead of**,
    `SSL_set1_host` + `SSL_set_verify(SSL_VERIFY_PEER, nullptr)`. The verify
    callback **must** be `nullptr`: a callback returning 1 silently reduces
    `SSL_VERIFY_PEER` to a no-op.
  - **Reject an empty host before any TLS call.** `SSL_set1_host(ssl, "")`
    returns success while *clearing* the expected-hostname list, leaving any
    CA-chained certificate for any name acceptable — complete MITM. For IP
    literals, omit SNI and use `X509_VERIFY_PARAM_set1_ip_asc`, or refuse.
  - `SSL_CTX_set_min_proto_version(TLS1_3_VERSION)`
  - `SSL_set_alpn_protos(ssl, (const unsigned char*)"h3", 3)` — **returns 0
    on SUCCESS** (inverted); checking `!= 1` would silently disable ALPN.
  - `SSL_set_app_data(ssl, &conn_ref)`, then
    `ngtcp2_crypto_quictls_configure_client_session(ssl)` — **check the return**.
  - `h3_tls_rand` → `RAND_priv_bytes` for connection IDs and the stateless reset
    token (they are secrets); `RAND_bytes` elsewhere.
- **Both backends, after handshake:** assert the negotiated ALPN is exactly
  `h3` (`SSL_get0_alpn_selected` / `gnutls_alpn_get_selected_protocol`) and fail
  otherwise — matching what the HTTP/2 path already does at `win32.h:642-645`.

**Non-negotiable:** `OBJECK_TLS_INSECURE_SKIP_VERIFY` must behave identically in
both backends, and verification must be **on by default** in both. The repo has
already been bitten once by client TLS verification being silently disabled
(see the TLS-verification fix of 2026-06-10); the quictls path must not
reintroduce that. A backend that fails to enable peer verification must fail
closed — refuse the connection — rather than proceed unverified.

**Verification:** POSIX CI green (GnuTLS path unchanged in behaviour). The
quictls path is still not compiled anywhere yet.

### Phase 3 — Windows build + dependencies

- CI (`ci-build.yml`, Windows leg): vcpkg `openssl`, `ngtcp2`, `nghttp3`.
  **Open question to resolve first:** whether vcpkg's `ngtcp2` port builds the
  `ngtcp2_crypto_quictls` library and exposes it as a linkable target. If it
  does not, fall back to a from-source ngtcp2 build in CI (as macOS already
  does for GnuTLS) — that fallback is proven, just slower.
- `core/vm/vs/vm.vcxproj`: define `OBJECK_HAS_NGTCP2` for the x64 and ARM64
  Release/Debug configurations, add the include/lib paths, and add
  `ngtcp2.lib;ngtcp2_crypto_quictls.lib;nghttp3.lib;libssl.lib;libcrypto.lib`
  to `AdditionalDependencies`.
- `core/release/deploy_windows.cmd`: copy `ngtcp2.dll`, `nghttp3.dll`,
  `ngtcp2_crypto_quictls.dll` and the OpenSSL runtime DLLs into `%TARGET%\bin`,
  following the existing `nghttp2.dll` pattern at line 168 — **including its
  `errorlevel` check that aborts the deploy on a failed copy.** A partially
  deployed HTTP/3 is worse than a disabled one: the feature would appear present
  and fail at runtime.

**Verification:** the Windows CI legs compile *and link* the VM with
`OBJECK_HAS_NGTCP2`. That is the gate — link success proves the dependency
chain is real, which is precisely what cannot be checked on the dev box.

### Phase 4 — functional verification

- Extend the existing HTTP/3 tests to run on Windows. They currently live in the
  quarantined **non-gating** network group because they hit live servers
  (`cloudflare-quic.com`) — keep that non-gating status on Windows too; a
  transient upstream outage must not red the majority platform's build.
- A gating offline check is still possible and worth having: assert that the
  HTTP/3 trap is *registered* (i.e. the feature is compiled in) rather than
  asserting a live handshake. That catches a silent regression back to
  compiled-out without depending on the network.
- Manual confirmation on a real Windows box: `Http3Client` against
  `cloudflare-quic.com`, plus a deliberate bad-certificate host to prove
  verification is actually on.

### Phase 5 — documentation

Update `docs/architecture.md`, the `net_quic.obs` doc comments, `CHANGELOG.md`,
and the memory note (`http3-windows-gap`) to state HTTP/3's real platform
support. **Until Phases 3-4 land, the docs should say HTTP/3 is POSIX-only** —
that correction is cheap and should not wait for the port.

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| vcpkg `ngtcp2` lacks a quictls crypto library | high (blocks Phase 3) | Resolve **before** starting Phase 3; fall back to from-source ngtcp2 in CI (macOS precedent) |
| quictls path silently skips peer verification | **critical** | Fail closed if verification cannot be enabled; test against a bad-cert host; mirror `OBJECK_TLS_INSECURE_SKIP_VERIFY` exactly |
| Two TLS backends drift over time | medium | One narrow interface (3 functions); no backend-specific logic above it |
| Phase 1 regresses working POSIX HTTP/3 | medium | Phase 1 is behaviour-preserving and merges alone against green POSIX CI |
| `WSAPoll` semantics differ from `poll` | medium | Known: `WSAPoll` does not report `POLLHUP` reliably and rejects empty fd sets; the h3 loop polls a single socket, so keep the timeout/`WSAEWOULDBLOCK` handling explicit |
| ARM64 Windows dependency availability | medium | Gate ARM64 separately; x64 first (the bulk of users), ARM64 only if vcpkg supplies the triplet |
| Blocking a release on a fragile new build | medium | Every phase is independently revertable; Phase 3 can ship disabled if CI proves unstable |

## Explicit non-goals

- No HTTP/3 **server** support (client only, matching POSIX today).
- No change to the POSIX GnuTLS path's behaviour.
- No attempt to unify HTTP/2 and HTTP/3 transport code.

## Security-review corrections (applied)

An independent security review of this plan and the code it extends produced
the following; each was verified against the tree before being folded in.

### A real, shipping bug in the POSIX HTTP/3 path — fix in Phase 2

`common.cpp:7259` declares `ngtcp2_cid dcid, scid;` as **uninitialised locals**,
and **all five** `gnutls_rnd` call sites (6900, 6905, 6907, 7262, 7263) ignore
the return value. `gnutls_rnd` leaves the buffer untouched on failure, so a
CSPRNG failure means uninitialised stack bytes become the QUIC connection IDs —
transmitted **in cleartext** in the Initial packet (stack disclosure + trivially
predictable CIDs), and at 6907 a forgeable **stateless reset token**, letting an
off-path attacker tear down connections at will.

This is not a Windows issue; it ships on Linux and macOS today. Phase 2 must:
zero-initialise the CIDs, make `h3_tls_rand` return `bool`, check all five
sites, and return `NGTCP2_ERR_CALLBACK_FAILURE` from callbacks that cannot
otherwise fail the operation.

### Dependency policy (Phase 3)

- **Resolve the backend choice before starting.** vcpkg's `openssl` port is
  vanilla OpenSSL, not quictls, so `ngtcp2_crypto_quictls` may not be buildable
  from it. Upstream **OpenSSL 3.5+** exposes the QUIC-TLS API natively and
  ngtcp2 ships `ngtcp2_crypto_ossl` for it — prefer that; quictls is a fork with
  a slower CVE pipeline. Confirm against current ngtcp2 docs first.
- **Do not commit new prebuilt crypto DLLs to git.** The `nghttp2.dll` pattern
  this plan cites is a committed, unversioned binary blob
  (`core/lib/openssl/win/x64/nghttp2.dll`) that CI does not even validate — CI
  links vcpkg's build while the release ships the committed one. Repeating that
  for `libcrypto` would ship an unpatchable copy of the highest-value CVE target
  in the dependency set. Build in CI from a pinned manifest, or record SHA-256 +
  upstream source for anything committed.
- **One trust anchor across the process.** After this change `obr.exe` links
  both mbedTLS (HTTP/2, TCP) and OpenSSL (HTTP/3). Both must use the same
  `cacert.pem` and identical `OBJECK_TLS_INSECURE_SKIP_VERIFY` semantics.
- **OpenSSL config loading.** Initialise with `OPENSSL_INIT_NO_LOAD_CONFIG`, or
  ship an explicit `openssl.cnf` and set `OPENSSL_CONF`/`OPENSSL_MODULES` to
  application-relative paths — a vcpkg build can bake in a build-machine
  `OPENSSLDIR`, and a writable one lets an attacker load a provider DLL into the
  interpreter process.

### Windows UDP hazard (Phase 1)

On Windows an **unconnected** UDP socket returns `WSAECONNRESET` from `recvfrom`
after an ICMP port-unreachable — which an off-path attacker can provoke. The
current loop (`common.cpp:7061-7063`) treats any non-`EAGAIN` error as fatal and
tears the connection down. Disable via the `SIO_UDP_CONNRESET` ioctl **and**
treat it as "continue". Separately, `connect()` the socket to the resolved peer
so the kernel filters foreign datagrams (today any source can inject a datagram
that fails `ngtcp2_conn_read_pkt` and kills the connection), and reset
`remotelen` inside the receive loop — it is declared outside it at 7056 and
shrinks after the first `recvfrom`.

### Phase 4 gate must actually test verification

The original Phase 4 gated only on "the HTTP/3 trap is registered", which
detects compiled-out but **cannot detect verification-disabled** — the failure
mode this plan rates critical and which the repo has already shipped once. Make
the bad-certificate test **conditionally gating**: if a known-good control
endpoint is reachable, then connecting to a bad-certificate host **must** fail,
and the build fails if it succeeds; skip only when the control host is also
unreachable. Also gate that, with `OBJECK_TLS_INSECURE_SKIP_VERIFY` unset, a
session whose `h3_tls_init` could not enable verification returns no client
object.

### Accepted as known limitations (documented, not fixed here)

- No CRL/OCSP revocation checking in either backend — "verification is on" does
  not imply revocation coverage.
- Response headers/body buffer without a cap (`common.cpp:6964, 6972`), so a
  hostile server can drive the VM to OOM. Same class as the existing HTTP/2
  path, so not a regression, but the exposure widens to Windows.

## Windows/MSVC review corrections (applied)

A second independent review checked the plan against the real Windows build.
Verified in-tree before folding in; several items **invalidate** the original
plan rather than refine it.

### The dependency model in Phase 3 was wrong

`core/vm/vs/vm.vcxproj:102` sets **`<VcpkgEnabled>false</VcpkgEnabled>`**. Windows
dependencies are **committed to the repo** under `core/lib/openssl/win/` and
reached through four `IncludePath`/`LibraryPath` PropertyGroups (lines 73/76,
81/84, 88/90, 95/97) — `AdditionalIncludeDirectories` is empty. That is exactly
how nghttp2 ships, and `release-build.yml:274-276` states it outright: the
Windows build needs *no vcpkg install*.

So "CI installs vcpkg openssl/ngtcp2/nghttp3" would fix `ci-build.yml` and leave
**`release-build.yml` unable to find `ngtcp2.h`**. Phase 3 must instead commit
headers to `core/lib/openssl/win/include/` and libs to
`core/lib/openssl/win/{x64,arm64}/`, matching the nghttp2 precedent — vcpkg may
*produce* those binaries but cannot be the build's dependency mechanism. This
also collides with the security review's "do not commit crypto DLLs" finding:
**the two must be reconciled before Phase 3 starts** (options: static OpenSSL —
`libcrypto-static.lib`/`libssl-static.lib` are already committed, though
vestigial and unreferenced — or a CI-produced, checksummed artifact).

### The API mapping table was incomplete — additions

| Issue | Why it matters |
|---|---|
| `ssize_t` shim (`common.h:76-84`) is nested **inside** `#ifdef OBJECK_HAS_NGHTTP2` | `common.cpp:7045/7059` use `ssize_t` inside the *ngtcp2* block; it compiles today only because `vm.vcxproj` happens to define both. Hoist it to an unconditional `#if defined(_WIN32)`. |
| Winsock `sendto`/`recvfrom` take `char*`/`int`, not `uint8_t*`/`size_t` | Hard compile errors (C2664). Route both through `h3_sendto`/`h3_recvfrom` helpers. The "no emulation needed" claim is true at API level, misleading at signature level. |
| `struct pollfd` aggregate-init narrows `int` → `SOCKET` | **error C2397** under C++20, not a warning. |
| `poll()` does not exist on MSVC | C3861. Not a drop-in. |
| `int udp_fd` (`common.h:304`) vs `SOCKET` | `socket()` returns `UINT_PTR`; truncation, and failure test should be `== INVALID_SOCKET`. **Latent bug this plan would activate:** `common.h:326` guards teardown with `if(udp_fd >= 0)`, which is *always true* for an unsigned `SOCKET` → `closesocket(INVALID_SOCKET)` on every failed session. Introduce `h3_socket_t` + `H3_INVALID_SOCKET` and change the member, ctor init (`:317`) and destructor together. |
| `errno`/`EAGAIN`/`EWOULDBLOCK` | **Compiles silently wrong.** MSVC defines `EAGAIN`(11)/`EWOULDBLOCK`(140); they never equal `WSAEWOULDBLOCK`(10035), so `h3_recv_packets` returns false on the first would-block and kills *every* connection. Must be mechanically eliminated, not "mapped" — forbid `errno` in the block. |
| `SIO_UDP_CONNRESET` missing entirely | On Windows an unconnected UDP socket returns `WSAECONNRESET` after an ICMP port-unreachable; the loop treats non-would-block as fatal, so one stray ICMP aborts the connection. Disable the ioctl at socket creation **and** treat `WSAECONNRESET`/`WSAENETRESET` as "skip datagram, continue". |

Drop the `socklen_t` row (already typedef'd by `ws2tcpip.h`; proven by
`common.cpp:4813` compiling on Windows today) and the "socket close" row
(`common.h:327-331` already has the `#ifdef`).

### Use `select()`, not `WSAPoll`

The repo already has a proven single-socket readiness helper —
`socket_ready_for_io` (`common.h:1787-1832`) — with the `_WIN32` vs POSIX
`nfds` difference handled. Building `h3_wait_readable(sock, ms)` on it removes
the `pollfd` narrowing error, the missing-`poll` error, and the whole WSAPoll
risk row at once. (My two stated WSAPoll caveats were also both moot here: the
loop always passes `nfds=1`, and the famous `POLLOUT` bug applies to
non-blocking `connect()`, which this socket never does.) Add explicitly:
handle `revents & (POLLERR|POLLHUP|POLLNVAL)` and the `n < 0` return — both are
ignored today (`common.cpp:7143-7145`), so an error would spin until the 30s
bail-out.

### Scope correction

It is **three blocks in `common.cpp` plus four in `common.h`** (`:85` includes,
`:287` `Http3SessionCtx` members incl. `gnutls_session_t`/`gnutls_certificate_credentials_t`,
`:312` ctor init, `:320` destructor). The Phase-2 interface must therefore also
cover struct members, construction and teardown via an **opaque per-backend
handle**, so `common.h` carries no backend types. This makes the "~20 lines"
estimate optimistic.

### Also

- **`h3_timestamp` is right, but finish the job:** the 30s deadline at
  `common.cpp:7111-7119` uses a second `clock_gettime` pair; Phase 1 must convert
  it too or it leaves 2 of 3 calls behind. Add `#include <chrono>` explicitly
  (currently transitive via `<thread>`). One clock source only — never mix in
  `clock()`/`system_clock`.
- **Stated non-goal:** `common.cpp` is compiled by five projects; only
  `vm.vcxproj` defines these macros, so HTTP/3 lands in **`obr.exe` only** —
  `obd`, the REPL and the embedding module keep the `#else` stub, exactly as
  HTTP/2 does today. Say so rather than let it surprise someone.
- **CRT mismatch:** Release links `msvcrt.lib` with `/NODEFAULTLIB:LIBCMT`;
  Debug links `msvcrtd.lib` with no such flag. Consider defining
  `OBJECK_HAS_NGTCP2` for **Release only** until that is settled. There is no
  separate sanitize configuration — `_SANITIZE` is a source-level macro, so drop
  that phantom from Phase 3.
- **Deploy naming is per-arch:** OpenSSL 3 DLLs embed the architecture
  (`libssl-3-x64.dll` vs `libssl-3-arm64.dll`), so a single copy line will not
  serve both. Pin vcpkg's **release** bin path — debug DLLs share filenames.
- **Phase 4 reality:** `prgm_http3.obs` is compiled only under the linux-x64
  gate (`ci-build.yml:558`), and `windows-arm64` skips tests entirely
  (`:572`) because it is cross-compiled — so the offline "trap is registered"
  assert is the *only* possible gate there.

## Cross-platform review corrections (applied)

A third review checked every platform's build. It confirms the two-backend
decision but corrects the reasoning, the platform matrix, and the phase order.

### Corrects a claim made elsewhere in this document

**HTTP/3 is not "POSIX-only" — it is linux-x64 + macos-arm64 only.**
`core/vm/make/Makefile.arm64` does not define `OBJECK_HAS_NGTCP2` and links no
ngtcp2/nghttp3/gnutls, so **Linux ARM64 has no HTTP/3 either** — while CI still
installs the ngtcp2 apt packages on that leg, paying for deps it never uses.
Every "POSIX-only" statement here and in the docs must say linux-x64 +
macos-arm64. Decide separately whether to enable ARM64 (a one-line Makefile
change; deps already installed) or drop the unused packages.

### The backend question must move to a new Phase 0

`ngtcp2_crypto_quictls` links the **quictls fork**, not vanilla OpenSSL. Vanilla
OpenSSL 3.5+ uses a different ngtcp2 library with a different API —
`ngtcp2_crypto_ossl`, whose `ngtcp2_conn_set_tls_native_handle()` takes an
`ngtcp2_crypto_ossl_ctx*` rather than a raw `SSL*`. vcpkg's `openssl` port is
vanilla. So the Phase 2 API **depends on** the question Phase 3 deferred.

**Insert Phase 0 — dependency resolution:** decide the obtainable backend for
MSVC x64 *and* arm64, and decide vendored-vs-vcpkg so **both** `ci-build.yml`
and `release-build.yml` are satisfied. Move the Phase 5 documentation
correction here too — it costs nothing and stops shipping a false claim now.

**Also evaluate explicitly:** Windows 11 / Server 2022 ship HTTP/3 in-box via
WinHTTP (`WINHTTP_PROTOCOL_FLAG_HTTP3`) / MsQuic — no vendored DLLs, no second
TLS stack, system trust store, cannot regress POSIX. Cost is a ~300-line
request path instead of a ~20-line TLS shim. Given the repo's self-contained
Windows dependency policy, silence on this option is a gap.

### Phase order and scope

- **Merge Phases 2 and 3.** As written, the `#ifdef _WIN32` work from Phase 1
  and the whole new backend from Phase 2 are **dead, uncompiled code for two
  merges** — nothing defines `_WIN32` *and* `OBJECK_HAS_NGTCP2` until Phase 3.
- **Split Phase 4** into 4a (offline gating check) and 4b (live network,
  non-gating). 4a lands first.
- **Phase 3 is NOT independently revertable** — it is atomic across vcxproj +
  `deploy_windows.cmd` + vendored binaries + both workflows. Say so, or add an
  `OBJECK_H3_DISABLE` env check so a bad build can be neutered without a rebuild.
- **The Phase 2 interface is under-specified by about half.** Add
  `h3_tls_native_handle(ctx)` — `ngtcp2_conn_set_tls_native_handle`
  (`common.cpp:7314`) is a **fourth** backend-specific call site, and exactly
  the one whose argument type differs between quictls and ossl.

### The proposed Phase 4 gate cannot work

`Http3Connect` is compiled **unconditionally** — the `#else` branch just sets
`instance[0]=0`. "Assert the trap is registered" therefore always passes, and
`prgm_http3.obs` prints the same SKIP text for compiled-out and network-down.

**Fix:** add a `runtime.feature.http3` entry (1 under `#ifdef
OBJECK_HAS_NGTCP2`, else 0) to the existing `runtime.*` property table at
`common.cpp:4067-4106`, read via `Runtime->GetProperty` — **no new bytecode
trap**, which matters because a new opcode would require touching
`linker.cpp`'s bytecode parser (a known silent-`exit(1)` trap here).

### Smaller corrections

- **Factual:** the tests hit **`quic.nginx.org`**, not `cloudflare-quic.com`.
- `steady_clock` must use `duration_cast<std::chrono::nanoseconds>` verbatim —
  its period is nanoseconds on libstdc++ but not guaranteed on libc++/macOS, so
  a naive `.count()` is right on the one platform CI tests and silently wrong
  on macOS.
- **macOS HTTP/3 is built and linked but never executed in CI** (network step is
  linux-x64 only), so the from-source GnuTLS build cited as evidence of
  fragility is itself only link-verified. Phase 4b should cover macos-arm64 and
  windows-x64.
- **CI time:** the Windows dep step lacks `VCPKG_BINARY_SOURCES` and the job cap
  is 60 min; an uncached vcpkg OpenSSL build is 10-20 min per Windows leg.
  Vendoring avoids this; otherwise add the cache and raise the timeout.
- The WiX installer harvests `bin\**`, so **no installer change is needed**.
- `core/vm/Makefile` is a tracked duplicate of `Makefile.amd64` that
  `deploy_posix.sh` overwrites — edit both. The two `Makefile.msys2-*` configs
  define `_WIN32` and will compile the new helper branches.
