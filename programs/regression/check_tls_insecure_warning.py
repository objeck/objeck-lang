#!/usr/bin/env python3
"""
OBJECK_TLS_INSECURE_SKIP_VERIFY turns TLS certificate verification off for every
secure client socket in the process; obr says so, once, on stderr.

Usage: check_tls_insecure_warning.py <bin_dir>

The variable is a test aid, but an exported variable outlives the test it was set
for, and until now nothing told anyone that every TLS connection the process
made, API-key clients included, was unauthenticated. The first connection that
reads the setting now prints one warning line.

A plain TCP listener on loopback stands in for a server: it accepts and hangs up,
so the TLS client reaches the point where it reads the setting (the handshake then
fails, which is all this needs). The program makes two connection attempts.
  - variable unset or "0": no warning
  - variable "1": exactly one warning line (not one per connection), naming the
    variable, and the program still runs to completion
"""
import os, socket, subprocess, sys, tempfile, threading

EXE = ".exe" if os.name == "nt" else ""
SOURCE = """class TlsInsecureProbe {
\tfunction : Main(args : String[]) ~ Nil {
\t\tport := args[0]->ToInt();
\t\tfor(i := 0; i < 2; i += 1;) {
\t\t\tsocket := System.IO.Net.TCPSecureSocket->New("127.0.0.1", port);
\t\t\t"attempt {$i}: open={$socket->IsOpen()}"->PrintLine();
\t\t\tsocket->Close();
\t\t};
\t\t"DONE"->PrintLine();
\t}
}
"""
MARKER = "OBJECK_TLS_INSECURE_SKIP_VERIFY is set"


def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=120)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def listener():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(8)
    srv.settimeout(0.2)
    stop = threading.Event()

    def serve():
        while not stop.is_set():
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            conn.close()

    t = threading.Thread(target=serve, daemon=True)
    t.start()
    return srv, stop, t


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    base_env = dict(os.environ)
    base_env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    base_env.pop("OBJECK_TLS_INSECURE_SKIP_VERIFY", None)
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail[-500:]))
        if not cond:
            failures.append(name)

    srv, stop, thread = listener()
    port = srv.getsockname()[1]
    try:
        with tempfile.TemporaryDirectory(prefix="tls-insecure-") as work:
            src = os.path.join(work, "probe.obs")
            with open(src, "w") as h:
                h.write(SOURCE)
            dest = os.path.join(work, "probe.obe")
            code, out, err = run([obc, "-src", src, "-dest", dest], base_env, work)
            check("the probe compiles", code == 0 and os.path.isfile(dest), out + err)
            if code != 0:
                return 2

            for label, value in (("unset", None), ("0", "0")):
                env = dict(base_env)
                if value is not None:
                    env["OBJECK_TLS_INSECURE_SKIP_VERIFY"] = value
                code, out, err = run([obr, dest, str(port)], env, work)
                check("variable %s: the program finishes" % label, code == 0 and "DONE" in out, "exit=%d out=%r err=%r" % (code, out, err))
                check("variable %s: no warning" % label, MARKER not in err, err)

            env = dict(base_env)
            env["OBJECK_TLS_INSECURE_SKIP_VERIFY"] = "1"
            code, out, err = run([obr, dest, str(port)], env, work)
            check("variable 1: the program still finishes", code == 0 and "DONE" in out, "exit=%d out=%r err=%r" % (code, out, err))
            check("variable 1: exactly one warning for two connections", err.count(MARKER) == 1, "count=%d err=%r" % (err.count(MARKER), err))
            check("variable 1: the warning says certificates are not verified", "NOT verified" in err, err)
    finally:
        stop.set()
        srv.close()
        thread.join(2)
    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
