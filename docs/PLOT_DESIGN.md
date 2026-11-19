# Plotting for Objeck: design note

Goal: a matplotlib-like library (scatter, histogram, heatmap, box plot, ROC, subplots) that
needs no display for its default output and can also draw to an SDL2 window or a PNG.
Status: decisions made 2026-11-18; the slices below are built in this order.

## What exists

**`System.ML.Lab.Chart`** (`ml_lab.obs`) writes standalone SVG 1.1: line chart with markers and
error bars, grouped bars, log x, categorical x, a 1-2-5 tick grid, a legend. No script, no font
file, coordinates with one decimal, so the same data gives the same bytes on every platform.
Five goldens guard it (`programs/regression/ml_lab_chart_svg.obs`, `ml_lab_golden/`). It has no
scatter, histogram, heatmap, box plot, subplots, annotations, PNG or window, and it writes
strings directly, so there is no object a second backend could draw.

**`Game.SDL2`** (`sdl2.obs`), checked by reading it and running `programs/regression/sdl2_bindings.obs`
(passes here, headless):

| Need | Bound? |
| --- | --- |
| Window, event loop, mouse, wheel, keys | yes (`Window`, `Event`, `Cursor`) |
| Lines, rects, points, filled rects, float variants | yes (`Renderer`) |
| Anti-aliased and thick lines, filled circles, ellipses, polygons | yes, SDL2_gfx (`AALineColor`, `ThickLineColor`, `FilledCircleColor`, `FilledPolygonColor`) |
| TrueType text | yes, SDL_ttf: `Font->Init`, `Font->New(file, pt)`, `SizeUTF8`, `RenderUTF8Blended`, `Close`. The `TTF_STYLE_*` constants are the style flags of that `Font` class; my first search missed it. |
| Built-in 8x8 bitmap text with no font file | yes, `Renderer->StringColor` |
| Software renderer on a plain surface, no window | yes, `Renderer->CreateSoftwareRenderer(surface)` |
| Read pixels back, save PNG, save BMP | yes, `Renderer->ReadPixels`, `Renderer->SavePNG(file)`, `Image->SavePNG`, `Surface->SaveBMP` |
| Rotated text | not directly; render the label to a surface/texture and rotate it (`Texture` copy with angle) |

So SDL2 can do everything a plot needs, including PNG export and, with `SDL_VIDEODRIVER=dummy`, a
headless software-surface test (the existing regression already does this, including a PNG round
trip). It cannot run in the web playground or on a machine with no SDL natives. The shipped font
is `lib/sdl/fonts/lato.ttf`; when it is absent the backend falls back to `StringColor`.

## Decisions

1. **A new library, `plot.obl`, bundle `System.Plot`, needing only `gen_collect`.** No `System.ML`
   dependency (ROC/PR helpers take plain `Float[]` scores and labels, or an `Roc` result the caller
   builds), no SDL. Pure SVG, so it is allowed in the web playground like `ml_lab`.
2. **`System.ML.Lab.Chart` is not touched and does not delegate.** Delegation would make `ml_lab`
   link `plot`, and a `.obe` links libraries at compile time, so every program that uses `ml_lab`
   would have to add `-lib plot`: a breaking change for no user-visible gain. The "golden test
   first" requirement is already met by the five existing goldens, which keep running unchanged.
   `Chart` stays the supported one-call API for experiment tables; `System.Plot` is the general one.
3. **A backend-neutral scene.** A `Figure` holds `Axes`; drawing an `Axes` produces a `PlotScene`, an ordered
   list of `PlotPrimitive` records in pixel space: `LINE`, `POLYLINE`, `POLYGON`, `RECT`, `CIRCLE`,
   `TEXT` (with anchor, size, weight, rotation). Style is stroke, fill, width, opacity. Backends
   are dumb walkers over that list, and tests can assert on primitives without parsing SVG.
4. **API in matplotlib's shape, Objeck's types.**
   `fig := Figure->New(800, 480); ax := fig->Axes(); ax->Scatter(xs, ys); ax->SetTitle("..."); fig->Save("a.svg");`
   `fig->Subplots(rows, cols)` returns the grid; every `Axes` method returns `Nil` and raises
   `PlotError` on bad input (matching `LabError`): a mismatched length, a non-finite value, an empty
   figure.
5. **Determinism is a requirement, not a hope.** Coordinates are written with one decimal, with `-0.0`
   normalised; nothing depends on the clock, a locale, a font or a random seed. Text widths for
   layout (legend boxes, label placement) use a fixed width table, not font metrics, so the SVG is
   byte-identical everywhere. Each chart type gets a byte-for-byte golden, and every golden test
   is shown to fail on a changed input.
6. **SDL2 is a separate library, `plot_sdl.obl`, bundle `System.Plot.SDL`** (needs `plot`, `sdl2`,
   `gen_collect`), blocked in the playground and absent from headless CI runs of the SVG tests.
   It walks the same `PlotScene`. Interactivity does not scale pixels: zoom, pan and hover change the
   `Axes` view limits and rebuild the scene (a plain function of data and limits), so ticks and
   labels stay crisp; hover uses `Axes->PixelToData`. PNG export renders the scene on a software
   renderer over a surface and calls `Renderer->SavePNG`; it does not need a window, so it also
   works over SSH and in CI with the dummy driver. PNG output is checked with pixel probes
   and a size check, not byte goldens (SDL, FreeType and gfx versions differ across platforms).
7. **No in-language PNG writer.** `Byte->CompressZlib` would make one possible, but it would also
   need a rasteriser and a bitmap font, which the SDL path already provides; the cost (a few
   hundred lines plus a font table) is not justified while SDL is a shipped dependency. If a
   no-SDL PNG is wanted later, the scene's rectangles, lines and circles make a rasteriser
   straightforward.

## Slices (one PR each, off `origin/master`)

1. **Core and first charts** (PR #1610): `PlotScene`, SVG backend, `Figure`/`Axes`, scatter (colour and
   size from further columns), histogram, line, grouped bars, log axes, subplots; every registration
   place. Goldens per type.
2. **More charts** (stacked on #1610): heatmap and confusion matrix (annotated cells), box plot, text annotations, ROC
   and precision-recall helpers (`System.ML` PR #1607 adds the metrics; the helpers take the curve
   points as arrays, so they do not depend on it at link time).
3. **SDL2 backend** (stacked on #1612): `plot_sdl.obl` with `PlotWindow` (wheel zoom about the
   pointer, drag to pan, hover readout, R to reset, Escape to close), `PlotPng` (software
   surface, no display) and `SdlCanvas` (the scene walker). `Axes` gained `Zoom`, `Pan`, `DataAt`,
   `GetPlotRect` and `Figure->AxesAt`, which are plain arithmetic and are tested without SDL
   (`plot_view.obs`). Verified headless with the dummy driver: exact pixel colors per primitive,
   clipping, text, a figure's marker, PNG signature, size and load-back, and a hidden window
   driven through its handlers and closed by a user event (`plot_sdl.obs`).

## Risks

* Layout code with many floats is where cross-platform byte drift appears. One-decimal output
  absorbs 1-ulp differences except exactly at a rounding boundary; the goldens run on all CI
  legs (Windows, Linux, macOS, ARM64) and any drift shows there first.
* Libraries registered in about twenty places, several checked strictly by CI. Slice 1 does the
  registration once and the checks (`check_doc_lists.py`, `check_api_index.py`, the playground
  policy test, `local_ci.py`) are run before the PR is opened.
