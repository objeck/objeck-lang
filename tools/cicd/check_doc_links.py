#!/usr/bin/env python3
"""
check_doc_links.py -- every relative link and anchor in the repository's Markdown resolves.

For each tracked .md file (plus untracked ones git does not ignore, so a new page is
checked before it is committed) it extracts inline links and images, `[id]: target`
reference definitions and `<a href>` / `<img src>` tags, skips code fences and code
spans, and checks each target that is not a URL:

  * the file or directory exists in the repository, with exactly that spelling
    (a link that works on Windows and macOS because their file systems ignore case
    is a 404 on GitHub; the check is against git's file list, not the disk);
  * a `#fragment` on a Markdown target names a heading there (GitHub's slug rules,
    including the -1, -2 suffix for a repeated heading) or an `id=`/`name=` anchor;
  * a `#fragment` on any other file is accepted only as a line reference (`#L10`,
    `#L10-L20`), which GitHub resolves for source files.

A link starting with `/` is read from the repository root, as GitHub does. No network
access: http(s), mailto and other schemes are not followed. About a second for the
whole repository.

    python tools/cicd/check_doc_links.py              check everything
    python tools/cicd/check_doc_links.py docs         only Markdown files under docs/
    python tools/cicd/check_doc_links.py --test       prove the checker can still fail

Exit 0 = every link resolves, 1 = broken links (each printed as file:line), 2 = usage.

Why: a rename or a deleted page leaves links that nothing complains about until a
reader hits the 404. docs/ holds about seventy pages that link to each other, to source
files and to line ranges.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# vendored third-party trees: their links are theirs, not ours
SKIP_PARTS = ("/eigen-", "/vendor/", "/node_modules/", "/third_party/")

INLINE_RE = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*(<[^>]*>|[^)\s]*(?:\([^)\s]*\)[^)\s]*)*)(?:\s+(?:\"[^\"]*\"|'[^']*'|\([^)]*\)))?\s*\)")
REFDEF_RE = re.compile(r"^\s{0,3}\[([^\]]+)\]:\s*(<[^>]*>|\S+)")
HTML_RE = re.compile(r"""<(?:a|img)\b[^>]*?\b(?:href|src)\s*=\s*["']([^"']+)["']""", re.I)
ANCHOR_RE = re.compile(r"""<a\b[^>]*?\b(?:name|id)\s*=\s*["']([^"']+)["']|\bid\s*=\s*["']([^"']+)["']""", re.I)
FENCE_RE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
LINE_REF_RE = re.compile(r"^L\d+(?:C\d+)?(?:-L\d+(?:C\d+)?)?$")


def repo_files(root):
    """(files, dirs): every path git knows (tracked, or untracked and not ignored)."""
    out = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=root,
                         stdout=subprocess.PIPE, universal_newlines=True, errors="replace")
    if out.returncode != 0:
        raise SystemExit("git ls-files failed; run from a repository checkout")
    files = set(p for p in out.stdout.split("\0") if p)
    return files


def dirs_of(files):
    dirs = {""}
    for f in files:
        d = os.path.dirname(f)
        while d and d not in dirs:
            dirs.add(d)
            d = os.path.dirname(d)
    return dirs


def strip_inline_md(text):
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)      # links and images keep their text
    text = re.sub(r"<[^>]+>", "", text)                          # html tags
    text = text.replace("`", "")
    text = re.sub(r"(\*\*|\*|__)", "", text)
    text = re.sub(r"(?<!\w)_(.+?)_(?!\w)", r"\1", text)
    return text


def slug(text):
    s = strip_inline_md(text).strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def anchors_of(lines):
    """Set of fragment names a GitHub-rendered Markdown file offers."""
    anchors = set()
    seen = {}
    in_fence = None
    for i, line in enumerate(lines):
        m = FENCE_RE.match(line)
        if m:
            if in_fence is None:
                in_fence = m.group(1)[0]
            elif m.group(1)[0] == in_fence:
                in_fence = None
            continue
        if in_fence:
            continue
        h = HEADING_RE.match(line)
        title = None
        if h:
            title = h.group(2)
        elif i + 1 < len(lines) and line.strip() and re.match(r"^\s{0,3}(=+|-+)\s*$", lines[i + 1]) \
                and not line.lstrip().startswith(("-", "*", "+", ">", "|")):
            title = line.strip()
        if title is not None:
            base = slug(title)
            n = seen.get(base, 0)
            seen[base] = n + 1
            anchors.add(base if n == 0 else "%s-%d" % (base, n))
        for am in ANCHOR_RE.finditer(line):
            anchors.add(am.group(1) or am.group(2))
    return anchors


def links_of(lines):
    """Yield (line number, target) for every link outside code."""
    in_fence = None
    for i, line in enumerate(lines, 1):
        m = FENCE_RE.match(line)
        if m:
            if in_fence is None:
                in_fence = m.group(1)[0]
            elif m.group(1)[0] == in_fence:
                in_fence = None
            continue
        if in_fence:
            continue
        text = re.sub(r"(`+)(?:(?!\1).)+\1", lambda mm: " " * len(mm.group(0)), line)  # code spans
        for lm in INLINE_RE.finditer(text):
            yield i, lm.group(1)
        rd = REFDEF_RE.match(text)
        if rd:
            yield i, rd.group(2)
        for hm in HTML_RE.finditer(text):
            yield i, hm.group(1)


def split_target(target):
    target = target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    path, _, frag = target.partition("#")
    path = path.split("?", 1)[0]
    return path, frag


KNOWN_NAME = "tools/cicd/doc_links_known.txt"


def load_known():
    """{(file, target)}: links known to be broken, each with a reason, to be fixed and deleted.

    One per line: `file :: target :: reason`. A line whose link resolves again is an error,
    so the list can only shrink.
    """
    path = os.path.join(ROOT, KNOWN_NAME)
    known = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = [x.strip() for x in line.split("::")]
                if len(parts) != 3 or not all(parts):
                    raise SystemExit("%s: expected 'file :: target :: reason', got: %s" % (KNOWN_NAME, line))
                known[(parts[0], parts[1])] = parts[2]
    return known


def percent_decode(s):
    return re.sub(r"%([0-9A-Fa-f]{2})", lambda m: chr(int(m.group(1), 16)), s) if "%" in s else s


def check(root, files, md_files):
    """Return a list of (file, line, target, message) for every broken link."""
    dirs = dirs_of(files)
    cache = {}
    problems = []

    def anchors_for(rel):
        if rel not in cache:
            with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as fh:
                cache[rel] = anchors_of(fh.read().splitlines())
        return cache[rel]

    for rel in md_files:
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as fh:
            lines = fh.read().splitlines()
        base = os.path.dirname(rel)
        for lineno, target in links_of(lines):
            path, frag = split_target(target)
            if SCHEME_RE.match(path) or path.startswith("//") or (not path and not frag):
                continue
            if not path:
                dest = rel
            else:
                path = percent_decode(path)
                joined = path.lstrip("/") if path.startswith("/") else os.path.join(base, path)
                dest = os.path.normpath(joined).replace("\\", "/")
                if dest == ".":
                    dest = ""
                if dest.startswith(".."):
                    problems.append((rel, lineno, target, "link leaves the repository: %s" % target))
                    continue
                if dest not in files and dest not in dirs:
                    problems.append((rel, lineno, target, "no such file or directory: %s" % target))
                    continue
            if not frag:
                continue
            frag = percent_decode(frag)
            if dest in files and dest.lower().endswith(".md"):
                if frag.lower() not in {a.lower() for a in anchors_for(dest)} and not LINE_REF_RE.match(frag):
                    problems.append((rel, lineno, target, "no heading or anchor '#%s' in %s" % (frag, dest)))
            elif dest in files and not LINE_REF_RE.match(frag):
                problems.append((rel, lineno, target, "'#%s' is not a line reference (#L10) for %s" % (frag, dest)))
            # a directory, or an html page: nothing to check against
    return problems


def select(files, prefixes):
    out = []
    for f in sorted(files):
        if not f.lower().endswith(".md"):
            continue
        slashed = "/" + f
        if any(part in slashed for part in SKIP_PARTS):
            continue
        if prefixes and not any(f == p or f.startswith(p.rstrip("/") + "/") for p in prefixes):
            continue
        out.append(f)
    return out


def self_test():
    """Build a tiny tree and require the checker to pass the good links and fail each bad one."""
    with tempfile.TemporaryDirectory() as tmp:
        def write(rel, text):
            p = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)

        write("a.md", "# Title\n\n## Same name\n\n## Same name\n\n"
                      "[ok](b.md) [ok2](sub/c.md#the-heading) [ok3](#same-name-1) [ok4](sub/code.py#L3-L9)\n"
                      "[ok5](/sub/c.md) [ok6](https://example.com/x) [ok7](sub/) [ok8](b.md#html-anchor)\n"
                      "`[not a link](gone.md)`\n```\n[nor this](gone.md)\n```\n")
        write("b.md", "<a name=\"html-anchor\"></a>\ntext\n")
        write("sub/c.md", "# The heading\n[up](../a.md#title)\n")
        write("sub/code.py", "x = 1\n")
        write("bad.md", "[missing](nope.md)\n[bad anchor](a.md#no-such-heading)\n[case](B.md)\n"
                        "[anchor on code](sub/code.py#top)\n[escape](../../x.md)\n[ref][r]\n\n[r]: gone2.md\n")
        files = {"a.md", "b.md", "sub/c.md", "sub/code.py", "bad.md"}
        good = check(tmp, files, ["a.md", "b.md", "sub/c.md"])
        bad = check(tmp, files, ["bad.md"])
    want_bad = 6
    if good:
        print("check_doc_links self-test FAILED: good links were rejected:")
        print("\n".join("  " + p for p in good))
        return 1
    if len(bad) != want_bad:
        print("check_doc_links self-test FAILED: expected %d problems in bad.md, found %d:" % (want_bad, len(bad)))
        print("\n".join("  " + p for p in bad))
        return 1
    print("check_doc_links self-test passed: the good links were accepted and %d bad ones rejected" % len(bad))
    return 0


def main(argv):
    if "--test" in argv:
        return self_test()
    args = [a for a in argv if not a.startswith("-")]
    if len(args) != len(argv):
        print(__doc__)
        return 2
    files = repo_files(ROOT)
    md = select(files, args)
    if not md:
        print("no Markdown files match " + " ".join(args))
        return 2
    problems = check(ROOT, files, md)
    known = load_known()
    seen = set()
    unknown = []
    for p in problems:
        key = (p[0], p[2])
        if key in known:
            seen.add(key)
        else:
            unknown.append(p)
    stale = [] if args else sorted(set(known) - seen)
    for rel, lineno, _target, msg in unknown:
        print("%s:%d: %s" % (rel, lineno, msg))
    for rel, target in stale:
        print("%s: %s is listed in %s but resolves now; delete the entry" % (rel, target, KNOWN_NAME))
    if unknown or stale:
        print("%d broken link(s), %d stale known entr%s, in %d Markdown file(s)" % (
            len(unknown), len(stale), "y" if len(stale) == 1 else "ies", len(md)))
        return 1
    note = " (%d known and listed in %s)" % (len(seen), KNOWN_NAME) if seen else ""
    print("%d Markdown files: every relative link and anchor resolves%s" % (len(md), note))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
