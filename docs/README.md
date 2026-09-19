# Objeck documentation

## Start here

| | |
|---|---|
| [FEATURES.md](FEATURES.md) | What the language has |
| [EXAMPLES.md](EXAMPLES.md) | Code examples |
| [cli_options.md](cli_options.md) | `obc` and `obr` command-line options, and the environment variables they read |
| [editors.md](editors.md) | Editor and IDE support |

## Reference

| | |
|---|---|
| [architecture.md](architecture.md) | How the compiler, VM and JIT fit together |
| [native_interface.md](native_interface.md) | Calling C/C++ from Objeck — the whole contract |
| [optimization_pipeline.md](optimization_pipeline.md) | What the optimizer does, and in what order |
| [performance.md](performance.md) | Benchmarks and where the time goes |
| [AI.md](AI.md) | AI and ML developer guide |
| [MODELS.md](MODELS.md) | Models the AI bindings expect |
| [opengl.md](opengl.md) | `Game.OpenGL` |
| [third_party_dependencies.md](third_party_dependencies.md) | Where each platform gets mbedTLS, nghttp2, SDL2, OpenCV, ONNX Runtime — and how to read the version a build used |

## Platform notes

Behaviour specific to one platform, usually discovered the hard way.

| | |
|---|---|
| [windows_loopback_sockets.md](windows_loopback_sockets.md) | Windows discards data if you write then immediately close on loopback — and what to do instead |
| [macos_http2_http3.md](macos_http2_http3.md) | Building and testing HTTP/2 and HTTP/3 on macOS |

## Process

| | |
|---|---|
| [CI_CD.md](CI_CD.md) | CI/CD architecture |
| [release_process.md](release_process.md) | Cutting a release |
| [release_integrity.md](release_integrity.md) | What a release verifies today, and the signed manifest that would let `obu` detect a substituted `SHA256SUMS` |

## Designs and plans

Written before the work, and kept afterwards as the record of why it looks the way it does.

- [SYSTEM_TERMINAL_DESIGN.md](SYSTEM_TERMINAL_DESIGN.md) — `System.Terminal`: styled output, progress bars, interactive controls, and the five VM traps they need
- [UPDATER_DESIGN.md](UPDATER_DESIGN.md) — auto-updater
- [ONNX_CUDA_PLAN.md](ONNX_CUDA_PLAN.md) — CUDA execution provider for ONNX
- [web_server_design.md](web_server_design.md) — `Web.Server` reimplemented over `Web.HTTP.Server`, after the native-bridge design proved unusable (#654)
- [PLAN_2026_10_0_HARDENING.md](PLAN_2026_10_0_HARDENING.md) — the compiler, GC and language hardening plan whose work shipped as v2026.9.5, and what it deferred
- [JIT_ENTRY_COMPILE_DESIGN.md](JIT_ENTRY_COMPILE_DESIGN.md) — compiling a method with a loop on its first call, and a thread's `Run` on entry, instead of after ten calls
- [JIT_LOOP_LOCALS_DESIGN.md](JIT_LOOP_LOCALS_DESIGN.md) — loop-carried locals in registers (F3, AMD64 only so far) and short-circuit conditions (F6)
- [JIT_SELECT_TABLES_DESIGN.md](JIT_SELECT_TABLES_DESIGN.md) — a dense `select` compiled as a jump table on both backends (F8)
- [JIT_CALLING_CONVENTION_DESIGN.md](JIT_CALLING_CONVENTION_DESIGN.md) — what a call between compiled methods costs, and the native entries and inline caches that cut it (F7)

## Investigations

Measurements and analyses, and the work they left open.

- [JIT_CODEGEN_ASSESSMENT_2026_09.md](JIT_CODEGEN_ASSESSMENT_2026_09.md) — how efficient the emitted AMD64/ARM64 code is, measured, with the ranked fixes; the AMD64 ones are done, ARM64's loop and float pins are still open
- [JIT_ARM64_HANDOFF_2026_09.md](JIT_ARM64_HANDOFF_2026_09.md) — the open ARM64 JIT track (loop locals and `D8`-`D15` float pins, `HasAndOr` narrowing) and the macOS build-and-test loop
- [ML_TABULAR_GAPS.md](ML_TABULAR_GAPS.md) — what `System.ML` lacks for an ordinary tabular classification study, checked against the library source

## What is not documentation

Several things in this directory are **build inputs or shipped artifacts**, not pages to read here. GitHub renders `.html` as source, so opening them in the file browser shows markup rather than a page — they are not broken, they are simply not meant to be read on GitHub.

> **Looking for the release notes?** Read **[CHANGELOG.md](../CHANGELOG.md)** in the repository root. `readme.html` and `readme.txt` in this directory are *generated forms of the same content* — for the website and the installer. GitHub shows the HTML one as raw markup, which is why it looks empty of content: there is nothing to fix there, the Markdown original is one directory up.

| | |
|---|---|
| `readme.html`, `readme.txt` | The release notes, rendered for shipping. `deploy_windows.cmd` copies `readme.html` into the install tree, where `style/` sits beside it and it displays properly. The source of truth is [CHANGELOG.md](../CHANGELOG.md). |
| `web/` | The **published site**. This is what GitHub Pages builds — see `.github/workflows/jekyll-gh-pages.yml`, which sets `source: ./docs/web`. Nothing else in this directory is published. |
| `style/`, `images/`, `syntax/`, `arch/` | Assets for the above. |
| `api.zip` | Pre-built API docs, unpacked into the install tree by the ARM64 deploy, which cannot run the doc generator while cross-compiling. |
| `eula.rtf` | Licence text for the installer. |
| `website.7z`, `benchmarks.xlsx`, `design.vsd`, `Compiler Design.pptx` | Historical material kept for reference. |

If you want the rendered documentation rather than the sources, use the published site rather than this directory.
