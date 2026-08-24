# Picking this up on Windows / WSL2

Scratch note for continuing the `feat/sdl2-opengl` work. **Delete this file
before merging** — it documents a moment, not the project.

Branch: `feat/sdl2-opengl`, 41 commits ahead of `master`. `master` is untouched.

```powershell
git fetch origin
git checkout feat/sdl2-opengl
git pull
```

---

## What is already true (verified on macOS ARM64)

- OpenGL works end to end: GL 3.3 core context (reports GL 4.1 Metal), all six
  examples compile, the walkthrough runs and exits 0.
- SDL2 is bundled inside the macOS distribution and reached by an `@rpath`; the
  tree has zero `/usr/local/lib` references.
- Every Mach-O is Developer ID signed with hardened runtime and a secure
  timestamp. The JIT still works under it (ARM64 uses `MAP_JIT` +
  `pthread_jit_write_protect_np`, and `allow-jit` is applied).
- macOS ships a notarizable `.zip`; `obu`'s suite passes 32/32 against it.
- `tools/install_deps.sh` gives Linux a one-command setup and ships inside the
  distribution.

## What is NOT verified — and is the reason to be on Windows

**Nobody has built or run the GL branch on Windows.** That is the gap.

---

## 1. The specific Windows question worth settling first

`core/vm/interpreter.cpp:2836` calls plain `LoadLibrary` with no
`SetDllDirectory` / `AddDllDirectory` (grep confirms: the only `codesign`-era
DLL-path calls in the tree are absent). Windows therefore resolves a loaded
DLL's *dependencies* from the **executable's** directory, the system
directories and `PATH` — **not** from the directory the DLL itself lives in.

The layout works against that:

| What | Where |
|---|---|
| `libobjk_sdl.dll` | `lib\native\` |
| `SDL2.dll` and friends | `lib\sdl\` (from `deploy_windows.cmd`: `copy lib\x64\*.dll ... \lib\sdl`) |
| What the MSI puts on `PATH` | `[INSTALLFOLDER]bin` only (`core/utils/setup/objeck.wxs`) |
| SDL2 DLLs copied into `bin\` | nothing does this |

By that reasoning `SDL2.dll` should not resolve. **But this is identical for the
existing 2D SDL library, which has shipped for years** — so either it works
through something I did not find, or Windows has had the same friction all
along. Do not trust my reasoning over an actual run; it is exactly the kind of
thing that looks broken on paper and works in practice (or the reverse).

Settle it by running any SDL program on a Windows install and watching whether
`libobjk_sdl.dll` loads. If it does not, the cheapest honest fixes are copying
the SDL2 DLLs into `bin\` alongside the executables, or adding `lib\sdl` to the
MSI's `PATH`.

## 2. GL 3.3 core on Windows needs a real GPU driver

`programs/regression/gl_context_test.obs` documents this in its header:

> Windows ships only a GL 1.1 generic implementation, so a 3.3 core request
> cannot succeed there without Mesa's `opengl32.dll` being installed.

On a physical desktop with NVIDIA/AMD/Intel drivers you get GL 3.3+ from the
vendor ICD and this is a non-issue. It bites on VMs and CI runners with no GPU
driver, which is why the regression test **skips** rather than fails there.
`OBJECK_GL_REQUIRED=1` turns that skip into a hard failure, and CI sets it only
on the POSIX legs (linux-x64, linux-arm64, macos-arm64).

So: if a GL demo fails on a Windows VM, check the driver before suspecting the
binding.

## 3. Building

**Native Windows** (MSVC, for the real DLL-resolution answer):

```cmd
cd core\release
deploy_windows.cmd x64
```

`opengl32.lib` is already linked — the branch added it to all six configurations
in `core/lib/sdl/sdl/sdl/sdl.vcxproj`. (That file has some pre-existing
duplicate `SDL2_ttf.lib;;` entries; harmless, and not from this work.)

**WSL2** (fastest way to exercise the code, and where `install_deps.sh` gets a
real test):

```bash
./tools/install_deps.sh          # apt/dnf/... detects and installs SDL2 + libGL
cd core/release && ./deploy_posix.sh x64
cd ../.. && ./programs/examples/run_gl.sh --verify
./programs/examples/run_gl.sh gl_walkthrough
```

WSLg supplies GL through Mesa's d3d12 driver. If a 3.3 core context is refused,
`LIBGL_ALWAYS_SOFTWARE=1` falls back to llvmpipe, which advertises 4.5 and is
plenty for the demos.

This is also the first real run of `install_deps.sh` on Linux. Its package lists
are unit-tested across apt/dnf/yum/pacman/zypper/apk and its loader probe is
tested against a stubbed `ldconfig`, but **the install itself has never been
executed** — no Linux box and no Docker on the Mac. If a package name is wrong
for your distro, that is the most likely thing to be wrong in this branch.

## 4. Running the examples

Six of them: `gl_clear`, `cube_gl`, `gl_boing`, `gl_model`, `gl_walkthrough`,
plus `3d_gl_24` under `programs/deploy`. Compile needs both libraries:

```
obc -src gl_clear.obs -lib sdl2,sdl_gl -dest gl_clear.obe
obr gl_clear.obe
```

`programs/examples/run_gl.sh` handles tree selection, staleness, dependencies
and per-demo assets — **but it is POSIX-only.** There is no `run_gl.cmd`. If you
want one on native Windows, that is a small, obviously-useful thing to add.

The GL regression test needs no wiring: `run_regression` auto-discovers `*.obs`
and reads `# EXTRA_LIBS: sdl2,sdl_gl` from the file's first line.

## 5. Follow-ups already queued

- **Delete the legacy macOS `.tgz`** once one release has shipped with the new
  `obu` (which now asks for `.zip`). Two places, both commented with the
  instruction: four lines in `core/release/deploy_macos_arm64.sh`, and the `TGZ`
  block in `.github/workflows/release-build.yml`.
- **Add two repo secrets** or the new playground job just prints a skip notice:
  `PLAYGROUND_HOST` and `PLAYGROUND_SSH_KEY`. Restrict the key on the VPS with
  `command="bash /opt/playground/repo/programs/web-playground/deploy/update.sh",no-pty,no-port-forwarding`.
- **Notarization is unproven.** It had been failing on every macOS release while
  CI reported success. The verdict is now parsed instead of trusted to the exit
  code, so the next release either notarizes or fails loudly with
  `notarytool log` output. `ALLOW_UNNOTARIZED=1` ships anyway if needed.

## 6. Do not re-litigate

- The macOS `/usr/local/lib` SDL2 instructions are gone from `docs/readme.html`
  (that file is hand-maintained and ships in every distribution; `build.cmd`
  deliberately does not generate it).
- `run_gl.sh`'s dependency check runs **before** the staleness probe on purpose:
  the probe executes `obc`, so on a quarantined tree it died and the script
  blamed a stale build.
- `core/utils/updater/obu` is now git-ignored; it builds in place like
  `core/compiler/obc` and `core/vm/obr`.
