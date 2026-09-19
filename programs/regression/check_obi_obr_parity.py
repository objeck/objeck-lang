#!/usr/bin/env python3
"""
obi and obr must report the same compiled-in protocol support.

Usage: check_obi_obr_parity.py <bin_dir>

<bin_dir> must contain obc, obr and obi (with .exe on Windows); the libraries
are read from the sibling lib/ directory, as run_vm_flag_tests.py does.

obr is the VM build. obi evaluates code in-process through the module build --
also the embedding API -- which is a separate compilation of the VM sources with
its own macros and link line. The module long lacked the HTTP/2 and HTTP/3
macros, so the same program had both protocols under obr and neither under obi,
and nothing noticed (#897). This runs one program under both and fails if their
runtime.feature.http2 / runtime.feature.http3 differ. It checks parity, not
presence: a build that lacks HTTP/3 in both is consistent, and passes.

Three things would let a broken check pass silently, and each one fails here:
- a binary that prints nothing. Both outputs must contain a well-formed
  "http2=<0|1> http3=<0|1>" line, so two empty outputs never "agree".
- obi waiting for input. stdin is closed and every run has a timeout.
- a shell mangling the program. Arguments go as a list, and obi reads the
  program from a file (--file), never from a quoted command line.
"""

import os
import re
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

PROGRAM = """class Features {
  function : Main(args : String[]) ~ Nil {
    h2 := System.Runtime->GetProperty("runtime.feature.http2");
    h3 := System.Runtime->GetProperty("runtime.feature.http3");
    "http2={$h2} http3={$h3}"->PrintLine();
  }
}
"""

LINE = re.compile(r"http2=([01]) http3=([01])")


def run(cmd, env, cwd):
    try:
        p = subprocess.run(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=TIMEOUT)
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        partial = (e.stdout or b"").decode("utf-8", "replace")
        return None, partial + "\n[timed out after %ds]" % TIMEOUT


def fail(msg, detail=""):
    print("FAIL: " + msg)
    if detail:
        for line in detail.rstrip().splitlines():
            print("    | " + line)
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    obi = os.path.join(bin_dir, "obi" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")

    for tool in (obc, obr, obi):
        if not os.path.isfile(tool):
            fail("missing " + tool)

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir + os.sep

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "features.obs")
        obe = os.path.join(tmp, "features.obe")
        with open(src, "w", newline="\n") as f:
            f.write(PROGRAM)

        rc, out = run([obc, "-src", src, "-dest", obe], env, tmp)
        if rc != 0 or not os.path.isfile(obe):
            fail("obc could not compile the probe (exit %s)" % rc, out)

        rc_r, out_r = run([obr, obe], env, tmp)
        rc_i, out_i = run([obi, "--file", src, "--quit"], env, tmp)

    m_r = LINE.search(out_r)
    m_i = LINE.search(out_i)
    if not m_r:
        fail("obr printed no feature line (exit %s)" % rc_r, out_r)
    if not m_i:
        fail("obi printed no feature line (exit %s)" % rc_i, out_i)

    print("obr: http2=%s http3=%s" % m_r.groups())
    print("obi: http2=%s http3=%s" % m_i.groups())

    if m_r.groups() != m_i.groups():
        fail("obi's compiled-in protocols differ from obr's -- the module build is missing "
             "macros or libraries the VM build has (see #897)")
    print("PASS: obi and obr agree on HTTP/2 and HTTP/3")


if __name__ == "__main__":
    main()
