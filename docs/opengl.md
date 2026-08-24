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
against the system SDL2 and OpenGL runtime.

| Platform | What you need to do |
|---|---|
| **macOS** | Nothing. Take the `.pkg` or `.zip` (both notarized). SDL2 ships in `lib/sdl`; OpenGL is a system framework. |
| **Windows** | Nothing. The SDL2 DLLs ship in `bin`, beside `obr.exe`. |
| **Linux** | One command: `./install_deps.sh`, shipped at the root of the distribution |

### Linux, in one command

The script ships **inside the distribution**, at its root, so a downloaded
release needs nothing else:

```bash
tar xzf objeck-linux-x64_2026.8.4.tgz
cd objeck-lang
./install_deps.sh              # install what is missing (asks first)
./install_deps.sh --yes        # ...without the prompt
```

From a repo checkout it lives at `tools/install_deps.sh`; the two are the same
script and take the same options.

It detects the package manager (apt, dnf/yum, pacman, zypper, apk) and installs
SDL2 (core, image, mixer, ttf) plus an OpenGL runtime. Useful variants:

```bash
./install_deps.sh --check   # report what is missing, install nothing
./install_deps.sh --print   # just print the command it would run
./install_deps.sh --dev     # also the headers, to BUILD libobjk_sdl.so
```

Use `--dev` only when compiling the native library yourself; running a release
build needs just the runtime packages.

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

**The tools die instantly with no message at all (macOS, legacy `.tgz`)** —
take the `.pkg` or the `.zip`; both are notarized and run as downloaded. The
`.tgz` that ships for one more release cannot be notarized, because Apple's
notary service only accepts `.zip`, `.pkg` and `.dmg`. macOS stamps
`com.apple.quarantine` on everything unpacked from it and Gatekeeper kills the
toolchain outright: `obr` exits 137 (SIGKILL) with nothing on stdout or stderr.
A quarantined library is marginally louder (`library load disallowed by system
policy`), but neither symptom says the word "quarantine". Clear it once on the
unpacked directory:

```bash
xattr -dr com.apple.quarantine objeck-lang
```

`./install_deps.sh` detects this and offers to clear it. Neither the `.pkg` nor
the notarized `.zip` needs any of this.

**No GL context on a headless machine** — the self-test skips when there is no
display. Set `OBJECK_GL_REQUIRED=1` to make that a failure instead, which is
what you want in CI.
