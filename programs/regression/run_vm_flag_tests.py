#!/usr/bin/env python3
"""VM command-line flag tests for obr.

Usage: python run_vm_flag_tests.py <bin_dir>

<bin_dir> must contain obc(.exe) and obr(.exe). Three things are asserted:

 1. Interpreter/JIT equivalence -- vm_jit_equiv.obs prints the same bytes under
    --jit=off, the default, and --jit=1 (compile everything on first call).
    The output is checked to be non-trivial first: an empty-vs-empty comparison
    would pass on a program that failed to run at all.
 2. Values are validated -- --objeck-stdio=1, --jit=banana and
    --gc-threshold=2x each exit non-zero with a message naming what was
    expected. Every one of these used to be silently accepted.
 3. The usage text states valid values for every flag, and is the same on
    every platform (it is generated from one string now).
 4. --lib-path actually reaches the VM's native-library loader. The fixture
    calls Cipher.Hash->SHA256, which lives in libobjk_crypto under
    <lib>/native/. A COPY of obr (with the runtime DLLs it needs on Windows)
    runs from a scratch directory that has no lib beside it, with
    OBJECK_LIB_PATH removed: it must FAIL with nothing pointing at the
    libraries, and pass when the flag (or the variable) does. Two earlier
    versions of this check proved nothing: one used a fixture that loaded no
    native library, the next ran the deployed obr from another cwd -- but
    Windows resolves the ..\lib\native fallback against obr.exe's OWN
    directory first, so the deployed binary finds its libraries from anywhere.
 6. An exception in a call the JIT's bridge made ends the program, not the
    process. Neither backend registers unwind information for the code it
    emits, so a C++ exception thrown under compiled code used to terminate
    obr (SIGABRT) before Execute's catch printed anything; the bridge catches
    it now and reports it as Execute would: the internal-error line, the
    stack, exit status 1. jit_bridge_exception.obs reaches it with
    "1e999"->ToFloat() from a native method (compiled in every mode), by
    default through the interpreter the bridge nests and under --jit=1
    through the bridge's own S2F case. The regression runner cannot tell the
    abort from the error (an expected runtime error is any non-zero exit with
    output), so the status is named here.
"""
import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXE = ".exe" if os.name == "nt" else ""

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  [PASS] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name}: {detail}")


def run(cmd, env=None, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=120)
    return p.returncode, p.stdout, p.stderr


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    for p in (obc, obr):
        if not os.path.exists(p):
            print(f"ERROR: missing {p}")
            return 2

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir + os.sep
    src = os.path.join(SCRIPT_DIR, "vm_jit_equiv.obs")
    obe = os.path.join(SCRIPT_DIR, "vm_jit_equiv.obe")

    print("Objeck VM flag tests")
    print(f"  bin: {bin_dir}\n")

    rc, out, err = run([obc, "-src", src, "-dest", obe], env=env, cwd=bin_dir)
    check("fixture compiles", rc == 0 and os.path.exists(obe), err.decode(errors="replace")[-300:])
    if rc != 0:
        return finish()

    # ---- 1. equivalence -------------------------------------------------------
    runs = {}
    for label, flags in (("default", []), ("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
        rc, out, err = run([obr] + flags + [obe], env=env, cwd=bin_dir)
        runs[label] = (rc, out)
        check(f"program exits 0 under {label}", rc == 0, f"rc={rc} stderr={err.decode(errors='replace')[-200:]}")

    base = runs["default"][1]
    # Non-trivial first, or two failures would compare equal.
    check("program output is non-trivial", b"fib digest=" in base and b"done" in base and len(base) > 200,
          f"got {len(base)} bytes: {base[:120]!r}")
    check("interpreter (--jit=off) output is byte-identical to the JIT'd run",
          runs["jit=off"][1] == base, f"differs; off={runs['jit=off'][1][:200]!r}")
    check("--jit=1 (compile on first call) output is byte-identical",
          runs["jit=1"][1] == base, f"differs; jit=1={runs['jit=1'][1][:200]!r}")

    # ---- 2. validation ----------------------------------------------------------
    for flag, expect in (("--objeck-stdio=1", b"expected 'binary', 'utf16' or 'utf8'"),
                         ("--jit=banana", b"expected 'off' or a positive call count"),
                         ("--jit=0", b"expected 'off' or a positive call count"),
                         ("--gc-threshold=2x", b"expected <number>(k|m|g)")):
        rc, out, err = run([obr, flag, obe], env=env, cwd=bin_dir)
        check(f"{flag} is refused with a message naming what was expected",
              rc != 0 and expect in err, f"rc={rc} stderr={err.decode(errors='replace')[-200:]!r}")

    # A gigabyte is 2^30. The old parser used 2^40, so 1g asked for a terabyte.
    rc, out, err = run([obr, "--gc-threshold=1g", obe], env=env, cwd=bin_dir)
    check("--gc-threshold=1g runs (a gigabyte, not a terabyte)", rc == 0 and out == base,
          f"rc={rc} stderr={err.decode(errors='replace')[-200:]!r}")

    # ---- 3. usage -------------------------------------------------------------
    rc, out, err = run([obr], env=env, cwd=bin_dir)
    usage = (out + err).decode(errors="replace")
    check("no-argument usage exits non-zero", rc != 0, f"rc={rc}")
    for needle in ("--gc-threshold=<size>", "<number>(k|m|g)", "--jit=off|<calls>",
                   "--lib-path=<dir>", "--objeck-stdio=binary|utf16|utf8"):
        check(f"usage states valid values: {needle}", needle in usage, usage[:400])

    # ---- 4. --lib-path reaches the native-library loader ----------------------------
    native_src = os.path.join(SCRIPT_DIR, "vm_lib_path_native.obs")
    native_obe = os.path.join(SCRIPT_DIR, "vm_lib_path_native.obe")
    rc, out, err = run([obc, "-src", native_src, "-lib", "cipher", "-dest", native_obe], env=env, cwd=bin_dir)
    check("native-library fixture compiles", rc == 0 and os.path.exists(native_obe), err.decode(errors="replace")[-300:])
    if rc == 0:
        # A copy of obr in a directory with no lib beside it: the exe-relative
        # fallback has nowhere to go, so only the flag or the variable can
        # supply the library directory.
        scratch = tempfile.mkdtemp(prefix="objeck-libpath-")
        scratch_bin = os.path.join(scratch, "bin")
        os.mkdir(scratch_bin)
        shutil.copy2(obr, scratch_bin)
        for entry in os.listdir(bin_dir):
            if entry.lower().endswith(".dll"):
                shutil.copy2(os.path.join(bin_dir, entry), scratch_bin)
        scratch_obr = os.path.join(scratch_bin, os.path.basename(obr))
        try:
            bare = {k: v for k, v in os.environ.items() if k != "OBJECK_LIB_PATH"}
            rc, out, err = run([scratch_obr, native_obe], env=bare, cwd=scratch_bin)
            check("without --lib-path or OBJECK_LIB_PATH the native library cannot be loaded (control)",
                  rc != 0 and b"native lib loaded" not in out,
                  f"rc={rc} out={out[-120:]!r} -- the control passed, so the flag test below proves nothing")
            rc, out, err = run([scratch_obr, "--lib-path=" + lib_dir, native_obe], env=bare, cwd=scratch_bin)
            check("--lib-path=<lib root> lets obr load <lib>/native/libobjk_crypto",
                  rc == 0 and b"native lib loaded" in out,
                  f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-200:]!r}")
            with_env = dict(bare)
            with_env["OBJECK_LIB_PATH"] = lib_dir
            rc, out, err = run([scratch_obr, native_obe], env=with_env, cwd=scratch_bin)
            check("OBJECK_LIB_PATH=<lib root> does the same (the flag and the variable agree)",
                  rc == 0 and b"native lib loaded" in out,
                  f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-200:]!r}")
        finally:
            shutil.rmtree(scratch, ignore_errors=True)

    # ---- 6. an exception in a bridge call ends the program, not the process ----
    # The fixture's Parse is native, so both runs reach the bridge: by default
    # String->ToFloat, a library method called once, runs in the interpreter the
    # bridge nests; under --jit=1 it is compiled and the bridge's own S2F case
    # throws. On the old VM both aborted (a signal, no line of the VM's), and
    # the regression runner counted the abort as the expected error.
    ex_src = os.path.join(SCRIPT_DIR, "jit_bridge_exception.obs")
    ex_obe = os.path.join(SCRIPT_DIR, "jit_bridge_exception.obe")
    rc, out, err = run([obc, "-src", ex_src, "-dest", ex_obe], env=env, cwd=bin_dir)
    check("bridge-exception fixture compiles", rc == 0 and os.path.exists(ex_obe), err.decode(errors="replace")[-300:])
    if rc == 0:
        for label, flags in (("default", []), ("jit=1", ["--jit=1"])):
            rc, out, err = run([obr] + flags + [ex_obe], env=env, cwd=bin_dir)
            detail = f"rc={rc} out={out[-80:]!r} stderr={err.decode(errors='replace')[-300:]!r}"
            check(f"under {label} the exception is reported as an internal error, with the stack",
                  b"before" in out and b"after" not in out
                  and b">>> virtual machine: internal error:" in err and b"Unwinding local stack" in err
                  and b"terminating" not in err,
                  detail)
            check(f"under {label} obr exits 1, not by a signal", rc == 1, detail)

    return finish()


def finish():
    print("")
    print("========================================")
    print(f"  Results: {passed} passed, {failed} failed")
    print("========================================")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
