#!/usr/bin/env python3
r"""VM command-line flag tests for obr.

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
 5. An unsupported locale does not stop the program. Python 3 puts
    LC_CTYPE=C.UTF-8 in every child's environment when LANG is unset (PEP 538),
    so this script hands obr exactly that from any shell with no LANG; macOS's C
    library reports it as the composite "C/C.UTF-8/C/C/C/C", which libc++ cannot
    turn into a std::locale, and obr used to exit with a collate_byname message
    before the program ran -- seven failures above that were not the VM's. A
    locale the system does not have (LANG=xx_YY.bogus) is the other shape. Both
    must run the program, print what the default run printed, and still write
    wide characters as UTF-8 (the byte check is POSIX-only: the Windows console
    path reads no locale variable).
 6. A locale the program asks for and the system lacks does not stop the
    program. Runtime->SetLocale("xx_YY.bogus") made the VM build a std::locale
    from the NULL that setlocale returned, and the constructor's exception
    ended the program. With the JIT on, the old VM aborted, which the
    regression runner sees; interpreted, it printed ">>> virtual machine:
    internal error: locale constructed with null <<<" and POSIX obr still
    exited 0, and the runner never runs a program with --jit=off. The VM
    refuses the name now; vm_set_locale_refused.obs must reach its last line
    with no internal error on stderr, interpreted and with every method compiled.
 7. A program that dies inside the VM exits non-zero. Execute prints
    ">>> virtual machine: internal error: ... <<<" and returns -1 when an
    exception escapes the interpreter, and the POSIX entry point returned 0
    regardless, so on Linux and macOS such a program reported success to
    whatever ran it. vm_error_exit.obs reaches that catch with
    "1e999"->ToFloat(); it must print the line and leave obr non-zero both by
    default and with --jit=1, where the JIT's bridge reports it the same way
    (since #778). And a command line that is all flags ("obr --jit=off")
    names no program: that is the usage and a non-zero exit, not a silent
    one (it was a silent exit 0 on POSIX).
 10. The nursery knob and the GC statistics. --nursery (and OBJECK_NURSERY)
    accepts 256k and 64m, refuses 2x and 0 with a message naming the range,
    and the flag wins over a bad variable. gc_nursery_knob.obs runs more minor
    collections with a 256k nursery than with the default; OBJECK_GC_STATS=1
    prints a summary line with every field; runtime.memory.peak is never below
    runtime.memory.used (the fixture checks that itself). And collection stays
    correct when minor GCs are frequent: minor_gc_stress.obs and
    core_thread_gc_stress.obs pass with --nursery=256k, interpreted and with
    every method compiled. A v2026.9.4 obr fails all of this: it does not know
    the flag.
 8. An exception in a call the JIT's bridge made ends the program, not the
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
 9. A zero divisor reads the same interpreted and compiled. The interpreter
    printed ">>> Divide by zero <<<" and compiled code ">>> Divide by zero in
    native JIT code <<<" (S4); both print the first now (the text lives in
    core/shared/int_ops.h). bad_runtime_divzero.obs must print exactly that
    line and exit non-zero under --jit=off, the default and --jit=1. The
    regression runner accepts any non-zero exit with output, so it cannot
    see the text.
 11. The heap verifier. OBJECK_GC_VERIFY=1 checks the collector's invariants
    at every collection (core/vm/arch/memory_verify.cpp): the GC stress tests
    and four collection tests pass with the same output with it on, and each
    OBJECK_GC_VERIFY_INJECT fault (field, barrier, mark) in
    vm_gc_verify_inject.obs stops obr with the verifier's report -- a verifier
    nothing can trip would pass the first half just as well as a correct heap.
"""
import os
import re
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


def run(cmd, env=None, cwd=None, timeout=120):
    try:
        p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        return -999, e.stdout or b"", (e.stderr or b"") + b"\n<timed out>"
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
                         ("--gc-threshold=2x", b"expected <number>(k|m|g)"),
                         ("--nursery=2x", b"--nursery: expected <number>(k|m) from 64k to 128m"),
                         ("--nursery=0", b"--nursery: expected <number>(k|m) from 64k to 128m"),
                         ("--nursery=1g", b"--nursery: expected <number>(k|m) from 64k to 128m")):
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
    for needle in ("--gc-threshold=<size>", "<number>(k|m|g)", "--nursery=<size>", "--jit=off|<calls>",
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

    # ---- 5. an unsupported locale does not stop the program ---------------------
    wide_src = os.path.join(SCRIPT_DIR, "vm_locale_wide.obs")
    wide_obe = os.path.join(SCRIPT_DIR, "vm_locale_wide.obe")
    rc, out, err = run([obc, "-src", wide_src, "-dest", wide_obe], env=env, cwd=bin_dir)
    check("wide-output fixture compiles", rc == 0 and os.path.exists(wide_obe), err.decode(errors="replace")[-300:])
    wide = "wide: héllo wörld 世界".encode("utf-8")
    for label, overrides in (("LC_CTYPE=C.UTF-8 with no LANG", {"LC_CTYPE": "C.UTF-8"}),
                             ("LANG=xx_YY.bogus", {"LANG": "xx_YY.bogus"})):
        loc_env = {k: v for k, v in env.items() if k != "LANG" and not k.startswith("LC_")}
        loc_env.update(overrides)
        rc, out, err = run([obr, obe], env=loc_env, cwd=bin_dir)
        check(f"program runs under {label} and prints what the default run printed",
              rc == 0 and out == base, f"rc={rc} stderr={err.decode(errors='replace')[-200:]!r}")
        if os.name != "nt" and os.path.exists(wide_obe):
            rc, out, err = run([obr, wide_obe], env=loc_env, cwd=bin_dir)
            check(f"wide characters are written as UTF-8 under {label}",
                  rc == 0 and wide in out,
                  f"rc={rc} out={out[-80:]!r} stderr={err.decode(errors='replace')[-200:]!r}")

    # ---- 6. a locale the program asks for and the system lacks is refused ----------
    loc_src = os.path.join(SCRIPT_DIR, "vm_set_locale_refused.obs")
    loc_obe = os.path.join(SCRIPT_DIR, "vm_set_locale_refused.obe")
    rc, out, err = run([obc, "-src", loc_src, "-dest", loc_obe], env=env, cwd=bin_dir)
    check("set-locale fixture compiles", rc == 0 and os.path.exists(loc_obe), err.decode(errors="replace")[-300:])
    if rc == 0:
        for label, flags in (("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
            rc, out, err = run([obr] + flags + [loc_obe], env=env, cwd=bin_dir)
            check(f"SetLocale with a name the system lacks is refused and the program finishes ({label})",
                  rc == 0 and out.rstrip().endswith(b"set-locale: done") and b"internal error" not in err,
                  f"rc={rc} out={out[-160:]!r} stderr={err.decode(errors='replace')[-200:]!r}")

    # ---- 7. a program that dies inside the VM exits non-zero ------------------------
    # Execute's catch prints the line and returns -1; the POSIX entry point used
    # to return 0 regardless. The regression runner proves the same thing more
    # coarsely (the fixture is # EXPECT_RUNTIME_ERROR); this names the message
    # and the status, twice. By default Main runs once and is interpreted, so
    # the exception reaches Execute's catch. With --jit=1 every method is
    # compiled and the exception is thrown under compiled code, where the JIT's
    # bridge catches it and reports it the same way; before #778 that run
    # aborted the process without printing the line.
    err_src = os.path.join(SCRIPT_DIR, "vm_error_exit.obs")
    err_obe = os.path.join(SCRIPT_DIR, "vm_error_exit.obe")
    rc, out, err = run([obc, "-src", err_src, "-dest", err_obe], env=env, cwd=bin_dir)
    check("VM-error fixture compiles", rc == 0 and os.path.exists(err_obe), err.decode(errors="replace")[-300:])
    if rc == 0:
        for label, flags in (("by default", []), ("with --jit=1", ["--jit=1"])):
            rc, out, err = run([obr] + flags + [err_obe], env=env, cwd=bin_dir)
            check(f'"1e999"->ToFloat() (std::stod out of range) stops the program with an internal error, {label}',
                  b"before" in out and b"after" not in out and b">>> virtual machine: internal error:" in err,
                  f"rc={rc} out={out[-80:]!r} stderr={err.decode(errors='replace')[-200:]!r}")
            check(f"obr exits non-zero after that internal error, {label} (was 0 on POSIX)", rc != 0, f"rc={rc}")

    # Every argument a flag, no program: Execute returns -1 for that without a
    # word, and the POSIX entry point turned it into a silent exit 0.
    rc, out, err = run([obr, "--jit=off"], env=env, cwd=bin_dir)
    check("--jit=off with no program prints the usage and exits non-zero",
          rc != 0 and b"Usage: obr" in (out + err),
          f"rc={rc} out={out[-80:]!r} stderr={err.decode(errors='replace')[-200:]!r}")

    # ---- 8. an exception in a bridge call ends the program, not the process ----
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

    # ---- 9. one divide-by-zero message on every path --------------------------
    dz_src = os.path.join(SCRIPT_DIR, "bad_runtime_divzero.obs")
    dz_obe = os.path.join(SCRIPT_DIR, "bad_runtime_divzero.obe")
    rc, out, err = run([obc, "-src", dz_src, "-dest", dz_obe], env=env, cwd=bin_dir)
    check("divide-by-zero fixture compiles", rc == 0 and os.path.exists(dz_obe), err.decode(errors="replace")[-300:])
    if rc == 0:
        for label, flags in (("jit=off", ["--jit=off"]), ("default", []), ("jit=1", ["--jit=1"])):
            rc, out, err = run([obr] + flags + [dz_obe], env=env, cwd=bin_dir)
            text = err.decode(errors="replace") + out.decode(errors="replace")
            lines = [l.strip() for l in text.splitlines() if "Divide by zero" in l]
            detail = f"rc={rc} lines={lines!r}"
            check(f'under {label} a zero divisor prints ">>> Divide by zero <<<" and nothing else about it',
                  lines == [">>> Divide by zero <<<"], detail)
            check(f"under {label} obr exits non-zero after the divide by zero", rc != 0, detail)

    check_nursery_and_gc_stats(obc, obr, env, bin_dir, obe, base)

    # ---- 11. the heap verifier (OBJECK_GC_VERIFY) ----------------------------------
    verify_section(obc, obr, env, bin_dir)

    return finish()


def check_nursery_and_gc_stats(obc, obr, env, bin_dir, obe, base):
    # ---- 10. the nursery knob and the GC statistics -----------------------------
    def detail(rc, out, err):
        return f"rc={rc} out={out[-160:]!r} stderr={err.decode(errors='replace')[-300:]!r}"

    for size in ("256k", "64m"):
        rc, out, err = run([obr, f"--nursery={size}", obe], env=env, cwd=bin_dir)
        check(f"--nursery={size} is accepted and the program prints what the default run printed",
              rc == 0 and out == base, detail(rc, out, err))

    bad_env = dict(env)
    bad_env["OBJECK_NURSERY"] = "2x"
    rc, out, err = run([obr, obe], env=bad_env, cwd=bin_dir)
    check("OBJECK_NURSERY=2x is refused with a message naming the range",
          rc != 0 and b"OBJECK_NURSERY: expected <number>(k|m) from 64k to 128m" in err, detail(rc, out, err))
    rc, out, err = run([obr, "--nursery=256k", obe], env=bad_env, cwd=bin_dir)
    check("--nursery wins over OBJECK_NURSERY (a bad variable is not read when the flag is given)",
          rc == 0 and out == base, detail(rc, out, err))

    knob_src = os.path.join(SCRIPT_DIR, "gc_nursery_knob.obs")
    knob_obe = os.path.join(SCRIPT_DIR, "gc_nursery_knob.obe")
    rc, out, err = run([obc, "-src", knob_src, "-dest", knob_obe], env=env, cwd=bin_dir)
    check("nursery fixture compiles", rc == 0 and os.path.exists(knob_obe), err.decode(errors="replace")[-300:])
    if rc != 0:
        return

    def minor_of(out):
        m = re.search(rb"^minor=(\d+)\s*$", out, re.M)
        return int(m.group(1)) if m else None

    small_env = dict(env)
    small_env["OBJECK_NURSERY"] = "256k"
    stats_env = dict(env)
    stats_env["OBJECK_GC_STATS"] = "1"
    runs = {}
    for label, flags, run_env in (("default", [], env),
                                  ("--nursery=256k", ["--nursery=256k"], env),
                                  ("OBJECK_NURSERY=256k", [], small_env),
                                  ("--nursery=256k --jit=1", ["--nursery=256k", "--jit=1"], env),
                                  ("--nursery=256k with OBJECK_GC_STATS=1", ["--nursery=256k"], stats_env)):
        rc, out, err = run([obr] + flags + [knob_obe], env=run_env, cwd=bin_dir)
        runs[label] = (rc, out, err)
        check(f"nursery fixture passes its own checks ({label})",
              rc == 0 and b"PASS: nursery knob and GC stats" in out and minor_of(out) is not None,
              detail(rc, out, err))

    default_minor = minor_of(runs["default"][1])
    for label in ("--nursery=256k", "OBJECK_NURSERY=256k", "--nursery=256k --jit=1"):
        small_minor = minor_of(runs[label][1])
        check(f"a 256k nursery runs more minor collections than the default ({label}: "
              f"{small_minor} vs {default_minor})",
              small_minor is not None and default_minor is not None and small_minor > default_minor,
              f"small={small_minor} default={default_minor}")
    check("the fixture reports the 256k limit as runtime.gc.nursery.capacity",
          b"nursery capacity=262144" in runs["--nursery=256k"][1], runs["--nursery=256k"][1][-200:])

    rc, out, err = runs["--nursery=256k with OBJECK_GC_STATS=1"]
    fields = ("minor", "major", "pauses", "pause_p50_us", "pause_p95_us", "pause_max_us",
              "promoted_objects", "promoted_bytes", "peak_rss_bytes")
    line = re.search(rb"^\[gc-stats\] (.*)$", err, re.M)
    values = {}
    if line:
        for key, value in re.findall(rb"(\w+)=(-?\d+)", line.group(1)):
            values[key.decode()] = int(value)
    check("OBJECK_GC_STATS=1 prints a [gc-stats] line on stderr with every field",
          line is not None and all(f in values for f in fields), detail(rc, out, err))
    if line is not None and all(f in values for f in fields):
        printed_minor = minor_of(out) or 0
        check("the summary's counts cover what the program saw (minor >= its runtime.gc.minor > 0, "
              "one pause per collection)",
              values["minor"] >= printed_minor > 0 and values["pauses"] == values["minor"] + values["major"],
              str(values))
        check("the summary's pauses are ordered: 0 <= p50 <= p95 <= max",
              0 <= values["pause_p50_us"] <= values["pause_p95_us"] <= values["pause_max_us"], str(values))
        check("the summary reports promoted bytes and a peak RSS",
              values["promoted_bytes"] > 0 and values["promoted_objects"] > 0 and values["peak_rss_bytes"] > 0,
              str(values))
    rc, out, err = runs["default"]
    check("without OBJECK_GC_STATS there is no summary line (control)", b"[gc-stats]" not in err,
          err.decode(errors="replace")[-200:])

    # Collection correctness when minor GCs are frequent. The regression runner
    # cannot pass VM flags per test, so the minor-GC stress fixtures run here.
    for name, libs in (("minor_gc_stress", None), ("core_thread_gc_stress", None)):
        src = os.path.join(SCRIPT_DIR, name + ".obs")
        dest = os.path.join(SCRIPT_DIR, name + ".obe")
        cmd = [obc, "-src", src, "-lib", "cipher,collect,xml,json", "-opt", "s3", "-dest", dest]
        rc, out, err = run(cmd, env=env, cwd=bin_dir)
        check(f"{name} compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
        if rc != 0:
            continue
        for label, flags in (("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
            rc, out, err = run([obr, "--nursery=256k"] + flags + [dest], env=env, cwd=bin_dir, timeout=600)
            check(f"{name} passes with --nursery=256k ({label})",
                  rc == 0 and b"PASS" in out and b"FAIL" not in out, detail(rc, out, err))


# Existing GC stress and collection tests that must pass unchanged, and print
# the same stdout, with the verifier checking every collection.
VERIFY_CLEAN_TESTS = ("minor_gc_stress", "core_thread_gc_stress", "jit_gc_stress", "jit_closure_gc_fixup",
                      "collect_map_ops", "collect_vector_ops", "collect_hash_ops", "collect_set_ops",
                      # G12: a closure capture reached only through an old holder
                      "closure_capture_old_holder_g12")

# Each injected fault and the report the verifier must stop the program with.
VERIFY_INJECTIONS = (("field", b">>> gc-verify: B2 violation"),
                     ("barrier", b">>> gc-verify: A2 violation"),
                     # the marked word still points into the nursery range: B3
                     ("mark", b">>> gc-verify: B3 violation"))


def extra_libs(src):
    libs = "cipher,collect,xml,json"
    with open(src, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("# EXTRA_LIBS:"):
                libs += "," + line.split(":", 1)[1].strip()
    return libs


def verify_section(obc, obr, env, bin_dir):
    """OBJECK_GC_VERIFY checks every collection against the collector's
    invariants (core/vm/arch/memory_verify.cpp) and aborts with a report.

    (a) with it on, the GC stress tests and four collection tests still pass
        and print what they print without it, interpreted and with --jit=1;
    (b) each OBJECK_GC_VERIFY_INJECT fault makes obr exit non-zero with the
        verifier's report -- the proof the verifier can fail -- while the same
        fixture passes with the verifier on and no fault;
    (c) with the verifier off, an injection request is ignored and the output
        is unchanged, and a malformed value is refused.
    """
    plain = {k: v for k, v in env.items() if not k.startswith("OBJECK_GC_VERIFY")}
    verify = dict(plain)
    verify["OBJECK_GC_VERIFY"] = "1"

    def compile_test(name):
        src = os.path.join(SCRIPT_DIR, name + ".obs")
        obe = os.path.join(SCRIPT_DIR, name + ".obe")
        rc, out, err = run([obc, "-src", src, "-lib", extra_libs(src), "-opt", "s3", "-dest", obe],
                           env=plain, cwd=bin_dir)
        check(f"{name} compiles", rc == 0 and os.path.exists(obe), (out + err).decode(errors="replace")[-300:])
        return obe if rc == 0 else None

    for name in VERIFY_CLEAN_TESTS:
        obe = compile_test(name)
        if not obe:
            continue
        for label, flags in (("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
            rc0, out0, err0 = run([obr] + flags + [obe], env=plain, cwd=bin_dir)
            rc, out, err = run([obr] + flags + [obe], env=verify, cwd=bin_dir)
            detail = f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-400:]!r}"
            check(f"{name} passes under OBJECK_GC_VERIFY=1 ({label})",
                  rc0 == 0 and rc == 0 and b"gc-verify" not in err, detail)
            check(f"{name} prints the same with the verifier on ({label})", out == out0,
                  f"off={out0[-160:]!r} on={out[-160:]!r}")

    obe = compile_test("vm_gc_verify_inject")
    if not obe:
        return
    for label, flags in (("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
        rc0, out0, err0 = run([obr] + flags + [obe], env=plain, cwd=bin_dir)
        check(f"injection fixture passes without the verifier ({label})",
              rc0 == 0 and b"PASS:" in out0, f"rc={rc0} out={out0[-120:]!r}")
        rc, out, err = run([obr] + flags + [obe], env=verify, cwd=bin_dir)
        check(f"injection fixture passes with the verifier and no fault ({label})",
              rc == 0 and out == out0 and b"gc-verify" not in err,
              f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-300:]!r}")
        for mode, report in VERIFY_INJECTIONS:
            injected = dict(verify)
            injected["OBJECK_GC_VERIFY_INJECT"] = mode
            rc, out, err = run([obr] + flags + [obe], env=injected, cwd=bin_dir)
            check(f"OBJECK_GC_VERIFY_INJECT={mode} is caught: non-zero exit and the verifier's report ({label})",
                  rc != 0 and report in err and b"PASS:" not in out,
                  f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-400:]!r}")
            ignored = dict(plain)
            ignored["OBJECK_GC_VERIFY_INJECT"] = mode
            rc, out, err = run([obr] + flags + [obe], env=ignored, cwd=bin_dir)
            check(f"OBJECK_GC_VERIFY_INJECT={mode} without the verifier changes nothing ({label})",
                  rc == 0 and out == out0 and b"gc-verify" not in err,
                  f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-300:]!r}")

    bad = dict(plain)
    bad["OBJECK_GC_VERIFY"] = "sometimes"
    rc, out, err = run([obr, obe], env=bad, cwd=bin_dir)
    check("OBJECK_GC_VERIFY=sometimes is refused with a message naming what was expected",
          rc != 0 and b"expected a positive collection period or 'checkmark'" in err,
          f"rc={rc} stderr={err.decode(errors='replace')[-200:]!r}")


def finish():
    print("")
    print("========================================")
    print(f"  Results: {passed} passed, {failed} failed")
    print("========================================")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
