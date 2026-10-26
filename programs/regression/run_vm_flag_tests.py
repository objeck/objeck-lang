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
    expected. Every one of these used to be silently accepted. So are values
    too large for their type, which used to wrap: --jit=4294967297 ran as
    --jit=1 on Windows, and --gc-threshold=18446744073709551617 as 1 byte.
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
    whatever ran it. vm_error_exit.obs reaches that catch through
    Runtime->SetProperty("runtime.test.throw", ...), a key the VM reserves to
    throw on purpose; it must print the line and leave obr non-zero both by
    default and with --jit=1, where the JIT's bridge reports it the same way
    (since #778). And a command line that is all flags ("obr --jit=off")
    names no program: that is the usage and a non-zero exit, not a silent
    one (it was a silent exit 0 on POSIX).
 10. The nursery knob and the GC statistics. --nursery (and OBJECK_NURSERY)
    accepts 256k and 64m, refuses 2x and 0 with a message naming the range,
    and the flag wins over a bad variable. gc_nursery_knob.obs runs more minor
    collections with a 256k nursery than with the default -- it reports its
    counters on stderr, since a collection count is not stable enough for the
    differential to compare across -opt levels, so they are read from there and
    its stdout stays the bare PASS line; OBJECK_GC_STATS=1
    prints a summary line with every field; runtime.memory.peak is never below
    runtime.memory.used (the fixture checks that itself). And collection stays
    correct when minor GCs are frequent: obj_size_layout, minor_gc_stress,
    core_thread_gc_stress, gc_minor_closure_capture, gc_zero_field_nursery_end,
    gc_closure_capture_nursery_end and opt_inline_and_or_slots pass with
    --nursery=128k and 256k, interpreted and with every method compiled. (At those sizes an integration-1
    obr lost a zero-field object that was the last allocation before a
    collection, and a closure capture copied just before one.) A v2026.9.4 obr
    fails all of this: it does not know the flag. gc_mt_small_nursery_stress
    then runs 100 times with --nursery=256k, interpreted and with every method
    compiled, on two CPUs where taskset exists: threads finish while others
    force collections, and a collection that landed after a thread's entry
    method returned crashed a mark thread on its null current frame (SIGSEGV
    with no output).
 8. An exception in a call the JIT's bridge made ends the program, not the
    process. Neither backend registers unwind information for the code it
    emits, so a C++ exception thrown under compiled code used to terminate
    obr (SIGABRT) before Execute's catch printed anything; the bridge catches
    it now and reports it as Execute would: the internal-error line, the
    stack, exit status 1. jit_bridge_exception.obs reaches it with
    Runtime->SetProperty("runtime.test.throw", ...) from a native method
    (compiled in every mode), by default through the interpreter the bridge
    nests and under --jit=1 through the bridge's trap callback. The regression
    runner cannot tell the
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
 12. Stack-trace method names read like the source. vm_trace_fn_param_format.obs
    faults inside a method with scalar, array, object and function-typed (single,
    nested, doubly nested, function-returning) parameters; the names on its
    unwinding lines must match exactly at -opt s0 and s3 under --jit=off, the
    default and --jit=1. MethodFormatter used to print a function-typed parameter
    as '(a:Int, , , b:Int, ...)~Int' and a mid-list Float[] as 'Float[,]'.
 14. Non-ASCII names end to end. Copies of obc and obr, a library directory
    and the sources sit under a directory named with Latin accents, CJK and a
    character outside the BMP. obc compiles a source there (the argument, the
    library path from OBJECK_LIB_PATH, the libraries it lists and reads, the
    .obe it writes); obr runs the result and loads a native library from that
    library directory, by OBJECK_LIB_PATH and by --lib-path; the program gets a
    non-ASCII argument, reads a non-ASCII variable this script set, sets one a
    child process checks (directly and through CommandOutput's variables), and
    runs CommandOutput with the temp directory on a path with a space. On
    Windows the tools took the CRT's ANSI argv, read the environment through
    getenv_s, loaded libraries with LoadLibraryA and opened files with narrow
    names, so all of it failed there; on Linux and macOS the program's
    arguments were widened byte by byte.
 15. The other tools take non-ASCII names too, each where <bin_dir> has it.
    obu verifies the committed signed fixture (tools/cicd/fixtures/obu_verify)
    from a copy of itself under the non-ASCII directory, given the archive and
    manifest by non-ASCII paths. obb packages a program from a non-ASCII .obe
    and toolchain directory (by -tool_dir and by OBJECK_LIB_PATH) into a
    non-ASCII target and application name, and the packaged launcher (obn,
    from <lib>/native/misc) runs it from another directory, handing it a
    non-ASCII argument, an argument with a space and the environment. On
    Windows all four used the CRT's ANSI argv (and obn its ANSI envp, with no
    quoting for _spawnve), so each failed there.
 16. Non-ASCII text reaches a pipe and a console intact. obd, debugging a
    program under the non-ASCII directory with its output piped, prints the
    binary's path, a source line and the program's output as UTF-8 (in the
    default narrow mode on Windows the first non-ASCII character left wcout
    failed, and the session printed nothing more; the listing wrote UTF-8
    through narrow cout). It also stops at a breakpoint set in a non-ASCII
    source file name: its command scanner knew only ASCII letters, so
    'b debogue_é.obs:4' was an unknown token on every platform. On Windows,
    with the console on code page 437, obu's
    messages and obd's banner read back from the screen as written (obu wrote
    UTF-8 bytes, which the console decoded as code page 437). The console half
    reads the screen buffer of a console it creates, so it needs a session
    that can create one; it reports a skip when it cannot. It reads the
    screen cell by cell and rejoins the lines the console wrapped: a
    double-width character that does not fit at the end of a row moves to
    the next row and leaves a blank cell behind, where the path's length and
    the console's width put it, so read as plain rows the same path held a
    space on the windows-arm64 runner (a 120-cell console) and not on
    windows-x64 (110 cells, the same temp directory).

 17. The call depth. --call-depth (and OBJECK_CALL_DEPTH) sets how deep calls
    may nest, from 256 to 1048576 (default 16384); 255, 1048577, 0 and 'deep'
    are refused with a message naming the range, and the flag wins over a bad
    variable. With --call-depth=300, vm_call_depth_limit.obs recursing 290
    frames deep prints "reached 290 of 300" and 310 frames ends with exactly
    ">>> call stack bounds have been exceeded! <<<" and exit status 1 -- not a
    signal -- under --jit=off, the default and --jit=1. The default maximum
    holds 10,000 frames on a spawned thread in all three modes, and 16,000
    interpreted (a POSIX thread's 8 MB native stack ends compiled recursion
    near 12,000 small frames, with the same error), and ends a runaway
    (20,000) with the same line and status 1; the listing names the innermost
    and outermost frames and counts the rest rather than printing 20,000
    lines. --call-depth=100000 lets a spawned thread recurse 60,000 frames,
    compiled or not: its native stack is sized for the larger maximum. And
    the collector works through a deep stack: with --nursery=256k and the
    heap verifier on, 10,000 frames each hold an object across dozens of
    minor collections and every object is intact afterwards, in all three
    modes. A
    v2026.10.1 obr fails all of this: its call stack was a fixed 256 frames,
    and it does not know the flag.
 17. A heap threshold given with --gc-threshold scales with the live heap.
    It holds as given while a major collection leaves a quarter of it free, and
    follows the live heap (twice what survived) while it does not. Held fixed
    whatever survived, a threshold below the live heap collected on every
    allocation: gc_fixed_threshold_scales.obs, keeping 4,000 objects under
    --gc-threshold=64k, ran 23,539 majors (11-18 s) and runs 8 now. It and
    gc_heap_sizing.obs pass under --gc-threshold=64k and 1m, interpreted, by
    default and with every method compiled; the regression runner passes no VM
    flags, so under it the fixture checks only its data.
 18. A native library's call back into Objeck is bounded. No library in the
    repository calls back, so this builds one with the platform's C++
    compiler (native_callback/callback_lib.cpp; skipped where there is none)
    whose function calls an Objeck method that calls it again. Through it
    (native_callback/vm_native_callback_depth.obs), a 300-deep recursion runs;
    runaways in try blocks recover with "call stack bounds have been exceeded",
    twice, on the main thread and a spawned one; and an unguarded runaway ends
    with exactly the call-stack line and exit status 1 -- under --jit=off, the
    default and --jit=1. Each call back used to get a call stack of its own, so
    nothing bounded the recursion and it died in the OS (0xC00000FD, SIGSEGV)
    with no output.
 19. Transparent huge pages stay off the stacks of the threads the VM starts
    (Linux only). While vm_thread_stack_pages.obs holds 100 idle threads, every
    thread's 8 MB native stack carries the kernel's no-huge-page flag ("nh" in
    /proc/<pid>/smaps) and holds no huge page, under --jit=off, the default and
    --jit=1. glibc allocated those stacks, and where THP is on for every mapping
    the kernel backed each with a 2 MB page at its first touch: in WSL, 100
    idle threads held 37 MB more (30 MB of it AnonHugePages), 130 MB more with
    --call-depth=100000. The check reads flags, not sizes, so it holds whatever
    the host's THP setting is.

Usage: python run_vm_flag_tests.py <bin_dir> [--unicode-only]

--unicode-only runs sections 14, 15 and 16 alone: how a MinGW (MSYS2) build of the
tools, which CI does not build, is checked by hand.
"""
import concurrent.futures
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


def mt_cpu_pin_prefix():
    """A taskset prefix confining a run to two CPUs, or [] when that is not possible.

    The multithreaded small-nursery loop only exercises its window when threads
    outnumber CPUs (see check_nursery_and_gc_stats). Returns [] on a host with no
    taskset (Windows, macOS) and on a host already limited to two CPUs or fewer,
    where the run is confined anyway. The pair is picked from this process's own
    affinity mask and offset by pid, so two copies of this script running side by
    side do not pile onto the same cores.
    """
    if not hasattr(os, "sched_getaffinity") or not shutil.which("taskset"):
        return []
    cpus = sorted(os.sched_getaffinity(0))
    if len(cpus) <= 2:
        return []
    first = (os.getpid() * 2) % len(cpus)
    pair = (cpus[first], cpus[(first + 1) % len(cpus)])
    return ["taskset", "-c", f"{pair[0]},{pair[1]}"]


def mt_oversubscribe_workers():
    """How many copies of the fixture to run at once when pinning is unavailable.

    Starves the same window from the other end: rather than confining one run to
    two CPUs, run enough copies that the fixture's threads outnumber the host's
    CPUs by about MT_OVERSUBSCRIBE. Returns 1 when the CPU count is unknown or
    small enough that a single copy already oversubscribes the host.
    """
    cpus = os.cpu_count()
    if not cpus:
        return 1
    workers = -(-cpus * MT_OVERSUBSCRIBE // MT_FIXTURE_THREADS)
    return max(1, min(workers, MT_NURSERY_LOOP_RUNS))


def main():
    # Sections 14 and 15 quote non-ASCII output in a failure's detail, and
    # stdout may be a pipe in the ANSI code page (it is on the hosted Windows
    # runners): escape what it cannot hold rather than die mid-report.
    sys.stdout.reconfigure(errors="backslashreplace")
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    unicode_only = "--unicode-only" in sys.argv[2:]
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

    if unicode_only:
        check_unicode_names(obc, obr, bin_dir, lib_dir)
        check_unicode_tools(obc, bin_dir, lib_dir)
        check_unicode_text_out(obc, bin_dir, lib_dir)
        return finish()

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
                         ("--nursery=1g", b"--nursery: expected <number>(k|m) from 64k to 128m"),
                         ("--jit=9223372036854775808", b"expected 'off' or a positive call count"),
                         ("--jit=99999999999999999999", b"expected 'off' or a positive call count"),
                         ("--gc-threshold=18446744073709551617", b"expected <number>(k|m|g)"),
                         ("--gc-threshold=99999999999g", b"expected <number>(k|m|g)"),
                         ("--call-depth=255", b"--call-depth: expected a number of frames from 256 to 1048576"),
                         ("--call-depth=1048577", b"--call-depth: expected a number of frames from 256 to 1048576"),
                         ("--call-depth=0", b"--call-depth: expected a number of frames from 256 to 1048576"),
                         ("--call-depth=deep", b"--call-depth: expected a number of frames from 256 to 1048576")):
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
    for needle in ("--gc-threshold=<size>", "<number>(k|m|g)", "--nursery=<size>", "--call-depth=<frames>", "--jit=off|<calls>",
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
            check(f"the reserved runtime.test.throw key stops the program with an internal error, {label}",
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
    # The fixture's Format is native, so both runs reach the bridge: by default
    # Runtime->SetProperty, a library function called once, runs in the
    # interpreter the bridge nests; under --jit=1 it is compiled and its trap
    # throws under the bridge's callback. On the old VM both aborted (a signal, no line of the VM's), and
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

    check_trace_signatures(obc, obr, env, bin_dir)

    check_nursery_and_gc_stats(obc, obr, env, bin_dir, obe, base)

    check_call_depth(obc, obr, env, bin_dir)

    check_gc_threshold_scales(obc, obr, env, bin_dir)

    check_native_callback_depth(obc, obr, env, bin_dir)

    check_thread_stack_pages(obc, obr, env, bin_dir)

    # ---- 11. the heap verifier (OBJECK_GC_VERIFY) ----------------------------------
    verify_section(obc, obr, env, bin_dir)

    check_binary_stdio_utf8(obc, obr, env, bin_dir)

    check_unicode_names(obc, obr, bin_dir, lib_dir)

    check_unicode_tools(obc, bin_dir, lib_dir)

    check_unicode_text_out(obc, bin_dir, lib_dir)

    return finish()


# non-ASCII text used by section 14, spelled with escapes so this file's own
# encoding cannot matter
UNI_DIR = "donn\u00e9es_\u65e5\u672c_\U0001F642"
UNI_ARG = "arg_\u00f1_\u65e5\u672c_\U0001F642"
UNI_VALUE = "valeur_\u00e9_\u65e5\u672c_\U0001F642"
UNI_GET_NAME = "OBJK_GET_\u00dcN\u00cf_\u65e5\u672c"

# The child the fixture runs: exits 0 when the variable named by its last
# argument holds UNI_VALUE. (The fixture names an ASCII variable on POSIX:
# system() runs /bin/sh, and dash drops from the environment any name that is
# not a shell identifier, so a non-ASCII name never reaches the child there.)
UNI_CHILD = (
    "import os, sys\n"
    f"sys.exit(0 if os.environ.get(sys.argv[-1]) == {UNI_VALUE!r} else 1)\n"
)


def copy_runtime_dlls(bin_dir, dest):
    """The runtime DLLs the tools load on Windows, not the large optional ones."""
    for entry in os.listdir(bin_dir):
        path = os.path.join(bin_dir, entry)
        if entry.lower().endswith(".dll") and os.path.getsize(path) < 8 * 1024 * 1024:
            shutil.copy2(path, dest)


# the bytes the committed obu_verify manifest lists a hash for, as
# tools/cicd/test_obu_verify.py writes them
OBU_PAYLOAD = b"objeck obu verify fixture; content is irrelevant, its hash is not\n"
OBU_PAYLOAD_NAME = "obu-verify-fixture.bin"
OBU_FIXTURES = os.path.join(SCRIPT_DIR, "..", "..", "tools", "cicd", "fixtures", "obu_verify")


# The fixture section 16 debugs: a non-ASCII string in its source and output
UNI_TEXT = "texte_\u00e9_\u65e5\u672c_\U0001F642"
DEBUG_FIXTURE = (
    "class DebugUnicodeText {\n"
    "  function : Main(args : String[]) ~ Nil {\n"
    f"    text := \"{UNI_TEXT}\";\n"
    "    text->PrintLine();\n"
    "  }\n"
    "}\n"
)

# Reads back the screen buffer of the console owned by process argv[1] and
# writes its lines, with that console's output code page, to stdout as UTF-8.
# Run as its own process: it must leave its own console to attach to that one.
#
# The screen is read as CHAR_INFO cells (ReadConsoleOutputW), not as the
# fixed-width rows of characters ReadConsoleOutputCharacterW gives, and the
# lines are put back together the way the console host broke them: a
# double-width (CJK) character fills two cells, marked leading and trailing,
# and is kept once; a row whose text reaches its last cell continues on the
# next row; and a row whose last cell is blank while the next row begins with
# a leading cell continues too -- that blank is the cell the double-width
# character could not fit in before it moved down, not a space anyone wrote.
# (The host writes that padding with the current attributes, so the cells
# alone cannot tell it from a space; a space a program wrote right before a
# double-width character that lands there is dropped the same way, which no
# text this script expects contains.) Any other row ends a line. With the
# plain character read, a long path wrapped at a column that depended on the
# temp directory's length and the console's width, and a padding space
# appeared inside the path on one runner and not another.
CONSOLE_READER = r'''
import ctypes, sys
from ctypes import wintypes as W
k = ctypes.WinDLL("kernel32", use_last_error=True)
class COORD(ctypes.Structure): _fields_ = [("X", ctypes.c_short), ("Y", ctypes.c_short)]
class RECT(ctypes.Structure): _fields_ = [("L", ctypes.c_short), ("T", ctypes.c_short), ("R", ctypes.c_short), ("B", ctypes.c_short)]
class INFO(ctypes.Structure): _fields_ = [("size", COORD), ("cursor", COORD), ("attr", W.WORD), ("win", RECT), ("max", COORD)]
class CELL(ctypes.Structure): _fields_ = [("ch", ctypes.c_wchar), ("attr", W.WORD)]
LEADING, TRAILING = 0x0100, 0x0200
k.CreateFileW.restype = W.HANDLE
k.FreeConsole()
if not k.AttachConsole(int(sys.argv[1])):
    sys.exit(2)
h = W.HANDLE(k.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None))
info = INFO()
k.GetConsoleScreenBufferInfo(h, ctypes.byref(info))
width, rows = info.size.X, info.cursor.Y + 1
cells = []
y = 0
while y < rows:
    count = min(16, rows - y)
    chunk = (CELL * (width * count))()
    region = RECT(0, y, width - 1, y + count - 1)
    if not k.ReadConsoleOutputW(h, chunk, COORD(width, count), COORD(0, 0), ctypes.byref(region)):
        sys.exit(3)
    cells.extend(chunk[r * width:(r + 1) * width] for r in range(count))
    y += count
cp = k.GetConsoleOutputCP()
k.FreeConsole()
out = []
for y, row in enumerate(cells):
    text = "".join(c.ch for c in row if not c.attr & TRAILING)
    last = row[-1]
    following = cells[y + 1][0] if y + 1 < len(cells) else None
    if last.attr & TRAILING or last.ch != " ":
        out.append(text)
    elif following is not None and following.attr & LEADING:
        out.append(text[:-1])
    else:
        out.append(text.rstrip(" ") + "\n")
sys.stdout.buffer.write(("%d\n" % cp).encode() + "".join(out).encode("utf-8", "surrogatepass"))
'''
CONSOLE_DONE = "OBJK_CONSOLE_DONE"


def console_screen(command, env, expect=(), timeout=60):
    """Runs 'command' in a new console whose code page is 437 and returns
    (code page, screen text), or None when no console could be read. The
    command is a cmd line: name its non-ASCII parts with %VARIABLES% from env,
    which cmd expands in UTF-16 (a batch file would be read in code page 437).
    The screen text is the console's lines, each ending in a newline, with a
    line the console wrapped put back together (see CONSOLE_READER). It is
    read until it holds the done marker and every text in 'expect' (compared
    as on_screen does), up to the timeout, rather than once after a fixed
    delay: a read can land between the process's output and the console host
    showing it."""
    import time
    proc = subprocess.Popen(f'cmd.exe /d /k "chcp 437 >nul & {command} & echo {CONSOLE_DONE}"',
                            env=env, creationflags=subprocess.CREATE_NEW_CONSOLE)
    screen = None
    try:
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(0.5)
            r = subprocess.run([sys.executable, "-c", CONSOLE_READER, str(proc.pid)], capture_output=True)
            if r.returncode != 0:
                continue
            cp, _, text = r.stdout.decode("utf-8", "replace").partition("\n")
            screen = (int(cp), text)
            # cmd /k does not echo its command, so the marker appears once
            # the command is done
            if CONSOLE_DONE in text and all(on_screen(e) in on_screen(text) for e in expect):
                break
    finally:
        proc.kill()
    return screen


def on_screen(text):
    """'text' compared as a console screen can hold it. A character outside
    the BMP reads back from the screen buffer either whole or as U+FFFD,
    depending on how it was written (cmd's own echo gives U+FFFD), so both
    sides of a comparison drop those and compare the rest, spaces included."""
    return "".join(c for c in text if ord(c) <= 0xFFFF and c != "\ufffd")


def screen_tail(text, size=700):
    """The end of a screen for a failure message."""
    return text.strip()[-size:]


def check_unicode_text_out(obc, bin_dir, lib_dir):
    """16. Non-ASCII text to a pipe and to a console (see the module docstring)."""
    obd = os.path.join(bin_dir, "obd" + EXE)
    obu = os.path.join(bin_dir, "obu" + EXE)
    scratch = tempfile.mkdtemp(prefix="objeck-unicode-text-")
    try:
        root = os.path.join(scratch, UNI_DIR)
        os.makedirs(root)
        env = {k: v for k, v in os.environ.items() if k != "OBJECK_LIB_PATH"}
        env["OBJECK_LIB_PATH"] = lib_dir + os.sep

        src = os.path.join(root, "debogue_é.obs")
        obe = os.path.join(root, "debogue_é.obe")
        with open(src, "w", encoding="utf-8", newline="\n") as f:
            f.write(DEBUG_FIXTURE)
        commands = os.path.join(scratch, "commands.txt")
        with open(commands, "w", encoding="utf-8", newline="\n") as f:
            f.write("b debogue_é.obs:4\nr\nl\nc\nq\n")

        if os.path.exists(obd):
            rc, out, err = run([obc, "-src", src, "-dest", obe, "-debug"], env=env, cwd=bin_dir)
            if not os.path.exists(obe):
                check("the obd fixture compiles", False, (out + err).decode("utf-8", "replace")[-300:])
            else:
                with open(commands, "rb") as stdin:
                    p = subprocess.run([obd, "-bin", obe, "-src_dir", root], stdin=stdin,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                                       cwd=scratch, timeout=120)
                text = p.stdout.decode("utf-8", "replace")
                detail = f"rc={p.returncode} out={text[-400:]!r}"
                check("obd prints a non-ASCII binary path to a pipe as UTF-8", obe in text, detail)
                check("obd stops at a breakpoint set in a non-ASCII file name",
                      "break: file='debogue_é.obs:4'" in text, detail)
                check("obd lists a non-ASCII source line to a pipe as UTF-8",
                      f'text := "{UNI_TEXT}"' in text, detail)
                check("obd passes a debugged program's non-ASCII output to a pipe as UTF-8",
                      re.search(r"^(> )?" + re.escape(UNI_TEXT) + r"\s*$", text, re.M) is not None, detail)
        else:
            print(f"  [SKIP] obd pipe checks: no {obd}")

        if os.name != "nt":
            return

        # ---- the console, on code page 437
        console_env = dict(env)
        console_env["OBJK_T_ROOT"] = root
        manifest_dir = os.path.join(root, "manifeste_ü_日本")
        os.makedirs(manifest_dir)
        for name in ("SHA256SUMS", "SHA256SUMS.minisig"):
            shutil.copy2(os.path.join(OBU_FIXTURES, name), manifest_dir)
        with open(os.path.join(manifest_dir, OBU_PAYLOAD_NAME), "wb") as f:
            f.write(OBU_PAYLOAD)
        console_env["OBJK_T_SUMS"] = os.path.join(manifest_dir, "SHA256SUMS")
        console_env["OBJK_T_ARCHIVE"] = os.path.join(manifest_dir, OBU_PAYLOAD_NAME)

        if os.path.exists(obu):
            console_env["OBJK_T_OBU"] = obu
            expected_line = "against " + console_env["OBJK_T_SUMS"]
            screen = console_screen('"%OBJK_T_OBU%" verify "%OBJK_T_ARCHIVE%" "%OBJK_T_SUMS%" & echo OBU_EXIT=%ERRORLEVEL%',
                                    console_env, expect=[expected_line], timeout=30)
            if screen is None:
                print("  [SKIP] console checks: no console could be created and read here")
                return
            cp, text = screen
            check("obu's message reads back from a code page 437 console as written",
                  cp == 437 and on_screen(expected_line) in on_screen(text),
                  f"cp={cp} screen={screen_tail(text)!r}")

        if os.path.exists(obd) and os.path.exists(obe):
            console_env["OBJK_T_OBD"] = obd
            console_env["OBJK_T_OBE"] = obe
            console_env["OBJK_T_COMMANDS"] = commands
            screen = console_screen('"%OBJK_T_OBD%" -bin "%OBJK_T_OBE%" -src_dir "%OBJK_T_ROOT%" < "%OBJK_T_COMMANDS%"',
                                    console_env, expect=[obe, UNI_TEXT], timeout=30)
            if screen is None:
                print("  [SKIP] console checks: no console could be created and read here")
                return
            cp, text = screen
            detail = f"cp={cp} screen={screen_tail(text)!r}"
            check("obd's banner reads back from a code page 437 console as written",
                  cp == 437 and on_screen(obe) in on_screen(text), detail)
            check("a debugged program's output reads back from a code page 437 console as written",
                  on_screen(UNI_TEXT) in on_screen(text), detail)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def check_unicode_tools(obc, bin_dir, lib_dir):
    """15. obu, obb and obn with non-ASCII names (see the module docstring)."""
    obu = os.path.join(bin_dir, "obu" + EXE)
    obb = os.path.join(bin_dir, "obb" + EXE)
    obn = os.path.join(lib_dir, "native", "misc", "obn" + EXE)
    scratch = tempfile.mkdtemp(prefix="objeck-unicode-tools-")
    try:
        root = os.path.join(scratch, UNI_DIR)
        os.makedirs(root)
        bare = {k: v for k, v in os.environ.items() if k != "OBJECK_LIB_PATH"}

        # ---- obu verify, run from and given non-ASCII paths
        if os.path.exists(obu):
            uni_bin = os.path.join(root, "bin_é")
            manifest_dir = os.path.join(root, "manifeste_ü_日本")
            os.makedirs(uni_bin)
            os.makedirs(manifest_dir)
            shutil.copy2(obu, uni_bin)
            copy_runtime_dlls(bin_dir, uni_bin)
            for name in ("SHA256SUMS", "SHA256SUMS.minisig"):
                shutil.copy2(os.path.join(OBU_FIXTURES, name), manifest_dir)
            archive = os.path.join(manifest_dir, OBU_PAYLOAD_NAME)
            with open(archive, "wb") as f:
                f.write(OBU_PAYLOAD)
            sums = os.path.join(manifest_dir, "SHA256SUMS")
            rc, out, err = run([os.path.join(uni_bin, os.path.basename(obu)), "verify", archive, sums],
                               env=bare, cwd=scratch)
            text = (out + err).decode("utf-8", errors="replace")
            check("obu verifies an archive and a signed manifest named by non-ASCII paths",
                  rc == 0 and "Verified " + OBU_PAYLOAD_NAME in text, f"rc={rc} out={text[-300:]!r}")
            check("obu names the non-ASCII manifest path as written", sums in text, f"out={text[-300:]!r}")
        else:
            print(f"  [SKIP] obu checks: no {obu}")

        # ---- obb packages a program; the packaged obn runs it
        if not (os.path.exists(obb) and os.path.exists(obn)):
            print(f"  [SKIP] obb/obn checks: need {obb} and {obn}")
            return

        # a toolchain directory as obb expects one: bin, lib (with the launcher
        # in native/misc), readme.html and LICENSE
        tool_dir = os.path.join(root, "outils_ñ")
        tool_bin = os.path.join(tool_dir, "bin")
        tool_lib = os.path.join(tool_dir, "lib")
        tool_misc = os.path.join(tool_lib, "native", "misc")
        os.makedirs(tool_bin)
        os.makedirs(tool_misc)
        shutil.copy2(os.path.join(bin_dir, "obr" + EXE), tool_bin)
        copy_runtime_dlls(bin_dir, tool_bin)
        for entry in os.listdir(lib_dir):
            if entry.endswith(".obl") or entry in ("configobjk.ini", "cacert.pem"):
                shutil.copy2(os.path.join(lib_dir, entry), tool_lib)
        shutil.copy2(obn, tool_misc)
        prop = os.path.join(lib_dir, "native", "misc", "config.prop")
        if os.path.exists(prop):
            shutil.copy2(prop, tool_misc)
        else:
            open(os.path.join(tool_misc, "config.prop"), "w").close()
        for name in ("readme.html", "LICENSE"):
            open(os.path.join(tool_dir, name), "w").close()

        # the program, compiled in an ASCII directory (obc is section 14's) and
        # moved to its non-ASCII name
        app_obe = os.path.join(root, "appli_é.obe")
        ascii_src = os.path.join(scratch, "fixture.obs")
        ascii_obe = os.path.join(scratch, "fixture.obe")
        shutil.copy2(os.path.join(SCRIPT_DIR, "vm_unicode_args_env.obs"), ascii_src)
        ascii_env = dict(bare)
        ascii_env["OBJECK_LIB_PATH"] = lib_dir + os.sep
        run([obc, "-src", ascii_src, "-dest", ascii_obe], env=ascii_env, cwd=bin_dir)
        if not os.path.exists(ascii_obe):
            check("the obb fixture compiles", False, "obc produced no .obe")
            return
        shutil.move(ascii_obe, app_obe)

        apps = []
        for label, name, flags, env in (
                ("-tool_dir", "appli_日本_🙂", ["-tool_dir", tool_dir], bare),
                ("OBJECK_LIB_PATH", "appli_ü", [], dict(bare, OBJECK_LIB_PATH=tool_lib))):
            rc, out, err = run([obb, "-src_file", app_obe, "-to_dir", root, "-to_name", name] + flags,
                               env=env, cwd=scratch)
            app_exe = os.path.join(root, name, name + EXE)
            check(f"obb packages a non-ASCII .obe into a non-ASCII application (toolchain by {label})",
                  rc == 0 and os.path.exists(app_exe) and
                  os.path.exists(os.path.join(root, name, "app", "app.obe")),
                  f"rc={rc} out={(out + err).decode('utf-8', errors='replace')[-300:]!r}")
            if os.path.exists(app_exe):
                apps.append(app_exe)

        if not apps:
            return

        # the packaged launcher, started from another directory: a non-ASCII
        # argument, an argument with a space (the child command), and the
        # environment, which it passes on with its own PATH and OBJECK_LIB_PATH
        child_script = os.path.join(scratch, "check_env.py")
        with open(child_script, "w", encoding="utf-8") as f:
            f.write(UNI_CHILD)
        run_env = dict(bare)
        run_env[UNI_GET_NAME] = UNI_VALUE
        # one argument with a space: the program runs it as a command line, and
        # unquoted, so neither path may have a space (cmd /c's quote rules)
        child_cmd = f"{sys.executable} {child_script}"
        has_child = " " not in sys.executable and " " not in child_script
        rc, out, err = run([apps[0], UNI_ARG] + ([child_cmd] if has_child else []), env=run_env, cwd=scratch)
        text = out.decode("utf-8", errors="replace")
        detail = f"rc={rc} out={text[-300:]!r} stderr={err.decode('utf-8', errors='replace')[-200:]!r}"
        check("the packaged launcher starts its program from another directory", rc == 0 and "arg" in text, detail)
        check("the packaged launcher passes a non-ASCII argument as written", "arg ok" in text, detail)
        check("the packaged launcher passes a non-ASCII variable as written", "env ok" in text, detail)
        if has_child:
            check("the packaged launcher passes an argument with a space as one argument",
                  "child env ok" in text and "command env ok" in text, detail)
        else:
            print("  [SKIP] launcher argument-with-a-space check: the Python path has a space")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def check_unicode_names(obc, obr, bin_dir, lib_dir):
    """14. Non-ASCII names end to end (see the module docstring)."""
    scratch = tempfile.mkdtemp(prefix="objeck-unicode-")
    child_dir = tempfile.mkdtemp(prefix="objeck-unicode-child-")
    try:
        root = os.path.join(scratch, UNI_DIR)
        uni_bin = os.path.join(root, "bin_é")
        uni_lib = os.path.join(root, "lib_ñ_日本")
        os.makedirs(uni_bin)
        os.makedirs(uni_lib)

        # the tools, with the runtime DLLs they load on Windows (not the large
        # optional ones), and no lib beside them
        for tool in (obc, obr):
            shutil.copy2(tool, uni_bin)
        copy_runtime_dlls(bin_dir, uni_bin)
        uni_obc = os.path.join(uni_bin, os.path.basename(obc))
        uni_obr = os.path.join(uni_bin, os.path.basename(obr))

        # the libraries, their index and the native libraries
        for entry in os.listdir(lib_dir):
            path = os.path.join(lib_dir, entry)
            if entry.endswith(".obl") or entry in ("configobjk.ini", "cacert.pem"):
                shutil.copy2(path, uni_lib)
        if os.path.isdir(os.path.join(lib_dir, "native")):
            shutil.copytree(os.path.join(lib_dir, "native"), os.path.join(uni_lib, "native"))

        bare = {k: v for k, v in os.environ.items() if k != "OBJECK_LIB_PATH"}
        uni_env = dict(bare)
        uni_env["OBJECK_LIB_PATH"] = uni_lib

        # obc: a source, a library path and a target, all named with non-ASCII text
        program_src = os.path.join(root, "programme_é.obs")
        program_obe = os.path.join(root, "programme_é.obe")
        native_src = os.path.join(root, "natif_ü.obs")
        native_obe = os.path.join(root, "natif_ü.obe")
        shutil.copy2(os.path.join(SCRIPT_DIR, "vm_unicode_args_env.obs"), program_src)
        shutil.copy2(os.path.join(SCRIPT_DIR, "vm_lib_path_native.obs"), native_src)

        rc, out, err = run([uni_obc, "-src", program_src, "-dest", program_obe], env=uni_env, cwd=uni_bin)
        check("obc compiles a non-ASCII source with a non-ASCII OBJECK_LIB_PATH into a non-ASCII target",
              rc == 0 and os.path.exists(program_obe),
              f"rc={rc} out={(out + err).decode(errors='replace')[-300:]!r}")
        rc, out, err = run([uni_obc, "-src", native_src, "-lib", "cipher", "-dest", native_obe],
                           env=uni_env, cwd=uni_bin)
        check("obc links a library from the non-ASCII library directory",
              rc == 0 and os.path.exists(native_obe),
              f"rc={rc} out={(out + err).decode(errors='replace')[-300:]!r}")

        # obr's checks do not depend on obc's: where obc failed, the programs
        # are compiled in an ASCII directory and copied to the non-ASCII names
        ascii_env = dict(bare)
        ascii_env["OBJECK_LIB_PATH"] = lib_dir + os.sep
        for src, obe, libs in ((program_src, program_obe, []), (native_src, native_obe, ["-lib", "cipher"])):
            if not os.path.exists(obe):
                ascii_src = os.path.join(child_dir, "fixture.obs")
                ascii_obe = os.path.join(child_dir, "fixture.obe")
                shutil.copy2(src, ascii_src)
                run([obc, "-src", ascii_src] + libs + ["-dest", ascii_obe], env=ascii_env, cwd=bin_dir)
                if os.path.exists(ascii_obe):
                    shutil.move(ascii_obe, obe)

        # obr: the native library loads from the non-ASCII library directory
        if os.path.exists(native_obe):
            rc, out, err = run([uni_obr, native_obe], env=uni_env, cwd=uni_bin)
            check("obr loads a native library from a non-ASCII OBJECK_LIB_PATH",
                  rc == 0 and b"native lib loaded" in out,
                  f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-200:]!r}")
            rc, out, err = run([uni_obr, "--lib-path=" + uni_lib, native_obe], env=bare, cwd=uni_bin)
            check("obr loads a native library from a non-ASCII --lib-path",
                  rc == 0 and b"native lib loaded" in out,
                  f"rc={rc} out={out[-120:]!r} stderr={err.decode(errors='replace')[-200:]!r}")

        # the program: its argument, the environment both ways, and command
        # output with a temp directory whose path has a space
        if os.path.exists(program_obe):
            run_env = dict(uni_env)
            run_env[UNI_GET_NAME] = UNI_VALUE
            if os.name == "nt":
                spaced_tmp = os.path.join(scratch, "temp dir")
                os.makedirs(spaced_tmp)
                run_env["TMP"] = spaced_tmp
                run_env["TEMP"] = spaced_tmp

            child_script = os.path.join(child_dir, "check_env.py")
            with open(child_script, "w", encoding="utf-8") as f:
                f.write(UNI_CHILD)
            child_cmd = f"{sys.executable} {child_script}"
            # unquoted, so neither path may have a space (cmd /c's quote rules)
            has_child = " " not in sys.executable and " " not in child_script

            args = [UNI_ARG] + ([child_cmd] if has_child else [])
            rc, out, err = run([uni_obr, program_obe] + args, env=run_env, cwd=uni_bin)
            text = out.decode(errors="replace")
            detail = f"rc={rc} out={text[-300:]!r} stderr={err.decode(errors='replace')[-200:]!r}"
            check("the program's non-ASCII argument arrives as written", "arg ok" in text, detail)
            check("a non-ASCII variable set for obr reads back as written", "env ok" in text, detail)
            what = "with the temp directory on a path with a space" if os.name == "nt" else "through its temp file"
            check(f"CommandOutput returns the command's output {what}", "command output ok" in text, detail)
            if has_child:
                check("a variable with a non-ASCII value the program sets reaches a child process as written",
                      "child env ok" in text, detail)
                check("a variable with a non-ASCII value passed with CommandOutput reaches the command as written",
                      "command env ok" in text, detail)
            else:
                print("  [SKIP] child-process variable checks: the Python path has a space")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.rmtree(child_dir, ignore_errors=True)


def check_binary_stdio_utf8(obc, obr, env, bin_dir):
    """13. --objeck-stdio=binary writes UTF-8 bytes.

    On Windows, binary mode put stdout in _O_BINARY and wide output then went
    out as each wchar_t's two bytes: every program run that way printed
    UTF-16LE, including the LSP's framed replies over stdio, which no client
    could read ('C\\0o\\0n\\0...'). Strings, numbers, characters and Bools all
    go through the same stream, so each is checked. POSIX ignores the flag and
    must print the same bytes.
    """
    work = tempfile.mkdtemp(prefix="objk-stdio-")
    try:
        src = os.path.join(work, "stdio_utf8.obs")
        with open(src, "w", encoding="utf-8") as fh:
            fh.write("class StdioUtf8 {\n  function : Main(args : String[]) ~ Nil {\n"
                     "    \"héllo \U0001F600 \"->Print();\n    42->Print();\n"
                     "    ' '->Print();\n    1.5->Print();\n    ' '->Print();\n"
                     "    true->PrintLine();\n  }\n}\n")
        obe = os.path.join(work, "stdio_utf8.obe")
        rc, out, err = run([obc, "-src", src, "-dest", obe], env=env, cwd=bin_dir)
        check("binary-stdio fixture compiles", rc == 0 and os.path.exists(obe), err.decode(errors="replace")[-300:])
        if rc != 0:
            return

        want = "héllo \U0001F600 42 1.5 true".encode("utf-8")
        for label, flags, extra in (("--objeck-stdio=binary", ["--objeck-stdio=binary"], {}),
                                    ("OBJECK_STDIO=binary", [], {"OBJECK_STDIO": "binary"})):
            rc, out, err = run([obr] + flags + [obe], env=dict(env, **extra), cwd=bin_dir)
            check(f"{label}: output is UTF-8 bytes, not UTF-16",
                  rc == 0 and out.rstrip(b"\r\n") == want and b"\x00" not in out,
                  f"rc={rc} out={out[:80]!r}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


# The method names vm_trace_fn_param_format.obs must unwind with, innermost
# first: every parameter kind the formatter handles, including function types
# nested one and two levels deep and a function type returning one.
TRACE_EXPECTED_NAMES = [
    "P1->Crash(a:Int, b:Float[], c:System.String, d:(Int)~Int, e:((Int)~Int, Int)~Int, f:Char[,], "
    "g:(Int)~(Int)~Int, h:(((Int)~Int)~Int, Int)~Int)",
    "TraceFnParamFormat->Main(a:System.String[])",
]


def check_trace_signatures(obc, obr, env, bin_dir):
    # ---- 12. stack-trace method names read like the source ------------------------
    # MethodFormatter counted every "m.(" in the whole signature and skipped that
    # many '~', so a function-typed parameter swallowed the parameters after it,
    # and a one-dimensional array in the middle of a list printed as "[,]". The
    # regression runner cannot see stderr text; the names are compared here.
    src = os.path.join(SCRIPT_DIR, "vm_trace_fn_param_format.obs")
    for opt in ("s0", "s3"):
        obe = os.path.join(SCRIPT_DIR, f"vm_trace_fn_param_format_{opt}.obe")
        rc, out, err = run([obc, "-src", src, "-opt", opt, "-dest", obe], env=env, cwd=bin_dir)
        check(f"trace-signature fixture compiles (-opt {opt})", rc == 0 and os.path.exists(obe),
              (out + err).decode(errors="replace")[-300:])
        if rc != 0:
            continue
        for label, flags in (("jit=off", ["--jit=off"]), ("default", []), ("jit=1", ["--jit=1"])):
            rc, out, err = run([obr] + flags + [obe], env=env, cwd=bin_dir)
            text = err.decode(errors="replace")
            names = re.findall(r"name='([^']*)'", text)
            check(f"the unwinding lines name each method as its source declares it (-opt {opt}, {label})",
                  rc != 0 and b"after" not in out and "Unwinding local stack" in text
                  and names == TRACE_EXPECTED_NAMES,
                  f"rc={rc} names={names!r}")


CALL_DEPTH_LINE = ">>> call stack bounds have been exceeded! <<<"


def check_gc_threshold_scales(obc, obr, env, bin_dir):
    # ---- 17. a heap threshold given with --gc-threshold scales with the live heap -----
    for name in ("gc_fixed_threshold_scales", "gc_heap_sizing"):
        src = os.path.join(SCRIPT_DIR, name + ".obs")
        dest = os.path.join(SCRIPT_DIR, name + ".obe")
        rc, out, err = run([obc, "-src", src, "-dest", dest], env=env, cwd=bin_dir)
        check(f"{name} compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
        if rc != 0:
            continue
        for threshold in ("64k", "1m"):
            for label, flags in (("jit=off", ["--jit=off"]), ("default", []), ("jit=1", ["--jit=1"])):
                rc, out, err = run([obr, "--gc-threshold=" + threshold] + flags + [dest], env=env, cwd=bin_dir)
                text = err.decode(errors="replace")
                check(f"{name} passes under --gc-threshold={threshold} ({label})",
                      rc == 0 and out.startswith(b"PASS:"),
                      f"rc={rc} out={out[-240:]!r} stderr={text[-240:]!r}")
                if name == "gc_fixed_threshold_scales":
                    # the fixture checks its own bound; this names the count when it fails
                    m = re.search(r"majors=(\d+)", text)
                    check(f"{name} reports a bounded major count under --gc-threshold={threshold} ({label})",
                          m is not None and int(m.group(1)) <= 200, f"stderr={text[-240:]!r}")


def build_callback_library(out_dir):
    """Builds native_callback/callback_lib.cpp as <out_dir>/libobjk_callback_test.

    Returns (path, None) on success, (None, reason) when no C++ compiler is
    available (a skip), and raises RuntimeError when one is and the build fails.
    """
    repo = os.path.dirname(os.path.dirname(SCRIPT_DIR))
    src = os.path.join(SCRIPT_DIR, "native_callback", "callback_lib.cpp")
    if os.name == "nt":
        import platform
        vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                               "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if not os.path.exists(vswhere):
            return None, "no vswhere.exe"
        p = subprocess.run([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        install = p.stdout.decode(errors="replace").strip().splitlines()
        vcvarsall = os.path.join(install[0], "VC", "Auxiliary", "Build", "vcvarsall.bat") if install else ""
        if not vcvarsall or not os.path.exists(vcvarsall):
            return None, "no Visual C++ toolset"
        target = "arm64" if platform.machine().upper() in ("ARM64", "AARCH64") else "x64"
        out = os.path.join(out_dir, "libobjk_callback_test.dll")
        script = os.path.join(out_dir, "build_callback.cmd")
        with open(script, "w") as f:
            f.write("@echo off\r\n")
            f.write(f'call "{vcvarsall}" {target} >nul\r\n')
            f.write(f'cd /d "{out_dir}"\r\n')
            f.write(f'cl /nologo /LD /EHsc /O2 /std:c++17 /DWIN32 /DNDEBUG /D_CONSOLE '
                    f'/I "{os.path.join(repo, "core", "lib", "zlib", "win")}" '
                    f'/I "{os.path.join(repo, "core", "lib", "crypto", "win", "include")}" '
                    f'"{src}" /Fe:libobjk_callback_test.dll\r\n')
        p = subprocess.run(["cmd", "/c", script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    else:
        cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        if not cxx:
            return None, "no C++ compiler"
        ext = ".dylib" if sys.platform == "darwin" else ".so"
        out = os.path.join(out_dir, "libobjk_callback_test" + ext)
        cmd = [cxx, "-std=c++17", "-O2", "-fPIC", "-shared", "-o", out, src]
        if sys.platform == "darwin":
            cmd[1:1] = ["-undefined", "dynamic_lookup"]
            # lib_api.h includes the VM's headers, which include mbedtls's
            includes = []
            if os.environ.get("OBJECK_DEPS"):
                includes.append(os.path.join(os.environ["OBJECK_DEPS"], "macos-bundle", "mbedtls", "include"))
            brew = shutil.which("brew")
            if brew:
                b = subprocess.run([brew, "--prefix"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                includes.append(os.path.join(b.stdout.decode().strip(), "include"))
            includes += ["/opt/homebrew/include", "/usr/local/include"]
            for inc in includes:
                if os.path.isdir(inc):
                    cmd.insert(1, "-I" + inc)
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode != 0 or not os.path.exists(out):
        raise RuntimeError(p.stdout.decode(errors="replace")[-1500:])
    return out, None


def check_native_callback_depth(obc, obr, env, bin_dir):
    # ---- 18. a native library's call back into Objeck is bounded ---------------------
    scratch = tempfile.mkdtemp(prefix="objeck-callback-")
    try:
        native_dir = os.path.join(scratch, "lib", "native")
        os.makedirs(native_dir)
        try:
            lib, reason = build_callback_library(native_dir)
        except RuntimeError as e:
            check("the callback test library builds", False, str(e))
            return
        if not lib:
            print(f"  [SKIP] native callback depth: {reason}")
            return
        check("the callback test library builds", True)

        src = os.path.join(SCRIPT_DIR, "native_callback", "vm_native_callback_depth.obs")
        dest = os.path.join(scratch, "vm_native_callback_depth.obe")
        rc, out, err = run([obc, "-src", src, "-dest", dest], env=env, cwd=bin_dir)
        check("native callback fixture compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
        if rc != 0:
            return

        lib_flag = "--lib-path=" + os.path.join(scratch, "lib")
        for label, flags in (("jit=off", ["--jit=off"]), ("default", []), ("jit=1", ["--jit=1"])):
            rc, out, err = run([obr, lib_flag] + flags + [dest, "300"], env=env, cwd=bin_dir)
            check(f"a bounded recursion through a native callback runs 300 deep ({label})",
                  rc == 0 and out.strip().endswith(b"reached 300"),
                  f"rc={rc} out={out[-160:]!r} stderr={err[-300:]!r}")

            rc, out, err = run([obr, lib_flag] + flags + [dest, "guarded"], env=env, cwd=bin_dir, timeout=300)
            check(f"runaways through a native callback recover in a try block, on two threads ({label})",
                  rc == 0 and b"FAIL" not in out and out.count(b"PASS:") == 10 and b"guarded: done" in out,
                  f"rc={rc} out={out[-400:]!r} stderr={err[-300:]!r}")

            rc, out, err = run([obr, lib_flag] + flags + [dest, "0"], env=env, cwd=bin_dir, timeout=300)
            text = err.decode(errors="replace")
            lines = [l.strip() for l in text.splitlines() if "call stack bounds" in l]
            detail = f"rc={rc} lines={lines!r} out={out[-160:]!r} stderr={text[-300:]!r}"
            check(f"an unguarded runaway through a native callback ends with the call-stack line ({label})",
                  lines == [CALL_DEPTH_LINE] and b"reached" not in out, detail)
            check(f"it exits 1, not by a signal ({label})", rc == 1, detail)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def smaps_entries(pid):
    """The process's mappings: (size_kb, anon_huge_kb, vmflags, path) each."""
    entries = []
    current = None
    with open(f"/proc/{pid}/smaps") as f:
        for line in f:
            if re.match(r"^[0-9a-f]+-[0-9a-f]+ ", line):
                parts = line.split()
                current = {"path": parts[5] if len(parts) > 5 else "", "size": 0, "huge": 0, "flags": ""}
                entries.append(current)
            elif current is not None:
                key, _, value = line.partition(":")
                if key == "Size":
                    current["size"] = int(value.split()[0])
                elif key == "AnonHugePages":
                    current["huge"] = int(value.split()[0])
                elif key == "VmFlags":
                    current["flags"] = value.split()
    return entries


def check_thread_stack_pages(obc, obr, env, bin_dir):
    # ---- 19. no transparent huge pages on VM thread stacks (Linux) ---------------------
    if not sys.platform.startswith("linux"):
        return
    src = os.path.join(SCRIPT_DIR, "vm_thread_stack_pages.obs")
    dest = os.path.join(SCRIPT_DIR, "vm_thread_stack_pages.obe")
    rc, out, err = run([obc, "-src", src, "-dest", dest], env=env, cwd=bin_dir)
    check("thread stack fixture compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
    if rc != 0:
        return

    threads = 100
    stack_kb = 8192    # NativeThreadStackBytes() at the default call depth
    for label, flags in (("jit=off", ["--jit=off"]), ("default", []), ("jit=1", ["--jit=1"])):
        p = subprocess.Popen([obr] + flags + [dest, str(threads), "4000"], cwd=bin_dir, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            started = p.stdout.readline()
            entries = smaps_entries(p.pid) if started.startswith(b"started") else []
            rest, err = p.communicate(timeout=120)
        except Exception as e:
            p.kill()
            p.communicate()
            check(f"thread stacks inspected ({label})", False, repr(e))
            continue
        # A stack's mapping is its size, or a little more where the kernel merged
        # it with an adjacent mapping carrying the same flags -- a thread's
        # committed call stack, which is marked the same way (measured: 95 of
        # 100 at 8,192 kB and 5 at 8,224 kB). A guard page sits between stacks.
        stacks = [e for e in entries
                  if not e["path"] and "wr" in e["flags"] and stack_kb <= e["size"] < stack_kb + 2048]
        flagged = [e for e in stacks if "nh" in e["flags"]]
        huge = sum(e["huge"] for e in stacks)
        detail = (f"rc={p.returncode} started={started!r} {stack_kb} kB anonymous mappings={len(stacks)} "
                  f"with nh={len(flagged)} AnonHugePages={huge} kB stderr={err[-200:]!r}")
        check(f"each of {threads} thread stacks is marked no-huge-page ({label})",
              p.returncode == 0 and len(flagged) >= threads, detail)
        check(f"the thread stacks hold no huge page ({label})", len(stacks) >= threads and huge == 0, detail)


def check_call_depth(obc, obr, env, bin_dir):
    # ---- 16. the call depth ---------------------------------------------------------
    src = os.path.join(SCRIPT_DIR, "vm_call_depth_limit.obs")
    dest = os.path.join(SCRIPT_DIR, "vm_call_depth_limit.obe")
    rc, out, err = run([obc, "-src", src, "-dest", dest], env=env, cwd=bin_dir)
    check("call-depth fixture compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
    if rc != 0:
        return

    modes = (("jit=off", ["--jit=off"]), ("default", []), ("jit=1", ["--jit=1"]))

    def exceeded(rc, out, err, label, what):
        text = err.decode(errors="replace")
        lines = [l.strip() for l in text.splitlines() if "call stack bounds" in l]
        detail = f"rc={rc} lines={lines!r} out={out[-120:]!r} stderr={text[-300:]!r}"
        check(f"{what} ends with the call-stack line ({label})", lines == [CALL_DEPTH_LINE] and b"reached" not in out, detail)
        check(f"{what} exits 1, not by a signal ({label})", rc == 1, detail)
        return text

    # the flag, and the variable, set the maximum
    bad_env = dict(env)
    bad_env["OBJECK_CALL_DEPTH"] = "deep"
    set_env = dict(env)
    set_env["OBJECK_CALL_DEPTH"] = "300"
    for label, flags in modes:
        rc, out, err = run([obr, "--call-depth=300"] + flags + [dest, "290"], env=env, cwd=bin_dir)
        check(f"--call-depth=300 runs a 290-frame recursion ({label})",
              rc == 0 and b"reached 290 of 300" in out, f"rc={rc} out={out[-120:]!r} stderr={err[-300:]!r}")
        rc, out, err = run([obr, "--call-depth=300"] + flags + [dest, "310"], env=env, cwd=bin_dir)
        exceeded(rc, out, err, label, "--call-depth=300: a 310-frame recursion")
        rc, out, err = run([obr] + flags + [dest, "290"], env=set_env, cwd=bin_dir)
        check(f"OBJECK_CALL_DEPTH=300 sets the maximum ({label})",
              rc == 0 and b"reached 290 of 300" in out, f"rc={rc} out={out[-120:]!r} stderr={err[-300:]!r}")
        rc, out, err = run([obr] + flags + [dest, "310"], env=set_env, cwd=bin_dir)
        exceeded(rc, out, err, label, "OBJECK_CALL_DEPTH=300: a 310-frame recursion")

    rc, out, err = run([obr, dest, "10"], env=bad_env, cwd=bin_dir)
    check("OBJECK_CALL_DEPTH=deep is refused with a message naming the range",
          rc != 0 and b"OBJECK_CALL_DEPTH: expected a number of frames from 256 to 1048576" in err,
          f"rc={rc} stderr={err[-200:]!r}")
    rc, out, err = run([obr, "--call-depth=300", dest, "10"], env=bad_env, cwd=bin_dir)
    check("--call-depth wins over OBJECK_CALL_DEPTH (a bad variable is not read when the flag is given)",
          rc == 0 and b"reached 10 of 300" in out, f"rc={rc} stderr={err[-200:]!r}")

    # the default maximum: deep enough for real recursion, and enforced
    for label, flags in modes:
        rc, out, err = run([obr] + flags + [dest, "10000", "thread"], env=env, cwd=bin_dir)
        check(f"the default maximum holds a 10,000-frame recursion in a spawned thread ({label})",
              rc == 0 and b"reached 10000 of 16384" in out, f"rc={rc} out={out[-120:]!r} stderr={err[-300:]!r}")
        rc, out, err = run([obr] + flags + [dest, "20000"], env=env, cwd=bin_dir)
        text = exceeded(rc, out, err, label, "a 20,000-frame recursion at the default maximum")
        unwind = [l for l in text.splitlines() if "method: pos=" in l]
        check(f"its listing shows the ends of the stack and counts the rest ({label})",
              0 < len(unwind) <= 50 and "frame(s) not shown" in text, f"{len(unwind)} frame lines: {text[-400:]!r}")

    rc, out, err = run([obr, "--jit=off", dest, "16000"], env=env, cwd=bin_dir)
    check("the default maximum holds a 16,000-frame interpreted recursion",
          rc == 0 and b"reached 16000 of 16384" in out, f"rc={rc} out={out[-120:]!r} stderr={err[-300:]!r}")

    # the collector marks and relocates what 10,000 frames hold
    verify_env = dict(env)
    verify_env["OBJECK_GC_VERIFY"] = "1"
    for label, flags in modes:
        rc, out, err = run([obr, "--nursery=256k"] + flags + [dest, "hold", "10000"], env=verify_env, cwd=bin_dir)
        m = re.search(rb"held (\d+) of 10000 intact after (\d+) minor", out)
        check(f"10,000 frames holding objects survive minor collections under the heap verifier ({label})",
              rc == 0 and m is not None and m.group(1) == b"10001" and int(m.group(2)) > 0,
              f"rc={rc} out={out[-160:]!r} stderr={err[-300:]!r}")

    # a larger maximum: a spawned thread's native stack is sized for it
    for label, flags in modes:
        rc, out, err = run([obr, "--call-depth=100000"] + flags + [dest, "60000", "thread"], env=env, cwd=bin_dir)
        check(f"--call-depth=100000 runs a 60,000-frame recursion in a spawned thread ({label})",
              rc == 0 and b"reached 60000 of 100000" in out, f"rc={rc} out={out[-120:]!r} stderr={err[-300:]!r}")


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

    # The fixture's collection counters are on STDERR: they depend on when a
    # collection lands, so run_differential.py compared them across -opt levels
    # and JIT modes and reported the fixture as divergent. Its stdout is the
    # stable PASS line; everything counted is read from stderr here.
    def minor_of(err):
        m = re.search(rb"^minor=(\d+)\s*$", err, re.M)
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
              rc == 0 and b"PASS: nursery knob and GC stats" in out and minor_of(err) is not None,
              detail(rc, out, err))

    default_minor = minor_of(runs["default"][2])
    for label in ("--nursery=256k", "OBJECK_NURSERY=256k", "--nursery=256k --jit=1"):
        small_minor = minor_of(runs[label][2])
        check(f"a 256k nursery runs more minor collections than the default ({label}: "
              f"{small_minor} vs {default_minor})",
              small_minor is not None and default_minor is not None and small_minor > default_minor,
              f"small={small_minor} default={default_minor}")
    check("the fixture reports the 256k limit as runtime.gc.nursery.capacity",
          b"nursery capacity=262144" in runs["--nursery=256k"][2], runs["--nursery=256k"][2][-200:])

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
        printed_minor = minor_of(err) or 0
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
    # cannot pass VM flags per test, so the minor-GC stress fixtures run here, at
    # two small nursery sizes: which allocation a collection lands on depends on the
    # size, and obj_size_layout lost objects at 128k and 256k but not at 384k -- a
    # zero-field object that was the last allocation before a collection (its address
    # equalled the young offset) and a closure capture copied just before one.
    for name in NURSERY_STRESS_TESTS:
        src = os.path.join(SCRIPT_DIR, name + ".obs")
        dest = os.path.join(SCRIPT_DIR, name + ".obe")
        cmd = [obc, "-src", src, "-lib", "cipher,collect,xml,json", "-opt", "s3", "-dest", dest]
        rc, out, err = run(cmd, env=env, cwd=bin_dir)
        check(f"{name} compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
        if rc != 0:
            continue
        for size in NURSERY_STRESS_SIZES:
            for label, flags in (("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
                rc, out, err = run([obr, f"--nursery={size}"] + flags + [dest], env=env, cwd=bin_dir, timeout=600)
                check(f"{name} passes with --nursery={size} ({label})",
                      rc == 0 and b"PASS" in out and b"FAIL" not in out, detail(rc, out, err))

    # Thread exits overlapping collections. A thread whose entry method returned
    # left its monitor registered (current frame null) after it left the
    # stop-the-world count; a collection another thread ran in that window
    # dereferenced the null frame on a mark thread (SIGSEGV, no output). One run
    # rarely lands in the window, so loop the fixture; a 256k nursery puts a
    # collection in nearly every exit window.
    #
    # PINNING IS WHAT GIVES THIS LOOP ITS POWER. The window is only wide enough
    # to hit when the exiting thread is descheduled between leaving the
    # stop-the-world count and unregistering its monitor, which needs more
    # runnable threads than CPUs. Measured on a 32-thread box against the
    # pre-fix obr, pinned to two CPUs: 8 SIGSEGVs in 3600 runs, and 7 in 2200 in
    # an independent set -- 15 in 5800 together, 0.26%. Unpinned on the same box:
    # 0 in 900, and 0 in 600 on Windows. The stress probe found the original
    # crash on a 2-core runner for the same reason.
    #
    # Two ways to get there, and the loop takes whichever the host allows.
    # Where taskset exists, confine each run to two CPUs: that is the measured
    # configuration above, so it stays exactly as it was. macOS has no
    # per-process affinity to borrow, so instead of giving the run fewer CPUs,
    # give the host more threads: run several copies at once until the fixture's
    # threads outnumber the CPUs by about 3x. Same starvation from the other
    # end, nothing needed from the platform, and 5.7x less wall time than the
    # sequential loop it replaces (9.4s against 53.7s for both modes).
    #
    # MEASURED pre-fix against this fixture at --nursery=256k:
    #   Linux x64, pinned to 2 CPUs ......... 15 / 5800   (0.26%)
    #   macOS arm64, 6 copies on 14 CPUs .... 11 / 10000  (0.11%)  <- oversubscribed
    #   Linux x64, sequential unpinned ....... 0 / 900
    #   Windows x64, 12 copies on 32 CPUs .... 0 / 8000
    #   Windows x64, PINNED to 2 CPUs ........ 0 / 8000
    #   Windows x64, sequential .............. 0 / 2000
    # Oversubscription recovers real power on macOS arm64, within a factor of two
    # or three of pinning, on a box where the sequential loop had none. The same
    # arm64 run with the fixed VM: 0 / 10000, 95% upper bound 0.03%. Under equal
    # rates all 11 events landing in one arm is p < 0.001, assuming nothing about
    # the underlying rate.
    #
    # WINDOWS IS THE PLATFORM, NOT THE TECHNIQUE, and an earlier version of this
    # comment had the reason wrong. Windows DOES have per-process affinity --
    # `start /affinity 3`, and children inherit the mask -- so the claim that it
    # had none to borrow was simply false. Pinned to the same two CPUs as the
    # Linux configuration, it measured 0 in 8000. Oversubscribed, 0 in 8000.
    # 18000 runs, zero failures of any kind, against P(0 | Linux's 0.26%) = 9e-10
    # and P(0 | macOS's 0.15%) = 6.1e-06. Windows is statistically incompatible
    # with both other platforms in both configurations, so nothing this loop can
    # do recovers power there and spending 4x the wall time to pin buys none.
    # A plausible mechanism, untested: 3x oversubscription on 32 CPUs is not 3x
    # on 14, because a descheduled thread on a 32-way box is likelier to be
    # picked up promptly by a near-idle core. That predicts the loop may regain
    # power on a 2-4 core CI runner, which is worth checking before trusting the
    # Windows CI leg for this bug.
    #
    # The defect itself is a plain unguarded null dereference in
    # platform-independent C++. A clean Windows run means THIS CONFIGURATION did
    # not reproduce; it says nothing about whether the bug is there.
    #
    # All 11 crashes are one signature -- SIGSEGV at address 0x20, which on LP64
    # is StackFrame::jit_mem read through a null cur_frame, the line the guard
    # wraps -- with an exiting thread in UnregisterMutator and a collection in
    # CollectMinor. The rate is also mode-dependent in the direction the
    # mechanism predicts: jit=1 runs are faster (0.25s vs 0.42s) and fail more
    # often (0.14% vs 0.08%), because more thread turnover per unit time means
    # more teardown windows, and the window is at teardown.
    #
    # Windows is the outlier: it does not reproduce even PINNED, so its scheduler
    # does not produce the interleaving. That is a statement about Windows, not
    # about the technique. Either way a clean run means THIS CONFIGURATION did
    # not reproduce -- never that the platform is immune.
    name = MT_NURSERY_LOOP_TEST
    src = os.path.join(SCRIPT_DIR, name + ".obs")
    dest = os.path.join(SCRIPT_DIR, name + ".obe")
    rc, out, err = run([obc, "-src", src, "-lib", "cipher,collect,xml,json", "-opt", "s3", "-dest", dest],
                       env=env, cwd=bin_dir)
    check(f"{name} compiles", rc == 0 and os.path.exists(dest), err.decode(errors="replace")[-300:])
    if rc == 0:
        pin = mt_cpu_pin_prefix()
        workers = 1 if pin else mt_oversubscribe_workers()
        if pin:
            how = "pinned to 2 CPUs"
        elif workers > 1:
            how = (f"{workers} at a time, ~{workers * MT_FIXTURE_THREADS}/{os.cpu_count()} threads "
                   "per CPU; 0.11% on macOS arm64, no measured power on Windows x64")
        else:
            how = "sequential -- no power for this bug"
        for label, flags in (("jit=off", ["--jit=off"]), ("jit=1", ["--jit=1"])):
            cmd = pin + [obr, "--nursery=256k"] + flags + [dest]
            bad = []

            def one(i, cmd=cmd):
                rc, out, err = run(cmd, env=env, cwd=bin_dir, timeout=300)
                if rc != 0 or b"PASS" not in out or b"FAIL" in out:
                    return f"run {i}: {detail(rc, out, err)}"
                return None

            if workers > 1:
                with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
                    bad = [r for r in pool.map(one, range(MT_NURSERY_LOOP_RUNS)) if r]
            else:
                bad = [r for r in (one(i) for i in range(MT_NURSERY_LOOP_RUNS)) if r]
            check(f"{name} passes {MT_NURSERY_LOOP_RUNS} runs with --nursery=256k ({label}, {how}; "
                  f"{len(bad)} failed)", not bad, "; ".join(bad[:3]))


# Run with small nurseries (check_nursery_and_gc_stats); each prints PASS.
NURSERY_STRESS_TESTS = ("obj_size_layout", "minor_gc_stress", "core_thread_gc_stress", "gc_minor_closure_capture",
                        "gc_zero_field_nursery_end", "gc_closure_capture_nursery_end",
                        # an -opt s3 inlined callee's object local stays a root; the
                        # old layout lost it interpreted, which the runner never runs
                        "opt_inline_and_or_slots",
                        # #913: the collector's closure-declaration lookup inserted into
                        # a std::map from several marking threads. Each absent id is a
                        # race window exactly once, so this one puts a fresh id per
                        # worker per round into live frames -- and it needs a small
                        # nursery, because the path is in the young-gen scan and the
                        # default nursery gave it no minor collections at all
                        "gc_closure_ids_stress")
NURSERY_STRESS_SIZES = ("128k", "256k")

# Looped with a 256k nursery in both JIT modes (check_nursery_and_gc_stats).
# One pinned run takes about 0.2s, so 100 per mode costs ~40s for both. The
# pre-fix crash rate pinned to two CPUs measured 0.26% per run (15 in 5800), so
# these 200 runs catch a regression roughly 40% of the time -- worth having,
# nowhere near a guarantee; the long loops belong in the stress probe.
MT_NURSERY_LOOP_TEST = "gc_mt_small_nursery_stress"
MT_NURSERY_LOOP_RUNS = 100
# Worker threads the fixture spawns per run, and how far past the CPU count the
# unpinned path aims to push them. 3x matches what pinning achieves on Linux
# (8 fixture threads against 2 CPUs); it is a target for the same starvation,
# not a second measured configuration.
MT_FIXTURE_THREADS = 8
MT_OVERSUBSCRIBE = 3

# The verifier's report prefix. Not a bare b"gc-verify": its back-off notice
# "[gc-verify] verification took N% of wall time" is printed by clean runs that
# are slow enough, and matching it failed them.
VERIFY_REPORT = b">>> gc-verify:"


# Existing GC stress and collection tests that must pass unchanged, and print
# the same stdout, with the verifier checking every collection.
VERIFY_CLEAN_TESTS = ("minor_gc_stress", "core_thread_gc_stress", "jit_gc_stress", "jit_closure_gc_fixup",
                      "collect_map_ops", "collect_vector_ops", "collect_hash_ops", "collect_set_ops",
                      # G12: a closure capture reached only through an old holder
                      "closure_capture_old_holder_g12",
                      # Bool[] declared as a byte array in every declaration kind,
                      # and array captures including Bool[] (B2 wrong memory TYPE)
                      "gc_bool_array_declaration", "closure_array_param_capture",
                      # an and/or callee inlined into a caller without one: its locals
                      # must sit in the slots declared for them (B2, Fill:i, slot 7)
                      "gc_scoped_local_slot_types", "gc_zero_field_nursery_end",
                      "opt_inline_and_or_slots")

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
                  rc0 == 0 and rc == 0 and VERIFY_REPORT not in err, detail)
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
              rc == 0 and out == out0 and VERIFY_REPORT not in err,
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
                  rc == 0 and out == out0 and VERIFY_REPORT not in err,
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
