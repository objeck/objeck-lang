# Writing then closing a socket on Windows loopback

On Windows, over `127.0.0.1`, a peer that sends data and then immediately tears
the connection down can have that data **discarded**. The reader gets
`WSAECONNRESET` and **zero bytes** — not a partial read — even though `send()`
accepted every byte and the data had physically arrived.

This is a platform behaviour, not an Objeck defect. It reproduces in about a
hundred lines of plain C with no VM involved:
`programs/tests/win32_socket_close_race.cpp`. Tracked as
[issue #669](https://github.com/objeck/objeck-lang/issues/669).

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
| drain to EOF, 100ms bound | no effect |
| drain to EOF, 400ms bound | no effect |
| asymmetric sender-only drain | no effect |
| `closesocket` with no `shutdown` | worse |
| `SIO_TCP_INFO` / `BytesInFlight` | no effect — reports 0 in flight, because the data *is* delivered; the RST is what loses it |

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

- **In a raw-socket protocol, let the client close.** Have the server finish
  writing and then read until it sees EOF, rather than closing immediately. The
  client closes when it has what it asked for.

- **Or acknowledge at the application level.** The client sends a byte after it
  has read the response; the server closes once it sees it. This is the same
  idea, made explicit.

- **If you cannot change the protocol, retry the transport.** A response that
  never arrives is distinguishable from a wrong one: no bytes at all, or a status
  code of 0. Retry only that, and let a response that arrives and is wrong fail
  immediately. `programs/regression/core_net_buffer.obs` and
  `core_http_server.obs` do exactly this, and say so.

What you should *not* do is sleep before closing. It works, which is precisely
what makes it tempting, but it is a race you have widened rather than removed —
and you pay for it on every close, forever.

## Reproducing it

```
Windows   cl /nologo /EHsc /O2 /std:c++17 win32_socket_close_race.cpp
Linux     g++ -O2 -std=c++17 -pthread win32_socket_close_race.cpp -o race
```

`race` reproduces; `race 1` shows that ephemeral-port reuse is not the cause
(every local port distinct, failures continue); `race 2` shows a pause before
teardown making it vanish. On Linux every mode reports zero.
