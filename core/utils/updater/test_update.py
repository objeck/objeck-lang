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

Three suites:

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

  selfswap  obu replacing the tree it is itself running from.

Fixtures are built with hashlib/tarfile/zipfile rather than sha256sum/tar/zip
so the harness needs no external tools of its own. The archive format follows
what the platform actually publishes -- .zip on Windows and macOS, .tgz on
Linux -- so
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

# Mirrors OBU_ASSET_SUFFIX: releases ship .zip on Windows and macOS (only an
# archive Apple recognises can be notarized), .tgz on Linux.
ASSET_SUFFIX = ".zip" if (IS_WINDOWS or sys.platform == "darwin") else ".tgz"
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


def make_release(reldir, asset_name, payload_version="NEW", corrupt_hash=False,
                 tag="v9999.1.0", sums_name=None, corrupt_archive=False,
                 drop_obc=False):
    """A fake release: the platform's archive, a SHA256SUMS, and release JSON.

    The keyword arguments build the deliberately broken releases:
      corrupt_hash     SHA256SUMS lists the wrong digest (tampered download)
      sums_name        SHA256SUMS lists some OTHER file, so the asset is absent
                       from the manifest entirely
      corrupt_archive  the archive is garbage but its digest is honest, so the
                       integrity gate passes and unpacking is what fails
      drop_obc         the payload has bin/ (so it is still detected as a tree)
                       but no bin/obc, which is what obu health-checks
    """
    stage = os.path.join(reldir, "stage")
    make_install(stage, payload_version)
    if drop_obc:
        os.remove(os.path.join(stage, "bin", "obc" + EXE_SUFFIX))
    os.makedirs(reldir, exist_ok=True)
    archive = os.path.join(reldir, asset_name)
    make_archive(stage, archive)
    if corrupt_archive:
        # overwritten AFTER packing, and hashed below, so the manifest is right
        # about a file that is not an archive at all
        with open(archive, "wb") as handle:
            handle.write(b"this is not an archive\n" * 64)

    digest = ("0" * 64 if corrupt_hash
              else hashlib.sha256(open(archive, "rb").read()).hexdigest())
    with open(os.path.join(reldir, "SHA256SUMS"), "w") as handle:
        handle.write("%s  %s\n" % (digest, sums_name or asset_name))

    write_json(os.path.join(reldir, "release.json"), tag,
               [asset_name, "SHA256SUMS"])
    return {"OBU_RELEASE_JSON_FILE": os.path.join(reldir, "release.json"),
            "OBU_ASSET_DIR": reldir}


# obu's own scratch entries, mirroring IsObuScratchEntry in obu.cpp. Every
# refusal is asserted against each of these BY NAME rather than against a
# summary flag, so a failure says which one was left behind.
RESIDUE_PATHS = (".previous", ".obu-work", ".obu-work/staging", ".obu-rollback",
                 ".obu.lock")


def check_no_residue(label, root):
    for rel in RESIDUE_PATHS:
        path = os.path.join(root, *rel.split("/"))
        check("%s left no %s" % (label, rel), not os.path.exists(path),
              "%s still exists" % path)


def tree_state(root):
    """Every path under 'root' with file contents hashed -- the evidence that a
    refused update changed nothing.

    Nothing is excluded. '.obu.lock' used to be, because ReleaseLock only closed
    the descriptor and left the file sitting in the install root; obu removes it
    on release now (#931), so a run that refuses really does leave the tree byte
    for byte as it found it, and this function can say so without a carve-out.
    """
    state = {}
    for base, dirs, files in os.walk(root):
        rel_base = os.path.relpath(base, root)
        for name in sorted(dirs):
            rel = os.path.normpath(os.path.join(rel_base, name)).replace("\\", "/")
            state[rel + "/"] = "dir"
        for name in sorted(files):
            rel = os.path.normpath(os.path.join(rel_base, name)).replace("\\", "/")
            with open(os.path.join(base, name), "rb") as handle:
                state[rel] = hashlib.sha256(handle.read()).hexdigest()
    return state


def check_tree_unchanged(label, root, before):
    after = tree_state(root)
    added = sorted(set(after) - set(before))
    removed = sorted(set(before) - set(after))
    changed = sorted(k for k in set(before) & set(after) if before[k] != after[k])
    check("%s left the install tree unchanged" % label,
          not (added or removed or changed),
          "added=%s removed=%s changed=%s" % (added, removed, changed))


# ------------------------------------------------- version-ordering fixtures
#
# CompareVersions in obu.cpp walks the components numerically and pads the
# shorter side with zeros. Both halves of that are easy to lose to a plain
# string compare, so the tags below are DERIVED from whatever version the
# harness just read out of the binary -- they keep their meaning across a
# release bump instead of encoding today's numbers.

def numeric_parts(version):
    return [int(part) for part in version.split(".")]


def lexically_smaller_newer_tag(version):
    """A tag numerically NEWER than 'version' that nonetheless sorts BEFORE it
    as a plain string -- 2026.9.10 against an installed 2026.9.5. Returns None
    when the installed version admits no such tag (one whose components are all
    0 or 1, say), in which case the case is reported pending rather than faked.
    """
    parts = numeric_parts(version)
    for i in range(len(parts) - 1, -1, -1):
        candidates = list(range(parts[i] + 1, parts[i] + 1000))
        candidates += [10 ** power for power in range(1, 9) if 10 ** power > parts[i]]
        for value in candidates:
            bumped = parts[:i] + [value] + [0] * (len(parts) - i - 1)
            tag = ".".join(str(part) for part in bumped)
            if tag < version:          # the comparison obu must NOT be doing
                return tag
    return None


# ---------------------------------------------------------------- check suite

def suite_verify(obu, work):
    """`obu verify <archive> <SHA256SUMS>` -- the check `update` already does,
    exposed for anyone who downloaded with curl.

    Runs everywhere, including Windows, because it touches nothing but two
    files the caller names. The interesting property is that a MISMATCH and an
    ERROR are different exit codes: a script has to be able to tell "this file
    is wrong" from "I could not tell", and folding them together would make a
    missing manifest look like a corrupted download.
    """
    print("\nverify command (offline, all platforms):")

    room = os.path.join(work, "verify")
    os.makedirs(room, exist_ok=True)

    good = os.path.join(room, "payload.tgz")
    with open(good, "wb") as handle:
        handle.write(b"objeck release payload\n")
    digest = hashlib.sha256(open(good, "rb").read()).hexdigest()

    sums = os.path.join(room, "SHA256SUMS")
    with open(sums, "w") as handle:
        handle.write("%s  payload.tgz\n" % digest)
        handle.write("%s  absent.tgz\n" % ("0" * 64))

    code, out = run(obu, ["verify", good, sums])
    check("a matching file verifies, exit 0",
          code == 0 and "Verified" in out, "exit=%d out=%r" % (code, out))

    # The manifest lists bare names, so the caller's path must not matter.
    code, out = run(obu, ["verify", "payload.tgz", "SHA256SUMS"], cwd=room)
    check("it looks the file up by name, not by the path given",
          code == 0, "exit=%d out=%r" % (code, out))

    tampered = os.path.join(room, "tampered.tgz")
    shutil.copyfile(good, tampered)
    with open(tampered, "ab") as handle:
        handle.write(b"x")
    os.replace(tampered, good)

    code, out = run(obu, ["verify", good, sums])
    check("a tampered file FAILS with exit 1, not 2",
          code == 1 and "FAILED" in out, "exit=%d out=%r" % (code, out))
    check("the failure names both hashes",
          digest in out, "out=%r" % out)

    code, out = run(obu, ["verify", good, sums, "--quiet"])
    check("--quiet prints nothing and still signals via exit 1",
          code == 1 and out == "", "exit=%d out=%r" % (code, out))

    stranger = os.path.join(room, "stranger.tgz")
    with open(stranger, "wb") as handle:
        handle.write(b"not in the manifest\n")
    code, out = run(obu, ["verify", stranger, sums])
    check("a file the manifest does not list is an ERROR, exit 2",
          code == 2, "exit=%d out=%r" % (code, out))

    code, out = run(obu, ["verify", os.path.join(room, "nope.tgz"), sums])
    check("a missing archive is an error, exit 2",
          code == 2, "exit=%d out=%r" % (code, out))

    code, out = run(obu, ["verify", good, os.path.join(room, "nope.sums")])
    check("a missing manifest is an error, exit 2",
          code == 2, "exit=%d out=%r" % (code, out))

    code, out = run(obu, ["verify", good])
    check("too few operands is an error, exit 2",
          code == 2, "exit=%d out=%r" % (code, out))

    code, out = run(obu, ["verify", good, sums, "--nonsense"])
    check("an unknown option is refused, exit 2",
          code == 2, "exit=%d out=%r" % (code, out))

    # A manifest fetched on Windows, or edited there, carries CRLF. ExpectedHash
    # strips it; without that every line would end in a stray character and
    # nothing would ever match.
    crlf_payload = os.path.join(room, "crlf.tgz")
    with open(crlf_payload, "wb") as handle:
        handle.write(b"crlf manifest case\n")
    crlf_digest = hashlib.sha256(open(crlf_payload, "rb").read()).hexdigest()
    crlf_sums = os.path.join(room, "CRLF_SUMS")
    with open(crlf_sums, "wb") as handle:
        handle.write(("%s  crlf.tgz\r\n" % crlf_digest).encode("ascii"))

    code, out = run(obu, ["verify", crlf_payload, crlf_sums])
    check("a CRLF manifest still matches",
          code == 0, "exit=%d out=%r" % (code, out))


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

    # --- ordering is numeric per component, not lexicographic
    parts = numeric_parts(version)

    lex_tag = lexically_smaller_newer_tag(version)
    if lex_tag:
        path = write_json(os.path.join(work, "order-lex.json"), "v" + lex_tag)
        code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": path})
        check("v%s is newer than the installed %s -- a lexicographic compare "
              "would call this older" % (lex_tag, version),
              code == 0 and "update is available" in out,
              "exit=%d out=%r" % (code, out))
    else:
        skip("numerically newer but lexicographically smaller tag",
             "installed version %s admits no such tag" % version)

    if len(parts) >= 2:
        # one component FEWER than the installed version: CompareVersions pads
        # the missing component with 0, so 2026.10 == 2026.10.0 and wins
        short_tag = ".".join(str(p) for p in parts[:-2] + [parts[-2] + 1])
        path = write_json(os.path.join(work, "order-short.json"), "v" + short_tag)
        code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": path})
        check("the shorter tag v%s pads to %s.0 and is newer than %s -- a "
              "compare that did not pad, or a lexicographic one, would call "
              "this older" % (short_tag, short_tag, version),
              code == 0 and "update is available" in out,
              "exit=%d out=%r" % (code, out))
    else:
        skip("shorter tag pads with zeros", "installed version %s has one component" % version)

    # one component MORE: the installed side is the one that gets padded, and
    # 2026.9.5.1 beats 2026.9.5.0
    long_tag = version + ".1"
    path = write_json(os.path.join(work, "order-long.json"), "v" + long_tag)
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": path})
    check("the longer tag v%s is newer than %s -- a compare that stopped at "
          "the shorter side would call these equal" % (long_tag, version),
          code == 0 and "update is available" in out, "exit=%d out=%r" % (code, out))

    # the same padding in the other direction: a trailing .0 adds nothing
    equal_tag = version + ".0"
    path = write_json(os.path.join(work, "order-equal.json"), "v" + equal_tag)
    code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": path})
    check("v%s pads to exactly the installed %s, so it is up to date, exit 1 -- "
          "a length-first or lexicographic compare would report an update"
          % (equal_tag, version),
          code == 1 and "up to date" in out, "exit=%d out=%r" % (code, out))

    # --- malformed tags: fail with a message, never report an update
    #
    # An empty tag stops earlier than the rest: ExtractTagName rejects the empty
    # string, so the diagnostic is the "tag_name" one rather than "Malformed
    # release tag". Both are asserted by their own message, so a case that
    # started failing for the wrong reason would not pass silently.
    for index, (tag, expected) in enumerate((("v2026.9.x", "Malformed release tag"),
                                             ("v", "Malformed release tag"),
                                             ("2026..6", "Malformed release tag"),
                                             ("", "tag_name"))):
        path = write_json(os.path.join(work, "bad-tag-%d.json" % index), tag)
        code, out = run(obu, ["check"], {"OBU_RELEASE_JSON_FILE": path})
        check("malformed tag %r fails with a message and reports no update" % tag,
              code == 2 and expected in out and "update is available" not in out,
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

    # release carries no asset for THIS platform (the macOS notarization gap
    # does this for real): refuse before anything is downloaded or moved
    root = os.path.join(work, "t5", "root")
    make_install(root, "NOASSET")
    before = tree_state(root)
    env = make_release(os.path.join(work, "t5", "rel"),
                       "objeck-nosuchplatform-x64_9999.1.0" + ASSET_SUFFIX)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--quiet"], env)
    check("a release with no %s asset is refused" % prefix,
          code == 2 and ("has no %s asset" % prefix) in out,
          "exit=%d out=%r" % (code, out))
    check_tree_unchanged("refused update (no asset for this platform)", root, before)
    check_no_residue("refused update (no asset for this platform)", root)

    # the asset exists but SHA256SUMS does not list it at all -- distinct from a
    # listed-but-wrong digest: there is no hash to compare, so obu must refuse
    # rather than treat an absent line as nothing to check
    root = os.path.join(work, "t6", "root")
    make_install(root, "NOSUM")
    before = tree_state(root)
    env = make_release(os.path.join(work, "t6", "rel"), asset,
                       sums_name="objeck-some-other-asset_9999.1.0" + ASSET_SUFFIX)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--quiet"], env)
    check("an asset absent from SHA256SUMS is refused",
          code == 2 and "SHA256SUMS does not list" in out,
          "exit=%d out=%r" % (code, out))
    check_tree_unchanged("refused update (asset not in SHA256SUMS)", root, before)
    check_no_residue("refused update (asset not in SHA256SUMS)", root)

    # a corrupt archive whose digest is honest: the integrity gate PASSES and
    # unpacking is what fails, so this covers the window between the two. Run
    # without --quiet to prove the run really got past "Verified ...".
    root = os.path.join(work, "t7", "root")
    make_install(root, "CORRUPT")
    before = tree_state(root)
    env = make_release(os.path.join(work, "t7", "rel"), asset, corrupt_archive=True)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update"], env)
    check("a corrupt archive that passes its hash is refused at unpack time",
          code == 2 and "Verified" in out and
          ("Unable to read the archive" in out or "Unable to unpack" in out or
           "did not contain an Objeck tree" in out),
          "exit=%d out=%r" % (code, out))
    check_tree_unchanged("refused update (corrupt archive)", root, before)
    check_no_residue("refused update (corrupt archive)", root)

    # payload with a bin/ but no bin/obc: DetectPayloadRoot accepts it, so the
    # failure lands on the post-install health check (obc -v, obu.cpp ~l.1403),
    # which must roll the old tree back rather than leave a broken install
    root = os.path.join(work, "t8", "root")
    make_install(root, "HEALTHY")
    before = tree_state(root)
    env = make_release(os.path.join(work, "t8", "rel"), asset,
                       payload_version="BROKEN", drop_obc=True)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--quiet"], env)
    check("a payload without bin/obc fails the post-install health check",
          code == 2 and "post-install check" in out, "exit=%d out=%r" % (code, out))
    check_tree_unchanged("failed health check", root, before)
    check_no_residue("failed health check", root)

    # idempotence. obu compares the version COMPILED INTO the running binary,
    # not the one in the tree it just wrote, so the release here is tagged with
    # the installed version and the first pass is forced; a 9999.1.0 fixture
    # would legitimately update again on the second pass.
    version = installed_version(obu)
    root = os.path.join(work, "t9", "root")
    make_install(root, "PRE")
    env = make_release(os.path.join(work, "t9", "rel"), asset, tag="v" + version)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--force", "--quiet"], env)
    if check("--force installs a release matching the installed version",
             code == 0, "exit=%d out=%r" % (code, out)):
        settled = tree_state(root)
        code, out = run(obu, ["update"], env)
        check("a second update against the same release reports up to date, exit 1",
              code == 1 and "already at" in out, "exit=%d out=%r" % (code, out))
        check_tree_unchanged("the repeated update", root, settled)
        check("the repeated update left no .obu-work",
              not os.path.exists(os.path.join(root, ".obu-work")))

    # a stale .obu-work left by a run that died mid-download must not block the
    # next update -- obu clears the staging tree before it creates it
    root = os.path.join(work, "t10", "root")
    make_install(root, "STALE")
    stale = os.path.join(root, ".obu-work", "staging", "leftover")
    os.makedirs(stale)
    with open(os.path.join(stale, "junk.txt"), "w") as handle:
        handle.write("debris from an interrupted run\n")
    with open(os.path.join(root, ".obu-work", "asset" + ASSET_SUFFIX), "wb") as handle:
        handle.write(b"half a download")
    env = make_release(os.path.join(work, "t10", "rel"), asset)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update", "--quiet"], env)
    if check("a stale .obu-work does not block the next update", code == 0,
             "exit=%d out=%r" % (code, out)):
        check("the update over a stale .obu-work installed the new tree",
              open(os.path.join(root, "VERSION")).read().strip() == "NEW",
              "VERSION=%r" % open(os.path.join(root, "VERSION")).read())
        # the debris was inside the staging dir the new payload unpacks into,
        # so an obu that stopped clearing it would install the leftovers too
        check("the stale staging debris was not installed into the tree",
              not os.path.exists(os.path.join(root, "leftover")),
              "%s was moved into the install" % os.path.join(root, "leftover"))
        check("the successful update removed the stale .obu-work",
              not os.path.exists(os.path.join(root, ".obu-work")))

    # interrupted swap: the state a run killed between "archive the current
    # tree" and "install the payload" leaves behind -- .previous/bin present,
    # no bin/ in the root. Built directly rather than by killing a process, so
    # the case is deterministic. RecoverInterruptedSwap (obu.cpp ~l.1182) must
    # put it back on the next run, before anything else.
    root = os.path.join(work, "t11", "root")
    make_install(os.path.join(root, ".previous"), "INTERRUPTED")
    env = make_release(os.path.join(work, "t11", "rel"), asset, tag="v" + version)
    env["OBU_INSTALL_ROOT"] = root
    code, out = run(obu, ["update"], env)
    check("an interrupted swap is announced and recovered on the next run",
          "previous update was interrupted" in out, "exit=%d out=%r" % (code, out))
    check("recovery restored bin/obc into the root",
          os.path.isfile(os.path.join(root, "bin", "obc" + EXE_SUFFIX)))
    check("recovery restored the interrupted tree, not the payload",
          os.path.isfile(os.path.join(root, "VERSION")) and
          open(os.path.join(root, "VERSION")).read().strip() == "INTERRUPTED")
    check("recovery cleared .previous",
          not os.path.exists(os.path.join(root, ".previous")))
    check("recovery left no .obu-work",
          not os.path.exists(os.path.join(root, ".obu-work")))
    check("the recovering run then continues normally and reports up to date, exit 1",
          code == 1 and "already at" in out, "exit=%d out=%r" % (code, out))


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
        suite_verify(obu, work)
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
