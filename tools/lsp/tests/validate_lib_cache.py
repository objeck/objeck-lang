#!/usr/bin/env python3
"""
Differential check of the language server's caches (issue #1236, phase 1).

Two servers answer the same requests over a corpus of real programs:

  reference  OBJECK_LSP_NO_CACHE=1: every request parses, links and analyzes
             afresh, with no program cache and no library cache
  cached     the defaults, plus OBJECK_LSP_LIB_CACHE_VERIFY=1, which inflates a
             library again on every cache hit and reports any difference

Each regression program is opened in a workspace folder of its own whose
build.json links what run_regression.sh links; each library source is opened as
a standalone document, which links the 13 default libraries. Each is asked for
its diagnostics, symbols, hovers on calls and locals, a completion and a
definition, and every answer must be identical between the two servers. The
report counts the answers that had content, since agreeing on empty answers
proves little. Several clients work on each server at once, so the process-wide
library cache is used from several analysis threads together.

Verify mode serves the freshly inflated bytes when it finds a difference, which
hides a wrong library from the answers; --no-verify leaves it off, so that a
wrong library can only show up as a different answer.

Usage:
  python3 validate_lib_cache.py <bin_dir> [server_dir]
      [--clients N] [--limit N] [--corpus regression,lib_src] [--no-verify]

Exit code 0 = every answer agreed, the cache was used, and verify mode found no
difference; 1 otherwise. Not part of run_lsp_tests.py: over the whole corpus it
takes several minutes.
"""
import argparse
import glob
import json
import os
import queue
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

CORPORA = {
    "regression": os.path.join(REPO_ROOT, "programs", "regression", "*.obs"),
    "lib_src": os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "*.obs"),
}
# where hover, completion and definition answer: a name after '->', a local
# being declared, and a class whose instance is made
CALL = re.compile(r"->([A-Z][A-Za-z0-9_]*)")
DECL = re.compile(r"\b([a-z_][A-Za-z0-9_]*) :=")
NEW = re.compile(r"\b([A-Z][A-Za-z0-9_]*)->New\b")
CALL_HOVERS = 3
DECL_HOVERS = 2
# the libraries run_regression.sh links every test with, less 'collect', which
# names no library file
BASE_LIBS = ["gen_collect", "cipher", "xml", "json"]
EXTRA = re.compile(r"# EXTRA_LIBS:\s*(\S+)")


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
            self.lines.append(raw.replace(b"\x00", b"").decode("utf-8", "replace").strip())

    def alive(self):
        return self.clients[0].proc.poll() is None

    def close(self):
        for c in reversed(self.clients):
            try:
                c.close()
            except Exception:
                pass


def diagnostics(c, uri, timeout=120):
    end = time.time() + timeout
    while time.time() < end:
        for n in c.notifications:
            if (n.get("method") == "textDocument/publishDiagnostics"
                    and n["params"].get("uri", "").lower() == uri.lower()):
                c.notifications.remove(n)
                return n["params"].get("diagnostics", [])
        c.drain(0.1)
    return None


def position(text, index):
    line = text.count("\n", 0, index)
    return {"line": line, "character": index - (text.rfind("\n", 0, index) + 1)}


def workspace_for(path, text, root):
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
        json.dump({"files": [name], "libs": [l + ".obl" for l in libs], "flags": ""}, fh)
    return folder, os.path.join(folder, name)


def answers(c, path, text, folder=None):
    """Everything this file is asked, as canonical JSON per request."""
    uri = path_to_uri(path)
    doc = {"textDocument": {"uri": uri}}
    if folder:
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {
            "added": [{"uri": path_to_uri(folder), "name": os.path.basename(folder)}], "removed": []}})
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    got = {}
    diags = diagnostics(c, uri)
    got["diagnostics"] = sorted(json.dumps(d, sort_keys=True) for d in diags) \
        if diags is not None else "<no diagnostics>"
    got["documentSymbol"] = result_of(c.request("textDocument/documentSymbol", doc, timeout=120))

    def ask(key, method, index):
        got[key] = result_of(c.request(method, dict(doc, position=position(text, index)), timeout=120))

    calls = [m.start(1) for m in CALL.finditer(text)]
    for i, index in enumerate(calls[:CALL_HOVERS]):
        ask(f"hover call {i}", "textDocument/hover", index + 1)
    for i, m in enumerate(list(DECL.finditer(text))[:DECL_HOVERS]):
        ask(f"hover local {i}", "textDocument/hover", m.start(1) + 1)
    if calls:
        # right after the '->': the members of what is on its left
        ask("completion", "textDocument/completion", calls[0])
    made = NEW.search(text)
    if made:
        ask("definition", "textDocument/definition", made.start(1) + 1)

    c.notify("textDocument/didClose", doc)
    if folder:
        c.notify("workspace/didChangeWorkspaceFolders", {"event": {
            "added": [], "removed": [{"uri": path_to_uri(folder), "name": os.path.basename(folder)}]}})
    return {k: json.dumps(v, sort_keys=True) for k, v in got.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bin_dir")
    ap.add_argument("server_dir", nargs="?", default=os.path.join(TESTS_DIR, "..", "server"))
    ap.add_argument("--clients", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0, help="files per corpus, 0 for all")
    ap.add_argument("--corpus", default="regression,lib_src")
    ap.add_argument("--no-verify", action="store_true",
                    help="leave verify mode off: a wrong library can then only show in the answers")
    args = ap.parse_args()

    bin_dir = os.path.abspath(args.bin_dir)
    server_dir = os.path.abspath(args.server_dir)
    exe = ".exe" if os.name == "nt" else ""
    obr = os.path.join(bin_dir, "obr" + exe)
    server_obe = os.path.join(server_dir, "objeck_lsp.obe")
    apis_json = os.path.join(server_dir, "objk_apis.json")
    os.environ.setdefault("OBJECK_JIT_THRESHOLD", "999999999")

    files = []
    for name in args.corpus.split(","):
        found = sorted(glob.glob(CORPORA[name]))
        files += found[:args.limit] if args.limit else found
    print(f"{len(files)} files, {args.clients} clients per server", flush=True)

    ref = Server("reference", {"OBJECK_LSP_NO_CACHE": "1"}, args.clients,
                 obr, server_obe, apis_json, bin_dir)
    # OBJECK_LSP_TIMING makes the cache report its hits without verify mode
    cached_env = {"OBJECK_LSP_TIMING": "1"} if args.no_verify else {"OBJECK_LSP_LIB_CACHE_VERIFY": "1"}
    cached = Server("cached", cached_env, args.clients,
                    obr, server_obe, apis_json, bin_dir)

    scratch = tempfile.mkdtemp(prefix="objk-libcache-")
    work = queue.Queue()
    for f in files:
        work.put(f)
    mismatches = []
    done = [0]
    # answers with content, per kind: agreement on empty answers proves little
    substantive = {}
    lock = threading.Lock()

    def worker(i):
        while True:
            try:
                path = work.get_nowait()
            except queue.Empty:
                return
            with open(path, encoding="utf-8", errors="replace", newline="") as fh:
                text = fh.read()
            source = path
            folder = None
            if os.path.dirname(path) == os.path.dirname(CORPORA["regression"]):
                folder, path = workspace_for(path, text, scratch)
            want = answers(ref.clients[i], path, text, folder)
            have = answers(cached.clients[i], path, text, folder)
            with lock:
                done[0] += 1
                for key, value in have.items():
                    kind = key.rstrip("0123456789 ")
                    total, full = substantive.get(kind, (0, 0))
                    substantive[kind] = (total + 1, full + (value not in ("null", "[]", '""')))
                for key in sorted(set(want) | set(have)):
                    if want.get(key) != have.get(key):
                        mismatches.append((os.path.relpath(source, REPO_ROOT), key,
                                           want.get(key, "<absent>")[:200], have.get(key, "<absent>")[:200]))
                if done[0] % 50 == 0:
                    print(f"  {done[0]}/{len(files)} files, {len(mismatches)} mismatches", flush=True)

    start = time.time()
    threads = [threading.Thread(target=worker, args=(i,)) for i in range(args.clients)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - start

    alive = ref.alive() and cached.alive()
    ref.close()
    cached.close()
    shutil.rmtree(scratch, ignore_errors=True)
    time.sleep(0.5)

    hits = sum(1 for l in cached.lines if "[lsp-lib-cache] hit" in l)
    misses = sum(1 for l in cached.lines if "[lsp-lib-cache] miss" in l)
    changed = sum(1 for l in cached.lines if "[lsp-lib-cache] changed" in l)
    verify_bad = [l for l in cached.lines if "MISMATCH" in l]

    print(f"\n{done[0]} files in {elapsed:.0f}s; both servers alive to the end: {alive}")
    print(f"library cache: {hits} hits, {misses} misses, {changed} changed; "
          f"verify-mode differences: {len(verify_bad)}")
    print("answers with content: " + ", ".join(
        f"{kind} {full}/{total}" for kind, (total, full) in sorted(substantive.items())))
    print(f"answers that differ between the reference and the cached server: {len(mismatches)}")
    for path, key, want, have in mismatches[:20]:
        print(f"  {path} {key}\n    reference: {want}\n    cached:    {have}")
    for l in verify_bad[:10]:
        print("  " + l)

    ok = alive and done[0] == len(files) and not mismatches and not verify_bad and hits > 0
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
