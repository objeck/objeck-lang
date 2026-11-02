#!/usr/bin/env python3
"""
Differential check of the language server's caches against a from-scratch
analysis (issue #1236, docs/INCREMENTAL_ANALYSIS_DESIGN.md).

Two servers answer the same requests over a corpus of real programs:

  reference  OBJECK_LSP_NO_CACHE=1: every request parses, links and analyzes
             afresh, with no program cache, no library cache, and library
             instructions built as obc builds them
  cached     the defaults (optionally with OBJECK_LSP_LIB_CACHE_VERIFY=1, which
             inflates a library again on every cache hit and reports any
             difference)

Each file is opened, asked for its diagnostics, symbols, hovers, a completion and
a definition, and then edited by a randomized edit script (seeded, so a failure
repeats): comment lines, deleted and duplicated lines, stray characters, renamed
identifiers, undo and revert to the original text. After every edit both servers
are asked again. Every answer must be identical, and diagnostics byte-identical
(the published JSON, in order). The report counts the answers that had content,
since agreeing on empty answers proves little. Several clients work on each
server at once, so the process-wide caches are used from several analysis
threads together.

Corpora:
  lsp         the language server's own test fixtures: tools/lsp/tests/*.obs
              standalone, and each ws_* folder as a workspace (build.json)
  regression  programs/regression/*.obs, each in a folder of its own whose
              build.json links what run_regression.sh links
  lib_src     core/compiler/lib_src/*.obs, standalone (the 13 default libraries),
              without the definition request: on lang.obs it never answers and
              stalls the server, on master as well, with or without the caches

Usage:
  python3 validate_cached_analysis.py <bin_dir> [server_dir]
      [--corpus lsp,regression,lib_src] [--edits N] [--seed N] [--clients N]
      [--limit N] [--skip N] [--verify] [--control]

Exit code 0 = every answer agreed, both servers stayed up, the library cache was
used, and verify mode (when on) found no difference; 1 otherwise. Not part of
run_lsp_tests.py: over the whole corpus it takes many minutes.
"""
import argparse
import glob
import json
import os
import queue
import random
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TESTS_DIR)

from run_lsp_tests import LspClient, REPO_ROOT, path_to_uri, result_of  # noqa: E402

REGRESSION_DIR = os.path.join(REPO_ROOT, "programs", "regression")
LIB_SRC_DIR = os.path.join(REPO_ROOT, "core", "compiler", "lib_src")
# where hover, completion and definition answer: a name after '->', a local
# being declared, and a class whose instance is made
CALL = re.compile(r"->([A-Z][A-Za-z0-9_]*)")
DECL = re.compile(r"\b([a-z_][A-Za-z0-9_]*) :=")
NEW = re.compile(r"\b([A-Z][A-Za-z0-9_]*)->New\b")
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
# a file whose questions take longer than this is named as it finishes
SLOW = 30
CALL_HOVERS = 3
DECL_HOVERS = 2
# the libraries run_regression.sh links every test with, less 'collect', which
# names no library file
BASE_LIBS = ["gen_collect", "cipher", "xml", "json"]
EXTRA = re.compile(r"# EXTRA_LIBS:\s*(\S+)")
STRAY = [";", "{", "}", "(", ")", "=", ":", ".", "->", "'", '"', "x", "1", ",", "~"]


class Attached(LspClient):
    """A second connection to a server another client started."""

    def __init__(self, port):
        self.port = port
        self.proc = None
        self.q = queue.Queue()
        self.notifications = []
        self.server_requests = []
        self.rid = 0
        self.sock = socket.create_connection(("127.0.0.1", port), 10)
        self.sock.settimeout(1.0)
        threading.Thread(target=self._reader, daemon=True).start()

    def close(self):
        try:
            self.request("shutdown", timeout=5)
            self.notify("exit")
        finally:
            try:
                self.sock.close()
            except OSError:
                pass


class Server:
    """One server process and N connections to it."""

    def __init__(self, name, env_extra, clients, obr, server_obe, apis_json, bin_dir):
        self.name = name
        self.lines = []
        saved = dict(os.environ)
        os.environ.update(env_extra)
        try:
            first = LspClient(obr, server_obe, apis_json, bin_dir, stderr=subprocess.PIPE)
        finally:
            os.environ.clear()
            os.environ.update(saved)
        threading.Thread(target=self._stderr, args=(first.proc,), daemon=True).start()
        self.clients = [first] + [Attached(first.port) for _ in range(clients - 1)]
        for c in self.clients:
            c.request("initialize", {"processId": os.getpid(), "rootUri": None,
                                     "capabilities": {}}, timeout=90)
            c.notify("initialized", {})

    def _stderr(self, proc):
        for raw in iter(proc.stderr.readline, b""):
            line = raw.replace(b"\x00", b"").decode("utf-8", "replace").strip()
            self.lines.append(line)
            # anything but the timing and cache reports is news: a stack trace, a
            # connection thread that ended
            if line and not line.startswith("[lsp-"):
                print(f"  {self.name} stderr: {line}", flush=True)

    def alive(self):
        return self.clients[0].proc.poll() is None

    def close(self):
        for c in reversed(self.clients):
            try:
                c.close()
            except Exception:
                pass


def diagnostics(c, uri, timeout=180):
    """The diagnostics published for uri: the last of those that arrive together."""
    end = time.time() + timeout
    found = None
    while time.time() < end:
        for n in list(c.notifications):
            if (n.get("method") == "textDocument/publishDiagnostics"
                    and n["params"].get("uri", "").lower() == uri.lower()):
                c.notifications.remove(n)
                found = n["params"].get("diagnostics", [])
        if found is not None:
            # a second publish for the same text can follow the first
            c.drain(0.3)
            for n in list(c.notifications):
                if (n.get("method") == "textDocument/publishDiagnostics"
                        and n["params"].get("uri", "").lower() == uri.lower()):
                    c.notifications.remove(n)
                    found = n["params"].get("diagnostics", [])
            return found
        c.drain(0.1)
    return None


def position(text, index):
    line = text.count("\n", 0, index)
    return {"line": line, "character": index - (text.rfind("\n", 0, index) + 1)}


def regression_workspace(path, text, root):
    """A folder of its own for a regression program, linking what the runner links.

    Opened alone, a test that needs a library outside the 13 defaults fails its
    analysis, and then hover and definition have nothing to say. In a folder whose
    build.json lists the runner's libraries, the analysis succeeds.
    """
    m = EXTRA.search(text)
    libs = list(BASE_LIBS)
    for lib in (m.group(1).split(",") if m else []):
        if lib and lib != "collect" and lib not in libs:
            libs.append(lib)
    folder = os.path.join(root, os.path.splitext(os.path.basename(path))[0])
    os.makedirs(folder, exist_ok=True)
    name = os.path.basename(path)
    with open(os.path.join(folder, name), "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    with open(os.path.join(folder, "build.json"), "w", encoding="utf-8") as fh:
        # an '@group' alias is named as it is
        json.dump({"files": [name], "libs": [l if l.startswith("@") else l + ".obl" for l in libs],
                   "flags": ""}, fh)
    return folder, os.path.join(folder, name)


def corpus_items(names, skip, limit, scratch):
    """(label, folder or None, path, text, ask definition) per file to check."""
    items = []

    def read(path):
        with open(path, encoding="utf-8", errors="replace", newline="") as fh:
            return fh.read()

    for name in names:
        found = []
        if name == "lsp":
            for path in sorted(glob.glob(os.path.join(TESTS_DIR, "*.obs"))):
                found.append((path, None, path, read(path), True))
            for ws in sorted(glob.glob(os.path.join(TESTS_DIR, "ws*"))):
                if not os.path.exists(os.path.join(ws, "build.json")):
                    continue
                for path in sorted(glob.glob(os.path.join(ws, "**", "*.obs"), recursive=True)):
                    found.append((path, ws, path, read(path), True))
        elif name == "regression":
            for path in sorted(glob.glob(os.path.join(REGRESSION_DIR, "*.obs"))):
                text = read(path)
                folder, copy = regression_workspace(path, text, scratch)
                found.append((path, folder, copy, text, True))
        elif name == "lib_src":
            for path in sorted(glob.glob(os.path.join(LIB_SRC_DIR, "*.obs"))):
                # no definition request: on lang.obs, opened alone, it never answers
                # and stalls the server (on master too, with or without the caches)
                found.append((path, None, path, read(path), False))
        else:
            raise SystemExit(f"unknown corpus '{name}'")
        found = found[skip:]
        items += found[:limit] if limit else found
    return items


def edit_script(rng, original, count):
    """count edited texts, each a small step from the one before."""
    texts = []
    text = original
    previous = original
    for _ in range(count):
        lines = text.split("\n")
        kind = rng.choice(["comment", "comment", "delete", "duplicate", "stray", "stray",
                           "drop", "rename", "undo", "revert"])
        before = text
        if kind == "comment":
            at = rng.randrange(len(lines) + 1)
            lines.insert(at, f"# edit {rng.randrange(1000)}")
            text = "\n".join(lines)
        elif kind == "delete" and len(lines) > 1:
            del lines[rng.randrange(len(lines))]
            text = "\n".join(lines)
        elif kind == "duplicate":
            at = rng.randrange(len(lines))
            lines.insert(at, lines[at])
            text = "\n".join(lines)
        elif kind == "stray":
            at = rng.randrange(len(text) + 1)
            text = text[:at] + rng.choice(STRAY) + text[at:]
        elif kind == "drop" and text:
            at = rng.randrange(len(text))
            text = text[:at] + text[at + 1:]
        elif kind == "rename":
            idents = list(IDENT.finditer(text))
            if idents:
                m = rng.choice(idents)
                text = text[:m.end()] + "Z" + text[m.end():]
        elif kind == "undo":
            text = previous
        elif kind == "revert":
            text = original
        previous = before
        texts.append((kind, text))
    return texts


def ask_all(c, uri, text, rng, full, note=lambda what: None, definition=True):
    """This text's answers, as canonical JSON per request.

    full: the fixed questions (symbols, the first calls and locals, completion,
    definition); otherwise two hovers and a completion at random places.
    """
    doc = {"textDocument": {"uri": uri}}
    got = {}

    def ask(key, method, index):
        note(method)
        got[key] = result_of(c.request(method, dict(doc, position=position(text, index)), timeout=180))

    calls = [m.start(1) for m in CALL.finditer(text)]
    if full:
        got["documentSymbol"] = result_of(c.request("textDocument/documentSymbol", doc, timeout=180))
        for i, index in enumerate(calls[:CALL_HOVERS]):
            ask(f"hover call {i}", "textDocument/hover", index + 1)
        for i, m in enumerate(list(DECL.finditer(text))[:DECL_HOVERS]):
            ask(f"hover local {i}", "textDocument/hover", m.start(1) + 1)
        if calls:
            # right after the '->': the members of what is on its left
            ask("completion", "textDocument/completion", calls[0])
        made = NEW.search(text)
        if made and definition:
            ask("definition", "textDocument/definition", made.start(1) + 1)
    else:
        idents = list(IDENT.finditer(text))
        for i in range(2):
            if idents:
                ask(f"hover {i}", "textDocument/hover", rng.choice(idents).start() + 1)
        if calls:
            ask("completion", "textDocument/completion", rng.choice(calls))
    return {k: json.dumps(v, sort_keys=True) for k, v in got.items()}


def session(c, item, edits, seed, note=lambda what: None):
    """Opens the file, asks, edits and asks again; [(step, answers)].

    note(what) is told each step, for the report of a file that takes long.
    """
    label, folder, path, original, definition = item
    uri = path_to_uri(path)
    doc = {"textDocument": {"uri": uri}}
    if folder:
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {
            "added": [{"uri": path_to_uri(folder), "name": os.path.basename(folder)}], "removed": []}})
    c.notifications.clear()
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": original}})
    steps = []
    note("open: diagnostics")
    diags = diagnostics(c, uri)
    # the published JSON as it came, order included
    answers = {"diagnostics": json.dumps(diags) if diags is not None else "<no diagnostics>"}
    answers.update(ask_all(c, uri, original, random.Random(seed), True,
                           lambda what: note("open: " + what), definition))
    steps.append(("open", answers))

    script = edit_script(random.Random(seed), original, edits)
    ask_rng = random.Random(seed + 1)
    for version, (kind, text) in enumerate(script, start=2):
        c.notifications.clear()
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
                                            "contentChanges": [{"text": text}]})
        step = f"edit {version - 1} ({kind})"
        note(step + ": diagnostics")
        diags = diagnostics(c, uri)
        answers = {"diagnostics": json.dumps(diags) if diags is not None else "<no diagnostics>"}
        answers.update(ask_all(c, uri, text, ask_rng, False, lambda what: note(step + ": " + what)))
        steps.append((step, answers))

    c.notify("textDocument/didClose", doc)
    if folder:
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {
            "added": [], "removed": [{"uri": path_to_uri(folder), "name": os.path.basename(folder)}]}})
    return steps


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bin_dir")
    ap.add_argument("server_dir", nargs="?", default=os.path.join(TESTS_DIR, "..", "server"))
    ap.add_argument("--corpus", default="lsp,regression,lib_src")
    ap.add_argument("--edits", type=int, default=6, help="randomized edits per file")
    ap.add_argument("--seed", type=int, default=1236)
    ap.add_argument("--clients", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0, help="files per corpus, 0 for all")
    ap.add_argument("--skip", type=int, default=0, help="files to skip at the start of each corpus")
    ap.add_argument("--verify", action="store_true",
                    help="OBJECK_LSP_LIB_CACHE_VERIFY=1 in the cached server: it inflates every "
                         "cache hit again and serves the fresh bytes on a difference, which hides "
                         "a wrong library from the answers but names it on stderr")
    ap.add_argument("--control", action="store_true",
                    help="negative control: the cached server is sent a different edit script, "
                         "so the run must FAIL; it shows the comparison can tell answers apart")
    args = ap.parse_args()

    bin_dir = os.path.abspath(args.bin_dir)
    server_dir = os.path.abspath(args.server_dir)
    exe = ".exe" if os.name == "nt" else ""
    obr = os.path.join(bin_dir, "obr" + exe)
    server_obe = os.path.join(server_dir, "objeck_lsp.obe")
    apis_json = os.path.join(server_dir, "objk_apis.json")
    for p in (obr, server_obe, apis_json):
        if not os.path.exists(p):
            print(f"Error: missing {p}")
            return 1
    # the JIT pin every editor client sets
    os.environ.setdefault("OBJECK_JIT_THRESHOLD", "999999999")

    scratch = tempfile.mkdtemp(prefix="objk-cached-analysis-")
    items = corpus_items(args.corpus.split(","), args.skip, args.limit, scratch)
    print(f"{len(items)} files, {args.edits} edits each, seed {args.seed}, "
          f"{args.clients} clients per server", flush=True)

    # OBJECK_LSP_TIMING here too: a library cache the switch failed to turn off
    # would then report its loads, and the run fails
    ref = Server("reference", {"OBJECK_LSP_NO_CACHE": "1", "OBJECK_LSP_TIMING": "1"}, args.clients,
                 obr, server_obe, apis_json, bin_dir)
    # OBJECK_LSP_TIMING makes the library cache report its hits without verify mode
    cached_env = {"OBJECK_LSP_LIB_CACHE_VERIFY": "1"} if args.verify else {"OBJECK_LSP_TIMING": "1"}
    cached = Server("cached", cached_env, args.clients, obr, server_obe, apis_json, bin_dir)

    work = queue.Queue()
    for n, item in enumerate(items):
        work.put((n, item))
    mismatches = []
    done = [0]
    steps_checked = [0]
    # answers with content, per kind: agreement on empty answers proves little
    substantive = {}
    lock = threading.Lock()
    busy = {}

    def worker(i):
        while True:
            try:
                n, item = work.get_nowait()
            except queue.Empty:
                return
            label = os.path.relpath(item[0], REPO_ROOT)
            seed = args.seed * 100003 + n
            began = time.time()
            busy[i] = [label, began, {}]

            # Both servers at once. One after the other, a connection sat idle while
            # the other server worked, and the VM gives an accepted socket a 30 s
            # receive timeout: the server read the idle connection as closed and
            # dropped it, and the client then waited for answers that never came.
            results = {}
            took = {}

            def run(name, server, run_seed):
                start = time.time()
                where = busy[i][2]
                results[name] = session(server.clients[i], item, args.edits, run_seed,
                                        lambda what: where.__setitem__(name, what))
                took[name] = time.time() - start
            # --control: the cached server gets another edit script
            runs = [threading.Thread(target=run, args=("reference", ref, seed)),
                    threading.Thread(target=run, args=("cached", cached, seed + 7 if args.control else seed))]
            for t in runs:
                t.start()
            for t in runs:
                t.join()
            want, have = results["reference"], results["cached"]
            busy.pop(i, None)
            if time.time() - began > SLOW:
                print(f"  slow: {label} reference {took['reference']:.0f}s, "
                      f"cached {took['cached']:.0f}s", flush=True)
            with lock:
                done[0] += 1
                for (step, w), (_, h) in zip(want, have):
                    steps_checked[0] += 1
                    for key, value in h.items():
                        kind = key.rstrip("0123456789 ")
                        total, full = substantive.get(kind, (0, 0))
                        substantive[kind] = (total + 1, full + (value not in ("null", "[]", '""')))
                    for key in sorted(set(w) | set(h)):
                        if w.get(key) != h.get(key):
                            mismatches.append((label, seed, step, key,
                                               w.get(key, "<absent>")[:300], h.get(key, "<absent>")[:300]))
                if len(want) != len(have):
                    mismatches.append((label, seed, "steps", "count", str(len(want)), str(len(have))))
                if done[0] % 25 == 0:
                    print(f"  {done[0]}/{len(items)} files, {len(mismatches)} mismatches", flush=True)

    finished = threading.Event()

    def watchdog():
        while not finished.wait(SLOW):
            for i, (label, began, where) in list(busy.items()):
                if time.time() - began > SLOW:
                    steps = ", ".join(f"{name}: {step}" for name, step in sorted(where.items()))
                    print(f"  still on: {label} for {time.time() - began:.0f}s (client {i}; {steps}); "
                          f"reference alive {ref.alive()}, cached alive {cached.alive()}", flush=True)
    threading.Thread(target=watchdog, daemon=True).start()

    start = time.time()
    threads = [threading.Thread(target=worker, args=(i,)) for i in range(args.clients)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - start
    finished.set()

    alive = ref.alive() and cached.alive()
    ref.close()
    cached.close()
    shutil.rmtree(scratch, ignore_errors=True)
    time.sleep(0.5)

    hits = sum(1 for l in cached.lines if "[lsp-lib-cache] hit" in l)
    misses = sum(1 for l in cached.lines if "[lsp-lib-cache] miss" in l)
    changed = sum(1 for l in cached.lines if "[lsp-lib-cache] changed" in l)
    verify_bad = [l for l in cached.lines if "MISMATCH" in l]
    ref_cache = sum(1 for l in ref.lines if "[lsp-lib-cache]" in l)

    print(f"\n{done[0]} files, {steps_checked[0]} texts compared, in {elapsed:.0f}s; "
          f"both servers alive to the end: {alive}")
    print(f"library cache: {hits} hits, {misses} misses, {changed} changed; "
          f"verify-mode differences: {len(verify_bad)}; reference server cache lines: {ref_cache}")
    print("answers with content: " + ", ".join(
        f"{kind} {full}/{total}" for kind, (total, full) in sorted(substantive.items())))
    print(f"answers that differ between the reference and the cached server: {len(mismatches)}")
    for label, seed, step, key, want, have in mismatches[:20]:
        print(f"  {label} (seed {seed}) {step} {key}\n    reference: {want}\n    cached:    {have}")
    for l in verify_bad[:10]:
        print("  " + l)
    if not alive:
        for server in (ref, cached):
            print(f"  {server.name} stderr, last lines:")
            for l in [l for l in server.lines if "[lsp-" not in l][-15:]:
                print("    " + l)

    ok = (alive and done[0] == len(items) and not mismatches and not verify_bad
          and hits > 0 and ref_cache == 0)
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
