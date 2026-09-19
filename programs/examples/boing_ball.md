# Boing Ball — a live GC / CPU / memory visualizer

`boing_ball.obs` recreates the 1984 Amiga **Boing Ball** in Objeck + SDL2, but with
a twist: a per-frame HUD that reads the VM's *actual* internals — heap, GC counts,
process memory, CPU. It's a demo you can watch *and* a hands-on way to feel how the
garbage collector and a draw-call-heavy workload behave in real time.

## The HUD

| line | meaning |
|---|---|
| `FPS`  | frames/sec (capped at 60 by `Game.Framework`'s `FrameEnd`) |
| `MEM`  | process resident set size (OS working set), MB |
| `HEAP` | the GC-tracked live heap (`allocation_size`), KB — **watch it sawtooth** |
| `GC`   | `minor / major` collection counts since launch |
| `PAU`  | the last GC stop-the-world pause, µs |
| `CPU`  | **% of one core** (process CPU-time ÷ wall-time) |

All of these come from a new VM facility: `System.Runtime->GetProperty("runtime.*")`
(see the table at the bottom). `CPU` answers a common question — yes, it's per-core,
so ~90% means the work nearly saturates a single core.

## Why it's cheap — the Amiga color-cycling lesson

The original was smooth because the custom **Copper** co-processor cycled palette
registers over a *stationary* grid — the 68000 CPU never redrew the ball and stayed
free (famously, free enough to run a terminal at the same time).

This port follows the same principle: each pixel's checker cell is precomputed once
(with a fixed axis tilt); per frame only the **spin offset scrolls the colors through
the static cells**. No per-frame geometry. The hard-won lesson: an earlier version
re-rasterized a *rotated* ball every frame (a continuously drifting axis lean) and
that per-pixel float work blew the frame budget. Static geometry + a cheap scroll is
the Amiga way; the spin instead reverses/varies on each corner bounce.

## Case study: the readout caught a real issue

Watching `MEM` revealed something (June 2026, before the fix under "What came
next"): it climbs ~**1.5 MB/s and never plateaus** (87→207 MB over 80 s), while
`HEAP` keeps sawtoothing with a flat ~1 MB floor and `GC major` ticks constantly.

- **The logical heap is healthy** — every major GC reclaims the per-frame garbage
  (the `allocation_size` low-water mark is constant). No leak in the tracked heap.
- **But process RSS grows unbounded** — it would eventually OOM a long run.
- **Root cause:** every SDL draw call boxes its args into a `Base[]` **array**, and
  `AllocateArray` has the nursery bump-allocator *disabled* (interior-pointer fixup
  isn't covered yet), so arrays go straight to old gen via `calloc`/`free`. This demo
  churns ~50 MB/s of small arrays; the reuse free-list is capped at the heap
  threshold (8 MB), so most freed blocks hit raw `calloc`/`free` and the CRT heap
  grows/fragments. (It's also why **minor GC never even fires** here — the array
  churn forces the major path.)

So the demo *stress-exposes* the gated "arrays in the nursery" work — and shows it
to you live, on screen.

## What came next

### Memory
The boxing churn is fixed at its source: the `Renderer` color draws in `sdl2.obs`
(`BoxColor`, `PixelColor`, `LineColor`, …) reuse one pooled argument buffer per
renderer and cached function names, instead of allocating a fresh `Base[]` and a
name string on every call (600k `BoxColor` calls: 33 major GCs → 0). Arrays still
allocate in the old generation: moving them into the nursery is still open, because
promotion cannot yet safely fix up every root that points at one (see
`AllocateArray` in `core/vm/arch/memory.cpp`).

### Multithreading — the Amiga multitasking angle
The 1984 demo proved multitasking by running a terminal *while* the ball bounced.
`boing_ball_mt.obs` recreates and **visualizes** that:

1. **Thread / stop-the-world stats** sit alongside the existing keys:
   `runtime.threads.active` (`mutator_count`), `runtime.gc.stw` (a collection in
   progress), pause times and lock contention. Its HUD reads them through
   `System.Concurrency.Monitor` and shows `FPS`, `THRD`, `WORK`, `GC`, `PAUSE` (ms),
   `CONT` and `CPU`.
2. **SPACE / W adds a worker:** a `System.Concurrency.TaskScope` task doing
   allocation-heavy work (the modern "terminal") while the main thread renders. This
   exercises the cooperative **stop-the-world** (SafePoint parking, `parked_count`),
   the parallel root scan, and the minor-GC STW path that was the subject of PR #574.
3. **Watch it:** more mutators → more STW coordination → visible GC-pause behavior in
   the HUD. A live teaching tool for the concurrent collector — and a faithful nod to
   what made the Amiga demo legendary.

> Note: SDL rendering stays on the main thread (the renderer isn't thread-safe); the
> worker threads do compute/allocation, not drawing.

## New VM API — `runtime.*` property keys

Read via `System.Runtime->GetProperty(key)->ToInt()`. Implemented in the VM's
`GetSysProp` trap (no new bytecode, no `.obl` rebuild needed):

| key | meaning |
|---|---|
| `runtime.memory.used`      | process RSS, bytes (Win `K32GetProcessMemoryInfo`, mac `task_info`, Linux `/proc/self/statm`) |
| `runtime.memory.allocated` | GC-tracked live heap, bytes |
| `runtime.memory.max`       | current heap threshold (`mem_max_size`), bytes |
| `runtime.gc.minor` / `.major` / `.total` | collection counts |
| `runtime.cpu.count`        | logical cores |
| `runtime.cpu.time`         | process CPU time, ms (sample over an interval for %) |

More keys ship beside these, among them `runtime.threads.active` / `.parked` /
`.running`, `runtime.gc.stw` and `runtime.gc.pause.last_us`; the full list is in
[FEATURES.md](../../docs/FEATURES.md#runtime-diagnostics). Typed wrappers ship as
`System.Concurrency.Monitor` in `concurrent.obl` (`Monitor->UsedMemory()`,
`Monitor->MajorCollections()`, …), not in `lang.obl`.

## Build & run

```
compile: obc -src boing_ball.obs -lib sdl2,sdl_game,json,gen_collect
run:     obr boing_ball
big heap: obr --gc-threshold=64M boing_ball   # fewer, larger collections
```
