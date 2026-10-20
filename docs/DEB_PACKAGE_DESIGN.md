# Debian packages for Linux

Status: implemented for v2026.10.1 (`tools/packaging/build_deb.sh`, `.github/workflows/release-deb.yml`).

## Why

Windows has an MSI and macOS a signed `.pkg`; Linux had only a tarball, after which
the user ran `install_deps.sh`, then sourced `env.sh` in every new shell or edited
their profile. A `.deb` does all three in one step:

```
sudo apt install ./objeck-ubuntu24.04-x64_2026.10.1.deb
```

apt resolves the system libraries, the commands are on `PATH` in every shell, and
`sudo apt remove objeck` takes everything back out.

## What it targets, and why only that

A `.deb` names its dependencies by package, and Debian names a shared-library
package after the soname it carries plus distribution-specific suffixes: Ubuntu
24.04 calls mbedTLS 2.28 `libmbedtls14t64`, Debian 12 `libmbedtls14`, and a release
that moves to mbedTLS 3 has no such package at all. The binaries are built on the
`ubuntu-latest` (24.04) and `ubuntu-24.04-arm` runners, so the package targets
**Ubuntu 24.04 LTS and the distributions built on it** (Linux Mint 22, Pop!_OS 24.04,
elementary OS 8). The file name says so: `objeck-ubuntu24.04-<arch>_<version>.deb`.
The tarball stays the portable choice for every other distribution.

One package per architecture: `x64` (Debian `amd64`) and `arm64`.

## Layout

The released tarball, unchanged, under `/usr/lib/objeck` -- the same files users
already run, so the package adds no new build and no new binaries to verify.

| Path | What |
|---|---|
| `/usr/lib/objeck/` | the tarball's `objeck-lang/` tree: `bin`, `lib`, `doc`, `examples` |
| `/usr/bin/obc`, `obr`, `obi`, `obd`, `obb`, `obu`, `objeck-shell` | wrappers (below) |
| `/usr/share/applications/objeck.desktop` | the desktop entry, `@OBJECK_HOME@` filled in |
| `/usr/share/doc/objeck/copyright` | the licence |

**Wrappers, not symlinks.** Without `OBJECK_LIB_PATH` the tools look for libraries
at `../lib/` relative to the *current directory* (`GetLibraryPath` in
`core/shared/sys.h`), not relative to the binary, so a symlink in `/usr/bin` would
work only when run from `/usr/lib/objeck/bin`. Each wrapper sets
`OBJECK_LIB_PATH=/usr/lib/objeck/lib` unless the user already set one, then `exec`s
the real binary.

## Dependencies

Computed by `dpkg-shlibdeps` on the build runner, never written by hand, and split
by who needs them:

- **Depends** -- what `bin/*` links: the C++ runtime, zlib, mbedTLS, readline.
  Without these `obr` does not start.
- **Recommends** -- what the optional native modules link: OpenCV
  (`libobjk_opencv`, `libobjk_onnx`), SDL2 and libGL (`libobjk_sdl`), unixODBC
  (`libobjk_odbc`), LAME (`libobjk_lame`). Each is loaded only when a program uses
  the library that wraps it. apt installs Recommends by default;
  `--no-install-recommends` gives a toolchain without the few hundred MB OpenCV
  pulls in, which still compiles and runs everything else.

ONNX Runtime ships inside the tree (`lib/native`, found through `$ORIGIN`) and
needs nothing.

## obu under apt

`obu update` and `obu rollback` rewrite the install in place, which would leave
dpkg's record of the files wrong. The `/usr/bin/obu` wrapper refuses those two and
says to install the new `.deb` instead; `check`, `verify`, `version` and `help` pass
through, so `obu verify` still checks a download against the signed manifest.

## Release integration

`release-deb.yml` runs when Release Publish succeeds, and on demand for a version
already published:

1. On a native runner per architecture, download that release's tarball, install
   its runtime libraries with its own `install_deps.sh`, and build the `.deb`.
2. Install it in a clean `ubuntu:24.04` container twice -- once with
   `--no-install-recommends`, once with -- and compile and run a program each time;
   check `obu update` is refused and `apt remove` leaves nothing behind.
3. Upload both packages, add their lines to `SHA256SUMS`, and re-sign the manifest
   through `resign-manifest.yml`, so `obu verify` and `sha256sum -c` cover them.

Windows signing regenerates `SHA256SUMS` only for names already in it, and keeps
those lines, so the two steps can run in either order -- but not at the same time.

## Not now

- **An apt repository** (`apt update` picks up new releases). It needs a signing
  key for the repository, hosting, and a policy for old versions. A downloaded
  `.deb` is already covered by the signed `SHA256SUMS`. Revisit if people ask.
- **Other distributions** (Debian 12, Fedora's `.rpm`). Each is a separate build
  against that distribution's libraries, not a repackaging.
- **A guard inside `obu`** that detects a package-managed install by itself. The
  wrapper covers the `.deb`; a marker file `obu` reads would also cover anyone
  calling `/usr/lib/objeck/bin/obu` directly. That changes a binary, so it waits for
  the next release.
