#!/usr/bin/env python3
"""gen_trap_signatures.py must refuse to write a header it cannot stand behind,
and --check must notice one that was written anyway.

core/shared/trap_signatures.h is what Loader::Verify holds every trap to. A row
of a trap whose handler reads its caller's frame or instance lists the library
methods the trap may stand in; with an empty list the VM built from the header
refuses every valid program that uses the trap (System.Base:GetClass is
LOAD_CLS_BY_INST). The generator used to write exactly that header, and exit 0,
when its corpus directory was misspelled or empty, and --check compared only the
instruction and the count, so it called the result "ok".

Nothing is built: the generator is run on directories that hold no usable image,
and --check on copies of the committed header with one row changed or removed
(a frame-reading trap's contexts; an array-literal copy's row, which no compiler
directive stands behind and --check did not require). A check that
rejects everything is worth no more than one that accepts everything, so the
committed header itself must pass.

Run: python3 tools/cicd/test_gen_trap_signatures.py
"""

import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_trap_signatures as gen  # noqa: E402

GENERATOR = os.path.join(HERE, "gen_trap_signatures.py")
HEADER = gen.HEADER
failures = []


def check(name, cond, detail=""):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", name))
    if not cond:
        failures.append(name)
        if detail:
            print("         " + str(detail)[-600:])


def digest():
    with open(HEADER, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run(*args):
    p = subprocess.run([sys.executable, GENERATOR] + list(args), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def check_copy(change):
    """--check's problems for the committed header after 'change' (text -> text)."""
    with open(HEADER, encoding="utf-8") as f:
        text = f.read()
    changed = change(text)
    with tempfile.TemporaryDirectory(prefix="trapsig-") as work:
        path = os.path.join(work, "trap_signatures.h")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(changed)
        saved = gen.HEADER
        gen.HEADER = path
        try:
            return changed != text, gen.check()
        finally:
            gen.HEADER = saved


def row_edit(trap, old_tail, new_tail):
    """Replaces the end of one trap's row; the row must end with old_tail."""
    def change(text):
        pattern = re.compile(r'(\{ instructions::%s, [^\n]*)%s\n' % (trap, re.escape(old_tail)))
        return pattern.sub(lambda m: m.group(1) + new_tail + "\n", text, count=1)
    return change


before = digest()

# ---------------------------------------------------- the generator's corpus ----
print("generator: a corpus that cannot describe the traps")
with tempfile.TemporaryDirectory(prefix="trapsig-") as work:
    missing = os.path.join(work, "cover_misspelled")
    rc, out = run(missing)
    check("a directory that does not exist: non-zero exit", rc != 0, out)
    check("a directory that does not exist: says so", "not a directory" in out and "not written" in out, out)
    check("a directory that does not exist: the committed header is untouched", digest() == before)

    empty = os.path.join(work, "empty")
    os.mkdir(empty)
    rc, out = run(empty)
    check("an empty directory: non-zero exit", rc != 0, out)
    check("an empty directory: the committed header is untouched", digest() == before)

    junk = os.path.join(work, "junk")
    os.mkdir(junk)
    for i in range(3):
        with open(os.path.join(junk, "bad%d.obe" % i), "wb") as f:
            f.write(b"\x00\x01\x02\x03 not an image " * (i + 1))
    rc, out = run(junk)
    check("a directory of files that are not images: non-zero exit", rc != 0, out)
    check("a directory of files that are not images: counts them and reads none", "3 .obe file(s) found" in out, out)
    check("a directory of files that are not images: the committed header is untouched", digest() == before)

# --------------------------------------------- rows from a partial corpus ----
print("generator: a corpus in which a frame-reading trap never appears")
ids = gen.trap_ids()
shapes, _ = gen.trap_shapes.shapes()
frames = gen.frame_traps()
check("the handlers name frame-reading traps, LOAD_CLS_BY_INST among them",
      "LOAD_CLS_BY_INST" in frames and frames["LOAD_CLS_BY_INST"] is True, sorted(frames))
rows, notes, errors = gen.build_rows({}, shapes, ids)
named = set(e.split(":")[0] for e in errors)
check("every frame-reading trap the compiler writes is an error, and no other trap is",
      named == set(t for t in frames if t in shapes) and len(named) > 0, sorted(named ^ set(t for t in frames if t in shapes)))

saved = dict(gen.OVERRIDES)
try:
    gen.OVERRIDES["LOAD_CLS_BY_INST"] = dict(saved["LOAD_CLS_BY_INST"], contexts=["System.Base:GetClass:"])
    rows, notes, errors = gen.build_rows({}, shapes, ids)
    check("a contexts override stands in for the corpus",
          not any(e.startswith("LOAD_CLS_BY_INST:") for e in errors) and
          any(r[0] == "LOAD_CLS_BY_INST" and r[6] == ["System.Base:GetClass:"] for r in rows), errors[:3])
finally:
    gen.OVERRIDES.clear()
    gen.OVERRIDES.update(saved)

# ------------------------------------- a corpus that lacks one valid context ----
# SYS_TIME stands in two constructors of System.Time.Date. A corpus without the image
# of one of them used to give a row that named only the other: not empty, so it was
# written and --check passed it, and the loader then refused Date->New(Bool) in every
# valid program. The library sources say where each directive is; the corpus is held
# to them.
print("generator: a corpus that lacks one of a trap's contexts")


def committed_contexts():
    """trap -> the contexts of its committed row, for every frame-reading trap."""
    with open(HEADER, encoding="utf-8") as f:
        text = f.read()
    found = {}
    for m in re.finditer(r'\{ instructions::([A-Z0-9_]+), L"[A-Z0-9_]+", [^\n]*, L"([^"\n]*)", (true|false) \},\n', text):
        found[m.group(1)] = m.group(2).split("|")
    return found


def corpus_with(contexts):
    """What census() returns for images holding each trap in exactly these methods."""
    seen = {}
    for trap, names in contexts.items():
        rec = gen.Seen()
        rec.contexts = set(names)
        seen[trap] = rec
    return seen


library = gen.library_traps(set(shapes))
whole = committed_contexts()
check("the library sources hold SYS_TIME in both Date constructors",
      sorted(where for where, _ in library.get("SYS_TIME", [])) == ["System.Time.Date:New", "System.Time.Date:New"], library.get("SYS_TIME"))
check("the committed row of SYS_TIME names both", sorted(whole.get("SYS_TIME", [])) == ["System.Time.Date:New:", "System.Time.Date:New:l,"],
      whole.get("SYS_TIME"))
check("the library sources and the committed rows name the same methods, for all %d frame-reading traps" % len(whole),
      all(not gen.context_problems(trap, whole[trap], library) for trap in whole) and set(whole) == set(t for t in frames if t in shapes))

rows, notes, errors = gen.build_rows(corpus_with(whole), shapes, ids)
check("a corpus with every context: no error", not errors, errors[:3])

partial = dict(whole, SYS_TIME=["System.Time.Date:New:"])
rows, notes, errors = gen.build_rows(corpus_with(partial), shapes, ids)
check("a corpus without System.Time.Date:New(Bool): an error for SYS_TIME, and only for it",
      len(errors) == 1 and errors[0].startswith("SYS_TIME:") and "System.Time.Date:New" in errors[0], errors[:3])

stray = dict(whole, GMT_TIME=whole["GMT_TIME"] + ["Stray.Class:Method:"])
rows, notes, errors = gen.build_rows(corpus_with(stray), shapes, ids)
check("a context in a method the library sources do not hold the directive in: an error",
      len(errors) == 1 and errors[0].startswith("GMT_TIME:") and "Stray.Class:Method" in errors[0], errors[:3])

# ------------------------------------------------------------------ --check ----
print("--check: the committed header and what it must not accept")
rc, out = run("--check")
check("the committed header passes", rc == 0 and "trap_signatures.h: ok" in out, out)

GETCLASS_TAIL = ', L"System.Base:GetClass:", true },'
with open(HEADER, encoding="utf-8") as f:
    check("the committed row of LOAD_CLS_BY_INST names System.Base:GetClass",
          re.search(r'\{ instructions::LOAD_CLS_BY_INST, [^\n]*' + re.escape(GETCLASS_TAIL) + r'\n', f.read()) is not None)

cases = [
    ("an empty context list (what an empty corpus wrote)", row_edit("LOAD_CLS_BY_INST", GETCLASS_TAIL, ', L"", true },'), "LOAD_CLS_BY_INST"),
    ("no context list at all", row_edit("LOAD_CLS_BY_INST", GETCLASS_TAIL, ', nullptr, true },'), "LOAD_CLS_BY_INST"),
    ("the instance flag dropped", row_edit("LOAD_CLS_BY_INST", GETCLASS_TAIL, ', L"System.Base:GetClass:", false },'), "LOAD_CLS_BY_INST"),
    ("a context on a trap that reads the operand stack alone",
     row_edit("STD_OUT_INT", ', nullptr, false },', ', L"System.Int:Print:", false },'), "STD_OUT_INT"),
    # not empty, and one valid caller short: what a partial corpus wrote
    ("SYS_TIME without Date->New(Bool)",
     row_edit("SYS_TIME", ', L"System.Time.Date:New:|System.Time.Date:New:l,", false },', ', L"System.Time.Date:New:", false },'), "SYS_TIME"),
    ("GMT_TIME in a method the library does not write it in",
     row_edit("GMT_TIME", ', L"System.Time.Date:New:l,", false },', ', L"System.Time.Date:New:l,|Stray.Class:Method:", false },'), "GMT_TIME"),
]


def row_drop(trap):
    return lambda text: re.sub(r'    \{ instructions::%s, [^\n]*\n' % trap, "", text, count=1)


def row_cells(trap, old, new):
    """Replaces the start of one trap's row (its instruction and count)."""
    return lambda text: text.replace('{ instructions::%s, L"%s", %s,' % (trap, trap, old),
                                     '{ instructions::%s, L"%s", %s,' % (trap, trap, new), 1)


# the array-literal copies: written by EmitStaticArray, in no directive --check reads
for trap in sorted(gen.LITERAL_COPIES) + ["CPY_CHAR_STR_ARYS"]:
    cases.append(("the row of %s missing" % trap, row_drop(trap), trap))
cases.append(("CPY_INT_STR_ARY with another count", row_cells("CPY_INT_STR_ARY", "true, 3", "true, 4"), "CPY_INT_STR_ARY"))
cases.append(("CPY_CHAR_STR_ARYS with a fixed count", row_cells("CPY_CHAR_STR_ARYS", "true, -1", "true, 3"), "CPY_CHAR_STR_ARYS"))
cases.append(("CPY_FLOAT_STR_ARY written TRAP", row_cells("CPY_FLOAT_STR_ARY", "true, 3", "false, 3"), "CPY_FLOAT_STR_ARY"))

for name, change, trap in cases:
    changed, problems = check_copy(change)
    check("%s: the fixture differs from the committed header" % name, changed)
    check("%s: reported against %s" % (name, trap), any(p.startswith(trap + ":") for p in problems), problems[:3])

changed, problems = check_copy(lambda text: text)
check("an unchanged copy: no problem (the cases above are the changes, not the copying)", not changed and not problems, problems[:3])

check("the committed header is the one this test started with", digest() == before)

# ------------------------------------------------------------------ report ----
if failures:
    print()
    for item in failures:
        print("FAIL: %s" % item)
    print()
    print("%d case(s) did not behave as stated." % len(failures))
    sys.exit(1)

print()
print("PASS: gen_trap_signatures.py writes no header from an unusable corpus, and --check rejects one that lost its contexts")
