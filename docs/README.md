# Objeck documentation

## Start here

| | |
|---|---|
| [FEATURES.md](FEATURES.md) | What the language has |
| [EXAMPLES.md](EXAMPLES.md) | Code examples |
| [cli_options.md](cli_options.md) | `obc`, `obr`, `obd`, `obi` command-line options |
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
| [CI_CD_QUICK_START.md](CI_CD_QUICK_START.md) | The short version |
| [release_process.md](release_process.md) | Cutting a release |

## Designs and plans

Written before the work, and kept afterwards as the record of why it looks the way it does.

- [UPDATER_DESIGN.md](UPDATER_DESIGN.md) — auto-updater
- [ONNX_CUDA_PLAN.md](ONNX_CUDA_PLAN.md) — CUDA execution provider for ONNX
- [HTTP3_WINDOWS_PLAN.md](HTTP3_WINDOWS_PLAN.md) — enabling HTTP/3 on Windows
- [HTTP_REQUEST_HEADERS_PLAN.md](HTTP_REQUEST_HEADERS_PLAN.md) — making `AddHeader()` work on HTTP/2 and HTTP/3
- [OBI_EDITOR_RUN_PLAN.md](OBI_EDITOR_RUN_PLAN.md) — F5 freezing `obi`

## Investigations

Closed bugs whose reasoning is worth keeping.

- [jit-malloc-corruption-investigation.md](jit-malloc-corruption-investigation.md) — JIT heap corruption (closed)
- [VERIFY_ARM64_JIT_FLOAT_FIX.md](VERIFY_ARM64_JIT_FLOAT_FIX.md) — ARM64 JIT float register bug
- [VERIFY_ARM64_JIT_IMM_FIX.md](VERIFY_ARM64_JIT_IMM_FIX.md) — Windows ARM64 unsigned ops

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
