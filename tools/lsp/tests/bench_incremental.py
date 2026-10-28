#!/usr/bin/env python3
"""
Where the language server spends an edit (issue #1236, phase 0 of
docs/INCREMENTAL_ANALYSIS_DESIGN.md).

Drives a fixed edit sequence on a few workspaces and splits each edit's cost into
the parts the native diagnostics library reports when OBJECK_LSP_TIMING=1:

  parse    Parser over every file of the program (diags.cpp diag_parse_*)
  link     loading lang.obl and every listed .obl (a Linker::Load timed alone)
  analyze  ContextAnalyzer::Analyze without its library load

and next to them the latency the client sees for a hover sent right after the
edit, which also covers the Objeck side of the server (collecting texts, building
the answer, JSON). Not a pass/fail test: it prints a table, and --out writes it.

Usage:
  python3 bench_incremental.py <bin_dir> [server_dir]
      [--edits N] [--obc-runs N] [--only a,b] [--raw] [--out results.md]

<bin_dir> and [server_dir] are as for run_lsp_tests.py; <bin_dir> must also hold
obc, and ../lib/native a libobjk_diags built from this tree (an older one prints
no timing lines, and the script says so).

The link column is measured by a second, standalone Linker that loads the same
libraries right after the analysis, so it approximates (from below) the cost
phase 1, a library cache, would remove. The analyze column is the analysis
total minus that load.
"""
import argparse
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TESTS_DIR)

from run_lsp_tests import LspClient, REPO_ROOT, path_to_uri, find_pos, make_env  # noqa: E402

SERVER_SRC = os.path.join(REPO_ROOT, "tools", "lsp", "server")
SERVER_FILES = ["frameworks.obs", "proxy.obs", "server.obs",
                "format_code/scanner.obs", "format_code/formatter.obs"]
SERVER_LIBS = ["gen_collect.obl", "diags.obl", "net.obl", "json.obl", "regex.obl", "cipher.obl"]

TINY_MAIN = """class Tiny {
	function : Main(args : String[]) ~ Nil {
		count := 3;
		"count={$count}"->PrintLine();
	}
}
"""

TIMING = re.compile(r"\[lsp-timing\] (\w+) (.*)")


def scenarios():
    """(name, description, files {rel: source or text}, build.json or None, edited, hover needle)."""
    return [
        ("tiny", "6-line standalone file (13 default libraries)",
         {"main.obs": TINY_MAIN}, None, "main.obs", "count :="),
        ("two_files", "tools/lsp/tests/ws_multi_a: 2 small files, gen_collect",
         {"types.obs": os.path.join(TESTS_DIR, "ws_multi_a", "types.obs"),
          "main.obs": os.path.join(TESTS_DIR, "ws_multi_a", "main.obs")},
         {"files": ["types.obs", "main.obs"], "libs": ["gen_collect.obl"]},
         "main.obs", "AlphaShape->New"),
        ("json_standalone", "lib_src/json.obs alone (2.5k lines, 13 default libraries)",
         {"json.obs": os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "json.obs")},
         None, "json.obs", "class JsonElement"),
        ("lsp_server", "the LSP server's own sources: 5 files, 7.4k lines, 6 libraries",
         {f: os.path.join(SERVER_SRC, f.replace("/", os.sep)) for f in SERVER_FILES},
         {"files": SERVER_FILES, "libs": SERVER_LIBS}, "server.obs", "CallbackInitialize"),
    ]


class TimedServer(LspClient):
    """An LspClient whose stderr is read for the [lsp-timing] lines."""

    def __init__(self, obr, server_obe, apis_json, bin_dir):
        super().__init__(obr, server_obe, apis_json, bin_dir, stderr=subprocess.PIPE)
        self.lines = []
        self.lock = threading.Lock()
        threading.Thread(target=self._stderr, daemon=True).start()

    def _stderr(self):
        for raw in iter(self.proc.stderr.readline, b""):
            # the VM may write stderr as UTF-16; dropping NULs reads either
            line = raw.replace(b"\x00", b"").decode("utf-8", "replace").strip()
            m = TIMING.search(line)
            if m:
                fields = dict(kv.split("=", 1) for kv in m.group(2).split() if "=" in kv)
                with self.lock:
                    self.lines.append((m.group(1), fields))

    def take(self):
        with self.lock:
            taken, self.lines = self.lines, []
        return taken


def split(events):
    """Sum one window's timing lines into parse/link/analyze."""
    out = {"parses": 0, "analyses": 0, "parse": 0.0, "link": 0.0, "analyze": 0.0,
           "lines": 0, "failed": 0}
    for kind, f in events:
        if kind == "parse":
            out["parses"] += 1
            out["parse"] += float(f.get("ms", 0))
            out["lines"] = max(out["lines"], int(f.get("lines", 0)))
        elif kind == "analyze":
            out["analyses"] += 1
            # the separate load can run a little slower than the one inside the
            # analysis; past the total it means "all of it"
            total = float(f.get("total_ms", 0))
            link = min(total, max(0.0, float(f.get("link_ms", 0))))
            out["link"] += link
            out["analyze"] += total - link
            out["failed"] += f.get("ok") == "0"
    return out


def wait_diagnostics(c, uri, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        for n in list(c.notifications):
            if (n.get("method") == "textDocument/publishDiagnostics"
                    and n["params"].get("uri", "").lower() == uri.lower()):
                c.notifications.remove(n)
                return n["params"].get("diagnostics", [])
        c.drain(0.1)
    return None


def run_scenario(args, obr, server_obe, apis_json, bin_dir, scenario):
    name, desc, files, build, edited, needle = scenario
    work = tempfile.mkdtemp(prefix=f"objk-bench-{name}-")
    try:
        texts = {}
        for rel, src in files.items():
            dest = os.path.join(work, rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if os.path.exists(src):
                shutil.copyfile(src, dest)
            else:
                with open(dest, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(src)
            with open(dest, encoding="utf-8") as fh:
                texts[rel] = fh.read()
        if build is not None:
            with open(os.path.join(work, "build.json"), "w", encoding="utf-8") as fh:
                fh.write('{"files": %s, "libs": %s, "flags": ""}' % (
                    str(build["files"]).replace("'", '"'), str(build["libs"]).replace("'", '"')))

        root = path_to_uri(work)
        uri = root + "/" + edited
        c = TimedServer(obr, server_obe, apis_json, bin_dir)
        rows = []
        try:
            c.request("initialize", {
                "processId": os.getpid(), "rootUri": root if build else None, "capabilities": {},
                "workspaceFolders": [{"uri": root, "name": name}] if build else None,
            }, timeout=90)
            c.notify("initialized", {})
            c.drain(1.0)
            c.take()

            # open: only the edited document, as an editor restoring one tab would
            text = texts[edited]
            start = time.perf_counter()
            c.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
            diags = wait_diagnostics(c, uri)
            open_ms = (time.perf_counter() - start) * 1000.0
            c.drain(0.5)
            opened = split(c.take())
            opened["latency"] = open_ms
            errors = [d.get("message") for d in diags or [] if d.get("severity") == 1]

            # N body-only edits: a comment line before the file's last line
            version = 1
            for i in range(args.edits):
                version += 1
                line = text.count("\n") - 1
                insert = f"# bench edit {i}\n"
                c.notify("textDocument/didChange", {
                    "textDocument": {"uri": uri, "version": version},
                    "contentChanges": [{"range": {
                        "start": {"line": line, "character": 0},
                        "end": {"line": line, "character": 0}}, "text": insert}]})
                lines = text.split("\n")
                lines.insert(line, insert[:-1])
                text = "\n".join(lines)

                hl, hc = find_pos(text, needle)
                start = time.perf_counter()
                resp = c.request("textDocument/hover", {
                    "textDocument": {"uri": uri}, "position": {"line": hl, "character": hc + 1}},
                    timeout=120)
                hover_ms = (time.perf_counter() - start) * 1000.0
                wait_diagnostics(c, uri)
                c.drain(0.3)
                events = c.take()
                if args.raw:
                    for kind, f in events:
                        print(f"  edit {i}: {kind} {f}")
                row = split(events)
                row["latency"] = hover_ms
                row["answered"] = resp is not None
                rows.append(row)
        finally:
            c.close()

        obc = time_obc(args, bin_dir, work, files, build, edited)
        return {"name": name, "desc": desc, "open": opened, "edits": rows,
                "errors": errors, "obc": obc}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def time_obc(args, bin_dir, work, files, build, edited):
    """Median wall time of obc on the same program (parse+analyze+emit, one process)."""
    if args.obc_runs <= 0:
        return None
    exe = ".exe" if os.name == "nt" else ""
    obc = os.path.join(bin_dir, "obc" + exe)
    if not os.path.exists(obc):
        return None
    if build is not None:
        srcs, libs = build["files"], [l.replace(".obl", "") for l in build["libs"]]
    else:
        srcs = [edited]
        libs = ["gen_collect", "cipher", "csv", "json", "json_rpc", "json_stream", "misc",
                "net", "net_server", "query", "regex", "rss", "xml"]
    cmd = [obc, "-src", ",".join(os.path.join(work, s) for s in srcs),
           "-lib", ",".join(libs), "-dest", os.path.join(work, "bench.obe")]
    times = []
    for _ in range(args.obc_runs):
        start = time.perf_counter()
        subprocess.run(cmd, cwd=bin_dir, env=make_env(bin_dir),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        times.append((time.perf_counter() - start) * 1000.0)
    return statistics.median(times)


def med(rows, key):
    vals = [r[key] for r in rows]
    return statistics.median(vals) if vals else 0.0


def report(results, args):
    out = []
    out.append(f"Edits per workspace: {args.edits} (body-only: a comment line). "
               "Times in ms; per-edit columns are medians.\n")
    out.append("| workspace | lines | parse | link | analyze | parse+link+analyze | "
               "hover after edit | analyses/edit | open (to diagnostics) | obc |")
    out.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in results:
        e = r["edits"]
        native = med(e, "parse") + med(e, "link") + med(e, "analyze")
        obc = f"{r['obc']:.0f}" if r["obc"] is not None else "-"
        out.append(f"| {r['name']} | {r['open']['lines']} | {med(e, 'parse'):.1f} | "
                   f"{med(e, 'link'):.1f} | {med(e, 'analyze'):.1f} | {native:.1f} | "
                   f"{med(e, 'latency'):.1f} | {med(e, 'analyses'):.0f} | "
                   f"{r['open']['latency']:.0f} | {obc} |")
    out.append("")
    for r in results:
        e = r["edits"]
        native = med(e, "parse") + med(e, "link") + med(e, "analyze")
        share = (lambda k: 100.0 * med(e, k) / native if native else 0.0)
        out.append(f"- **{r['name']}** ({r['desc']}): parse {share('parse'):.0f}%, "
                   f"link {share('link'):.0f}%, analyze {share('analyze'):.0f}% of the native "
                   f"time; {med(e, 'parses'):.0f} parse(s) and {med(e, 'analyses'):.0f} "
                   f"analysis(es) per edit"
                   + (f"; the program has errors: {r['errors'][:2]}" if r["errors"] else "")
                   + (f"; {sum(x['failed'] for x in e)} analyses reported failure"
                      if any(x["failed"] for x in e) else "")
                   + ".")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bin_dir")
    ap.add_argument("server_dir", nargs="?", default=SERVER_SRC)
    ap.add_argument("--edits", type=int, default=10)
    ap.add_argument("--obc-runs", type=int, default=3)
    ap.add_argument("--only", help="comma-separated workspace names")
    ap.add_argument("--raw", action="store_true", help="print every timing line")
    ap.add_argument("--out")
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

    # read by diags.cpp; the JIT pin is what every LSP client sets
    os.environ["OBJECK_LSP_TIMING"] = "1"
    os.environ.setdefault("OBJECK_JIT_THRESHOLD", "999999999")

    wanted = set(args.only.split(",")) if args.only else None
    results = []
    for s in scenarios():
        if wanted and s[0] not in wanted:
            continue
        print(f"--- {s[0]}: {s[1]}", flush=True)
        r = run_scenario(args, obr, server_obe, apis_json, bin_dir, s)
        if not r["edits"] or not any(x["analyses"] for x in r["edits"]):
            print("  no [lsp-timing] lines: is libobjk_diags built from this tree?")
        results.append(r)

    text = report(results, args)
    print()
    print(text)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
