#!/usr/bin/env python3
"""check_api_index.py must actually detect a stale index.

A guard is worth exactly what its failing case is worth. This builds throwaway
trees for each way the index can be wrong and requires the check to reject every
one of them, then requires it to accept a correct tree -- because a check that
rejects everything is no more useful than one that accepts everything.

Nothing built, no toolchain, no network: fixtures are written to a temporary
directory and the checker is run against them with --root.

Run: python3 tools/cicd/test_check_api_index.py
"""

import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKER = os.path.join(HERE, "check_api_index.py")

SERVER_REL = os.path.join("tools", "lsp", "server", "objk_apis.json")
VSCODE_REL = os.path.join("tools", "lsp", "clients", "vscode", "server", "objk_apis.json")

# The control classes check_api_index.py requires, plus enough else to look real.
BASE_CLASSES = [
    "API.Onnx.Session",
    "API.Onnx.RunResult",
    "System.ML.FoldSplit",
    "Collection.Vector",
    "API.Inference.Client",
    "Data.JSON.JsonElement",
]

failures = []


NL = "\n"


def indent(text):
    """A checker's own output, set in from the report around it."""
    return NL.join("    " + line for line in text.strip().splitlines())


def index_for(classes):
    """A well-formed index for `classes`.

    class-bundle-names values are LISTS, grouped by short name -- the shape since
    #1052. A short name defined in two bundles carries both candidates, and the
    checker requires the candidate total to equal the class-details count, so a
    fixture that groups wrongly fails for the right reason.
    """
    grouped = {}
    for name in classes:
        bundle, short = name.rsplit(".", 1)
        grouped.setdefault(short, []).append(
            {"bundle": bundle, "file": name.split(".")[1].lower() + ".obl"})
    return {
        "class-details": {name: {"description": name} for name in classes},
        "class-bundle-names": grouped,
    }


def write_tree(root, server_index, vscode_index=None):
    """A repo-shaped directory holding the two index copies."""
    server = os.path.join(root, SERVER_REL)
    vscode = os.path.join(root, VSCODE_REL)
    os.makedirs(os.path.dirname(server))
    os.makedirs(os.path.dirname(vscode))

    text = json.dumps(server_index, indent=1)
    io.open(server, "w", encoding="utf-8").write(text)
    if vscode_index is None:
        io.open(vscode, "w", encoding="utf-8").write(text)
    else:
        io.open(vscode, "w", encoding="utf-8").write(json.dumps(vscode_index, indent=1))
    return server


def run(root, regenerated=None):
    argv = [sys.executable, CHECKER, "--root", root]
    if regenerated:
        argv += ["--regenerated", regenerated]
    done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return done.returncode, done.stdout.decode("utf-8", "replace")


def case(label, expect_pass, server_index, vscode_index=None, regenerated_classes=None,
         must_mention=()):
    root = tempfile.mkdtemp(prefix="apiindex_")
    try:
        write_tree(root, server_index, vscode_index)

        regen_path = None
        if regenerated_classes is not None:
            regen_path = os.path.join(root, "fresh.json")
            io.open(regen_path, "w", encoding="utf-8").write(
                json.dumps(index_for(regenerated_classes), indent=1))

        code, output = run(root, regen_path)
        passed = (code == 0)

        if passed != expect_pass:
            failures.append("%s: expected %s, got exit %d\n%s"
                            % (label, "pass" if expect_pass else "failure", code,
                               indent(output)))
            return

        for needle in must_mention:
            if needle not in output:
                failures.append("%s: rejected it, but the message never mentions %r.\n"
                                "A failure nobody can act on is barely better than no check.\n%s"
                                % (label, needle,
                                   indent(output)))
                return

        print("  ok    %s" % label)
    finally:
        shutil.rmtree(root, ignore_errors=True)


# ---------------------------------------------------------------- accepts -----
# A correct tree, so the suite is not just proving the checker rejects things.
case("a current index is accepted",
     True, index_for(BASE_CLASSES), regenerated_classes=BASE_CLASSES)

case("without --regenerated it still accepts matching copies",
     True, index_for(BASE_CLASSES))

# ---------------------------------------------------------------- rejects -----
# The real failure: the libraries gained a class and the index was not rebuilt.
# This is the 2026-10-07 state, where API.Onnx.Session existed and was unindexed.
case("a library ahead of the index is rejected, naming the class and its .obl",
     False, index_for(BASE_CLASSES),
     regenerated_classes=BASE_CLASSES + ["API.Inference.Tensor"],
     must_mention=("API.Inference.Tensor", "inference.obl", "gen_json"))

# The attribution must come from the BUNDLE, not the short name. class-bundle-names
# is keyed by short name and cannot hold two classes that share one, so the obvious
# lookup reported API.Inference.Tensor against onnx.obl, API.Inference.Client
# against json_rpc.obl and API.Inference.EndPoint against openai.obl -- sending a
# maintainer to three libraries that are not the one to regenerate. Wrong
# attribution is worse than none, so the fixture below contains a REAL collision:
# two bundles each defining Tensor, with API.Onnx written last so a short-name
# lookup resolves Tensor to it.
root = tempfile.mkdtemp(prefix="apiindex_attr_")
try:
    both = BASE_CLASSES + ["API.Inference.Tensor", "API.Onnx.Tensor"]
    write_tree(root, index_for(BASE_CLASSES))
    saved = os.path.join(root, "committed.json")
    io.open(saved, "w", encoding="utf-8").write(json.dumps(index_for(BASE_CLASSES), indent=1))
    fresh = os.path.join(root, "fresh.json")
    io.open(fresh, "w", encoding="utf-8").write(json.dumps(index_for(both), indent=1))

    # the fixture really does hold two definitions of one short name, or this
    # proves nothing. Before #1052 one of them was dropped here.
    both_entries = json.load(io.open(fresh, encoding="utf-8"))["class-bundle-names"]["Tensor"]
    if not isinstance(both_entries, list) or len(both_entries) != 2:
        failures.append("fixture is not exercising the collision: Tensor holds %r"
                        % (both_entries,))
    else:
        argv = [sys.executable, CHECKER, "--committed", saved, "--packaged", saved,
                "--regenerated", fresh]
        done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = done.stdout.decode("utf-8", "replace")

        wanted = {"API.Inference.Tensor": "inference.obl", "API.Onnx.Tensor": "onnx.obl"}
        got = {}
        for line in output.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] in wanted:
                got[parts[0]] = parts[1]

        if done.returncode == 0:
            failures.append("a stale index with a colliding short name was accepted")
        elif got != wanted:
            failures.append("each class must be attributed to ITS OWN library."
                            "%s    expected %s%s    got      %s%s%s"
                            % (NL, wanted, NL, got, NL, indent(output)))
        else:
            print("  ok    a colliding short name is still attributed to its own library")
finally:
    shutil.rmtree(root, ignore_errors=True)

case("a class the libraries no longer define is rejected",
     False, index_for(BASE_CLASSES + ["API.Onnx.Removed"]),
     regenerated_classes=BASE_CLASSES,
     must_mention=("API.Onnx.Removed",))

# The .vsix copy is the index users actually get, and it shipped older than the
# server's for as long as gen_json wrote only one of them.
case("a packaged copy out of step with the server's is rejected",
     False, index_for(BASE_CLASSES), vscode_index=index_for(BASE_CLASSES[:-1]),
     must_mention=(".vsix",))

# The positive control. Without it, a check pointed at a truncated or wrong file
# reports a clean index -- indistinguishable from a clean tree.
case("an index missing the control classes is rejected, not reported clean",
     False, index_for(["Collection.Vector"]),
     must_mention=("API.Onnx.Session",))

case("an empty index is rejected",
     False, {"class-details": {}, "class-bundle-names": {}})

case("an index with no class-details at all is rejected",
     False, {"class-bundle-names": {}})

# ---------------------------------------------- the #1052 shape invariant -----
# The pre-#1052 index held one entry per short name rather than a list, so a name
# defined in several bundles kept only whichever the generator emitted last. That
# shape must be rejected as stale, not read as current.
old_shape = index_for(BASE_CLASSES)
old_shape["class-bundle-names"] = {
    k: v[0] for k, v in old_shape["class-bundle-names"].items()}
case("the pre-#1052 single-entry shape is rejected",
     False, old_shape, must_mention=("1052", "gen_json"))

# And a list-shaped index that nonetheless dropped a candidate: the totals no
# longer agree, which is the invariant doing the work rather than the shape check.
dropped = index_for(BASE_CLASSES + ["API.Onnx.Client"])
dropped["class-bundle-names"]["Client"] = dropped["class-bundle-names"]["Client"][:1]
case("a list that lost a candidate is rejected on the count",
     False, dropped, must_mention=("candidate",))

# ------------------------------------------------- the self-comparison trap ---
# gen_json.sh overwrites the tracked copies in place. A CI step that regenerates
# and then points --regenerated at the tracked path is comparing the fresh index
# against itself, which passes no matter how stale the committed one was. The
# --committed flag exists so that cannot happen silently; this proves the flag is
# wired through and not quietly ignored.
root = tempfile.mkdtemp(prefix="apiindex_trap_")
try:
    # the tree holds a FRESH index (as it would after gen_json.sh ran)...
    fresh_classes = BASE_CLASSES + ["API.Inference.Tensor"]
    write_tree(root, index_for(fresh_classes))
    # ...while the saved committed pair is the older one
    saved = os.path.join(root, "committed.json")
    io.open(saved, "w", encoding="utf-8").write(json.dumps(index_for(BASE_CLASSES), indent=1))

    argv = [sys.executable, CHECKER,
            "--committed", saved, "--packaged", saved,
            "--regenerated", os.path.join(root, SERVER_REL)]
    done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = done.stdout.decode("utf-8", "replace")

    if done.returncode == 0:
        failures.append("--committed is ignored: the checker compared the fresh index "
                        "against itself and passed, so every CI run would pass.\n"
                        + indent(output))
    elif "API.Inference.Tensor" not in output:
        failures.append("rejected it, but without naming the missing class:\n"
                        + indent(output))
    else:
        print("  ok    --committed is honoured, so a regenerated tree is not compared to itself")
finally:
    shutil.rmtree(root, ignore_errors=True)

# ------------------------------------------------------------------ report ----
if failures:
    print()
    for item in failures:
        print("FAIL: %s" % item)
    print()
    print("%d of the checker's own cases did not behave as stated." % len(failures))
    sys.exit(1)

print()
print("PASS: check_api_index.py accepts a current index and rejects every way it can go stale")
