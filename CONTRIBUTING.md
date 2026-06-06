# Contributing to Objeck

Thanks for your interest in improving Objeck! This guide covers how the project
is laid out, how to build and test, and what we look for in a pull request.

## Getting oriented

Start with the [system architecture](docs/architecture.md) and the
[`core/`](core/) tree to get familiar with the subsystems:

- `core/compiler/` — front end (parser, type/context analysis, code generation).
  Standard-library sources live in `core/compiler/lib_src/*.obs`.
- `core/vm/` — the bytecode virtual machine. The JIT lives under
  `core/vm/arch/jit/` (`amd64/` and `arm64/` back ends, plus shared
  `jit_common.*`).
- `core/shared/` — utilities shared across the compiler and VM.
- `core/lib/` — bundled native libraries and their compiled `.obl` outputs.
- `programs/` — examples, tests, and framework demos.

## Building

The native toolchain (compiler `obc`, VM `obr`, debugger `obd`) is built per
platform:

- **Windows** — Visual Studio solutions/projects under `core/**/vs/`
  (e.g. `core/vm/vs/vm.vcxproj`), built Release / x64 or ARM64.
- **Linux** — makefiles under the respective `core/**` directories.
- **macOS** — the Xcode project at `core/vm/xcode/VM.xcodeproj`.

The exact dependency list and build steps for every platform are encoded in
[`.github/actions/setup-objeck-deps`](.github/actions/setup-objeck-deps/action.yml)
and the [`ci-build.yml`](.github/workflows/ci-build.yml) workflow — that workflow
is the authoritative, always-current build recipe.

## Testing

- **Regression suite** — `programs/regression/*.obs`. Tests follow a `PASS`/`FAIL`
  output convention; a change should keep the suite green.
- **Test programs** — `programs/tests/prgm*.obs`.

CI runs the build and tests across Windows, Linux, and macOS on x64 and arm64.
Please make sure your change builds and passes regression locally on at least one
platform before opening a PR.

## Pull requests

- Branch off `master` and open a PR against it.
- Keep changes focused; match the style and conventions of the surrounding code.
- Describe what changed and why, and note which platforms/architectures you
  tested on.
- If you add a bytecode opcode, remember the linker contains a binary bytecode
  parser that must be updated in lockstep.

## Security and secrets

Never commit API keys, tokens, or other credentials — see
[SECURITY.md](SECURITY.md). For reporting vulnerabilities, use private
disclosure rather than a public issue.
