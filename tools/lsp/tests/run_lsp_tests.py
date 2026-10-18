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
hardcoded. Hand-written line numbers drift out of sync with the fixtures they
describe; searching keeps this harness from rotting that way.
"""
import glob
import json
import os
import re
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TESTS_DIR, "..", "..", ".."))
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
        # requests the server sent us (client/registerCapability), in order
        self.server_requests = []
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
            self._keep(msg)
        return None

    def _keep(self, msg):
        if "method" in msg and "id" not in msg:
            self.notifications.append(msg)
        elif "method" in msg:
            self.server_requests.append(msg)

    def send_raw(self, body, headers=None):
        """Frame and send bytes as-is, with optional extra header lines first."""
        head = b"".join(h + b"\r\n" for h in (headers or []))
        try:
            self.sock.sendall(head + b"Content-Length: " + str(len(body)).encode()
                              + b"\r\n\r\n" + body)
        except OSError:
            pass

    def response_to(self, rid, timeout=20):
        """The response carrying this id (any JSON type), or None."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                msg = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                break
            if "method" not in msg and msg.get("id", "<absent>") == rid:
                return msg
            self._keep(msg)
        return None

    def drain(self, seconds):
        """Collect whatever arrives in the next few seconds."""
        end = time.time() + seconds
        while time.time() < end:
            try:
                self._keep(self.q.get(timeout=0.1))
            except queue.Empty:
                pass

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
            self._keep(msg)

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
                # over TCP, exit ends the connection, not the multi-client process
                self.proc.wait(timeout=1)
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


def offset_of(text, pos):
    """Index into text of an LSP position (UTF-16 columns)."""
    lines = text.split("\n")
    line = min(pos["line"], len(lines))
    index = sum(len(l) + 1 for l in lines[:line])
    if line < len(lines):
        units = lines[line].encode("utf-16-le")
        col = min(pos["character"] * 2, len(units))
        index += len(units[:col].decode("utf-16-le", errors="ignore"))
    return min(index, len(text))


def apply_edits(text, edits):
    """Apply TextEdit[] the way a client does: back to front."""
    spans = sorted(((offset_of(text, e["range"]["start"]),
                     offset_of(text, e["range"]["end"]), e.get("newText", ""))
                    for e in edits), reverse=True)
    for start, end, new in spans:
        text = text[:start] + new + text[end:]
    return text


def document_end(text):
    lines = text.split("\n")
    return {"line": len(lines) - 1,
            "character": len(lines[-1].encode("utf-16-le")) // 2}


def check_formatting_applies(edits, original, label):
    """A formatting answer is only correct if APPLYING it keeps the program.

    The handler used to escape its output twice: every newline reached the
    client as a literal backslash-n, so the edit collapsed the file onto one
    line. The old assertion was non_empty, which that passed (#1213).
    """
    if not edits:
        log_result(f"{label}: formatting edits apply", False, "no edits")
        return
    rng = edits[0].get("range", {})
    log_result(f"{label}: whole-document edit ends at the true end",
               rng.get("end") == document_end(original),
               f"end {rng.get('end')} vs {document_end(original)}")
    applied = apply_edits(original, edits)
    lines_in, lines_out = original.count("\n"), applied.count("\n")
    log_result(f"{label}: formatted text keeps its lines",
               lines_out * 4 >= lines_in * 3, f"{lines_in} lines in, {lines_out} out")
    log_result(f"{label}: formatting adds no backslashes",
               applied.count(chr(92)) == original.count(chr(92)),
               f"{original.count(chr(92))} in, {applied.count(chr(92))} out")


def check_formatting_literals(c):
    """String literals holding escapes must come back byte-for-byte."""
    bs = chr(92)
    lit_nl = '"a' + bs + 'nb"'
    lit_q = '"a' + bs + '"b"'
    text = ("class Lits {\n"
            "  function : Main(args : String[]) ~ Nil {\n"
            "    x := " + lit_nl + ";\n"
            "    y := " + lit_q + ";\n"
            "    x->PrintLine(); y->PrintLine();\n"
            "  }\n"
            "}\n")
    uri = path_to_uri(os.path.join(TESTS_DIR, "lsp_format_literals.obs"))
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    c.diagnostics_for(uri, timeout=60)
    edits = result_of(c.request("textDocument/formatting", {
        "textDocument": {"uri": uri},
        "options": {"tabSize": 2, "insertSpaces": True}}))
    check_formatting_applies(edits, text, "escaped literals")
    applied = apply_edits(text, edits or [])
    log_result("formatting keeps an escaped newline inside a literal",
               lit_nl in applied, repr(applied[:160]))
    log_result("formatting keeps an escaped quote inside a literal",
               lit_q in applied, repr(applied[:160]))
    c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})


def completion_items(resp):
    r = result_of(resp)
    if isinstance(r, dict):
        r = r.get("items")
    return r if isinstance(r, list) else []


def markup_value(doc):
    """The text of a MarkupContent, or None if it is not one."""
    if isinstance(doc, dict) and doc.get("kind") in ("markdown", "plaintext"):
        return doc.get("value")
    return None


def check_completion_docs(c, caps, probe_text, probe_uri):
    """Completion carries real signatures and documentation (#1228).

    Every item used to say "<name> details" and "<name> documentation", which
    the editor showed as written, and completionItem/resolve echoed the item
    back behind a resolveProvider of false.
    """
    log_result("completionProvider.resolveProvider is true",
               (caps.get("completionProvider") or {}).get("resolveProvider") is True,
               f"got: {caps.get('completionProvider')}")

    items = completion_items(c.request("textDocument/completion", dict(
        position_in(probe_text, probe_uri, "circle->GetArea", 1, 11),
        context={"triggerKind": 1})))
    placeholder = [i.get("label") for i in items
                   if " details" in str(i.get("detail", ""))
                   or " documentation" in str(i.get("documentation", ""))]
    log_result("no completion item carries placeholder text",
               bool(items) and not placeholder, f"{len(items)} items, placeholders on {placeholder[:5]}")
    keywords = [i for i in items if i.get("kind") == 14]
    log_result("keyword items say they are keywords",
               bool(keywords) and all(i.get("detail") == "keyword" for i in keywords),
               f"got: {[(i.get('label'), i.get('detail')) for i in keywords[:3]]}")

    area = next((i for i in items if i.get("label") == "GetArea"), None)
    log_result("completion offers the user method", area is not None,
               f"labels: {[i.get('label') for i in items][:12]}")
    if area:
        got = result_of(c.request("completionItem/resolve", area)) or {}
        log_result("resolve gives a user method its signature",
                   "GetArea" in str(got.get("detail", "")) and "Float" in str(got.get("detail", "")),
                   f"got: {got.get('detail')!r}")
        log_result("and documentation as MarkupContent",
                   "GetArea" in str(markup_value(got.get("documentation"))),
                   f"got: {got.get('documentation')!r}")
        log_result("resolve keeps the item's label and kind",
                   got.get("label") == "GetArea" and got.get("kind") == area.get("kind"),
                   f"got: {got.get('label')!r} {got.get('kind')!r}")

    # A library receiver: the documentation comes from objk_apis.json.
    text = ("class LibDocs {\n"
            "  function : Main(args : String[]) ~ Nil {\n"
            "    v := Collection.Vector->New()<String>;\n"
            "    v->AddBack(\"a\");\n"
            "    v->Each(" + chr(92) + "^(s) => s->PrintLine());\n"
            "  }\n"
            "}\n")
    uri = path_to_uri(os.path.join(TESTS_DIR, "lsp_completion_docs.obs"))
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    c.diagnostics_for(uri, timeout=60)

    def resolved(needle, offset, name):
        found = completion_items(c.request("textDocument/completion", dict(
            position_in(text, uri, needle, 1, offset), context={"triggerKind": 1})))
        item = next((i for i in found if i.get("label") == name), None)
        if item is None:
            return None, [i.get("label") for i in found][:12]
        return result_of(c.request("completionItem/resolve", item)) or {}, None

    got, labels = resolved("v->AddBack", 6, "AddBack")
    log_result("completion offers a library method", got is not None, f"labels: {labels}")
    if got is not None:
        log_result("resolve gives a library method its signature",
                   "Collection.Vector->AddBack" in str(got.get("detail", "")),
                   f"got: {got.get('detail')!r}")
        doc = markup_value(got.get("documentation"))
        log_result("and its documentation from the API index",
                   doc is not None and "Adds a value to the end" in doc
                   and got["documentation"].get("kind") == "markdown",
                   f"got: {got.get('documentation')!r}")

    got, labels = resolved("v->Each", 6, "Each")
    doc = markup_value((got or {}).get("documentation")) or ""
    # The index is written for the HTML pages: inside markdown code an entity
    # is shown literally, so it is decoded there (and only there -- outside
    # code the renderer decodes it, and a bare '<' could read as a tag).
    log_result("documentation code shows <H>, not &lt;H&gt;",
               "~ Vector<H>`" in doc and "~ Vector&lt;" not in doc, f"got: {doc[:200]!r}")

    hover = result_of(c.request("textDocument/hover",
                                position_in(text, uri, "AddBack", 1, 2))) or {}
    value = markup_value(hover.get("contents"))
    log_result("hover is MarkupContent",
               value is not None and hover["contents"].get("kind") == "markdown",
               f"got: {hover!r}"[:300])
    log_result("hover on a library method shows its documentation",
               "Adds a value to the end" in str(value), f"got: {str(value)[:160]!r}")

    hover = result_of(c.request("textDocument/hover",
                                position_in(probe_text, probe_uri, "circle := Circle", 1, 1))) or {}
    log_result("hover on a user type is MarkupContent too",
               markup_value(hover.get("contents")) is not None, f"got: {hover!r}"[:300])
    c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_rename_and_organize(c, probe_text, probe_uri):
    """prepareRename, name validation, and source.organizeImports (#1229)."""
    def prepare(needle, offset):
        return result_of(c.request("textDocument/prepareRename",
                                   position_in(probe_text, probe_uri, needle, 1, offset)))

    got = prepare("circle := Circle", 2)
    line, char = find_pos(probe_text, "circle := Circle")
    log_result("prepareRename on a local gives its range and name",
               isinstance(got, dict) and got.get("placeholder") == "circle"
               and got.get("range") == {"start": {"line": line, "character": char},
                                        "end": {"line": line, "character": char + 6}},
               f"got: {got!r}")
    log_result("prepareRename declines a keyword", prepare("if(drawable", 0) is None)
    log_result("prepareRename declines a string literal", prepare('"circle"', 2) is None)
    log_result("prepareRename declines a library method",
               prepare("total->PrintLine", 9) is None)
    got = prepare("@radius := radius", 3)
    log_result("prepareRename on an instance variable includes its '@'",
               isinstance(got, dict) and got.get("placeholder") == "@radius", f"got: {got!r}")

    for bad in ("x y", "if", "1abc", "a-b"):
        resp = c.request("textDocument/rename", dict(
            position_in(probe_text, probe_uri, "circle := Circle", 1, 2), newName=bad))
        log_result(f"rename refuses {bad!r}",
                   bool(resp) and "result" not in resp
                   and resp.get("error", {}).get("code") == -32803, f"got: {resp!r}"[:200])

    # Renaming an instance variable wrote the new name over '@radius' without
    # its '@', and missed the class-level declaration: either way the result
    # did not compile.
    ren = result_of(c.request("textDocument/rename", dict(
        position_in(probe_text, probe_uri, "@radius := radius", 1, 3), newName="rad")))
    edits = (ren or {}).get("documentChanges", [{}])[0].get("edits", [])
    applied = apply_edits(probe_text, edits)
    log_result("renaming an instance variable keeps its '@'",
               bool(edits) and all(e.get("newText") == "@rad" for e in edits),
               f"newTexts: {sorted({e.get('newText') for e in edits})}")
    dline, _ = find_pos(probe_text, "@radius : Float")
    log_result("and renames its declaration",
               any(e["range"]["start"]["line"] == dline for e in edits)
               and "@radius" not in applied and "@rad : Float" in applied,
               f"edit lines: {sorted(e['range']['start']['line'] for e in edits)}")

    # --- organizeImports ---------------------------------------------------
    def organize(name, text, only=("source.organizeImports",)):
        uri = path_to_uri(os.path.join(TESTS_DIR, f"lsp_organize_{name}.obs"))
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
        context = {"diagnostics": []}
        if only is not None:
            context["only"] = list(only)
        got = result_of(c.request("textDocument/codeAction", {
            "textDocument": {"uri": uri},
            "range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}},
            "context": context})) or []
        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        actions = [a for a in got if isinstance(a, dict) and a.get("kind") == "source.organizeImports"]
        if not actions:
            return None
        return apply_edits(text, actions[0].get("edit", {}).get("changes", {}).get(uri, []))

    body = "class Org {\n  function : Main(args : String[]) ~ Nil {\n  }\n}\n"
    messy = "#~\nheader\n~#\n\nuse System.IO;\nuse Collection;\n\nuse System.IO;\n\n" + body
    log_result("organizeImports sorts and deduplicates the 'use' block",
               organize("messy", messy) == "#~\nheader\n~#\n\nuse Collection;\nuse System.IO;\n\n" + body,
               repr(organize("messy", messy))[:200])
    log_result("organizeImports keeps one statement as one statement",
               organize("single", "use System.IO, Collection;\n\n" + body)
               == "use Collection, System.IO;\n\n" + body)
    log_result("organizeImports is answered for the parent kind 'source'",
               organize("parent", messy, ("source",)) is not None)
    log_result("organizeImports offers nothing for a sorted block",
               organize("sorted", "use Collection;\nuse System.IO;\n\n" + body) is None)
    log_result("organizeImports is not offered at a plain lightbulb",
               organize("lightbulb", messy, None) is None)
    crlf = ("use System.IO;\nuse Collection;\n\n" + body).replace("\n", "\r\n")
    log_result("organizeImports keeps CRLF line endings",
               organize("crlf", crlf) == ("use Collection;\nuse System.IO;\n\n" + body).replace("\n", "\r\n"))


def range_key(r):
    return ((r["start"]["line"], r["start"]["character"]),
            (r["end"]["line"], r["end"]["character"]))


def inside(inner, outer):
    (a, b), (c, d) = range_key(inner), range_key(outer)
    return c <= a and b <= d


def symbol_tree_faults(symbols, text, parent=None, path=""):
    """Every rule a client enforces on DocumentSymbol[], as a list of faults.

    VS Code rejects the WHOLE answer when one selectionRange falls outside its
    range, so a single bad node empties the outline.
    """
    lines = text.split("\n")
    faults = []
    previous = None
    for s in symbols:
        name = f"{path}/{s.get('name')}"
        rng, sel = s.get("range"), s.get("selectionRange")
        if not rng or not sel:
            faults.append(f"{name}: no range or selectionRange")
            continue
        if not inside(sel, rng):
            faults.append(f"{name}: selectionRange {range_key(sel)} outside range {range_key(rng)}")
        if parent and not inside(rng, parent):
            faults.append(f"{name}: range outside its parent's")
        if previous and range_key(previous)[1] > range_key(rng)[0]:
            faults.append(f"{name}: overlaps the symbol before it")
        line = lines[sel["start"]["line"]] if sel["start"]["line"] < len(lines) else ""
        selected = line[sel["start"]["character"]:sel["end"]["character"]]
        if selected != s.get("name"):
            faults.append(f"{name}: selectionRange selects {selected!r}")
        previous = rng
        faults += symbol_tree_faults(s.get("children") or [], text, rng, name)
    return faults


def check_document_symbol_tree(c, probe_text, probe_uri):
    """documentSymbol answers a DocumentSymbol tree to a client that takes one (#1230)."""
    syms = result_of(c.request("textDocument/documentSymbol",
                               {"textDocument": {"uri": probe_uri}})) or []
    top = {s.get("name"): s for s in syms if isinstance(s, dict)}
    log_result("documentSymbol answers a tree (DocumentSymbol[])",
               bool(top) and all("selectionRange" in s and "location" not in s for s in top.values()),
               f"got: {json.dumps(syms)[:200]}")
    probe = top.get("Probe") or {}
    kids = {k.get("name"): k for k in probe.get("children") or []}
    log_result("methods are nested under their class",
               {"Main", "Describe"} <= set(kids), f"Probe's children: {sorted(kids)}")
    log_result("an enum declared in a class is nested under it",
               (kids.get("Kind") or {}).get("kind") == 10, f"Probe's children: {sorted(kids)}")
    log_result("an interface is reported as an interface",
               (top.get("Drawable") or {}).get("kind") == 11,
               f"got kind {(top.get('Drawable') or {}).get('kind')}")
    faults = symbol_tree_faults(syms, probe_text)
    log_result("every symbol range holds its name and its children", not faults, f"{faults[:4]}")

    path = os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "json.obs")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    uri = path_to_uri(path)
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    syms = result_of(c.request("textDocument/documentSymbol",
                               {"textDocument": {"uri": uri}}, timeout=60)) or []
    bundles = [s for s in syms if isinstance(s, dict) and s.get("kind") == 3]
    classes = [k for b in bundles for k in b.get("children") or []]
    log_result("a real library file nests its classes under their bundles",
               len(bundles) >= 2 and any(k.get("name") == "JsonElement"
                                         and k.get("detail") == "Data.JSON.JsonElement" for k in classes),
               f"bundles {[b.get('name') for b in bundles]}, classes {[k.get('name') for k in classes][:6]}")
    faults = symbol_tree_faults(syms, text)
    log_result("and every range in it is well formed", not faults, f"{len(faults)}: {faults[:3]}")
    c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})

    # Every library source (about 30 s). The probe and json.obs were clean
    # while nine of these were not: lambdas' synthetic classes, a default-
    # parameter method compiled into one overload per arity at one position,
    # and native method ends that ran into the next method, which nested
    # methods inside methods six deep.
    bad = []
    sources = sorted(glob.glob(os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "*.obs")))
    for path in sources:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        uri = path_to_uri(path)
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
        resp = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, timeout=120)
        faults = symbol_tree_faults(result_of(resp) or [], text)
        if resp is None or "error" in resp or faults:
            bad.append((os.path.basename(path), faults[:2] if faults else resp))
        c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
    log_result(f"every library source has a well-formed outline ({len(sources)} files)",
               len(sources) > 40 and not bad, f"{len(bad)} bad: {bad[:3]}")


def flatten_symbols(symbols):
    out = []
    for s in symbols or []:
        if isinstance(s, dict):
            out.append(s)
            out += flatten_symbols(s.get("children"))
    return out


def check_large_real_file(c):
    """Every read-only handler over a real library source, errors and all.

    lsp_probe.obs is deliberately small and clean. lib_src/json.obs is neither:
    2.5k lines, two bundles, and -- opened without its build.json -- ten
    diagnostics. documentSymbol killed the server on it, because the native
    symbol walk lost every bundle but the last (#1214).
    """
    path = os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "json.obs")
    if not os.path.exists(path):
        log_result("large real file: fixture present", False, path)
        return
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    uri = path_to_uri(path)
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    log_result("large real file: diagnostics published",
               c.diagnostics_for(uri, timeout=60) is not None)

    doc = {"textDocument": {"uri": uri}}
    mid = {"line": text[:text.find("class JsonElement")].count("\n"), "character": 8}
    last = text.count("\n")
    requests = [
        ("documentSymbol", "textDocument/documentSymbol", doc),
        ("semanticTokens/full", "textDocument/semanticTokens/full", doc),
        ("foldingRange", "textDocument/foldingRange", doc),
        ("selectionRange", "textDocument/selectionRange", dict(doc, positions=[mid])),
        ("inlayHint", "textDocument/inlayHint", dict(doc, range={
            "start": {"line": 0, "character": 0}, "end": {"line": last, "character": 0}})),
        ("hover", "textDocument/hover", dict(doc, position=mid)),
        ("documentHighlight", "textDocument/documentHighlight", dict(doc, position=mid)),
    ]
    symbols = None
    first = {}
    for label, method, params in requests:
        resp = c.request(method, params, timeout=60)
        first[label] = result_of(resp)
        if label == "documentSymbol":
            symbols = result_of(resp)
        log_result(f"large real file: {label} answers", resp is not None,
                   "no response (server down?)")

    # a tree (DocumentSymbol[]) or a flat list (SymbolInformation[]), either way
    flat = flatten_symbols(symbols)
    names = {s.get("detail") or s.get("name") for s in flat}
    log_result("large real file: symbols from both bundles",
               {"Data.JSON.JsonElement", "Data.JSON.Scheme.FunctionType"} <= names,
               f"{len(names)} names, e.g. {sorted(n for n in names if n)[:6]}")

    # A bundle spans its 'bundle' keyword to its closing brace. The start was the
    # file's first 'use', and a named bundle's end was never recorded, so it read
    # as uninitialized memory: 140421408 in one run and 0 in the next (#1218).
    lines = text.split("\n")
    opening = next(i for i, l in enumerate(lines) if l.startswith("bundle Data.JSON {"))
    second = next(i for i, l in enumerate(lines) if l.startswith("bundle Data.JSON.Scheme {"))
    closing = max(i for i, l in enumerate(lines[:second]) if l.startswith("}"))
    ns = next((s for s in flat if s.get("name") == "Data.JSON" and s.get("kind") == 3), None)
    rng = (ns or {}).get("range") or (ns or {}).get("location", {}).get("range", {})
    log_result("large real file: a bundle spans 'bundle' to its closing brace",
               rng.get("start", {}).get("line") == opening and rng.get("end", {}).get("line") == closing,
               f"got {rng}, want lines {opening}..{closing}")

    # The analysis is kept between requests now (#1218): asking again, with no
    # edit in between, must give the same answer, not a second, damaged analysis.
    again = result_of(c.request("textDocument/documentSymbol", doc, timeout=60))
    log_result("large real file: a repeated request gets the same answer",
               json.dumps(again, sort_keys=True) == json.dumps(symbols, sort_keys=True))
    tokens_again = c.request("textDocument/semanticTokens/full", doc, timeout=60)
    log_result("large real file: and so does a different one after it",
               tokens_again is not None
               and json.dumps(result_of(tokens_again), sort_keys=True)
               == json.dumps(first.get("semanticTokens/full"), sort_keys=True))

    # Several standalone documents are open by now. workspace/symbol then asks
    # for every open document's text, and that path dereferenced a Nil uri.
    ws = c.request("workspace/symbol", {"query": "JsonElement"}, timeout=60)
    log_result("workspace/symbol with several open documents answers", ws is not None,
               "no response (server down?)")
    log_result("and finds the class in the large file",
               any(str(s.get("name", "")).endswith("JsonElement")
                   for s in result_of(ws) or [] if isinstance(s, dict)),
               json.dumps(result_of(ws))[:160])
    c.notify("textDocument/didClose", doc)


def column(line, index, encoding):
    """The column of line[index] in the negotiated position encoding."""
    if encoding == "utf-32":
        return index
    return len(line[:index].encode("utf-16-le")) // 2


def check_text_encoding(c, caps):
    """Non-ASCII text, sent the two legal ways, lands where the client put it.

    A client may send a non-ASCII character raw (UTF-8) or as a \\u escape.
    The server kept escapes literally in its copy of the document, so every
    column after the character was off by five (BMP) or ten (an emoji) (#1216).
    """
    encoding = caps.get("positionEncoding")
    log_result("positionEncoding is advertised", encoding in ("utf-16", "utf-32"),
               f"got: {encoding}")
    if os.name == "nt":
        log_result("positionEncoding is utf-16 where a Char is UTF-16", encoding == "utf-16")
    else:
        log_result("positionEncoding is utf-32 where a Char is a code point and it is offered",
                   encoding == "utf-32", f"got: {encoding}")

    shapes = [("comment", "# caf{ch}\nclass U{n} {{\n  function : Main(a : String[]) ~ Nil {{\n"
                          "    y := zzz;\n  }}\n}}\n", 3),
              ("literal", "class U{n} {{\n  function : Main(a : String[]) ~ Nil {{\n"
                          "    s := \"caf{ch}\"; s->PrintLine(); y := zzz;\n  }}\n}}\n", 2)]
    n = 0
    for where, template, line_no in shapes:
        for ch_name, ch in (("e-acute", "é"), ("emoji", "\U0001F600")):
            for escaped in (True, False):
                n += 1
                text = template.format(ch=ch, n=n)
                uri = path_to_uri(os.path.join(TESTS_DIR, f"lsp_unicode_{n}.obs"))
                c.send_raw(json.dumps({"jsonrpc": "2.0", "method": "textDocument/didOpen",
                                       "params": {"textDocument": {
                                           "uri": uri, "languageId": "objeck", "version": 1,
                                           "text": text}}}, ensure_ascii=escaped).encode("utf-8"))
                diags = c.diagnostics_for(uri, timeout=60) or []
                line = text.split("\n")[line_no]
                want = {"line": line_no, "character": column(line, line.index("zzz"), encoding)}
                got = [d["range"]["start"] for d in diags if "zzz" in str(d.get("message", ""))]
                how = "escaped" if escaped else "raw"
                label = f"{where} with {ch_name} sent {how}"
                log_result(f"{label}: diagnostic column", want in got, f"want {want}, got {got}")

                # an edit after the character lands in the right place too
                c.notifications.clear()
                col = column(line, line.index("zzz"), encoding)
                c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                         "contentChanges": [{"range": {
                             "start": {"line": line_no, "character": col},
                             "end": {"line": line_no, "character": col + 3}}, "text": "qqq"}]})
                diags = c.diagnostics_for(uri, timeout=60) or []
                got = [d["range"]["start"] for d in diags if "qqq" in str(d.get("message", ""))]
                log_result(f"{label}: an edit after it", want in got, f"want {want}, got {got}")
                c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})

    # A position past the end of its line means the end of that line. It ran on
    # into the next line; one past the last line was dropped silently (#1216).
    text = "class Clamp {\n  function : Main(a : String[]) ~ Nil {\n  }\n}\n"
    uri = path_to_uri(os.path.join(TESTS_DIR, "lsp_clamp.obs"))
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    c.diagnostics_for(uri, timeout=60)
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
             "contentChanges": [
                 {"range": {"start": {"line": 0, "character": 40}, "end": {"line": 0, "character": 40}},
                  "text": " # past the end"},
                 {"range": {"start": {"line": 9, "character": 0}, "end": {"line": 9, "character": 0}},
                  "text": "# appended\n"}]})
    expected = text.replace("class Clamp {", "class Clamp { # past the end", 1) + "# appended\n"
    edits = result_of(c.request("textDocument/formatting", {
        "textDocument": {"uri": uri}, "options": {"tabSize": 2, "insertSpaces": True}}))
    end = (edits or [{}])[0].get("range", {}).get("end")
    log_result("positions past a line, or past the document, are clamped",
               end == document_end(expected), f"server's end {end}, editor's {document_end(expected)}")
    c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_batching(c):
    """A burst of edits costs one analysis, and queued work can be refused (#1217).

    The server read one message, handled it, then read the next: each
    keystroke paid for a whole-workspace analysis and published diagnostics,
    every request waited behind all of them, and a cancel arrived only after
    its request had been answered.
    """
    text = ("class Burst {\n  function : Main(a : String[]) ~ Nil {\n"
            "    x := 1;\n    x->PrintLine();\n  }\n}\n")
    uri = path_to_uri(os.path.join(TESTS_DIR, "lsp_burst.obs"))
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
    c.diagnostics_for(uri, timeout=60)

    c.notifications.clear()
    final_text = text
    for v in range(2, 22):
        # typing 'y' at the start of line 2, twenty times
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": v},
                 "contentChanges": [{"range": {"start": {"line": 2, "character": 4},
                                               "end": {"line": 2, "character": 4}}, "text": "y"}]})
        final_text = final_text.replace("    ", "    y", 1) if v == 2 else \
            final_text.replace("    y", "    yy", 1)
    start = time.time()
    resp = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, timeout=60)
    log_result("a request after a burst of edits is answered promptly",
               resp is not None and time.time() - start < 10,
               f"{time.time() - start:.2f}s")
    c.drain(1.5)
    pubs = [n for n in c.notifications if n.get("method") == "textDocument/publishDiagnostics"
            and n["params"].get("uri", "").lower() == uri.lower()]
    # once, normally; a scheduling stall mid-burst on a loaded runner may split it
    log_result("twenty edits publish diagnostics once", 1 <= len(pubs) <= 3, f"{len(pubs)} publishes")
    log_result("for the final version",
               bool(pubs) and pubs[-1]["params"].get("version") == 21,
               f"version {pubs[-1]['params'].get('version') if pubs else None}")

    # the coalesced result matches a fresh analysis of the same text
    fresh = path_to_uri(os.path.join(TESTS_DIR, "lsp_burst_fresh.obs"))
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": fresh, "languageId": "objeck", "version": 1, "text": final_text}})
    want = sorted(d["message"] for d in (c.diagnostics_for(fresh, timeout=60) or []))
    got = sorted(d["message"] for d in (pubs[-1]["params"]["diagnostics"] if pubs else []))
    log_result("and match a fresh analysis of the same text", want == got,
               f"burst {got} vs fresh {want}")
    c.notify("textDocument/didClose", {"textDocument": {"uri": fresh}})

    # Keep the server busy analyzing a 2.5k-line file, and meanwhile queue a
    # request and its cancel, then a hover and an edit to the hover's document:
    # all four are handled in one batch once the analysis ends.
    path = os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "json.obs")
    with open(path, encoding="utf-8") as fh:
        big = fh.read()
    busy = path_to_uri(os.path.join(TESTS_DIR, "lsp_busy.obs"))
    c.notify("textDocument/didOpen", {"textDocument": {
        "uri": busy, "languageId": "objeck", "version": 1, "text": big}})
    # let that analysis start (it takes a few hundred ms), then send the four in
    # one write so they are read while it runs
    time.sleep(0.05)
    queued = [
        {"jsonrpc": "2.0", "id": "cancel-me", "method": "textDocument/documentSymbol",
         "params": {"textDocument": {"uri": uri}}},
        {"jsonrpc": "2.0", "method": "$/cancelRequest", "params": {"id": "cancel-me"}},
        {"jsonrpc": "2.0", "id": "stale", "method": "textDocument/hover",
         "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 6}}},
        # names no document, so no edit can make it stale
        {"jsonrpc": "2.0", "id": "no-document", "method": "workspace/symbol",
         "params": {"query": "Burst"}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {
            "textDocument": {"uri": uri, "version": 22},
            "contentChanges": [{"range": {"start": {"line": 0, "character": 0},
                                          "end": {"line": 0, "character": 0}}, "text": "# edited\n"}]}},
    ]
    wire = b""
    for m in queued:
        head, body = frame(m)
        wire += head + body
    try:
        c.sock.sendall(wire)
    except OSError:
        pass
    cancelled = c.response_to("cancel-me", timeout=60)
    log_result("a request cancelled while queued gets RequestCancelled",
               (cancelled or {}).get("error", {}).get("code") == -32800, f"got: {cancelled}")
    stale = c.response_to("stale", timeout=60)
    log_result("a request whose document then changed gets ContentModified",
               (stale or {}).get("error", {}).get("code") == -32801, f"got: {stale}")
    # It inherited the hover's 'changed later' mark: a loop-body local that was
    # declared but not assigned keeps the previous pass's value.
    no_document = c.response_to("no-document", timeout=60)
    log_result("a request that names no document is answered, not ContentModified",
               no_document is not None and "result" in no_document, f"got: {no_document}")
    c.notify("textDocument/didClose", {"textDocument": {"uri": busy}})
    c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_multi_root(obr, server_obe, apis_json, bin_dir):
    """Each workspace folder is configured by its own build.json (#1232).

    The server kept one root and one file list, and each folder overwrote the
    last: with two folders open, the first was analyzed against the second's
    files. A removed folder was only logged, and a watched build.json change
    reloaded whichever folder had been configured last.
    """
    work = tempfile.mkdtemp(prefix="objk-lsp-multi-")
    try:
        folders = {}
        for name in ("ws_multi_a", "ws_multi_b"):
            shutil.copytree(os.path.join(TESTS_DIR, name), os.path.join(work, name))
            folders[name] = path_to_uri(os.path.join(work, name))
        a_main = folders["ws_multi_a"] + "/main.obs"
        b_main = folders["ws_multi_b"] + "/main.obs"

        def errors(diags):
            return [d.get("message") for d in diags or [] if d.get("severity") == 1]

        def fresh_diagnostics(uri, timeout=60):
            """The next publish for uri after the notifications seen so far."""
            end = time.time() + timeout
            while time.time() < end:
                for n in c.notifications:
                    if (n.get("method") == "textDocument/publishDiagnostics"
                            and n["params"].get("uri", "").lower() == uri.lower()):
                        c.notifications.remove(n)
                        return n["params"].get("diagnostics", [])
                c.drain(0.2)
            return None

        c = LspClient(obr, server_obe, apis_json, bin_dir)
        try:
            c.request("initialize", {
                "processId": os.getpid(), "rootUri": folders["ws_multi_a"], "capabilities": {},
                "workspaceFolders": [{"uri": folders["ws_multi_a"], "name": "a"},
                                     {"uri": folders["ws_multi_b"], "name": "b"}],
            }, timeout=90)
            c.notify("initialized", {})
            for uri in (a_main, b_main):
                with open(os.path.join(work, uri.split("/")[-2], "main.obs"), encoding="utf-8") as fh:
                    c.notify("textDocument/didOpen", {"textDocument": {
                        "uri": uri, "languageId": "objeck", "version": 1, "text": fh.read()}})
            a_diags, b_diags = fresh_diagnostics(a_main), fresh_diagnostics(b_main)
            log_result("two folders: the first resolves against its own build.json",
                       a_diags is not None and not errors(a_diags), f"{errors(a_diags)}")
            log_result("two folders: and so does the second",
                       b_diags is not None and not errors(b_diags), f"{errors(b_diags)}")

            found = {s.get("name") for s in result_of(c.request(
                "workspace/symbol", {"query": "Main"})) or []}
            log_result("workspace/symbol searches every folder",
                       {"AlphaMain", "BetaMain"} <= found, f"got: {sorted(found)}")

            # a removed folder no longer configures its documents
            c.notifications.clear()
            c.notify("workspace/didChangeWorkspaceFolders", {"event": {
                "added": [], "removed": [{"uri": folders["ws_multi_b"], "name": "b"}]}})
            b_diags = fresh_diagnostics(b_main)
            log_result("a removed folder's document is analyzed without its build.json",
                       any("BetaShape" in str(m) for m in errors(b_diags)), f"{errors(b_diags)}")
            a_diags = fresh_diagnostics(a_main)
            log_result("and the remaining folder still resolves",
                       a_diags is not None and not errors(a_diags), f"{errors(a_diags)}")

            c.notifications.clear()
            c.notify("workspace/didChangeWorkspaceFolders", {"event": {
                "added": [{"uri": folders["ws_multi_b"], "name": "b"}], "removed": []}})
            b_diags = fresh_diagnostics(b_main)
            log_result("adding it back configures it again",
                       b_diags is not None and not errors(b_diags), f"{errors(b_diags)}")

            # a changed build.json reloads its own folder, not the other
            with open(os.path.join(work, "ws_multi_a", "build.json"), "w", encoding="utf-8") as fh:
                json.dump({"files": ["main.obs"], "libs": ["gen_collect.obl"], "flags": ""}, fh)
            c.notifications.clear()
            c.notify("workspace/didChangeWatchedFiles", {"changes": [
                {"uri": folders["ws_multi_a"] + "/build.json", "type": 2}]})
            a_diags = fresh_diagnostics(a_main)
            log_result("a changed build.json reloads its folder",
                       any("AlphaShape" in str(m) for m in errors(a_diags)), f"{errors(a_diags)}")
            b_diags = fresh_diagnostics(b_main)
            log_result("and leaves the other folder as it was",
                       b_diags is not None and not errors(b_diags), f"{errors(b_diags)}")
        finally:
            c.close()
    finally:
        shutil.rmtree(work, ignore_errors=True)


def check_jsonrpc_conformance(obr, server_obe, apis_json, bin_dir):
    """The base protocol, not the language features (#1215)."""
    init_params = {"processId": os.getpid(), "rootUri": None, "capabilities": {}}

    # --- before initialize, and the framing --------------------------------
    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        early = c.request("workspace/symbol", {"query": "x"}, timeout=20)
        log_result("a request before initialize gets ServerNotInitialized",
                   (early or {}).get("error", {}).get("code") == -32002, f"got: {early}")

        # Content-Type is a legal header, and some clients send it first
        body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                           "params": init_params}).encode()
        c.send_raw(body, [b"Content-Type: application/vscode-jsonrpc; charset=utf-8"])
        init = c.response_to(1, timeout=60)
        log_result("a Content-Type header before Content-Length is accepted",
                   bool((init or {}).get("result", {}).get("capabilities")), f"got: {init}")

        # no dynamicRegistration in these capabilities: nothing may be registered
        c.notify("initialized", {})
        c.drain(1.5)
        log_result("no registerCapability for a client that cannot take one",
                   not c.server_requests, f"got: {c.server_requests}")

        # ids are 'integer | string' and come back unchanged
        c.send_raw(json.dumps({"jsonrpc": "2.0", "id": "abc-7", "method": "workspace/symbol",
                               "params": {"query": "x"}}).encode())
        sid = c.response_to("abc-7", timeout=20)
        log_result("a string request id is echoed back unchanged", sid is not None,
                   "no response carrying id 'abc-7'")

        unknown = c.request("textDocument/codeLens", {"textDocument": {"uri": "file:///x.obs"}})
        log_result("an unknown request gets MethodNotFound",
                   (unknown or {}).get("error", {}).get("code") == -32601, f"got: {unknown}")

        c.send_raw(b"{not json")
        bad = c.response_to(None, timeout=20)
        log_result("a body that is not JSON gets ParseError",
                   (bad or {}).get("error", {}).get("code") == -32700, f"got: {bad}")
        alive = c.request("workspace/symbol", {"query": "x"})
        log_result("and the session carries on", alive is not None and "result" in alive)

        # --- shutdown: only exit is valid afterwards --------------------------
        down = c.request("shutdown")
        log_result("shutdown answers null", down is not None and down.get("result") is None,
                   f"got: {down}")
        after = c.request("workspace/symbol", {"query": "x"})
        log_result("a request after shutdown gets InvalidRequest",
                   (after or {}).get("error", {}).get("code") == -32600, f"got: {after}")
    finally:
        try:
            c.notify("exit")
            c.sock.close()
        except OSError:
            pass
        c.proc.kill()

    # --- dynamic registration waits for 'initialized' -------------------------
    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        c.request("initialize", dict(init_params, capabilities={"workspace": {
            "didChangeWatchedFiles": {"dynamicRegistration": True}}}), timeout=90)
        c.drain(1.5)
        log_result("registerCapability is not sent before 'initialized'",
                   not c.server_requests, f"got: {c.server_requests}")
        c.notify("initialized", {})
        c.drain(1.5)
        regs = [r for r in c.server_requests if r.get("method") == "client/registerCapability"]
        log_result("and is sent after it, to a client that supports it", len(regs) == 1,
                   f"got: {c.server_requests}")
        if regs:
            # the client's reply is a response, not a request to be answered
            c._write({"jsonrpc": "2.0", "id": regs[0]["id"], "result": None})
            c.drain(1.0)
            alive = c.request("workspace/symbol", {"query": "x"})
            log_result("the client's reply to it is not treated as a request",
                       alive is not None and "result" in alive, f"got: {alive}")
    finally:
        c.close()

    # --- exit codes, over stdio (the process is the client's to end) ---------
    for label, shutdown_first, expected in (("after shutdown", True, 0),
                                            ("without shutdown", False, 1)):
        rc = stdio_exit_code(obr, server_obe, apis_json, bin_dir, shutdown_first)
        log_result(f"exit {label} ends the process with {expected}", rc == expected,
                   f"exit code {rc}")

    if os.name == "nt":
        check_pipe_transport(obr, server_obe, apis_json, bin_dir)


def frame(obj):
    body = json.dumps(obj).encode("utf-8")
    return b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n", body


def stdio_exit_code(obr, server_obe, apis_json, bin_dir, shutdown_first):
    env = make_env(bin_dir)
    env["OBJECK_STDIO"] = "binary"
    proc = subprocess.Popen([obr, server_obe, apis_json, "stdio"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            cwd=bin_dir, env=env)
    try:
        msgs = [{"jsonrpc": "2.0", "id": 1, "method": "initialize",
                 "params": {"processId": os.getpid(), "rootUri": None, "capabilities": {}}},
                {"jsonrpc": "2.0", "method": "initialized", "params": {}}]
        if shutdown_first:
            msgs.append({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
        msgs.append({"jsonrpc": "2.0", "method": "exit"})
        for m in msgs:
            head, body = frame(m)
            proc.stdin.write(head + body)
        proc.stdin.flush()
        threading.Thread(target=proc.stdout.read, daemon=True).start()
        return proc.wait(timeout=90)
    except (subprocess.TimeoutExpired, OSError) as e:
        proc.kill()
        return f"no exit ({type(e).__name__})"


def check_pipe_transport(obr, server_obe, apis_json, bin_dir):
    """VS Code's transport on Windows: a message-mode named pipe.

    vscode-jsonrpc writes the header and the body separately, and a 56 KB
    document is far over the VM's 8 KB pipe read. The server read the pipe as
    if each read were one whole message, so the first lost every body and the
    second lost the document (#1215).
    """
    pipe_name = r"\\.\pipe\objk-pipe"
    proc = subprocess.Popen([obr, server_obe, apis_json, "pipe"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, cwd=bin_dir, env=make_env(bin_dir))
    f = None
    try:
        end = time.time() + 60
        while time.time() < end and f is None:
            try:
                f = open(pipe_name, "r+b", buffering=0)
            except OSError:
                time.sleep(0.5)
        if f is None:
            log_result("named pipe: server listens", False, pipe_name)
            return

        pending = [b""]

        def read_until(pred, timeout):
            # a synchronous pipe handle cannot read and write at once, so reads
            # happen only here, one at a time
            stop = time.time() + timeout
            while time.time() < stop:
                while b"\r\n\r\n" in pending[0]:
                    head, rest = pending[0].split(b"\r\n\r\n", 1)
                    n = int(head.split(b"Content-Length: ")[1].split(b"\r\n")[0])
                    if len(rest) < n:
                        break
                    pending[0] = rest[n:]
                    msg = json.loads(rest[:n])
                    if pred(msg):
                        return msg
                got = [None]
                t = threading.Thread(target=lambda: got.__setitem__(0, f.read(65536)), daemon=True)
                t.start()
                t.join(max(0.1, stop - time.time()))
                if t.is_alive() or not got[0]:
                    return None
                pending[0] += got[0]
            return None

        def send(obj):
            head, body = frame(obj)
            f.write(head)       # two writes, as vscode-jsonrpc does
            f.write(body)

        send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
              "params": {"processId": os.getpid(), "rootUri": None, "capabilities": {}}})
        log_result("named pipe: initialize with header and body written separately",
                   read_until(lambda m: m.get("id") == 1, 60) is not None)
        send({"jsonrpc": "2.0", "method": "initialized", "params": {}})

        path = os.path.join(REPO_ROOT, "core", "compiler", "lib_src", "json.obs")
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        uri = path_to_uri(path)
        send({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {
            "uri": uri, "languageId": "objeck", "version": 1, "text": text}}})
        diag = read_until(lambda m: m.get("method") == "textDocument/publishDiagnostics", 60)
        log_result("named pipe: a document larger than one pipe read arrives whole",
                   diag is not None, f"{len(text)} characters")
    finally:
        if f is not None:
            try:
                f.close()
            except OSError:
                pass
        proc.kill()


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
        # Offer both encodings: where a Char is a code point (Linux, macOS) the
        # server then answers in utf-32, which is exact there (#1216, #1226).
        resp = c.request("initialize", {
            "processId": os.getpid(),
            "rootUri": None,
            "capabilities": {"general": {"positionEncodings": ["utf-16", "utf-32"]},
                             "textDocument": {"documentSymbol": {
                                 "hierarchicalDocumentSymbolSupport": True}}},
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

        # serverInfo: clients display this, and it is how a stale deployed
        # server gets noticed instead of silently answering old results.
        info = (result_of(resp) or {}).get("serverInfo") or {}
        log_result("reports serverInfo name and version",
                   bool(info.get("name")) and bool(info.get("version")),
                   f"got: {info}")

        # ...and it must be the CURRENT version. A note in server.obs asked for
        # a manual bump alongside version.h and was ignored through two
        # releases, leaving the server introducing itself as 2026.8.0. A
        # reminder nothing reads is not a reminder; this is what reads it.
        expected = None
        try:
            with open(os.path.join(REPO_ROOT, "core", "shared", "version.h"),
                      encoding="utf-8", errors="replace") as fh:
                m = re.search(r'VERSION_STRING\s+L"([0-9.]+)"', fh.read())
                expected = m.group(1) if m else None
        except OSError:
            pass
        log_result("serverInfo version matches core/shared/version.h",
                   expected is not None and info.get("version") == expected,
                   f"server said {info.get('version')!r}, version.h says {expected!r}")

        # codeActionProvider must name its kinds, otherwise clients cannot
        # filter them or route "Organize Imports" to the server.
        kinds = caps.get("codeActionProvider")
        kinds = kinds.get("codeActionKinds") if isinstance(kinds, dict) else None
        log_result("codeActionProvider declares codeActionKinds",
                   isinstance(kinds, list) and "quickfix" in kinds,
                   f"got: {kinds}")

        retrigger = caps.get("signatureHelpProvider", {}).get("retriggerCharacters")
        log_result("signatureHelp declares retriggerCharacters",
                   isinstance(retrigger, list) and len(retrigger) > 0,
                   f"got: {retrigger}")

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
        fmt = result_of(c.request(
            "textDocument/formatting",
            {"textDocument": {"uri": probe_uri},
             "options": {"tabSize": 4, "insertSpaces": False}}))
        log_result("textDocument/formatting", bool(fmt))
        check_formatting_applies(fmt, probe_text, "lsp_probe.obs")
        check_formatting_literals(c)
        log_result("textDocument/foldingRange", non_empty(c.request(
            "textDocument/foldingRange", {"textDocument": {"uri": probe_uri}})))
        log_result("textDocument/selectionRange", non_empty(c.request(
            "textDocument/selectionRange",
            {"textDocument": {"uri": probe_uri},
             "positions": [circle_decl["position"]]})))

        # Several positions: each answer is its own chain. Each was chained onto
        # the previous position's, because a loop-body local was declared but not
        # assigned and so kept its last value.
        first_pos = at("return 3.14159")["position"]
        second_pos = at("return @side * @side")["position"]
        chains = result_of(c.request("textDocument/selectionRange", {
            "textDocument": {"uri": probe_uri}, "positions": [first_pos, second_pos]})) or []

        def holds(rng, pos):
            return ((rng["start"]["line"], rng["start"]["character"]) <= (pos["line"], pos["character"])
                    <= (rng["end"]["line"], rng["end"]["character"]))

        def chain_ok(node, pos):
            while node:
                if not holds(node["range"], pos):
                    return False
                node = node.get("parent")
            return True
        log_result("selectionRange answers each position with its own chain",
                   len(chains) == 2 and chain_ok(chains[0], first_pos) and chain_ok(chains[1], second_pos),
                   f"got {len(chains)} chains")
        hints_resp = c.request(
            "textDocument/inlayHint",
            {"textDocument": {"uri": probe_uri},
             "range": {"start": {"line": 0, "character": 0},
                       "end": {"line": probe_text.count("\n"), "character": 0}}})
        log_result("textDocument/inlayHint", non_empty(hints_resp))

        # SHAPE, not just presence. The server wrapped the coordinates in a
        # "start" object -- InlayHint.position is a Position, not a Range -- so
        # every client dropped every hint while a non-empty check still passed.
        hints = result_of(hints_resp) or []
        first = hints[0] if isinstance(hints, list) and hints else {}
        pos = first.get("position") if isinstance(first, dict) else None
        log_result("inlayHint position is a Position, not a Range",
                   isinstance(pos, dict) and "line" in pos and "character" in pos
                   and "start" not in pos,
                   f"got: {pos}")

        # --- codeAction without context.only ---------------------------------
        # LSP makes context.only OPTIONAL, and VS Code omits it for an ordinary
        # lightbulb. The handler used to REQUIRE it -- and require an array of
        # exactly one -- so the common request returned null and quick fixes were
        # unreachable from the editor while codeActionProvider was advertised.
        ca_obs = os.path.join(TESTS_DIR, "lsp_codeaction.obs")
        with open(ca_obs, encoding="utf-8") as fh:
            ca_text = fh.read()
        ca_uri = path_to_uri(ca_obs)
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": ca_uri, "languageId": "objeck", "version": 1, "text": ca_text}})
        ca_diags = c.diagnostics_for(ca_uri, timeout=60) or []
        undefined = next((d for d in ca_diags
                          if str(d.get("message", "")).startswith("Undefined class")), None)
        log_result("fixture reports an Undefined class diagnostic",
                   undefined is not None, f"got: {[d.get('message') for d in ca_diags]}")
        if undefined:
            fixes = result_of(c.request("textDocument/codeAction", {
                "textDocument": {"uri": ca_uri},
                "range": undefined["range"],
                "context": {"diagnostics": [undefined]},   # no "only": the VS Code shape
            }))
            # The assertion is that the handler RAN: it now returns an array
            # (possibly empty) where it used to leave 'response' unassigned and
            # answer JSON null. Whether a fix is offered additionally depends on
            # the class-to-bundle index resolving the name, which this gate does
            # not control -- asserting on a fix would test that instead.
            log_result("codeAction runs when context.only is absent",
                       isinstance(fixes, list),
                       f"got: {fixes!r} (None means the null-answering path)")

            # A restriction that includes quickfix must still work.
            fixes_only = result_of(c.request("textDocument/codeAction", {
                "textDocument": {"uri": ca_uri},
                "range": undefined["range"],
                "context": {"diagnostics": [undefined], "only": ["quickfix"]},
            }))
            log_result("codeAction still honours an explicit quickfix filter",
                       isinstance(fixes_only, list), f"got: {fixes_only!r}")

            # ...and a filter that excludes quickfix must NOT be served.
            fixes_other = result_of(c.request("textDocument/codeAction", {
                "textDocument": {"uri": ca_uri},
                "range": undefined["range"],
                "context": {"diagnostics": [undefined], "only": ["refactor.extract"]},
            }))
            log_result("codeAction declines a filter that excludes quickfix",
                       fixes_other is None, f"got: {fixes_other!r}")

        # --- what the quick fix actually PRODUCES (#1055) --------------------
        # The case above asserts only that the handler ran. The fixtures below
        # assert what it PRODUCES, for both declaration styles.
        #
        # `v := Vector->New()` used to yield no fixes at all, so the applied
        # edits -- a 'use' insert, a variable qualification, a generated build
        # file -- had no coverage in the commonest style, which is how #1052
        # shipped a fix naming the wrong bundle for twenty-two class names. Two
        # things were wrong (#1057): the class name was taken from between the
        # FIRST and LAST quote of the message, which mangles the inferred
        # form's message because it carries a bundle hint with more quotes in
        # it; and an inferred local has no symbol-table entry to match, because
        # the entry is created during type inference and analysis aborts on this
        # very undefined class.
        def fix_titles(obs_name):
            obs = os.path.join(TESTS_DIR, obs_name)
            with open(obs, encoding="utf-8") as fh:
                text = fh.read()
            uri = path_to_uri(obs)
            c.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
            diags = c.diagnostics_for(uri, timeout=60) or []
            undef = next((d for d in diags
                          if str(d.get("message", "")).startswith("Undefined class")), None)
            if undef is None:
                return None, [d.get("message") for d in diags]
            got = result_of(c.request("textDocument/codeAction", {
                "textDocument": {"uri": uri},
                "range": undef["range"],
                "context": {"diagnostics": [undef]},
            })) or []
            return [f.get("title", "") for f in got if isinstance(f, dict)], None

        def qualify_edit(obs_name):
            """Where a 'Fully qualify' fix would insert, and the source line.

            Returns (line, character, inserted_text, source_line) or None. The
            fix bundles the build-file creation edit when no build.json exists,
            so the qualifier is picked out as the short single-line insert
            ending in a dot.
            """
            obs = os.path.join(TESTS_DIR, obs_name)
            with open(obs, encoding="utf-8") as fh:
                text = fh.read()
            lines = text.split("\n")
            uri = path_to_uri(obs)
            c.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
            diags = c.diagnostics_for(uri, timeout=60) or []
            undef = next((d for d in diags
                          if str(d.get("message", "")).startswith("Undefined class")), None)
            if undef is None:
                return None
            got = result_of(c.request("textDocument/codeAction", {
                "textDocument": {"uri": uri},
                "range": undef["range"],
                "context": {"diagnostics": [undef]},
            })) or []
            for f in got:
                if not isinstance(f, dict) or not str(f.get("title", "")).startswith("Fully qualify"):
                    continue
                for change in f.get("edit", {}).get("documentChanges", []):
                    if not isinstance(change, dict):
                        continue
                    for e in change.get("edits", []) or []:
                        rng = e.get("range", {}).get("start", {})
                        new_text = e.get("newText", "")
                        if "line" not in rng or "\n" in new_text or not new_text.endswith("."):
                            continue
                        ln = rng["line"]
                        src = lines[ln] if ln < len(lines) else ""
                        return ln, rng.get("character"), new_text, src
            return None

        titles, why = fix_titles("lsp_codeaction_declared.obs")
        log_result("a declared undefined class reports its diagnostic",
                   titles is not None, f"diagnostics: {why}")
        if titles is not None:
            log_result("the quick fix names the bundle that resolves the class",
                       any("use Collection;" in t for t in titles), f"titles: {titles}")
            log_result("and offers to qualify the variable with it",
                       any("Collection.Vector" in t for t in titles), f"titles: {titles}")

        # --- what the edits and messages carry is plain text (#1213) ---------
        # The server pre-escaped these before a serializer that escapes on its
        # own, so the 'use' fix inserted a literal backslash-r-backslash-n into
        # the user's file and multi-line diagnostics showed literal escapes.
        obs = os.path.join(TESTS_DIR, "lsp_codeaction.obs")
        with open(obs, encoding="utf-8") as fh:
            text = fh.read()
        uri = path_to_uri(obs)
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
        diags = c.diagnostics_for(uri, timeout=60) or []
        log_result("diagnostic messages carry no literal escapes",
                   bool(diags) and not any(chr(92) in str(d.get("message", "")) for d in diags),
                   f"messages: {[d.get('message') for d in diags][:3]}")
        undef = next((d for d in diags
                      if str(d.get("message", "")).startswith("Undefined class")), None)
        use_texts = []
        if undef is not None:
            for f in result_of(c.request("textDocument/codeAction", {
                    "textDocument": {"uri": uri}, "range": undef["range"],
                    "context": {"diagnostics": [undef]}})) or []:
                if isinstance(f, dict) and "use Collection;" in str(f.get("title", "")):
                    for change in f.get("edit", {}).get("documentChanges", []):
                        for e in (change.get("edits", []) if isinstance(change, dict) else []):
                            if str(e.get("newText", "")).startswith("use "):
                                use_texts.append(e["newText"])
        log_result("the 'use' fix inserts a real line break",
                   bool(use_texts) and all(t.endswith("\n") and chr(92) not in t for t in use_texts),
                   f"newText: {use_texts!r}")

        # With no workspace folder the build file goes beside the document. It
        # was 'file:////build.json' -- the root of the file system -- because a
        # null rootUri configured a folder at '', and the path was lowercased.
        creates = []
        if undef is not None:
            for f in result_of(c.request("textDocument/codeAction", {
                    "textDocument": {"uri": uri}, "range": undef["range"],
                    "context": {"diagnostics": [undef]}})) or []:
                for change in (f.get("edit", {}).get("documentChanges", []) if isinstance(f, dict) else []):
                    if isinstance(change, dict) and change.get("kind") == "create":
                        creates.append(change.get("uri"))
        want = path_to_uri(TESTS_DIR) + "/build.json"
        log_result("a quick fix outside any folder creates build.json beside the file",
                   bool(creates) and all(u == want for u in creates), f"{set(creates)} vs {want}")

        # --- the same, for an inferred-type local (#1057) --------------------
        # `:=` is the dominant style in this codebase -- the examples, the
        # libraries and the regression suite are written almost entirely with
        # it -- so a quick fix that only worked for an explicit declaration was
        # effectively unavailable.
        inf_titles, inf_why = fix_titles("lsp_codeaction.obs")
        log_result("an inferred-type local reports its diagnostic",
                   inf_titles is not None, f"diagnostics: {inf_why}")
        if inf_titles is not None:
            log_result("an inferred-type local is offered the 'use' fix",
                       any("use Collection;" in t for t in inf_titles),
                       f"titles: {inf_titles}")
            log_result("and is offered the qualification too",
                       any("Collection.Vector" in t for t in inf_titles),
                       f"titles: {inf_titles}")

        # The assertion that matters: the qualifier must be inserted
        # immediately before the CLASS token, or applying the fix produces code
        # that still does not compile -- which is what #1052 was. The inferred
        # form's position comes from the diagnostic's own range, and only half
        # of these diagnostics are anchored on the class across other shapes,
        # so this pins the anchoring rather than trusting it.
        for fixture in ("lsp_codeaction.obs", "lsp_codeaction_declared.obs"):
            spot = qualify_edit(fixture)
            if spot is None:
                log_result(f"{fixture}: a qualification edit is offered", False, "none found")
                continue
            ln, char, new_text, src = spot
            after = src[char:] if char is not None else ""
            log_result(f"{fixture}: the qualifier lands on the class token",
                       after.startswith("Vector"),
                       f"line {ln} char {char} inserts {new_text!r} before {after[:24]!r}")
            applied = src[:char] + new_text + src[char:]
            log_result(f"{fixture}: applying it qualifies the class",
                       "Collection.Vector" in applied, f"would read: {applied.strip()!r}")

        # --- every shape, including the ones that must be DECLINED (#1057) ---
        # The gate that stops the inferred-type fallback firing where the
        # diagnostic is not on the class token is the part a later change drops
        # silently, so it is asserted directly. Two of this fixture's ten
        # diagnostics sit on a method name and on a `return` line; offering a
        # qualifier there would insert "Collection." in front of them and leave
        # the program still not compiling.
        def all_qualify_edits(obs_name):
            obs = os.path.join(TESTS_DIR, obs_name)
            with open(obs, encoding="utf-8") as fh:
                text = fh.read()
            lines = text.split("\n")
            uri = path_to_uri(obs)
            c.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "objeck", "version": 1, "text": text}})
            diags = c.diagnostics_for(uri, timeout=60) or []
            undefs = [d for d in diags
                      if str(d.get("message", "")).startswith("Undefined class")]
            out = []
            for d in undefs:
                got = result_of(c.request("textDocument/codeAction", {
                    "textDocument": {"uri": uri},
                    "range": d["range"],
                    "context": {"diagnostics": [d]},
                })) or []
                spot = None
                for f in got:
                    if not isinstance(f, dict) or not str(f.get("title", "")).startswith("Fully qualify"):
                        continue
                    for change in f.get("edit", {}).get("documentChanges", []):
                        if not isinstance(change, dict):
                            continue
                        for e in change.get("edits", []) or []:
                            rng = e.get("range", {}).get("start", {})
                            new_text = e.get("newText", "")
                            if "line" not in rng or "\n" in new_text or not new_text.endswith("."):
                                continue
                            spot = (rng["line"], rng.get("character"), new_text)
                out.append((d["range"]["start"], spot, lines))
            return out

        shapes = all_qualify_edits("lsp_codeaction_shapes.obs")
        log_result("every shape reports its undefined class",
                   len(shapes) >= 8, f"{len(shapes)} diagnostics")

        misplaced = []
        declined = 0
        for start, spot, lines in shapes:
            if spot is None:
                declined += 1
                continue
            ln, char, new_text = spot
            src = lines[ln] if ln < len(lines) else ""
            if not src[char:].startswith("Vector"):
                misplaced.append((ln, char, src.strip()))
        log_result("no qualifier is offered anywhere but on the class token",
                   not misplaced, f"misplaced: {misplaced}")
        log_result("the shapes that cannot be served offer no qualification",
                   declined >= 2, f"{declined} declined of {len(shapes)}")

        # --- a name defined in several bundles (#1052) -----------------------
        # Offering one of them is the old behaviour, and it looked correct.
        amb_titles, amb_why = fix_titles("lsp_codeaction_ambiguous.obs")
        log_result("an ambiguous undefined class reports its diagnostic",
                   amb_titles is not None, f"diagnostics: {amb_why}")
        if amb_titles:
            log_result("a name defined in two bundles offers a fix for each",
                       any("API.Inference" in t for t in amb_titles)
                       and any("Data.JSON.RPC" in t for t in amb_titles),
                       f"titles: {amb_titles}")
            log_result("and those fixes are distinguishable",
                       len(amb_titles) == len(set(amb_titles)) and len(amb_titles) > 1,
                       f"titles: {amb_titles}")

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

        # rename a receiver used by method-call STATEMENTS on a generic
        # collection. Reported against programs/tests/prgm99.obs: renaming the
        # declaration updated the declaration and the trailing Each() call but
        # left the three Insert() statements on the old name, so the rename
        # produced source that no longer compiles.
        #
        # Both classes in the fixture are identical apart from boxing: the
        # IntRef-keyed map boxes its int literal arguments, the String-keyed one
        # does not. Asserting them separately says WHICH layer is wrong rather
        # than just that something is.
        gm_obs = os.path.join(TESTS_DIR, "test_generic_map.obs")
        with open(gm_obs, encoding="utf-8") as f:
            gm_text = f.read()
        gm_uri = path_to_uri(gm_obs)
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": gm_uri, "languageId": "objeck", "version": 1,
            "text": gm_text}})
        c.diagnostics_for(gm_uri, timeout=60)

        # 5 occurrences each: declaration + 3 Insert receivers + 1 Each receiver
        for var, needle, note in (("boxed", "boxed := Map", "boxed (IntRef keys)"),
                                  ("plain", "plain := Map", "plain (String keys)")):
            decl_pos = position_in(gm_text, gm_uri, needle)
            ren = result_of(c.request("textDocument/rename",
                                      dict(decl_pos, newName="renamed")))
            edits = (ren or {}).get("documentChanges", [{}])[0].get("edits", [])
            lines = sorted({e["range"]["start"]["line"] for e in edits})
            log_result(f"rename edits every receiver — {note}",
                       len(edits) == 5,
                       f"got {len(edits)} edits on lines {lines}; "
                       f"missing Insert receivers leaves uncompilable source")

        # renaming from a USE (an Insert receiver) must reach the declaration
        # and the siblings too, not just the line under the cursor
        use_pos = position_in(gm_text, gm_uri, "boxed->Insert(3")
        ren = result_of(c.request("textDocument/rename",
                                  dict(use_pos, newName="renamed")))
        edits = (ren or {}).get("documentChanges", [{}])[0].get("edits", [])
        log_result("rename from an Insert receiver edits all 5 occurrences",
                   len(edits) == 5, f"got {len(edits)} edits")

        # references must agree with rename — they share GetExpressionsCalls(),
        # so a miss in one is a miss in the other
        refs = result_of(c.request("textDocument/references", dict(
            position_in(gm_text, gm_uri, "boxed := Map"),
            context={"includeDeclaration": True}))) or []
        log_result("references finds the same 5 occurrences rename does",
                   len(refs) == 5, f"got {len(refs)}")

        # chained user-defined call: LocateExpression's chain descent must
        # resolve the SECOND call's method name (plain chains, not just nil-safe)
        chain_pos = position_in(ns_text, ns_uri, "Get();", 1, 1)
        decl = result_of(c.request("textDocument/declaration", chain_pos))
        get_line, _ = find_pos(ns_text, "method : public : Get()")
        decl_line = (decl or {}).get("range", {}).get("start", {}).get("line", -1)
        log_result("declaration on a chained call resolves the chained method",
                   decl_line == get_line, f"got: {decl}")

        check_completion_docs(c, caps, probe_text, probe_uri)
        check_rename_and_organize(c, probe_text, probe_uri)
        check_document_symbol_tree(c, probe_text, probe_uri)
        check_large_real_file(c)
        check_text_encoding(c, caps)
        check_batching(c)
    finally:
        c.close()

    check_jsonrpc_conformance(obr, server_obe, apis_json, bin_dir)
    check_multi_root(obr, server_obe, apis_json, bin_dir)

    # --- rootUri-only session (Kate/ecode-style clients) -----------------------
    # No workspaceFolders at all: the rootUri fallback must configure the
    # workspace and the watched-files handler must stay alive.
    c = LspClient(obr, server_obe, apis_json, bin_dir)
    try:
        # ...and a client that renders only plain text (#1228)
        resp = c.request("initialize", {
            "processId": os.getpid(), "rootUri": path_to_uri(TESTS_DIR),
            "capabilities": {"textDocument": {"hover": {"contentFormat": ["plaintext"]},
                                              "rename": {"prepareSupport": True}}},
        }, timeout=90)
        rename_cap = ((result_of(resp) or {}).get("capabilities") or {}).get("renameProvider")
        log_result("a client with prepareSupport is offered prepareRename",
                   rename_cap == {"prepareProvider": True}, f"got: {rename_cap!r}")
        log_result("initialize with rootUri only (no workspaceFolders)",
                   bool((result_of(resp) or {}).get("capabilities")))
        c.notify("initialized", {})
        c.notify("textDocument/didOpen", {"textDocument": {
            "uri": probe_uri, "languageId": "objeck", "version": 1,
            "text": probe_text}})
        c.diagnostics_for(probe_uri, timeout=60)
        c.notify("workspace/didChangeWatchedFiles", {"changes": [
            {"uri": path_to_uri(TESTS_DIR) + "/build.json", "type": 2}]})
        hover = c.request("textDocument/hover",
                          position_in(probe_text, probe_uri, "circle := Circle", 1, 1))
        log_result("rootUri-only session survives didChangeWatchedFiles",
                   non_empty(hover) and c.proc.poll() is None)
        flat = result_of(c.request("textDocument/documentSymbol",
                                   {"textDocument": {"uri": probe_uri}})) or []
        log_result("a client without hierarchical support gets SymbolInformation[]",
                   bool(flat) and all("location" in s and "children" not in s for s in flat),
                   f"got: {json.dumps(flat)[:160]}")
        contents = (result_of(hover) or {}).get("contents")
        log_result("a plain-text client gets plain-text hover",
                   isinstance(contents, dict) and contents.get("kind") == "plaintext",
                   f"got: {contents!r}"[:200])
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
