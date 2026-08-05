#!/usr/bin/env python3
"""
LSP regression tests for the Objeck language server (objeck_lsp.obe).

Drives the server with framed JSON-RPC and asserts the editor-facing features
actually respond: the advertised capability set, diagnostics, and the
navigation/edit handlers a user touches every day.

Usage:
  python3 run_lsp_tests.py <bin_dir> [server_dir]

<bin_dir> must contain obr(.exe). <server_dir> defaults to ../server relative to
this file and must contain objeck_lsp.obe and objk_apis.json (build them with
tools/lsp/server/build_server.sh).

Exit code 0 = all passed, 1 = at least one failed.

Transport: TCP, so the harness needs no environment setup. The server's stdio
mode works too, but only with OBJECK_STDIO=binary set -- which every shipped
editor client does. Without it the VM reads the header through std::wcin and
the body through fread(stdin) (core/vm/common.cpp), and that wide/byte mix
yields one character per body. TCP drives the same request handlers.

Handler assertions run against lsp_probe.obs, a single self-contained fixture,
so a red test means a handler regressed rather than that project configuration
drifted. A separate workspace-mode check covers build.json loading against
ws_lsp_features/.

Positions are located by searching the fixture text at runtime rather than being
hardcoded. The hand-written TEST_CHECKLIST.md in ws_lsp_features/ drifted out of
sync with the fixtures it describes; searching keeps this harness from rotting
the same way.
"""
import json
import os
import queue
import socket
import subprocess
import sys
import threading
import time

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
WS_DIR = os.path.join(TESTS_DIR, "ws_lsp_features")
PASS = 0
FAIL = 0

# Every capability the server advertises (server.obs CallbackInitialize).
# Adding a handler without adding it here is fine; removing one breaks this.
EXPECTED_CAPS = [
    "textDocumentSync", "completionProvider", "signatureHelpProvider",
    "documentSymbolProvider", "codeActionProvider", "referencesProvider",
    "declarationProvider", "definitionProvider", "renameProvider",
    "hoverProvider", "documentFormattingProvider",
    "documentRangeFormattingProvider", "workspaceSymbolProvider",
    "documentHighlightProvider", "foldingRangeProvider",
    "selectionRangeProvider", "typeDefinitionProvider",
    "implementationProvider", "inlayHintProvider", "callHierarchyProvider",
    "typeHierarchyProvider", "semanticTokensProvider",
]


KNOWN = 0

# Handlers that crash the server today. All three die the same way:
#
#   >>> Invalid object cast: '?' to 'System.Diagnostics.Result' <<<
#     System.Diagnostics.Analysis->SemanticTokens(...)
#
# i.e. the Result[] return pattern that FindReferences/GetSymbols were already
# converted away from (to the @-instance-var pattern) in diags.obs. These three
# never were. They are reported but not counted as failures so the suite can
# gate everything else; when one is fixed the harness says so and asks for the
# entry to be removed.
KNOWN_BROKEN = {}


def log_result(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  {detail}")


def log_known(method, ok):
    """Report a known-broken handler without failing the run."""
    global PASS, FAIL, KNOWN
    if ok:
        FAIL += 1
        print(f"  [FIXED] {method} now works -- remove it from KNOWN_BROKEN")
    else:
        KNOWN += 1
        print(f"  [KNOWN] {method}  {KNOWN_BROKEN[method]}")


def make_env(bin_dir):
    """Environment so obr resolves its libraries on every platform."""
    env = dict(os.environ)
    lib = os.path.join(os.path.dirname(bin_dir), "lib")
    native = os.path.join(lib, "native")
    env["OBJECK_LIB_PATH"] = lib + os.sep
    if sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = native + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    elif os.name != "nt":
        env["LD_LIBRARY_PATH"] = native + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    # No JIT pin: the "crashes after a handful of requests" flake was the TRAP
    # *_ARY_LEN operand over-count, fixed in the VM (ProcessReturn clamp) and
    # compiler (TRAP 5->4). Running at the default threshold keeps that fix
    # covered; if this suite starts dying mid-run again, suspect a JIT
    # regression before suspecting the server.
    return env


def path_to_uri(path):
    path = os.path.abspath(path).replace("\\", "/")
    if not path.startswith("/"):
        path = "/" + path          # Windows drive letter
    return "file://" + path


def find_pos(text, needle, occurrence=1, offset=0):
    """(line, char) of the Nth occurrence of needle, 0-based, LSP style."""
    idx = -1
    for _ in range(occurrence):
        idx = text.find(needle, idx + 1)
        if idx < 0:
            raise AssertionError(f"fixture missing {needle!r} #{occurrence}")
    line = text.count("\n", 0, idx)
    char = idx - (text.rfind("\n", 0, idx) + 1)
    return line, char + offset


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def position_in(text, uri, needle, occurrence=1, offset=0):
    """A textDocument/position params dict for the Nth occurrence of needle."""
    line, char = find_pos(text, needle, occurrence, offset)
    return {"textDocument": {"uri": uri},
            "position": {"line": line, "character": char}}


class LspClient:
    """Minimal LSP TCP client with a background reader."""

    def __init__(self, obr, server_obe, apis_json, bin_dir, timeout=90):
        self.port = free_port()
        self.proc = subprocess.Popen(
            [obr, server_obe, apis_json, str(self.port)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            cwd=bin_dir, env=make_env(bin_dir))
        self.q = queue.Queue()
        self.notifications = []
        self.rid = 0

        # The server loads a 600KB API index before it binds, so retry.
        end = time.time() + timeout
        self.sock = None
        while time.time() < end:
            if self.proc.poll() is not None:
                raise RuntimeError(
                    f"server exited early (code {self.proc.returncode})")
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), 2)
                break
            except OSError:
                time.sleep(0.5)
        if self.sock is None:
            self.proc.kill()
            raise RuntimeError(f"server never listened on port {self.port}")
        self.sock.settimeout(1.0)
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b""
        while True:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while b"\r\n\r\n" in buf:
                head, rest = buf.split(b"\r\n\r\n", 1)
                try:
                    n = int(head.split(b"Content-Length: ")[1].split(b"\r\n")[0])
                except (IndexError, ValueError):
                    buf = rest
                    continue
                if len(rest) < n:
                    break
                body, buf = rest[:n], rest[n:]
                try:
                    self.q.put(json.loads(body))
                except json.JSONDecodeError:
                    pass

    def _write(self, msg):
        body = json.dumps(msg).encode("utf-8")
        try:
            self.sock.sendall(
                f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
        except OSError:
            pass

    def notify(self, method, params=None):
        self._write({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def request(self, method, params=None, timeout=20):
        self.rid += 1
        rid = self.rid
        self._write({"jsonrpc": "2.0", "id": rid, "method": method,
                     "params": params or {}})
        end = time.time() + timeout
        while time.time() < end:
            try:
                msg = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                break
            if msg.get("id") == rid and ("result" in msg or "error" in msg):
                return msg
            if "method" in msg and "id" not in msg:
                self.notifications.append(msg)
        return None

    def diagnostics_for(self, uri, timeout=20):
        """Drain publishDiagnostics for a uri (may already have arrived)."""
        end = time.time() + timeout
        while True:
            for n in self.notifications:
                if (n.get("method") == "textDocument/publishDiagnostics"
                        and n.get("params", {}).get("uri", "").lower() == uri.lower()):
                    return n["params"].get("diagnostics", [])
            if time.time() >= end:
                return None
            try:
                msg = self.q.get(timeout=0.2)
            except queue.Empty:
                continue
            if "method" in msg and "id" not in msg:
                self.notifications.append(msg)

    def close(self):
        try:
            self.request("shutdown", timeout=5)
            self.notify("exit")
        finally:
            try:
                self.sock.close()
            except OSError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def result_of(resp):
    """Response payload, or None when the call errored or timed out."""
    if not resp or "result" not in resp:
        return None
    return resp["result"]


def non_empty(resp):
    r = result_of(resp)
    if r is None:
        return False
    if isinstance(r, dict):
        # locations may come back as a single Location, or {data: [...]}
        return bool(r.get("data", r))
    return bool(r)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    bin_dir = os.path.abspath(sys.argv[1])
    server_dir = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 \
        else os.path.join(TESTS_DIR, "..", "server")
    server_dir = os.path.abspath(server_dir)

    exe = ".exe" if os.name == "nt" else ""
    obr = os.path.join(bin_dir, "obr" + exe)
    server_obe = os.path.join(server_dir, "objeck_lsp.obe")
    apis_json = os.path.join(server_dir, "objk_apis.json")

    for p in (obr, server_obe, apis_json):
        if not os.path.exists(p):
            print(f"Error: missing {p}")
            print("Build the server first: tools/lsp/server/build_server.sh")
            return 1

    probe_obs = os.path.join(TESTS_DIR, "lsp_probe.obs")
    with open(probe_obs, encoding="utf-8") as f:
        probe_text = f.read()
    probe_uri = path_to_uri(probe_obs)

    print("Objeck LSP regression tests")
    print(f"  server: {server_obe}")
    print(f"  bin:    {bin_dir}\n")

    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        # --- initialize -----------------------------------------------------
        resp = c.request("initialize", {
            "processId": os.getpid(),
            "rootUri": None,
            "capabilities": {},
        }, timeout=90)

        caps = (result_of(resp) or {}).get("capabilities")
        if caps is None:
            log_result("initialize returns capabilities", False, "no response")
            print("\n  Results: server did not initialize - aborting")
            return 1
        log_result("initialize returns capabilities", True)

        missing = [k for k in EXPECTED_CAPS if k not in caps]
        log_result("advertises all expected capabilities", not missing,
                   f"missing: {missing}")

        legend = caps.get("semanticTokensProvider", {}).get("legend", {})
        log_result("semantic tokens legend present",
                   bool(legend.get("tokenTypes")) and bool(legend.get("tokenModifiers")))

        c.notify("initialized", {})

        # --- didOpen + diagnostics ------------------------------------------
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": probe_uri, "languageId": "objeck", "version": 1,
            "text": probe_text}})

        diags = c.diagnostics_for(probe_uri, timeout=90)
        if diags is None:
            log_result("publishDiagnostics for lsp_probe.obs", False, "none received")
        else:
            log_result("publishDiagnostics for lsp_probe.obs", True)
            log_result("clean fixture produces no diagnostics", not diags,
                       f"{len(diags)}: {[d.get('message') for d in diags[:3]]}")

        def at(needle, occurrence=1, offset=0):
            line, char = find_pos(probe_text, needle, occurrence, offset)
            return {"textDocument": {"uri": probe_uri},
                    "position": {"line": line, "character": char}}

        circle_decl = at("circle := Circle", 1, 1)
        describe_call = at("Describe(circle)", 1, 2)

        # --- navigation ------------------------------------------------------
        log_result("textDocument/definition",
                   non_empty(c.request("textDocument/definition", describe_call)))
        log_result("textDocument/typeDefinition",
                   non_empty(c.request("textDocument/typeDefinition", circle_decl)))
        log_result("textDocument/declaration",
                   non_empty(c.request("textDocument/declaration", describe_call)))
        log_result("textDocument/references",
                   non_empty(c.request("textDocument/references", dict(
                       describe_call, context={"includeDeclaration": True}))))
        log_result("textDocument/hover",
                   non_empty(c.request("textDocument/hover", circle_decl)))
        log_result("textDocument/documentHighlight",
                   non_empty(c.request("textDocument/documentHighlight", circle_decl)))
        log_result("textDocument/implementation", non_empty(c.request(
            "textDocument/implementation", at("interface Drawable", 1, 10))))

        # --- symbols ---------------------------------------------------------
        syms = result_of(c.request("textDocument/documentSymbol",
                                   {"textDocument": {"uri": probe_uri}}))
        names = json.dumps(syms) if syms else ""
        log_result("documentSymbol lists Circle and Describe",
                   bool(syms) and "Circle" in names and "Describe" in names,
                   f"got: {names[:120]}")
        log_result("workspace/symbol",
                   non_empty(c.request("workspace/symbol", {"query": "Circle"})))

        # --- editing ---------------------------------------------------------
        log_result("textDocument/completion", non_empty(c.request(
            "textDocument/completion",
            dict(at("circle->GetArea", 1, 8),
                 context={"triggerKind": 2, "triggerCharacter": ">"}))))
        log_result("textDocument/formatting", non_empty(c.request(
            "textDocument/formatting",
            {"textDocument": {"uri": probe_uri},
             "options": {"tabSize": 4, "insertSpaces": False}})))
        log_result("textDocument/foldingRange", non_empty(c.request(
            "textDocument/foldingRange", {"textDocument": {"uri": probe_uri}})))
        log_result("textDocument/selectionRange", non_empty(c.request(
            "textDocument/selectionRange",
            {"textDocument": {"uri": probe_uri},
             "positions": [circle_decl["position"]]})))
        log_result("textDocument/inlayHint", non_empty(c.request(
            "textDocument/inlayHint",
            {"textDocument": {"uri": probe_uri},
             "range": {"start": {"line": 0, "character": 0},
                       "end": {"line": probe_text.count("\n"), "character": 0}}})))
        log_result("textDocument/semanticTokens/full", non_empty(c.request(
            "textDocument/semanticTokens/full", {"textDocument": {"uri": probe_uri}})))

        # --- hierarchies -----------------------------------------------------
        # Ask on Describe's declaration, which is the SECOND method of its
        # class on purpose: method ranges used to overrun onto the next
        # declaration, so any position past the first method resolved to the
        # first one. Asserting the name, not just non-empty, is what catches it.
        item = result_of(c.request("textDocument/prepareCallHierarchy",
                                   position_in(probe_text, probe_uri,
                                               "function : Describe", 1, 12)))
        ch_item = (item[0] if isinstance(item, list) else item) if item else None
        log_result("prepareCallHierarchy resolves the method under the cursor",
                   bool(ch_item) and ch_item.get("name") == "Describe",
                   f"got: {ch_item.get('name') if ch_item else None}")

        if ch_item:
            inc = result_of(c.request("callHierarchy/incomingCalls",
                                      {"item": ch_item}))
            callers = [i.get("from", {}).get("name") for i in inc] if inc else []
            log_result("callHierarchy/incomingCalls finds the caller",
                       "Main" in callers, f"got: {callers}")

            out = result_of(c.request("callHierarchy/outgoingCalls",
                                      {"item": ch_item}))
            callees = [o.get("to", {}).get("name") for o in out] if out else []
            log_result("callHierarchy/outgoingCalls finds the callees",
                       {"GetName", "GetArea", "Draw"} <= set(callees),
                       f"got: {callees}")

        # --- rename ----------------------------------------------------------
        ren = result_of(c.request("textDocument/rename",
                                  dict(at("circle := Circle"), newName="disc")))
        ren_edits = (ren or {}).get("documentChanges", [{}])[0].get("edits", [])
        log_result("textDocument/rename edits every occurrence",
                   len(ren_edits) >= 2, f"got {len(ren_edits)} edits")

        # --- nil-safe operators ('?->' / '??') --------------------------------
        # They desugar to synthesized Try()/Otherwise() calls whose method-name
        # position aliases the receiver variable; LocateExpression used to
        # resolve the receiver to the phantom method, so rename / declaration /
        # references at the use site all returned null.
        ns_obs = os.path.join(TESTS_DIR, "test_nil_safe.obs")
        with open(ns_obs, encoding="utf-8") as f:
            ns_text = f.read()
        ns_uri = path_to_uri(ns_obs)
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": ns_uri, "languageId": "objeck", "version": 1,
            "text": ns_text}})
        c.diagnostics_for(ns_uri, timeout=60)

        ns_use = position_in(ns_text, ns_uri, "b?->", 1, 0)
        ren = result_of(c.request("textDocument/rename",
                                  dict(ns_use, newName="dog")))
        ren_edits = (ren or {}).get("documentChanges", [{}])[0].get("edits", [])
        log_result("rename at a '?->' use edits declaration and uses",
                   len(ren_edits) == 3, f"got: {ren}")

        decl = result_of(c.request("textDocument/declaration", ns_use))
        decl_line = (decl or {}).get("range", {}).get("start", {}).get("line", -1)
        b_decl_line, _ = find_pos(ns_text, "b := ")
        log_result("declaration at a '?->' use jumps to the declaration",
                   decl_line == b_decl_line, f"got: {decl}")

        log_result("references at a '?->' use finds all three",
                   len(result_of(c.request("textDocument/references", dict(
                       ns_use, context={"includeDeclaration": True}))) or []) == 3)

        # bare '??' (no '?->') takes a different parser branch: the receiver
        # variable itself becomes the synthesized Otherwise() call's receiver
        bare_use = position_in(ns_text, ns_uri, "b ?? ", 1, 0)
        ren = result_of(c.request("textDocument/rename",
                                  dict(bare_use, newName="dog")))
        ren_edits = (ren or {}).get("documentChanges", [{}])[0].get("edits", [])
        log_result("rename at a bare '??' use edits all occurrences",
                   len(ren_edits) >= 2, f"got: {ren}")
        decl = result_of(c.request("textDocument/declaration", bare_use))
        decl_line = (decl or {}).get("range", {}).get("start", {}).get("line", -1)
        log_result("declaration at a bare '??' use jumps to the declaration",
                   decl_line == b_decl_line, f"got: {decl}")

        # chained user-defined call: LocateExpression's chain descent must
        # resolve the SECOND call's method name (plain chains, not just nil-safe)
        chain_pos = position_in(ns_text, ns_uri, "Get();", 1, 1)
        decl = result_of(c.request("textDocument/declaration", chain_pos))
        get_line, _ = find_pos(ns_text, "method : public : Get()")
        decl_line = (decl or {}).get("range", {}).get("start", {}).get("line", -1)
        log_result("declaration on a chained call resolves the chained method",
                   decl_line == get_line, f"got: {decl}")
    finally:
        c.close()

    # --- rootUri-only session (Kate/ecode-style clients) -----------------------
    # No workspaceFolders at all: the rootUri fallback must configure the
    # workspace and the watched-files handler must stay alive.
    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        resp = c.request("initialize", {
            "processId": os.getpid(), "rootUri": path_to_uri(TESTS_DIR),
            "capabilities": {},
        }, timeout=90)
        log_result("initialize with rootUri only (no workspaceFolders)",
                   bool((result_of(resp) or {}).get("capabilities")))
        c.notify("initialized", {})
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": probe_uri, "languageId": "objeck", "version": 1,
            "text": probe_text}})
        c.diagnostics_for(probe_uri, timeout=60)
        c.notify("workspace/didChangeWatchedFiles", {"changes": [
            {"uri": path_to_uri(TESTS_DIR) + "/build.json", "type": 2}]})
        alive = non_empty(c.request("textDocument/hover",
                                    position_in(probe_text, probe_uri,
                                                "circle := Circle", 1, 1)))
        log_result("rootUri-only session survives didChangeWatchedFiles",
                   alive and c.proc.poll() is None)
    finally:
        c.close()

    # --- no-workspace session ---------------------------------------------------
    # rootUri null and no folders: nothing configures build.json, so the
    # watched-files handler must hit the Nil guard instead of dying on
    # String->Append(Nil) while logging the (unset) config path.
    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        resp = c.request("initialize", {
            "processId": os.getpid(), "rootUri": None, "capabilities": {},
        }, timeout=90)
        log_result("initialize with no workspace at all",
                   bool((result_of(resp) or {}).get("capabilities")))
        c.notify("initialized", {})
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": probe_uri, "languageId": "objeck", "version": 1,
            "text": probe_text}})
        c.diagnostics_for(probe_uri, timeout=60)
        c.notify("workspace/didChangeWatchedFiles", {"changes": [
            {"uri": path_to_uri(TESTS_DIR) + "/build.json", "type": 1}]})
        alive = non_empty(c.request("textDocument/hover",
                                    position_in(probe_text, probe_uri,
                                                "circle := Circle", 1, 1)))
        log_result("no-workspace session survives didChangeWatchedFiles (Nil guard)",
                   alive and c.proc.poll() is None)
    finally:
        c.close()

    # --- Sublime-style session -------------------------------------------------
    # Sublime passes workspaceFolders WITHOUT the workspace.configuration
    # capability and later fires workspace/didChangeWatchedFiles (e.g. after an
    # external compile). The server used to skip ProcessConfiguration for such
    # clients, leaving the build.json path Nil, and the watched-files handler
    # then died interpolating it (String->Append(Nil)).
    sublime_ws = path_to_uri(TESTS_DIR)
    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        resp = c.request("initialize", {
            "processId": os.getpid(), "rootUri": sublime_ws,
            "capabilities": {},   # no workspace.configuration, like Sublime
            "workspaceFolders": [{"uri": sublime_ws, "name": "tests"}],
        }, timeout=90)
        log_result("initialize without workspace.configuration capability",
                   bool((result_of(resp) or {}).get("capabilities")))
        c.notify("initialized", {})
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": probe_uri, "languageId": "objeck", "version": 1,
            "text": probe_text}})
        c.diagnostics_for(probe_uri, timeout=60)
        # the notification that used to kill the server
        c.notify("workspace/didChangeWatchedFiles", {"changes": [
            {"uri": sublime_ws + "/whatever.obe", "type": 1}]})
        alive = non_empty(c.request("textDocument/hover",
                                    position_in(probe_text, probe_uri,
                                                "circle := Circle", 1, 1)))
        log_result("server survives didChangeWatchedFiles (Sublime scenario)",
                   alive and c.proc.poll() is None)
    finally:
        c.close()

    # --- workspace mode ------------------------------------------------------
    # Separate session: build.json handling is independent of the handlers above.
    ws_uri = path_to_uri(WS_DIR)
    main_obs = os.path.join(WS_DIR, "main.obs")
    with open(main_obs, encoding="utf-8") as f:
        main_text = f.read()
    main_uri = path_to_uri(main_obs)

    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        # ProcessConfiguration (which loads build.json) runs whenever the
        # client passes workspace folders (or a rootUri); the old
        # workspace.configuration gate broke Sublime and is gone.
        resp = c.request("initialize", {
            "processId": os.getpid(), "rootUri": ws_uri,
            "capabilities": {"workspace": {"configuration": True}},
            "workspaceFolders": [{"uri": ws_uri, "name": "ws_lsp_features"}],
        }, timeout=90)
        log_result("workspace mode initializes",
                   bool((result_of(resp) or {}).get("capabilities")))
        c.notify("initialized", {})
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": main_uri, "languageId": "objeck", "version": 1,
            "text": main_text}})
        diags = c.diagnostics_for(main_uri, timeout=90)
        log_result("multi-file workspace resolves cross-file types",
                   diags is not None and not [d for d in diags
                                              if d.get("severity") == 1],
                   f"{diags}")
    finally:
        c.close()

    summary = f"\n  Results: {PASS} passed, {FAIL} failed"
    if KNOWN:
        summary += f", {KNOWN} known-broken"
    print(summary)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
