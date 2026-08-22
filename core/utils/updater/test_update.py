#!/usr/bin/env python3
"""
Offline tests for the obu updater. Builds obu with the test hooks and drives it
against fake releases under a temp dir -- no network on any path.

Usage:
  python3 test_update.py [--keep]

Exit code 0 = all passed, 1 = at least one failed.

The hooks (OBU_INSTALL_ROOT / OBU_RELEASE_JSON_FILE / OBU_ASSET_DIR) exist ONLY
in a build compiled with -DOBU_TEST_HOOKS; the shipped binary ignores them, so
this harness cannot weaken a real install.

Two suites:

  check   the 'check' command. Runs everywhere, including Windows. This was
          previously untested on every platform: DoCheck built its own request
          URL instead of going through FetchReleaseJson, so the hook covered
          'update' only and 'check' always hit the live network. That also made
          the "an update is available" exit-0 path untestable until a release
          newer than the compiled VERSION_STRING actually existed.

  update  'update' and 'rollback', on every shipped platform including
          Windows. The swap needs no copy-self-and-re-exec dance there: it
          moves the current tree aside with fs::rename, and Windows permits
          renaming a running image even though it forbids deleting one.

Fixtures are built with hashlib/tarfile/zipfile rather than sha256sum/tar/zip
so the harness needs no external tools of its own. The archive format follows
what the platform actually publishes -- .zip on Windows, .tgz elsewhere -- so
a format mismatch in obu shows up here rather than at release time.
"""
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "obu.cpp")
IS_WINDOWS = sys.platform == "win32"

# Mirrors OBU_UPDATE_SUPPORTED in obu.cpp. Kept as a platform test rather than
# probed from the binary on purpose: probing would mean running a real 'update'.
UPDATE_SUPPORTED = True

# Mirrors OBU_ASSET_SUFFIX: releases ship .zip on Windows, .tgz elsewhere.
ASSET_SUFFIX = ".zip" if IS_WINDOWS else ".tgz"
EXE_SUFFIX = ".exe" if IS_WINDOWS else ""

# Set by build() on Windows: a compiled stub used as the fake bin/obc.exe. The
# post-install health check runs `bin/obc -v` through CreateProcess, which needs
# a real PE image -- a .cmd or a shell script is not launchable that way.
OBC_STUB = None

passes, failures, pending = [], [], []


def ok(name):
    passes.append(name)
    print("  [PASS] %s" % name)


def bad(name, detail):
    failures.append((name, detail))
    print("  [FAIL] %s\n         %s" % (name, detail))


def skip(name, why):
    pending.append((name, why))
    print("  [PENDING] %s -- %s" % (name, why))


def check(name, cond, detail=""):
    ok(name) if cond else bad(name, detail)
    return cond


# ---------------------------------------------------------------- build

def find_vcvars():
    """Locate vcvars64.bat through vswhere, the only supported way to find an
    arbitrary VS install."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return None
    try:
        root = subprocess.run([vswhere, "-latest", "-products", "*",
                               "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                               "-property", "installationPath"],
                              capture_output=True, text=True, timeout=60).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    if not root:
        return None
    bat = os.path.join(root, "VC", "Auxiliary", "Build", "vcvars64.bat")
    return bat if os.path.exists(bat) else None


def build(workdir):
    """Compile obu with the offline hooks. Returns the binary path, or exits."""
    out = os.path.join(workdir, "obu.exe" if IS_WINDOWS else "obu")
    if IS_WINDOWS:
        vcvars = find_vcvars()
        if not vcvars:
            print("SKIP: no MSVC toolchain found (vswhere/vcvars64.bat)")
            sys.exit(0)
        # Written to a .bat and invoked directly: passing this through
        # `cmd /c "..."` cannot be quoted reliably once vcvars' own path
        # contains spaces.
        script = os.path.join(workdir, "build.bat")
        with open(script, "w") as handle:
            handle.write("@echo off\r\n")
            handle.write('call "%s" >nul\r\n' % vcvars)
            handle.write('if errorlevel 1 exit /b 1\r\n')
            handle.write('cl /nologo /std:c++17 /EHsc /O2 /W3 /D OBU_TEST_HOOKS '
                         '/D _CRT_SECURE_NO_WARNINGS "%s" /Fe:"%s" /Fo:"%s" >nul\r\n'
                         % (SOURCE, out, os.path.join(workdir, "obu.obj")))
            handle.write('exit /b %ERRORLEVEL%\r\n')
        proc = subprocess.run([script], capture_output=True, text=True)
    else:
        cxx = os.environ.get("CXX") or "c++"
        proc = subprocess.run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra",
                               "-DOBU_TEST_HOOKS", "-o", out, SOURCE],
                              capture_output=True, text=True)
    if proc.returncode != 0 or not os.path.exists(out):
        print("BUILD FAILED (exit %d)\n%s\n%s" % (proc.returncode, proc.stdout, proc.stderr))
        sys.exit(1)

    if IS_WINDOWS:
        global OBC_STUB
        OBC_STUB = build_obc_stub(workdir, find_vcvars())
    return out


def build_obc_stub(workdir, vcvars):
    """Compile the fake bin/obc.exe used by fixtures. It must be a real PE
    image because obu's health check launches it with CreateProcess."""
    src = os.path.join(workdir, "obc_stub.cpp")
    with open(src, "w") as handle:
        handle.write('#include <stdio.h>\n'
                     'int main(int argc, char** argv) {\n'
                     '  (void)argc; (void)argv;\n'
                     '  printf("Objeck 9999.1.0\\n");\n'
                     '  return 0;\n'
                     '}\n')
    out = os.path.join(workdir, "obc_stub.exe")
    script = os.path.join(workdir, "build_stub.bat")
    with open(script, "w") as handle:
        handle.write("@echo off\r\n")
        handle.write('call "%s" >nul\r\n' % vcvars)
        handle.write('if errorlevel 1 exit /b 1\r\n')
        handle.write('cl /nologo /EHsc /O2 "%s" /Fe:"%s" /Fo:"%s" >nul\r\n'
                     % (src, out, os.path.join(workdir, "obc_stub.obj")))
        handle.write('exit /b %ERRORLEVEL%\r\n')
    proc = subprocess.run([script], capture_output=True, text=True)
    if proc.returncode != 0 or not os.path.exists(out):
        print("STUB BUILD FAILED (exit %d)\n%s\n%s"
              % (proc.returncode, proc.stdout, proc.stderr))
        sys.exit(1)
    return out


# ---------------------------------------------------------------- helpers

def run(obu, args, env_extra=None, cwd=None):
    env = dict(os.environ)
    # never let an outer hook leak into a case that did not ask for it
    for key in ("OBU_INSTALL_ROOT", "OBU_RELEASE_JSON_FILE", "OBU_ASSET_DIR"):
        env.pop(key, None)
    env.update(env_extra or {})
    proc = subprocess.run([obu] + args, capture_output=True, text=True,
                          env=env, cwd=cwd, timeout=120)
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def installed_version(obu):
    _, out = run(obu, ["--version"])
    return out.strip().split()[-1]


def write_json(path, tag, assets=()):
    # The host must be github.com: ExtractAssetUrl only accepts an https URL on
    # a GitHub host, so a placeholder domain here would be correctly rejected
    # and the fixture would look like a missing asset.
    entries = ",".join(
        '{"name":"%s","browser_download_url":"https://github.com/objeck/objeck-lang/releases/download/x/%s"}'
        % (a, a) for a in assets)
    with open(path, "w") as handle:
        handle.write('{ "tag_name":"%s", "assets":[%s] }\n' % (tag, entries))
    return path


def asset_prefix():
    """Mirrors OBU_ASSET_PREFIX in obu.cpp."""
    machine = platform.machine().lower()
    if IS_WINDOWS:
        return "objeck-windows-arm64" if machine == "arm64" else "objeck-windows-x64"
    if sys.platform == "darwin":
        return "objeck-macos-arm64"
    if machine in ("aarch64", "arm64"):
        return "objeck-linux-arm64"
    return "objeck-linux-x64"


def install_exe(src, dest):
    """Copy an executable and keep it executable (shutil.copyfile drops mode)."""
    shutil.copyfile(src, dest)
    if not IS_WINDOWS:
        os.chmod(dest, 0o755)


def make_install(root, version):
    """A minimal install tree. bin/obc answers -v, which is obu's health check."""
    os.makedirs(os.path.join(root, "bin"), exist_ok=True)
    if IS_WINDOWS:
        # a real executable: the health check launches it via CreateProcess
        for name in ("obr", "obc"):
            install_exe(OBC_STUB, os.path.join(root, "bin", name + ".exe"))
    else:
        for name, body in (("obr", '#!/bin/sh\necho obr\n'),
                           ("obc", '#!/bin/sh\necho "Objeck %s"; exit 0\n' % version)):
            path = os.path.join(root, "bin", name)
            with open(path, "w") as handle:
                handle.write(body)
            os.chmod(path, 0o755)
    with open(os.path.join(root, "VERSION"), "w") as handle:
        handle.write(version)


def make_archive(stage, archive):
    """Pack 'stage' in the format this platform's releases actually ship."""
    if archive.endswith(".zip"):
        # forward-slash entries, which is what bsdtar and every unzip expect
        import zipfile
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
            for base, _dirs, files in os.walk(stage):
                for name in sorted(files):
                    full = os.path.join(base, name)
                    zf.write(full, os.path.relpath(full, stage).replace("\\", "/"))
    else:
        with tarfile.open(archive, "w:gz") as tar:
            for entry in sorted(os.listdir(stage)):
                tar.add(os.path.join(stage, entry), arcname=entry)


def make_release(reldir, asset_name, payload_version="NEW", corrupt_hash=False):
    """A fake release: the platform's archive, a SHA256SUMS, and release JSON."""
    stage = os.path.join(reldir, "stage")
    make_install(stage, payload_version)
    os.makedirs(reldir, exist_ok=True)
    archive = os.path.join(reldir, asset_name)
    make_archive(stage, archive)

    digest = ("0" * 64 if corrupt_hash
              else hashlib.sha256(open(archive, "rb").read()).hexdigest())
    with open(os.path.join(reldir, "SHA256SUMS"), "w") as handle:
        handle.write("%s  %s\n" % (digest, asset_name))

    write_json(os.path.join(reldir, "release.json"), "v9999.1.0",
               [asset_name, "SHA256SUMS"])
    return {"OBU_RELEASE_JSON_FILE": os.path.join(reldir, "release.json"),
            "OBU_ASSET_DIR": reldir}


# ---------------------------------------------------------------- check suite

def suite_check(obu, work):
    print("\ncheck command (offline, all platforms):")
    version = installed_version(obu)

    newer = write_json(os.path.join(work, "newer.json"), "v9999.1.0")
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": newer})
    check("newer release reports an update, exit 0",
          code == 0 and "update is available" in out, "exit=%d out=%r" % (code, out))

    same = write_json(os.path.join(work, "same.json"), "v" + version)
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": same})
    check("same version reports up to date, exit 1",
          code == 1 and "up to date" in out, "exit=%d out=%r" % (code, out))

    older = write_json(os.path.join(work, "older.json"), "v1.0.0")
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": older})
    check("older release reports up to date, exit 1",
          code == 1 and "up to date" in out, "exit=%d out=%r" % (code, out))

    code, out = run(obu, ["check", "--quiet"], {"OBU_RELEASE_JSON_FILE": newer})
    check("--quiet prints nothing and still signals via exit 0",
          code == 0 and out == "", "exit=%d out=%r" % (code, out))

    malformed = os.path.join(work, "malformed.json")
    with open(malformed, "w") as handle:
        handle.write('{ "no_tag_here": true }\n')
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": malformed})
    check("malformed JSON fails and names the file it actually read",
          code == 2 and malformed in out, "exit=%d out=%r" % (code, out))

    missing = os.path.join(work, "does-not-exist.json")
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": missing})
    check("unreadable hook file is reported, not silently ignored",
          code == 2 and "Unable to read" in out, "exit=%d out=%r" % (code, out))

    # The channel guard is the sink CodeQL flagged as command injection; assert
    # it rejects rather than trusting that no shell is left to exploit.
    for payload in ('v1.0" ; touch PWNED ; "', "v1.0$(touch PWNED)",
                    "v1.0`touch PWNED`", "v1.0 && touch PWNED", "../../etc/passwd"):
        code, out = run(obu, ["check", "--channel", payload],
                        {"OBU_RELEASE_JSON_FILE": newer}, cwd=work)
        rejected = code == 2 and "Invalid channel tag" in out
        landed = os.path.exists(os.path.join(work, "PWNED"))
        if landed:
            os.remove(os.path.join(work, "PWNED"))
        check("channel payload rejected: %s" % payload,
              rejected and not landed,
              "exit=%d landed=%s out=%r" % (code, landed, out))

    code, out = run(obu, ["check", "--channel", "v2026.1.0"],
                    {"OBU_RELEASE_JSON_FILE": newer})
    check("a well-formed --channel is accepted",
          code == 0 and "update is available" in out, "exit=%d out=%r" % (code, out))

    code, out = run(obu, ["check", "--bogus"], {"OBU_RELEASE_JSON_FILE": newer})
    check("unknown option is rejected", code == 2 and "Unknown option" in out,
          "exit=%d out=%r" % (code, out))


# ---------------------------------------------------------------- update suite

def suite_update(obu, work):
    print("\nupdate / rollback:")
    if not UPDATE_SUPPORTED:
        code, out = run(obu, ["update"])
        check("unsupported platform refuses 'update' with exit 2",
              code == 2 and "not yet available on this platform" in out,
              "exit=%d out=%r" % (code, out))
        code, out = run(obu, ["rollback"])
        check("unsupported platform refuses 'rollback' with exit 2",
              code == 2 and "Rollback is not available on this platform" in out,
              "exit=%d out=%r" % (code, out))
        skip("update + rollback flow", "obu reports this platform as unsupported")
        return

    prefix = asset_prefix()
    asset = "%s_9999.1.0%s" % (prefix, ASSET_SUFFIX)

    # happy path: verify, swap, obc health check, then rollback
    root = os.path.join(work, "t1", "root")
    make_install(root, "OLD")
    env = make_release(os.path.join(work, "t1", "rel"), asset)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--quiet"], env)
    if check("update succeeds", code == 0, "exit=%d out=%r" % (code, out)):
        check("update installed the new tree",
              open(os.path.join(root, "VERSION")).read().strip() == "NEW",
              "VERSION=%r" % open(os.path.join(root, "VERSION")).read())
    code, out = run(obu, ["rollback", "--quiet"], {"OBU_INSTALL_ROOT": root})
    if check("rollback succeeds", code == 0, "exit=%d out=%r" % (code, out)):
        check("rollback restored the old tree",
              open(os.path.join(root, "VERSION")).read().strip() == "OLD",
              "VERSION=%r" % open(os.path.join(root, "VERSION")).read())
        check("rollback left no .previous behind",
              not os.path.isdir(os.path.join(root, ".previous")))

    # tampered checksum: reject, tree untouched, no residue
    root = os.path.join(work, "t2", "root")
    make_install(root, "ORIG")
    env = make_release(os.path.join(work, "t2", "rel"), asset, corrupt_hash=True)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--quiet"], env)
    check("tampered asset is rejected", code != 0, "exit=%d out=%r" % (code, out))
    check("tampered update did not change the tree",
          open(os.path.join(root, "VERSION")).read().strip() == "ORIG")
    check("tampered update left no .previous",
          not os.path.isdir(os.path.join(root, ".previous")))
    check("tampered update left no staging dir",
          not os.path.isdir(os.path.join(root, ".obu-work")))

    # rollback with nothing to roll back to: refuse, install intact
    root = os.path.join(work, "t3", "root")
    make_install(root, "SOLO")
    code, out = run(obu, ["rollback", "--quiet"], {"OBU_INSTALL_ROOT": root})
    check("rollback with no previous version fails", code != 0,
          "exit=%d out=%r" % (code, out))
    check("refused rollback did not erase the install",
          os.path.isfile(os.path.join(root, "bin", "obc" + EXE_SUFFIX)))

    # a crafted asset name must never reach a shell
    root = os.path.join(work, "t4", "root")
    make_install(root, "V")
    evil = "%s_$(touch${IFS}PWNED)%s" % (prefix, ASSET_SUFFIX)
    reldir = os.path.join(work, "t4", "rel")
    env = make_release(reldir, evil)
    env["OBU_INSTALL_ROOT"] = root
    cwd = os.path.join(work, "t4", "cwd")
    os.makedirs(cwd, exist_ok=True)
    run(obu, ["update", "--quiet"], env, cwd=cwd)
    check("crafted asset name did not execute",
          not os.path.exists(os.path.join(cwd, "PWNED")),
          "COMMAND INJECTION: %r executed" % evil)


# ---------------------------------------------------------------- self-swap

def suite_selfswap(obu, work):
    """obu replacing the tree it is itself running from.

    The suites above point OBU_INSTALL_ROOT at a fake tree while obu runs from
    somewhere else, so the running image is never the one being renamed. That
    is the whole Windows question -- a running .exe can be renamed but never
    deleted -- so it needs its own case: obu is installed at <root>/bin/ and
    invoked by that path with OBU_INSTALL_ROOT UNSET, leaving InstallRoot() to
    resolve to the tree about to be swapped.
    """
    print("\nself-replacement (obu swaps its own tree):")
    root = os.path.join(work, "self", "install")
    make_install(root, "OLD")
    live = os.path.join(root, "bin", "obu" + EXE_SUFFIX)
    install_exe(obu, live)

    prefix = asset_prefix()
    asset = "%s_9999.1.0%s" % (prefix, ASSET_SUFFIX)
    reldir = os.path.join(work, "self", "rel")
    stage = os.path.join(reldir, "stage")
    make_install(stage, "NEW")
    # the archived copy needs the exec bit too: it becomes the live obu after
    # the swap, and copyfile does not carry mode across
    install_exe(obu, os.path.join(stage, "bin", "obu" + EXE_SUFFIX))
    os.makedirs(reldir, exist_ok=True)
    archive = os.path.join(reldir, asset)
    make_archive(stage, archive)
    with open(os.path.join(reldir, "SHA256SUMS"), "w") as handle:
        handle.write("%s  %s\n"
                     % (hashlib.sha256(open(archive, "rb").read()).hexdigest(), asset))
    write_json(os.path.join(reldir, "release.json"), "v9999.1.0", [asset, "SHA256SUMS"])

    # deliberately NO OBU_INSTALL_ROOT: obu must derive the root from its own path
    env = {"OBU_RELEASE_JSON_FILE": os.path.join(reldir, "release.json"),
           "OBU_ASSET_DIR": reldir}
    code, out = run(live, ["update", "--quiet"], env)
    if check("obu updates the tree it is running from", code == 0,
             "exit=%d out=%r" % (code, out)):
        check("self-swap installed the new tree",
              open(os.path.join(root, "VERSION")).read().strip() == "NEW")
        check("the running obu was preserved in .previous",
              os.path.isfile(os.path.join(root, ".previous", "bin", "obu" + EXE_SUFFIX)))
        check("an obu is present at the live path after the swap", os.path.isfile(live))

    code, out = run(live, ["rollback", "--quiet"], {"OBU_ASSET_DIR": reldir})
    if check("the swapped-in obu can roll itself back", code == 0,
             "exit=%d out=%r" % (code, out)):
        check("self-swap rollback restored the old tree",
              open(os.path.join(root, "VERSION")).read().strip() == "OLD")
        check("an obu is present at the live path after rollback", os.path.isfile(live))


# ---------------------------------------------------------------- main

def main():
    keep = "--keep" in sys.argv
    work = tempfile.mkdtemp(prefix="obu-test-")
    try:
        obu = build(work)
        print("obu %s (%s), update supported: %s"
              % (installed_version(obu), sys.platform, UPDATE_SUPPORTED))
        suite_check(obu, work)
        suite_update(obu, work)
        if UPDATE_SUPPORTED:
            suite_selfswap(obu, work)
    finally:
        if keep:
            print("\nkept: %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)

    print("\n" + "=" * 44)
    print("  %d passed, %d failed, %d pending" % (len(passes), len(failures), len(pending)))
    print("=" * 44)
    for name, detail in failures:
        print("  FAILED: %s -- %s" % (name, detail))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
