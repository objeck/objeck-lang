#!/usr/bin/env python3
"""
check_docs_deps.py -- keep the docs and the binaries honest about dependencies.

core/release/runtime_deps.json lists what Objeck needs from the operating
system. This gate checks it from both sides:

  --docs
      Every package a doc tells users to install (apt-get/apt, brew, dnf,
      pacman command lines, including backslash continuations and HTML <pre>
      blocks) must be listed in the manifest. A package that is not -- because a
      library became static, or a binding was bundled -- is reported with its
      file and line, so no page keeps telling users to install something the
      build no longer uses.

  --artifacts <deploy tree> --platform linux-x64|linux-arm64|macos-arm64
      The libraries the built tree actually links must match the manifest.
      Linux: every ELF NEEDED soname that is neither in the tree nor a toolchain
      library. macOS: every install name outside /usr/lib, /System and the tree.
      A dependency the manifest does not list fails (the docs cannot mention
      it); a listed one nothing links fails too (the docs overstate it).

  --portable <deploy tree> --platform linux-x64|linux-arm64
      The RELEASE tree, built by tools/packaging/build_portable_linux.sh, must
      run on every distribution the manifest's linux.release section promises
      (docs/design/PORTABLE_LINUX.md):
        - no file needs a glibc, libstdc++ or libgcc symbol version newer than
          the floor (GLIBC_2.28 is what AlmaLinux 8, Debian 10 and Ubuntu 18.10
          have; one newer symbol and the tool does not start there);
        - bin/* link the C and C++ runtime and zlib and nothing else, so no
          missing package can stop obr, obc, obd, obi, obu or obb from starting;
        - the native libraries listed as self-contained link nothing outside
          the tree either, and the rest link exactly the listed system sonames;
        - bin/* keep the hardening the Ubuntu-built releases had (PIE, full
          RELRO, non-executable stack).

Why: v2026.9.2's release notes told Linux users to install nghttp2, ngtcp2 and
GnuTLS packages and macOS users to `brew install opencv@4 onnxruntime` -- while
the macOS obr also needed four more Homebrew formulas no document mentioned,
one of which Homebrew does not ship. Docs described what someone remembered,
not what the binaries link.

Exit 0 = consistent, 1 = drift (each finding printed), 2 = usage error.
"""
import argparse
import html
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MANIFEST = os.path.join(ROOT, "core", "release", "runtime_deps.json")

# Where install instructions live. Globs are expanded relative to the repo root.
DOC_GLOBS = [
    "README.md",
    "core/readme.md",
    "core/vm/README.md",
    "docs/*.md",
    "docs/readme.html",
    "docs/web/getting_started.html",
    "programs/deploy/util/readme/readme.in.html",
    "tools/lsp/README.md",
    "tools/lsp/README.txt",
    "tools/lsp/docs/install_guide.html",
    "tools/install_deps.sh",
]
# The changelog describes old releases on purpose.
DOC_EXCLUDE = {"CHANGELOG.md"}

INSTALL_RE = re.compile(
    r"(?P<pm>apt-get\s+install|apt\s+install|brew\s+install|dnf\s+install|yum\s+install|pacman\s+-S\w*)\s+(?P<rest>.*)")
PM_KEY = {"apt-get": "apt", "apt": "apt", "brew": "brew", "dnf": "dnf", "yum": "dnf", "pacman": "pacman"}
# Tokens on an install line that are not package names.
NOT_PACKAGES = {"sudo", "&&", "||", ";", "\\", "install", "-y", "--yes", "--no-install-recommends",
                "--needed", "--noconfirm", "--cask", "-q", "-qq"}
# Prose that happens to contain "brew install x on macOS": an English word ends the command.
STOPWORDS = {"on", "or", "and", "for", "to", "if", "with", "the", "then", "from", "in", "via", "as", "is", "first"}

# Prose, not commands: a retired dependency named as something users need.
# Mentioning it with "static", "bundled" and the like is describing the change, not requiring it.
NEED_WORDS = re.compile(r"\b(install|require[sd]?|depend(s|encies|ency)?|runtime|dynamically|package|apt-get|brew|dnf|pacman)\b", re.I)
EXEMPT_WORDS = re.compile(r"\b(static(ally)?|bundled?|linked (in|into)|build_quic_deps|AWS-LC|no longer|removed|instead of|v2026\.[0-8]|v2026\.9\.[0-2])\b", re.I)
# Windows still uses nghttp2 (vcpkg) and WinHTTP; a line about Windows alone is not about the POSIX runtime.
WINDOWS_ONLY = re.compile(r"\b(vcpkg|Windows|MSYS2|mingw|WinHTTP)\b", re.I)
POSIX_WORDS = re.compile(r"\b(apt|apt-get|brew|Homebrew|Linux|macOS|dnf|pacman|zypper)\b", re.I)
# An install command quoted in order to say it is wrong ("plain `brew install opencv` does not help").
NEG_CONTEXT = re.compile(r"does not help|is not enough|not enough|instead of|do not|don't|never|won't", re.I)
# Link targets and file names are not prose: third_party_dependencies.md is not a requirement,
# and "ONNX Runtime" is a product name, not a statement about the runtime.
NOT_PROSE = re.compile(r"\]\([^)]*\)|\S+\.(md|html|sh|py|cmd|obs|txt)\b|ONNX Runtime", re.I)
MSYS2_PACKAGE = re.compile(r"\bmingw-w64-")


def expand_globs():
    import glob
    files = []
    for g in DOC_GLOBS:
        for path in sorted(glob.glob(os.path.join(ROOT, g))):
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            if rel not in DOC_EXCLUDE and os.path.isfile(path):
                files.append(rel)
    return files


def clean(line):
    line = re.sub(r"<[^>]+>", " ", line)          # HTML tags
    line = html.unescape(line)                     # &gt; &amp; ...
    line = line.replace("`", " ")
    return line


def install_commands(text):
    """Yield (line_number, package_manager, [packages]) for every install command."""
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        cleaned = clean(lines[i])
        m = INSTALL_RE.search(cleaned)
        following = clean(lines[i + 1]) if i + 1 < len(lines) else ""
        # "Plain `brew install opencv`\ndoes not help" -- the verdict can sit on the next line.
        if not m or NEG_CONTEXT.search(cleaned[m.end():] + " " + following):
            i += 1
            continue
        start = i + 1
        rest = m.group("rest")
        # Follow backslash continuations.
        while rest.rstrip().endswith("\\") and i + 1 < len(lines):
            i += 1
            rest = rest.rstrip()[:-1] + " " + clean(lines[i])
        pm = PM_KEY[m.group("pm").split()[0]]
        tokens = []
        for tok in re.split(r"\s+", rest.strip()):
            tok = tok.strip("\"',;.)(")
            if not tok or tok in NOT_PACKAGES or tok.startswith("-"):
                continue
            if tok in ("&&", "|", ">", "then", "echo") or tok.startswith(("$", "#", "http", "/", "./", "<")):
                break                                   # end of the command
            if tok.lower() in STOPWORDS or not re.match(r"^[A-Za-z0-9][A-Za-z0-9@+._-]*$", tok):
                break
            tokens.append(tok)
        # MSYS2 uses pacman too, with its own package names.
        if pm == "pacman" and any(t.startswith("mingw-w64-") for t in tokens):
            pm = "msys2"
        if tokens:
            yield start, pm, tokens
        i += 1


def check_docs(manifest):
    known = {pm: set(v.get("runtime", [])) | set(v.get("build", [])) | set(v.get("test", [])) | set(v.get("tools", []))
             for pm, v in manifest["packages"].items()}
    findings = 0
    for rel in expand_globs():
        with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
            text = f.read()
        for line, pm, pkgs in install_commands(text):
            for pkg in pkgs:
                if pkg not in known.get(pm, set()):
                    print(f"  {rel}:{line}: {pm} package '{pkg}' is not in core/release/runtime_deps.json")
                    findings += 1
        retired = manifest.get("retired", {})
        prev = ""
        for n, raw in enumerate(text.splitlines(), 1):
            line = NOT_PROSE.sub(" ", clean(raw))
            # A sentence often names the need on one line and the libraries on the next.
            context = prev + " " + line
            prev = line
            if MSYS2_PACKAGE.search(line) or (WINDOWS_ONLY.search(line) and not POSIX_WORDS.search(line)):
                continue
            for name, why in retired.items():
                if re.search(r"\b" + re.escape(name) + r"\b", line, re.I) and NEED_WORDS.search(context) \
                        and not EXEMPT_WORDS.search(context):
                    print(f"  {rel}:{n}: names '{name}' as something users need, but {why}")
                    findings += 1
    return findings


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout


class ToolFailed(Exception):
    """An inspection tool (readelf) did not run, or said it could not read the file."""


def run_checked(cmd):
    """Like run, but a tool that is missing or exits non-zero is an error, not empty output.

    An empty answer reads as "nothing found" to every check that follows, so a file readelf
    could not read would have passed them all."""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as e:
        raise ToolFailed(f"{cmd[0]} could not run: {e}")
    if r.returncode != 0:
        raise ToolFailed(f"{' '.join(cmd[:-1])} exited {r.returncode}: {(r.stderr or '').strip()[:200]}")
    return r.stdout


# e_machine of an ELF header, by the architecture names runtime_deps.json uses
ELF_MACHINE = {"x64": (62, "x86-64"), "arm64": (183, "AArch64")}


def core_binary_problem(path, arch):
    """Why `path` is not an executable ELF file for `arch`, or None. A text file or a script
    left at bin/obi has the right name and nothing else."""
    if not os.path.isfile(path):
        return "is missing"
    with open(path, "rb") as f:
        head = f.read(20)
    if head[:4] != b"\x7fELF":
        return "is not an ELF file (it starts with %r)" % head[:16]
    if len(head) < 20 or head[4] != 2 or head[5] != 1:
        return "is not a 64-bit little-endian ELF file"
    machine = int.from_bytes(head[18:20], "little")
    want = ELF_MACHINE.get(arch)
    if want and machine != want[0]:
        return "is an ELF file for machine %d, not %s (%d)" % (machine, want[1], want[0])
    if not os.access(path, os.X_OK):
        return "is not executable"
    return None


def check_linux_artifacts(tree, arch, manifest):
    linux = manifest["linux"]
    toolchain = set(linux["toolchain_sonames"])
    expected = set(linux["sonames"][arch])
    in_tree = set()
    elf_files = []
    for dirpath, _, names in os.walk(tree):
        for n in names:
            p = os.path.join(dirpath, n)
            # A soname the tree ships as a version symlink (libonnxruntime.so.1 ->
            # libonnxruntime.so.1.19.0) is provided in-tree even though only the
            # target is a regular file.
            if os.path.islink(p):
                in_tree.add(n)
                continue
            if not os.path.isfile(p):
                continue
            with open(p, "rb") as f:
                if f.read(4) != b"\x7fELF":
                    continue
            elf_files.append(p)
            in_tree.add(n)
    needed = {}
    for p in elf_files:
        for m in re.finditer(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", run(["readelf", "-d", p])):
            needed.setdefault(m.group(1), set()).add(os.path.relpath(p, tree))
    external = {s for s in needed if s not in toolchain and s not in in_tree}
    findings = 0
    for s in sorted(external - expected):
        print(f"  linux-{arch}: links {s} (from {', '.join(sorted(needed[s]))}) but runtime_deps.json does not list it")
        findings += 1
    for s in sorted(expected - external):
        print(f"  linux-{arch}: runtime_deps.json lists {s} but nothing in {tree} links it")
        findings += 1
    return findings


def version_key(v):
    return tuple(int(x) for x in v.split("."))


def elf_files_of(tree):
    """(regular ELF files, every name the tree provides -- files and version symlinks)."""
    in_tree = set()
    elf_files = []
    for dirpath, _, names in os.walk(tree):
        for n in names:
            p = os.path.join(dirpath, n)
            if os.path.islink(p):
                in_tree.add(n)
                continue
            if not os.path.isfile(p):
                continue
            with open(p, "rb") as f:
                if f.read(4) != b"\x7fELF":
                    continue
            elf_files.append(p)
            in_tree.add(n)
    return sorted(elf_files), in_tree


def check_linux_portable(tree, arch, manifest):
    linux = manifest["linux"]
    release = linux["release"]
    toolchain = set(linux["toolchain_sonames"]) | set(release["core_sonames"])
    floors = release["max_symbol_versions"]
    families = "|".join(re.escape(f) for f in floors)
    core_linked = set()
    core = release["core_binaries"]
    self_contained = set(release["self_contained_natives"])
    expected = set(release["sonames"][arch])
    elf_files, in_tree = elf_files_of(tree)
    findings = 0

    def fail(msg):
        nonlocal findings
        print(f"  linux-{arch}: {msg}")
        findings += 1

    for name in core:
        problem = core_binary_problem(os.path.join(tree, "bin", name), arch)
        if problem:
            fail(f"bin/{name} {problem}")
    for name in sorted(self_contained):
        if not os.path.isfile(os.path.join(tree, "lib", "native", name)):
            fail(f"lib/native/{name} is missing")
    if not elf_files:
        fail(f"no ELF files under {tree}")

    external = {}
    for p in elf_files:
        rel = os.path.relpath(p, tree).replace(os.sep, "/")
        # 1. symbol versions: the highest of each family this file asks the system for
        highest = {}
        try:
            versions = run_checked(["readelf", "-V", "-W", p])
            dynamic = run_checked(["readelf", "-d", "-W", p])
        except ToolFailed as e:
            fail(f"{rel} could not be inspected: {e}")
            continue
        for fam, ver in re.findall(r"Name: (" + families + r")_([0-9]+(?:\.[0-9]+)*)\b", versions):
            if fam not in highest or version_key(ver) > version_key(highest[fam]):
                highest[fam] = ver
        for fam, ver in sorted(highest.items()):
            if version_key(ver) > version_key(floors[fam]):
                fail(f"{rel} needs {fam}_{ver}; the release floor is {fam}_{floors[fam]}")

        # 2. what it links outside the tree and the C/C++ runtime
        needed = set(re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dynamic))
        outside = {s for s in needed if s not in toolchain and s not in in_tree}
        core_linked |= needed & set(release["core_sonames"])
        is_core = rel.startswith("bin/")
        if is_core or os.path.basename(p) in self_contained:
            for s in sorted(outside):
                fail(f"{rel} links {s}; it must need nothing but the C and C++ runtime, zlib"
                     + ("" if is_core else " and the tree"))
        else:
            for s in outside:
                external.setdefault(s, set()).add(rel)

        # 3. hardening of the executables
        if is_core:
            try:
                header = run_checked(["readelf", "-h", "-W", p])
                segments = run_checked(["readelf", "-l", "-W", p])
            except ToolFailed as e:
                fail(f"{rel} could not be inspected: {e}")
                continue
            if not re.search(r"Type:\s+DYN", header):
                fail(f"{rel} is not position-independent (PIE)")
            if "GNU_RELRO" not in segments:
                fail(f"{rel} has no read-only relocations (RELRO)")
            if not re.search(r"\bBIND_NOW\b|Flags:.*\bNOW\b", dynamic):
                fail(f"{rel} does not bind at start-up (BIND_NOW), so RELRO is partial")
            stack = re.search(r"GNU_STACK\s+(?:0x[0-9a-f]+\s+){5}(\S+)", segments)
            if not stack or "E" in stack.group(1):
                fail(f"{rel} has an executable stack")

    for s in sorted(set(external) - expected):
        fail(f"links {s} (from {', '.join(sorted(external[s]))}) but runtime_deps.json linux.release does not list it")
    for s in sorted(expected - set(external)):
        fail(f"runtime_deps.json linux.release lists {s} but nothing in {tree} links it")
    for s in sorted(set(release["core_sonames"]) - core_linked):
        fail(f"runtime_deps.json linux.release core_sonames lists {s} but nothing in {tree} links it")
    if not findings:
        print(f"  {len(elf_files)} ELF files: " + ", ".join(f"{f} <= {v}" for f, v in sorted(floors.items()))
              + f"; bin/ links only the C/C++ runtime and {', '.join(release['core_sonames'])}; {len(expected)} optional system sonames, as listed")
    return findings


def check_macos_artifacts(tree, manifest):
    allowed = manifest["macos"]["external_prefixes"]
    tree = os.path.abspath(tree)
    findings = 0
    seen_prefixes = set()
    for dirpath, _, names in os.walk(tree):
        for n in names:
            p = os.path.join(dirpath, n)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            out = run(["otool", "-L", p])
            if "is not an object file" in out or not out:
                continue
            for dep in out.splitlines()[1:]:
                name = dep.strip().split(" (")[0]
                if not name or name.startswith(("/usr/lib/", "/System/", "@rpath/", "@loader_path/", "@executable_path/")):
                    continue
                prefix = next((a for a in allowed if name.startswith(a)), None)
                if prefix:
                    seen_prefixes.add(prefix)
                    continue
                print(f"  macos-arm64: {os.path.relpath(p, tree)} links {name}, outside the package and not in runtime_deps.json")
                findings += 1
    for a in sorted(set(allowed) - seen_prefixes):
        print(f"  macos-arm64: runtime_deps.json allows {a} but nothing links it -- remove it")
        findings += 1
    return findings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--docs", action="store_true", help="check install instructions in the docs")
    ap.add_argument("--artifacts", metavar="TREE", help="check what a built deploy tree links")
    ap.add_argument("--portable", metavar="TREE", help="hold a Linux release tree to the portability floor")
    ap.add_argument("--platform", choices=["linux-x64", "linux-arm64", "macos-arm64"])
    args = ap.parse_args()
    if not args.docs and not args.artifacts and not args.portable:
        ap.print_usage()
        return 2
    if (args.artifacts or args.portable) and not args.platform:
        print("--artifacts and --portable need --platform")
        return 2
    if args.portable and not args.platform.startswith("linux"):
        print("--portable is for linux-x64 and linux-arm64")
        return 2

    with open(MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)

    findings = 0
    if args.docs:
        print("== docs: install instructions vs core/release/runtime_deps.json")
        findings += check_docs(manifest)
    if args.artifacts:
        print(f"== artifacts: {args.artifacts} ({args.platform}) vs core/release/runtime_deps.json")
        if args.platform.startswith("linux"):
            findings += check_linux_artifacts(args.artifacts, args.platform.split("-")[1], manifest)
        else:
            findings += check_macos_artifacts(args.artifacts, manifest)

    if args.portable:
        print(f"== portable: {args.portable} ({args.platform}) vs core/release/runtime_deps.json linux.release")
        findings += check_linux_portable(args.portable, args.platform.split("-")[1], manifest)

    if findings:
        print(f"FAIL: {findings} dependency statement(s) disagree with what Objeck actually needs")
        return 1
    print("OK: docs and binaries agree with core/release/runtime_deps.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
