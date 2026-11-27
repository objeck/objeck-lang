# OpenGL (`Game.OpenGL`)

Hardware-accelerated 3D for Objeck, built on an OpenGL 3.3 core context created
through SDL2. The binding lives in `sdl_gl.obl` and is used alongside `sdl2.obl`:

```objeck
use Game.SDL2;
use Game.OpenGL;
```

## Setup

**There is nothing to install on macOS or Windows.** Both ship SDL2 inside the
distribution. Only Linux needs packages, because there `libobjk_sdl.so` links
against the system SDL2 and OpenGL runtime. The rest of the toolchain does too,
so on Linux this is the first setup step for any Objeck program, not just for
graphics.

| Platform | What you need to do |
|---|---|
| **macOS** | Nothing. Take the `.pkg` or `.zip` (both notarized). SDL2 ships in `lib/sdl`; OpenGL is a system framework. |
| **Windows** | Nothing. The SDL2 DLLs ship in `bin`, beside `obr.exe`. |
| **Linux** | One command: `./install_deps.sh`, shipped at the root of the distribution |

### Linux, in one command

The script ships **inside the distribution**, at its root, so a downloaded
release needs nothing else:

```bash
tar xzf objeck-linux-x64_<VERSION>.tgz        # e.g. objeck-linux-x64_2026.11.0.tgz
cd objeck-lang
./install_deps.sh              # install what is missing (asks first)
./install_deps.sh --yes        # ...without the prompt
```

From a repo checkout it lives at `tools/install_deps.sh`; the two are the same
script and take the same options.

It covers every system library the distribution links against, not only SDL2.
Since v2026.11.4 the tools themselves (`obc`, `obr`, `obd`, `obi`) need none:
mbedTLS and OpenCV are linked in
([PORTABLE_LINUX.md](design/PORTABLE_LINUX.md)). What is left belongs to four
optional bindings, and to the debugger's line editing:

| Used by | Libraries |
|---|---|
| `libobjk_sdl.so` | SDL2 (core, image, mixer, ttf), OpenGL |
| `libobjk_opencv.so` | GTK 3 and GStreamer (windows and video files) |
| `libobjk_odbc.so` | unixODBC |
| `libobjk_lame.so` | LAME |
| `obd` | readline, if present (line editing and history) |

A program that loads one of these bindings without its libraries stops with a
message that names the missing library; nothing else is affected.

The list is not hardcoded. The script runs `ldd` on the distribution's own
`bin/*` and `lib/native/libobjk_*.so`, then asks the package manager (apt,
dnf/yum, pacman, zypper) which package provides each library that does not
resolve, so it picks the right name on each distribution (for example,
`libgtk-3-0t64` on Ubuntu 24.04 and `libgtk-3-0` on Debian 12). Useful
variants:

```bash
./install_deps.sh --check   # report what is missing and what needs it, install nothing
./install_deps.sh --print   # just print the command it would run
./install_deps.sh --sdl     # only what SDL2/OpenGL programs need (obc, obr, libobjk_sdl.so)
./install_deps.sh --dev     # also the SDL2/OpenGL headers, to BUILD libobjk_sdl.so
```

Use `--dev` only when compiling the native library yourself; running a release
build needs just the runtime packages.

Linux releases are built against glibc 2.28 and run on Ubuntu 22.04 and
later, Debian 12 and later, Fedora, Arch and RHEL 8 and its rebuilds. Where a
tree cannot run at all, the script says why instead of installing anything:

- **An older C library.** A distribution older than glibc 2.28 cannot load the
  tools. So is a v2026.11.3 or earlier tree anywhere but Ubuntu 24.04: those
  needed glibc 2.38 and that release's mbedTLS and OpenCV.
- **musl or another architecture.** Alpine cannot run glibc binaries, and an
  arm64 tree cannot run on x86-64. The script names the case and exits 1.

In the first case, use v2026.11.4 or later, or build Objeck from source on that
system (`core/release/deploy_posix.sh`).

### Verifying the setup

On macOS the same script checks the bundle instead of installing anything — it
confirms every SDL2 dylib the loader will ask for is actually present in the
distribution:

```bash
./install_deps.sh --check
./install_deps.sh --check --tree /usr/local/objeck-lang   # an installed copy
```

Run from an unpacked tarball or an installed copy it checks *that* tree, because
it resolves the distribution it is sitting in before falling back to a repo
checkout.

On macOS there is nothing else to check: since v2026.9.3 the package carries
OpenCV, ONNX Runtime, mbedTLS, ODBC and LAME inside itself, and the release build
fails if any file in it links a library outside the package or macOS.

## Running the examples

```bash
programs/examples/run_gl.sh                  # the 3D walkthrough
programs/examples/run_gl.sh cube_gl          # any example by name
programs/examples/run_gl.sh --verify         # the GL self-test
```

The runner picks a deploy tree, checks it is not stale against
`core/shared/version.h`, confirms the native library and dependencies are in
place, sets the library paths, and passes each demo the assets it needs.

| Example | What it shows |
|---|---|
| `gl_clear` | The minimum: a window, a clear colour, a present loop |
| `cube_gl` | A textured, rotating cube |
| `gl_boing` | The classic bouncing ball |
| `gl_model` | Loading geometry from an OBJ file |
| `gl_walkthrough` | First-person camera and collision in a 3D scene |

Compiling one by hand needs both libraries:

```bash
obc -src gl_clear.obs -lib sdl2,sdl_gl -dest gl_clear.obe
obr gl_clear.obe
```

## A minimal program

```objeck
use Game.SDL2;
use Game.OpenGL;

class GLClear {
  function : Main(args : String[]) ~ Nil {
    window := GLWindow->New("Objeck OpenGL", 640, 480);
    if(<>window->IsOk()) {
      window->GetError()->ErrorLine();
      return;
    };
    leaving { window->Free(); };

    GL->ClearColor(0.15, 0.35, 0.6, 1.0);
    while(<>window->PollEvents()) {
      window->BeginFrame();
      window->EndFrame();
    };
    GL->ReportErrors("the run");
  }
}
```

`GLWindow` handles what a GL 3.3 core context needs and is easy to get wrong:
the six context attributes, set in order and before the window is created; the
version assertion macOS requires; resolving the GL entry points; taking the
viewport from the *drawable* size rather than the requested size (they differ on
a HiDPI display); and shutting everything down in an order that works.

## The API

| Class | Purpose |
|---|---|
| `GLWindow` | Window, context, frame loop, timing, input |
| `GL` | Global state and error reporting |
| `Shader` | Compile, link, and set uniforms |
| `Mesh` | Vertex data, drawing, instancing |
| `Texture2D` | Uploads from file, solid colours, checkers |
| `RenderTarget` | Offscreen framebuffers |
| `Camera`, `Transform`, `Matrix4`, `Vector3` | Scene maths |
| `Light`, `LightRig`, `Material` | Shading |
| `ShadowMap`, `PointShadow`, `Frustum` | Shadows and culling |
| `Overlay`, `Box` | 2D overlay and bounding volumes |
| `Scene`, `PropBatch` | A scene of solids with raycasting and sliding movement (`Scene->Raycast`, `Scene->SlideMove`), and instanced props drawn in one call |

### How it is validated, and how to check

`programs/regression/gl_context_test.obs` (`# EXTRA_LIBS: sdl2,sdl_gl`) creates a real context and
checks what it draws by reading pixels back, not by completing without a crash; it skips when
there is no display. `programs/regression/gl_quaternion_test.obs` is pure arithmetic and runs
anywhere. To run the first one against a deploy tree, with a missing GL counted as a failure:

```bash
programs/examples/run_gl.sh --verify
```

The pixel-level checks have not been run for this page; whether they pass on a given machine is
what `--verify` tells you. Known limits: OpenGL 3.3 core only; the binding is coarse-grained by
design, so there is no one-to-one wrapper of the GL API.

## Why the calls are coarse-grained

The native call boundary is expensive. The VM resolves each native symbol by
string on **every** call (`GetProcAddress`/`dlsym`) and boxes every argument into
a fresh holder, so a 1:1 mapping of OpenGL onto it would be thousands of lookups
and allocations per frame.

So each call here does real work. "Compile a program from two sources" is one
call, not the five GL calls it decomposes into, and bulk data crosses as whole
arrays rather than element by element.

That happens to be exactly what GL 3.3 core wants anyway: upload geometry once,
then draw with few calls.

### Adding a call (maintainers)

One `void fn(VMContext&)` in the OpenGL section of `core/lib/sdl/sdl.cpp`, and
one method in `core/compiler/lib_src/sdl_gl.obs` that names it. See that file's
header for the two rules that matter: positional slot indices, and keeping each
call coarse.

## How SDL2 is bundled (maintainers)

The vendored macOS dylibs were built with an absolute install name
(`/usr/local/lib/libSDL2-2.0.0.dylib`), so anything linking them recorded that
path and dyld looked nowhere else. That is the only reason the old instructions
told macOS users to untar `sdl2_arm64.tgz` into `/usr/local/lib` — a
system-wide install that needs `sudo`, collides with a Homebrew SDL2, and on
Apple Silicon puts the libraries where Homebrew never looks.

`deploy_macos_arm64.sh` now copies the dylibs into `lib/sdl`, rewrites their
install names to `@rpath`, gives `lib/native/libobjk_sdl.dylib` an rpath of
`@loader_path/../sdl`, and re-signs each one ad-hoc (`install_name_tool`
invalidates a signature, and Apple Silicon refuses to load a Mach-O whose
signature does not match). It then fails the build if any `/usr/local/lib`
reference survives, or if any `@rpath` dependency does not resolve inside the
tree.

The macOS `.pkg` installs that tree to `/usr/local/objeck-lang`, so
`@loader_path/../sdl` resolves to `/usr/local/objeck-lang/lib/sdl` and the
install is self-contained.

## Troubleshooting

**"This executable appears to be invalid or compiled with an incompatible
version of the tool chain"** — the deploy tree is older than the source. Rebuild
it, or point `run_gl.sh --tree` at a current one.

**A `dlopen` failure that never mentions SDL2 (Linux)** — the system SDL2 or
libGL is missing. `tools/install_deps.sh --check` will name it.

**The tools die instantly with no message at all (macOS)** — this is
quarantine plus an *unnotarized* build, and since v2026.9.0 it should not happen
to a downloaded release. Apple's notary service registers each binary's cdhash,
so the published `.pkg`, `.zip` and `.tgz` all run straight from a download with
no user action; verified by extracting a published archive, stamping
`com.apple.quarantine` on all 23 Mach-O files, and running the toolchain.

It still applies to a **locally built** tree, which is signed ad-hoc or not at
all: `obr` is killed outright, exit 137, with nothing on stdout or stderr. A
quarantined library is marginally louder (`library load disallowed by system
policy`), but neither symptom says the word "quarantine". Clear it on the
unpacked directory:

```bash
xattr -dr com.apple.quarantine objeck-lang
```

`./install_deps.sh --check` reports which case you are in — it tests the
binaries against the `notarized` code requirement rather than assuming, so a
notarized tree is reported as fine instead of raising a false alarm.

**No GL context on a headless machine** — the self-test skips when there is no
display. Set `OBJECK_GL_REQUIRED=1` to make that a failure instead, which is
what you want in CI.
