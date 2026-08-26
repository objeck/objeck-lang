#!/usr/bin/env python3
"""
Hold native entry-point names in fields instead of writing them at call sites.

An Objeck string literal is materialised into a fresh String every time it is
EVALUATED, not once at load. A name written at a `CallFunction` call site
therefore costs an allocation and a copy on every native call. Measured on
Windows x64: 1655ns for a call whose name is a literal against 130ns for the
same call with the name held in a field -- 92% of the cost of a native call,
spent in code that reads like a constant.

The transformation, per class:

  1. Collect every distinct name passed to CallFunction inside that class.
  2. Declare one STATIC String field per name, just after the class opens.
     Static because the names are class-wide constants, and because a
     `function :` (static) cannot read an instance field.
  3. At each call site, fill the field on first use and pass it.

     before:  Proxy->GetDllProxy()->CallFunction("odbc_connect", args);
     after:   if(@fn_odbc_connect = Nil) { @fn_odbc_connect := "odbc_connect"; };
              Proxy->GetDllProxy()->CallFunction(@fn_odbc_connect, args);

Two threads can race on the lazy fill. That race is benign: both write an
equivalent String and one wins, so the worst case is a discarded allocation.
Nothing can read a half-built value, because the store is a pointer store.

Usage:
  hoist_native_names.py <file.obs> [<file.obs> ...]   rewrite in place
  hoist_native_names.py --check <file.obs> [...]      report literals, exit 1

`--check` is the durable half: it is what keeps a new library, or a regenerated
one, from quietly reintroducing the cost. Note that `core/lib/sdl/code_gen`
GENERATES sdl2.obs, and its emitter writes the literal form -- regenerating it
undoes this and `--check` is how you find out.
"""
import io
import re
import sys

CALL = re.compile(r'CallFunction\("([A-Za-z0-9_]+)"')

# Must END WITH the opening brace. Without that anchor a doc-comment line
# beginning with the word "class" or "interface" -- sdl_gl.obs has one reading
# "interface exactly, and does not accept ..." -- is taken for a declaration,
# and the field block lands outside any class body.
CLASS_OPEN = re.compile(r'^\t(?:class|interface)\s+(?::\s*private\s*:\s*)?(\w+)[^\n]*\{\s*$')


def scan(lines):
    """Map each line to its enclosing class, and each class to its first body line."""
    owner = [None] * len(lines)
    body_start = {}
    current = None
    for i, line in enumerate(lines):
        match = CLASS_OPEN.match(line)
        if match:
            current = match.group(1)
            body_start.setdefault(current, i + 1)
        owner[i] = current
    return owner, body_start


def code_lines(lines):
    """Yield (index, line) for lines that are code, skipping comments.

    A doc block quoting a call site -- this codebase has several, including ones
    describing this very change -- would otherwise have a guard statement spliced
    into the middle of it, which breaks the comment and the class with it.
    """
    in_doc = False
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("#~"):
            in_doc = True
        if in_doc:
            if stripped.endswith("~#"):
                in_doc = False
            continue
        if stripped.startswith("#"):
            continue
        yield i, line


def check(path):
    """Return the call sites that still write their name as a literal."""
    lines = io.open(path, encoding="utf-8", newline="").read().split("\n")
    found = []
    for i, line in code_lines(lines):
        for name in CALL.findall(line):
            found.append((i + 1, name))
    return found


def transform(path):
    src = io.open(path, encoding="utf-8", newline="").read()
    eol = "\r\n" if "\r\n" in src else "\n"
    lines = src.split(eol)
    owner, body_start = scan(lines)

    used = {}
    for i, line in code_lines(lines):
        for name in CALL.findall(line):
            if owner[i]:
                used.setdefault(owner[i], set()).add(name)

    # Rewrite call sites first, so inserting declarations does not shift indices.
    changed = 0
    for i, line in code_lines(lines):
        names = CALL.findall(line)
        if not names or not owner[i]:
            continue
        indent = line[:len(line) - len(line.lstrip("\t"))]
        guards = []
        new_line = line
        for name in names:
            field = "@fn_" + name
            guards.append('%sif(%s = Nil) { %s := "%s"; };' % (indent, field, field, name))
            new_line = new_line.replace('CallFunction("%s"' % name,
                                        "CallFunction(%s" % field, 1)
        lines[i] = eol.join(guards + [new_line])
        changed += len(names)

    # Then the declarations, from the bottom up for the same reason.
    for cls in sorted(used, key=lambda c: body_start.get(c, 0), reverse=True):
        at = body_start.get(cls)
        if at is None:
            raise SystemExit("could not find the body of class %s in %s" % (cls, path))
        decls = [
            "\t\t# Native entry-point names, held rather than written at each call",
            "\t\t# site: a string literal is materialised into a fresh String every",
            "\t\t# time it is evaluated, which on a hot path is an allocation and a",
            "\t\t# copy per call. Static because these are class-wide constants and a",
            "\t\t# static function cannot read an instance field.",
        ]
        decls += ["\t\t@fn_%s : static : String;" % n for n in sorted(used[cls])]
        lines.insert(at, eol.join(decls))

    io.open(path, "w", encoding="utf-8", newline="").write(eol.join(lines))
    return changed


def main(argv):
    checking = "--check" in argv
    targets = [a for a in argv[1:] if not a.startswith("--")]
    if not targets:
        print(__doc__.strip())
        return 2

    if checking:
        total = 0
        for path in targets:
            for line_no, name in check(path):
                print("%s:%d: native name written as a literal: \"%s\"" % (path, line_no, name))
                total += 1
        if total:
            print("\n%d call site(s) still allocate their name on every call." % total)
            print("Run this tool without --check to hold them in fields.")
            return 1
        print("No native-name literals at call sites.")
        return 0

    for path in targets:
        print("  %s: hoisted %d call sites" % (path, transform(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
