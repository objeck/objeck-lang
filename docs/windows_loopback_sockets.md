# Writing then closing a socket on Windows loopback

On Windows, over `127.0.0.1`, a peer that sends data and then immediately tears
the connection down can have that data **discarded**. The reader gets
`WSAECONNRESET` and **zero bytes** — not a partial read — even though `send()`
accepted every byte and the data had physically arrived.

This is a platform behaviour, not an Objeck defect. It reproduces in about a
hundred lines of plain C with no VM involved:
`programs/tests/win32_socket_close_race.cpp`. Tracked as
[issue #669](https://github.com/objeck/objeck-lang/issues/669).

**Not every Windows machine does this (measured 2026-11-02, #1443).** On GitHub's
hosted Windows runners (x64 and arm64, plain Python sockets), a server that wrote
its reply and closed first lost **0 of 30,000** replies, and a client that
half-closed always ended in `TIME_WAIT`. On the development machine that measured
the table below, the same scripts lost 104 of 2,400 replies, and 2-3% of client
half-closes left the client in `LAST_ACK` while the server's end stayed
`ESTABLISHED`, never told of the close. The difference is the machine's loopback
path (filter drivers sit on it), not Windows itself, which is why Objeck keeps the
client-closes-first design: it is safe on both kinds of machine.

## Who this affects

**Only loopback, and only Windows.** Measured, two processes, 50 transfers a run:

| configuration | lost |
|---|---|
| `127.0.0.1` | 9–14 per 50 |
| a real network interface | **0 per 50, three runs** |
| Linux, any interface | **0 per 50** |

So a deployed server answering remote clients is not exposed. What is exposed is
everything that talks to itself: the regression suite, local development, a
Windows developer browsing to a locally hosted Objeck app, and any program that
runs a client and a server together.

The scale is worth stating plainly. Before this was fixed, an Objeck HTTP client
talking to an Objeck web server over loopback lost **roughly half its responses**
— 28, 30 and 25 lost out of 60 in three consecutive runs.

## Why it happens

`send()` returning `N` means the bytes reached the kernel. It does not mean the
receiving *application* has them.

When the sender then tears the connection down, Windows can abort rather than
finish, and the RST **flushes the receiver's kernel buffer**. Bytes that had
already arrived are thrown away. That is why the reader sees zero rather than a
short read: the data was there, and the reset erased it.

Once the receiving application has called `recv` and copied the bytes into its
own memory, they are safe. So the whole problem is a race between the sender's
teardown and the reader's `recv`.

## What does not fix it

All of these were measured against the reproduction, interleaved to control for
run-to-run variance. **None of them work.** Do not spend a day rediscovering it:

| attempt | result |
|---|---|
| `SO_LINGER` before `shutdown` | no effect |
| `SO_LINGER` with no `shutdown` | blocks >1s per close — unusable |
| drain to EOF, 100ms bound | no effect — see note below |
| drain to EOF, 400ms bound | no effect — see note below |
| asymmetric sender-only drain | no effect |
| `closesocket` with no `shutdown` | worse |
| `SIO_TCP_INFO` / `BytesInFlight` | no effect — reports 0 in flight, because the data *is* delivered; the RST is what loses it |

**On the two drain rows.** These are not saying a drain cannot work — `CloseGracefully()` below is a drain, and it measures 0 losses in 180 transfers. They are saying these *bounds* are too short: at 100ms and 400ms the drain gives up while the peer is still reading, this side closes first, and you are back in the failing case. What matters is not draining as such but who closes last, so a bound only helps if it is long enough for the peer to finish and hang up. The 1000ms default was chosen on that basis.

The only mechanical thing that works is elapsed time before teardown, and that
is not a fix. A delay inside `IPSocket::Close` would tax every socket close, on
every platform, for a race that only loopback has. The VM does not do this, and
should not.

## What to do instead

**Do not be the side that closes.** That is the entire remedy, and it is free.

If your peer closes first, there is no teardown to race. Concretely:

- **Use HTTP rather than raw sockets where you can.** `Web.HTTP.Server` and
  `HttpClient` already do this: the server holds the connection and the client
  closes. That is what removed the exposure, and it made the exchange about three
  times faster as a side effect, because connection setup stopped being
  per-request.

- **In a raw-socket protocol, call `CloseGracefully()` instead of `Close()`.**
  `TCPSocket` and `TCPSecureSocket` both have it. It reads and discards whatever
  is still arriving until the peer hangs up, then closes — so the server finishes
  writing and the *client* owns the teardown, which is the whole remedy:

  ```objeck
  socket->WriteString(response);
  socket->CloseGracefully();     # not Close()
  ```

  It is bounded on both sides and cannot park a thread: it gives up after one
  second of silence (tune with `CloseGracefully(ms)`) and after 16 reads of a peer
  that keeps sending rather than closing. In the ordinary case it returns the
  moment the peer hangs up and costs nothing.

  Measured with `programs/regression/socket_graceful_close_test.obs`, 30 transfers
  of a 16KB response per run, six runs on Windows loopback:

  | server's last call | responses lost |
  |---|---|
  | `Close()` | **21 of 180** |
  | `CloseGracefully()` | **0 of 180** |

  `Close()` stays correct when the peer has already closed, or when this side is
  the one being told to hang up.

- **Or acknowledge at the application level.** The client sends a byte after it
  has read the response; the server closes once it sees it. This is the same
  idea, made explicit.

- **If you cannot control the closing side at all, retry the transport.** A response that
  never arrives is distinguishable from a wrong one: no bytes at all, or a status
  code of 0. Retry only that, and let a response that arrives and is wrong fail
  immediately. `programs/regression/core_net_buffer.obs` and
  `core_http_server.obs` do exactly this, and say so.

What you should *not* do is sleep before closing. It works, which is precisely
what makes it tempting, but it is a race you have widened rather than removed —
and you pay for it on every close, forever.

## The cost of closing first: TIME_WAIT (#1443)

The side that closes first keeps the connection in `TIME_WAIT` for about two
minutes, and on Windows that holds a local port out of the 16,384 the system hands
out (49152-65535). A client that opens a connection per request and closes it
first therefore has a budget of roughly 130 requests a second, sustained; past it
every connect fails at once with `WSAEADDRINUSE` (10048). Measured on the hosted
runners: the 16,135th connection failed, and so did the next 13,849. The nightly's
`query_url_cache_processes`, run in four configurations back to back beside other
network tests, made about 2,900 connections a configuration and hit that wall.

So `HttpClient` reuses connections: a GET or DELETE goes out on a connection an
earlier request to the same host and port left open, when that response said
`Connection: keep-alive` and framed its body with its own `Content-Length` (or
had none, 204 or 304). A connection waits at most four seconds for its next
request. A server may close a waiting connection at any time, so a request that
gets no status line at all on a REUSED connection is sent once more on a new one
(RFC 9112 section 9.3.1); a second failure is the caller's error. POST, PUT and
PATCH always open a connection of their own. With reuse, the same nightly run
peaks at about 60 `TIME_WAIT` entries instead of the whole range
(`programs/regression/http_client_reuse.obs` checks the reuse and the single retry).

## Reproducing it

```
Windows   cl /nologo /EHsc /O2 /std:c++17 win32_socket_close_race.cpp
Linux     g++ -O2 -std=c++17 -pthread win32_socket_close_race.cpp -o race
```

`race` reproduces; `race 1` shows that ephemeral-port reuse is not the cause
(every local port distinct, failures continue); `race 2` shows a pause before
teardown making it vanish. On Linux every mode reports zero.
