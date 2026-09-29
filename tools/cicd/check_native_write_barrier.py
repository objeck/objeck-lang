#!/usr/bin/env python3
"""A native library that stores a RECEIVED object must run the write barrier.

Usage: check_native_write_barrier.py [repo_root]

Objeck's collector is generational. Objects a native library allocates go
straight to the old generation (`MemoryManager::AllocateObjectNative`), where
nothing moves them, so storing one anywhere is safe without a barrier. An object
the library *received* -- an argument, an element of a received array, or a
callback's result -- may be young. Storing one into a container the library
allocated is an old-to-young reference, and a minor collection neither recurses
into an old object nor repairs a reference the barrier did not record. The next
promotion moves the object and leaves the stored pointer dangling (#864).

`APITools_SetObjectValue` barriers on its own, so returning a value through the
argument array is covered. A direct `target[i] = value` store is not, and that is
what this checks.

## Why a checker and not just the rule

`docs/native_interface.md` forbade this store outright. `core/lib/onnx` did it
anyway at three sites, unbarriered, for as long as those entry points have
existed. Nobody noticed because it never bit -- callers keep the labels array
alive, so the missing remembered-set entry never mattered. A prohibition nothing
checks is not a guard, which is the whole reason this file exists.

## Why it refuses to report a clean tree it cannot verify

This is a textual scan with transitive provenance. It sees `target[i] = value`
and traces `value` back through local assignments; it does not see a store made
inside a helper that takes the received object as a parameter.

A scan like that fails silently. The first version of the audit behind #864
reported zero everywhere, *including* the three ONNX sites already confirmed by
hand, because its pattern for "local bound from a getter" required a comma after
the argument and `APITools_GetArray(labels_array)` takes one. A clean zero from a
scanner that cannot see a known positive is indistinguishable from a clean tree.

So those three sites are wired in below as a positive control: this exits
non-zero unless it FINDS all three and each one is barriered. If a refactor moves
them the control fails loudly and someone has to re-point it -- which is the
intended outcome, because at that moment nobody knows whether the scan still
works.

Limits, stated rather than papered over:
- a store laundered through two levels of helper is not seen
- provenance is textual and file-wide, not a parse
- the control proves the scan sees these three shapes, not every possible shape
"""

import os
import re
import sys

# Libraries that marshal Objeck objects. The others (crypto, lame, zlib,
# openssl) hand back primitives and allocate nothing that can hold a reference.
LIB_DIRS = [
    os.path.join("core", "lib", name)
    for name in ("onnx", "diags", "matrix", "odbc", "opencv", "sdl")
]
VM_FILES = [os.path.join("core", "vm", "lib_api.h")]

# Vendored third-party trees carry their own copies of everything and are not
# ours to edit.
SKIP_DIRS = ("vendor", "third_party", "external", "build", "deps")
SOURCE_EXT = (".cpp", ".h", ".hpp", ".cc")

# A value is OLD -- and so never needs a barrier -- when it came from one of the
# library-facing allocators, all of which route to AllocateObjectNative or
# AllocateArray.
ALLOCATORS = re.compile(
    r"APITools_(?:CreateObject|CreateStringObject|Make\w*Array|CreateNativeObject)\b"
    r"|\bcontext\.alloc_managed_(?:obj|array)\b"
)

# A received OBJECT may be young: AllocateObject uses the nursery, so anything the
# caller built with `->New()` can still be there when it is handed over.
RECEIVED_OBJ = re.compile(r"APITools_Get(?:ObjectValue|StringValue)\b")

# A received ARRAY is old whatever the caller did with it -- AllocateArray never
# uses the nursery, so every Objeck array is old-generation from birth. Storing
# one needs no barrier. Reading an ELEMENT out of one does: the elements are
# objects, and those can be young. That is the shape all three ONNX sites have.
RECEIVED_ARY = re.compile(
    r"APITools_Get(?:Array|ArrayAddress|StringArray|ByteArray|CharArray)\b")

# Deliberately absent: APITools_GetIntValue and GetFloatValue. They return
# primitives, so no store of one can ever dangle. Including them cost one false
# positive with a confident-looking message -- `pixel_obj[2] = (size_t)height` in
# sdl.cpp, where `height` is an int out of SDL_QueryTexture that shares its name
# with a GetIntValue result elsewhere in the same file.

# `name = <expr>;` or `TYPE* name = <expr>;`
ASSIGN = re.compile(r"^\s*(?:[\w:<>,\s\*&]+?[\s\*&])?(\w+)\s*=\s*([^;]+);")
# `target[expr] = value;` -- the store shape this is about
STORE = re.compile(r"^\s*(\w+)\s*\[([^\]]+)\]\s*=\s*([^;]+);")
BARRIER = re.compile(r"APITools_WriteBarrier\s*\(\s*\w+\s*,\s*(\w+)\s*\)")

# A stored value is a REFERENCE only in these shapes: a bare local, or one
# element read out of one, with at most a size_t cast. Anything else -- `box.x`,
# `bundle->GetName()`, `ResultType::TYPE_NAMESPACE`, `a + b`, a call -- is an
# integer, a double or a C++ object, none of which the collector traces.
#
# Matching the shape rather than looking for received names anywhere in the
# expression is the difference between 3 findings and 40. Every one of the extra
# 37 was an integer store into an object slot: `class_rect_obj[0] = box.x` got
# reported because `x` appears as a word in it.
# Group 2 is the subscript when there is one, because `a` and `a[i]` differ: a
# received array is old, an element read out of it may be young.
VALUE_REF = re.compile(r"^\s*(?:\(\s*size_t\s*\)\s*)?(\w+)\s*(\[[^\]]+\])?\s*$")

# How far after a store a barrier still counts as belonging to it. Three lines
# covers a closing brace plus a comment; a barrier further away than that is not
# obviously about this store and should be moved next to it.
BARRIER_WINDOW = 4

# The positive control: sites known to have this exact shape, kept here so a scan
# that stops seeing them fails instead of reporting a clean tree. Each must be
# FOUND and each must be BARRIERED.
CONTROL = [
    (os.path.join("core", "lib", "onnx", "eq", "common.h"), "class_result_obj", "labels_objs[...]"),
    (os.path.join("core", "lib", "onnx", "eq", "common.h"), "resnet_result_obj", "labels_objs[...]"),
    (os.path.join("core", "lib", "onnx", "eq", "common.h"), "openpose_class_obj", "labels_objs[...]"),
]


def source_files(root):
    """Every non-vendored source file under the libraries that marshal objects."""
    found = []
    for rel in LIB_DIRS:
        base = os.path.join(root, rel)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_DIRS]
            for name in sorted(filenames):
                if name.endswith(SOURCE_EXT):
                    found.append(os.path.join(dirpath, name))
    for rel in VM_FILES:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            found.append(path)
    return found


def classify(lines):
    """Map each local name to what it holds, file-wide.

    One of 'old' (we allocated it, never moves), 'young' (received object, may
    move), 'received_ary' (received array: old itself, elements may be young), or
    absent (a primitive, a C++ object, or something unrecognised).

    File-wide rather than per-function on purpose: a name reused for an allocated
    value in one function and a received one in another comes out 'young', which
    asks for a barrier that is merely unnecessary. The other way round would hide
    a real store, and a spurious barrier costs one branch.
    """
    kind = {}
    for line in lines:
        match = ASSIGN.match(line)
        if not match:
            continue
        name, value = match.group(1), match.group(2)
        if ALLOCATORS.search(value):
            if kind.get(name) != "young":
                kind[name] = "old"
        elif RECEIVED_OBJ.search(value):
            kind[name] = "young"
        elif RECEIVED_ARY.search(value):
            if kind.get(name) != "young":
                kind[name] = "received_ary"
        else:
            # one transitive hop, through the reference shape only:
            # `labels_objs = APITools_GetArray(labels_array)` then
            # `label = labels_objs[i]`. Requiring the shape is what keeps
            # `const double x = box.x;` from making `x` look like an object.
            inner = VALUE_REF.match(value)
            if not inner:
                continue
            source, subscripted = inner.group(1), inner.group(2) is not None
            if kind.get(source) == "young":
                kind[name] = "young"
            elif kind.get(source) == "received_ary" and subscripted:
                kind[name] = "young"       # an element of a received array
    return kind


def barriered(lines, index, target):
    """Is there a barrier for 'target' within the window after line 'index'?"""
    for line in lines[index + 1:index + 1 + BARRIER_WINDOW]:
        for match in BARRIER.finditer(line):
            if match.group(1) == target:
                return True
    return False


def scan_file(path, root):
    """Received-into-allocated stores as (rel, lineno, target, source, barriered)."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")

    kind = classify(lines)
    hits = []
    for index, line in enumerate(lines):
        match = STORE.match(line)
        if not match:
            continue
        target, value = match.group(1), match.group(3)
        if kind.get(target) != "old":
            continue                       # container is not one we allocated
        shape = VALUE_REF.match(value)
        if not shape:
            continue                       # not a reference store at all
        source, subscripted = shape.group(1), shape.group(2) is not None
        young = kind.get(source) == "young" or (
            kind.get(source) == "received_ary" and subscripted)
        if not young:
            continue                       # old, or not a reference
        hits.append((os.path.relpath(path, root), index + 1, target,
                     source + ("[...]" if subscripted else ""),
                     barriered(lines, index, target)))
    return hits


def check_vm_wiring(root):
    """Every VMContext the VM builds must carry the barrier. Messages, or [].

    This is the other end of the same invariant, and it has a silent failure mode
    that the library-side scan cannot see. `APITools_WriteBarrier` no-ops when
    `context.write_barrier` is null -- deliberately, so a library built against
    this header still loads on a VM that predates the field. The cost is that a
    VM which simply forgot to set it turns every barrier in every library into a
    no-op, with nothing to show for it: no crash, no message, and a static scan of
    the libraries still reporting all-clear.

    There are four construction sites today. A fifth added without the assignment
    is the regression this catches.
    """
    path = os.path.join(root, "core", "vm", "interpreter.cpp")
    if not os.path.isfile(path):
        return ["cannot find core/vm/interpreter.cpp, so the VM-side wiring is unchecked"]

    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")

    decls = [i for i, line in enumerate(lines)
             if re.match(r"^\s*VMContext\s+\w+\s*(\{\s*\})?\s*;", line)]

    errors = []
    found = len(decls)
    for position, index in enumerate(decls):
        # The assignments follow immediately, so 20 lines is generous for seven
        # fields -- but the window must also stop at the NEXT construction, or one
        # site's assignment covers for the site above it. Two adjacent blocks,
        # the second wired and the first not, read as clean otherwise.
        limit = index + 20
        if position + 1 < len(decls):
            limit = min(limit, decls[position + 1])
        window = "\n".join(lines[index:limit])
        if "write_barrier" not in window:
            errors.append("core/vm/interpreter.cpp:%d: this VMContext never sets "
                          "write_barrier, so every barrier in every native library "
                          "silently does nothing" % (index + 1))
    if not found:
        errors.append("found no VMContext construction in core/vm/interpreter.cpp -- "
                      "the check for it no longer works and must be re-pointed")
    return errors


def check_control(all_hits):
    """Problems with the positive control, as a list of messages."""
    errors = []
    for rel, target, source in CONTROL:
        want = rel.replace("\\", "/")
        matching = [h for h in all_hits
                    if h[0].replace("\\", "/") == want and h[2] == target and h[3] == source]
        if not matching:
            errors.append("the scan no longer finds the known store %s[i] = %s in %s"
                          % (target, source, rel))
            continue
        for hit in matching:
            if not hit[4]:
                errors.append("%s:%d: %s[i] = %s has no barrier"
                              % (hit[0], hit[1], target, source))
    return errors


def main():
    root = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    files = source_files(root)
    if not files:
        print("FAIL: no native library sources found under %s" % root)
        print("      this check cannot pass by finding nothing to look at")
        return 1

    wiring_errors = check_vm_wiring(root)
    if wiring_errors:
        print("FAIL: the VM does not hand the write barrier to every native call, so the")
        print("      barriers the libraries do run would have nothing to run.")
        for message in wiring_errors:
            print("  %s" % message)
        return 1

    all_hits = []
    for path in files:
        all_hits.extend(scan_file(path, root))

    control_errors = check_control(all_hits)
    if control_errors:
        print("FAIL: the positive control did not hold, so a clean result would mean nothing.")
        for message in control_errors:
            print("  %s" % message)
        print()
        print("  Either a barrier was removed -- put it back -- or these sites moved and the")
        print("  CONTROL table in this script has to be re-pointed at stores that still exist.")
        print("  Do not delete the control: a scan with nothing known-visible cannot fail.")
        return 1

    unbarriered = [h for h in all_hits if not h[4]]
    if unbarriered:
        print("FAIL: a received object is stored into a natively-allocated container with no")
        print("      write barrier. A minor GC will move it and leave the pointer dangling.")
        print()
        for rel, lineno, target, source, _ in unbarriered:
            print("  %s:%d" % (rel, lineno))
            print("      %s[...] = %s" % (target, source))
            print("      add: APITools_WriteBarrier(context, %s);" % target)
        print()
        print("  See 'Arrays are born old, objects are born young' in docs/native_interface.md.")
        return 1

    print("PASS: %d file(s) scanned; %d received-object store(s), all barriered "
          "(control: %d/%d); the VM wires the barrier into every native call."
          % (len(files), len(all_hits), len(CONTROL), len(CONTROL)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
