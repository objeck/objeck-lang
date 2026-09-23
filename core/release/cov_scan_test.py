#!/usr/bin/env python3
"""cov_scan.sh's large-submission flow, against a mock Coverity Scan.

Usage: python3 cov_scan_test.py        (run from anywhere; POSIX only)

Above 500 MB the single-shot form POST stops working and cov_scan.sh switches to
Coverity's three-step flow: initialize the build, PUT the tarball at the URL that
comes back, then enqueue it. That path cannot be exercised by a normal run -- the
capture is at about a third of the limit -- so without this it would first
execute on the day it is needed, which is the worst day to find out it is wrong.

The script talks to $COVERITY_API_BASE, so the tests point it at a local server
that speaks the same three endpoints and records what arrived.

What each test would catch:

- the happy path: all three requests, in order, with the archive intact. A flow
  that skipped enqueue would upload the bytes and leave them unanalysed, which
  looks exactly like success everywhere except the project page.
- a malformed init response: the URL is validated before anything is PUT at it.
  Parsing "" and continuing would PUT into nothing and print "Submitted".
- a failing enqueue: reported, not swallowed.
- the archive is KEPT on every failure, so a retry does not need another
  multi-hour rebuild.
"""

import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "cov_scan.sh")

passed = 0
failed = 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print("  [PASS] %s" % name)
    else:
        failed += 1
        print("  [FAIL] %s%s" % (name, (" -- " + detail) if detail else ""))


class Recorder:
    """What the server saw, so the assertions read against evidence."""

    def __init__(self):
        self.calls = []            # (method, path)
        self.uploaded = b""
        self.init_body = ""
        self.enqueue_body = ""


def make_server(rec, init_response, enqueue_status=200):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def _body(self):
            length = int(self.headers.get("Content-Length") or 0)
            return self.rfile.read(length) if length else b""

        def _reply(self, status, payload=b"ok"):
            self.send_response(status)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def do_POST(self):
            rec.calls.append(("POST", self.path))
            if self.path.endswith("/builds/init"):
                rec.init_body = self._body().decode("utf-8", "replace")
                port = self.server.server_address[1]
                body = init_response.replace("{PORT}", str(port)).encode("utf-8")
                self._reply(200, body)
            elif self.path.startswith("/builds"):
                rec.uploaded = self._body()      # the whole multipart form
                self._reply(200)
            else:
                self._reply(404)

        def do_PUT(self):
            rec.calls.append(("PUT", self.path))
            if self.path.startswith("/upload"):
                rec.uploaded = self._body()
                self._reply(200)
            elif self.path.endswith("/enqueue"):
                rec.enqueue_body = self._body().decode("utf-8", "replace")
                self._reply(enqueue_status)
            else:
                self._reply(404)

    server = HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def make_tree(root):
    """The directory shape cov_scan.sh expects: it cds to its own directory and
    reads ../shared/version.h from there."""
    release = os.path.join(root, "release")
    shared = os.path.join(root, "shared")
    os.makedirs(release, exist_ok=True)
    os.makedirs(shared, exist_ok=True)
    with open(os.path.join(shared, "version.h"), "w") as handle:
        handle.write('#define VERSION_STRING L"2026.9.6"\n')
    return release


def make_archive(work):
    """A minimal archive that passes cov_scan.sh's own top-level cov-int/ check."""
    emit = os.path.join(work, "cov-int")
    os.makedirs(emit, exist_ok=True)
    with open(os.path.join(emit, "build-log.txt"), "w") as handle:
        handle.write("mock emit\n")
    archive = os.path.join(work, "objeck-int.tgz")
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(emit, arcname="cov-int")
    return archive


def run_scan(work, base_url, token_file, force_large=True):
    # cov_scan.sh cds to its own directory and looks for the archive there, so
    # the copy under test has to sit beside the fixture rather than in the repo.
    local = os.path.join(work, "cov_scan.sh")
    shutil.copy2(SCRIPT, local)
    env = dict(os.environ)
    env["COVERITY_UPLOAD_ONLY"] = "1"
    if force_large:
        env["COVERITY_FORCE_LARGE"] = "1"
    env["COVERITY_API_BASE"] = base_url
    env["COVERITY_TOKEN_FILE"] = token_file
    return subprocess.run(["sh", local, "x64"], cwd=work, env=env,
                          stdin=subprocess.DEVNULL, timeout=120,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def scenario(name, init_response, enqueue_status=200, force_large=True):
    root = tempfile.mkdtemp(prefix="covtest-")
    work = make_tree(root)
    try:
        archive = make_archive(work)
        original = open(archive, "rb").read()
        token_file = os.path.join(work, "token.dat")
        with open(token_file, "w") as handle:
            handle.write("not-a-real-token\n")

        rec = Recorder()
        server = make_server(rec, init_response, enqueue_status)
        port = server.server_address[1]
        try:
            proc = run_scan(work, "http://127.0.0.1:%d" % port, token_file, force_large)
        finally:
            server.shutdown()

        out = proc.stdout.decode("utf-8", "replace")
        return rec, proc, out, original, archive
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main():
    if os.name == "nt":
        print("POSIX only (cov_scan.sh is a shell script); run under WSL.")
        return 0
    if not os.path.isfile(SCRIPT):
        print("no cov_scan.sh beside this test")
        return 1

    good = '{"url":"http://127.0.0.1:{PORT}/upload/abc?sig=xyz","build_id":4242}'

    print("\nlarge-submission flow, happy path:")
    rec, proc, out, original, archive = scenario("happy", good)
    check("exits 0", proc.returncode == 0, "exit %d\n%s" % (proc.returncode, out))
    check("three requests, in order",
          [c[0] for c in rec.calls] == ["POST", "PUT", "PUT"],
          str(rec.calls))
    check("initialized the build",
          any(c[1].endswith("/projects/10314/builds/init") for c in rec.calls),
          str(rec.calls))
    check("enqueued build 4242",
          any(c[1].endswith("/projects/10314/builds/4242/enqueue") for c in rec.calls),
          str(rec.calls))
    check("the archive arrived intact", rec.uploaded == original,
          "%d bytes uploaded, %d expected" % (len(rec.uploaded), len(original)))
    check("init carried the version and file name",
          "file_name=" in rec.init_body and "version=" in rec.init_body,
          rec.init_body[:120])
    check("enqueue carried the token", "token=" in rec.enqueue_body,
          rec.enqueue_body[:60])
    check("the token was never printed", "not-a-real-token" not in out)
    check("the signed URL was never printed", "sig=xyz" not in out)
    check("reports the submission", "Submitted" in out, out[-300:])

    print("\ninit returns no usable URL:")
    rec, proc, out, _, _ = scenario("no-url", '{"build_id":7}')
    check("fails rather than PUTting into nothing", proc.returncode != 0,
          "exit %d" % proc.returncode)
    check("nothing was uploaded", rec.uploaded == b"")
    check("says the URL was the problem", "upload URL" in out, out[-300:])
    check("does not claim a submission", "Submitted" not in out)
    check("keeps the archive for a retry", "COVERITY_UPLOAD_ONLY=1" in out)

    print("\ninit returns no build_id:")
    rec, proc, out, _, _ = scenario("no-id", '{"url":"http://127.0.0.1:{PORT}/upload/a"}')
    check("fails", proc.returncode != 0, "exit %d" % proc.returncode)
    check("says build_id was the problem", "build_id" in out, out[-300:])
    check("does not claim a submission", "Submitted" not in out)

    print("\nenqueue fails:")
    rec, proc, out, original, _ = scenario("enqueue-500", good, enqueue_status=500)
    check("fails", proc.returncode != 0, "exit %d" % proc.returncode)
    check("the upload still happened", rec.uploaded == original)
    check("names enqueue, not the upload", "enqueue failed" in out, out[-300:])
    check("does not claim a submission", "Submitted" not in out)

    print("\nsingle-shot form flow (under the limit):")
    rec, proc, out, original, _ = scenario("form", good, force_large=False)
    check("exits 0", proc.returncode == 0, "exit %d" % proc.returncode)
    check("one request, a form POST",
          [c[0] for c in rec.calls] == ["POST"] and rec.calls[0][1].startswith("/builds?"),
          str(rec.calls))
    check("did not initialize a build",
          not any(c[1].endswith("/builds/init") for c in rec.calls), str(rec.calls))
    check("the archive is inside the multipart body", original in rec.uploaded,
          "%d bytes posted" % len(rec.uploaded))
    check("reports the size against the limit",
          "of the 500 MB single-shot limit" in out, out[:200])
    check("reports the submission", "Submitted" in out, out[-200:])

    print("\n============================================")
    print("  %d passed, %d failed" % (passed, failed))
    print("============================================")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
