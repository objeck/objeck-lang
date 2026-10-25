#!/usr/bin/env python3
"""SetObjEnv.cmd must set PATH and OBJECK_LIB_PATH for a non-ASCII install.

Usage: python test_set_obj_env.py        (Windows only; exits 0 elsewhere)

The launcher's "Command Prompt" button writes SetObjEnv.cmd (AppLauncher/
set_obj_env.h) and runs it with cmd /k. cmd reads a batch file in the
console's code page, and the script used to be plain UTF-8, so for an install
under a directory such as 'donnees_<CJK>_<emoji>' both variables were set to
mojibake and nothing ran from that prompt.

This builds the generator exactly as the launcher compiles it, writes the
script for an install under a non-ASCII directory, and runs it in a NEW
console whose code page is 437 -- cmd reads the file the way it does on a
default Western install -- then has a child process report what it got:
  - OBJECK_LIB_PATH names the install's lib directory, which exists;
  - PATH's last entry is the install's bin directory;
  - the console is back on code page 437 afterwards.
A control runs the same check on the script as master wrote it (UTF-8, no
code-page switch) and requires it to FAIL, so a console that happened to be on
UTF-8 already cannot make the test pass by accident.

The GUI itself is not run (it needs a desktop); the button runs exactly this
file with cmd /k.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.join(HERE, "AppLauncher")

UNI_DIR = "donn\u00e9es_\u65e5\u672c_\U0001F642"

failures = []


def check(name, cond, detail=""):
    print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else ": " + detail))
    if not cond:
        failures.append(name)
    return cond


def find_vcvars():
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return None
    root = subprocess.run([vswhere, "-latest", "-products", "*",
                           "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                           "-property", "installationPath"],
                          capture_output=True, text=True, timeout=60).stdout.strip()
    bat = os.path.join(root, "VC", "Auxiliary", "Build", "vcvars64.bat") if root else ""
    return bat if bat and os.path.exists(bat) else None


def build(work):
    """The generator, compiled from set_obj_env.h as the launcher compiles it."""
    vcvars = find_vcvars()
    if not vcvars:
        print("SKIP: no MSVC toolchain found (vswhere/vcvars64.bat)")
        sys.exit(0)
    src = os.path.join(work, "write_set_obj_env.cpp")
    with open(src, "w") as f:
        f.write('#include "set_obj_env.h"\n'
                "int wmain(int argc, wchar_t* argv[]) {\n"
                "  if(argc != 3) { return 2; }\n"
                '  return WriteSetObjEnv(argv[1], L"Objeck Command Prompt (test)", argv[2]) ? 0 : 1;\n'
                "}\n")
    exe = os.path.join(work, "write_set_obj_env.exe")
    script = os.path.join(work, "build.bat")
    with open(script, "w") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" >nul\r\n' % vcvars)
        f.write("if errorlevel 1 exit /b 1\r\n")
        f.write('cl /nologo /EHsc /W4 /I "%s" "%s" /Fe:"%s" /Fo:"%s/" \r\n'
                % (APP_DIR, src, exe, work.replace("\\", "/")))
        f.write("exit /b %ERRORLEVEL%\r\n")
    proc = subprocess.run([script], capture_output=True, text=True)
    if proc.returncode != 0 or not os.path.exists(exe):
        print("BUILD FAILED\n%s\n%s" % (proc.stdout, proc.stderr))
        sys.exit(1)
    return exe


# The child the script's prompt runs: what it inherited, and the console's code page
CHECKER = r'''
import ctypes, json, os, sys
path = os.environ.get("PATH", "")
json.dump({"lib": os.environ.get("OBJECK_LIB_PATH"),
           "last_path": path.split(";")[-1] if path else None,
           "cp": ctypes.windll.kernel32.GetConsoleOutputCP()},
          open(sys.argv[1], "w", encoding="utf-8"))
'''


def run_in_console(script, work, label):
    """Runs 'script' as the button does (cmd /k) in a new console on code page
    437 and returns what the checker reported, or None on a timeout."""
    checker = os.path.join(work, "checker.py")
    with open(checker, "w") as f:
        f.write(CHECKER)
    result = os.path.join(work, "result_%s.json" % label)
    env = dict(os.environ)
    env.pop("OBJECK_LIB_PATH", None)
    # cmd expands these in UTF-16, so the command line itself carries no
    # non-ASCII text through any code page
    env.update(OBJK_T_SCRIPT=script, OBJK_T_PY=sys.executable, OBJK_T_CHECK=checker, OBJK_T_RESULT=result)
    proc = subprocess.Popen('cmd.exe /d /k "chcp 437 >nul & call "%OBJK_T_SCRIPT%" & '
                            '"%OBJK_T_PY%" "%OBJK_T_CHECK%" "%OBJK_T_RESULT%""',
                            env=env, cwd=work, creationflags=subprocess.CREATE_NEW_CONSOLE)
    try:
        deadline = time.time() + 60
        while time.time() < deadline:
            if os.path.exists(result):
                time.sleep(0.2)
                try:
                    with open(result, encoding="utf-8") as f:
                        return json.load(f)
                except ValueError:
                    continue
            time.sleep(0.25)
        return None
    finally:
        proc.kill()


def same_dir(a, b):
    try:
        return a is not None and os.path.samefile(a, b)
    except OSError:
        return False


def main():
    # a failure detail can quote non-ASCII paths, and stdout may be a pipe in
    # the ANSI code page (it is on the hosted Windows runners)
    sys.stdout.reconfigure(errors="backslashreplace")
    if os.name != "nt":
        print("SKIP: SetObjEnv.cmd is Windows only")
        return 0

    work = tempfile.mkdtemp(prefix="set-obj-env-")
    try:
        exe = build(work)
        install = os.path.join(work, UNI_DIR)
        app = os.path.join(install, "app")
        bin_dir = os.path.join(install, "bin")
        lib_dir = os.path.join(install, "lib")
        for d in (app, bin_dir, lib_dir):
            os.makedirs(d)

        print("SetObjEnv.cmd for an install under '%s':" % UNI_DIR.encode("ascii", "backslashreplace").decode())
        script = os.path.join(work, "SetObjEnv.cmd")
        rc = subprocess.run([exe, script, app]).returncode
        if not check("the launcher's generator writes the script", rc == 0 and os.path.exists(script), "rc=%d" % rc):
            return 1

        got = run_in_console(script, work, "new")
        if not check("the script runs to completion in a code page 437 console", got is not None, "timed out"):
            return 1
        check("OBJECK_LIB_PATH names the install's lib directory",
              same_dir(got["lib"], lib_dir), "got %r" % got["lib"])
        check("PATH ends with the install's bin directory",
              same_dir(got["last_path"], bin_dir), "got %r" % got["last_path"])
        check("the console is back on code page 437 afterwards", got["cp"] == 437, "cp=%r" % got["cp"])

        # control: the script as it used to be written must fail here
        old = os.path.join(work, "SetObjEnv_old.cmd")
        with open(old, "w", encoding="utf-8", newline="") as f:
            f.write("@echo off\r\n"
                    "set PATH=%%PATH%%;%s\\..\\bin\r\n"
                    "set OBJECK_LIB_PATH=%s\\..\\lib\r\n" % (app, app))
        got = run_in_console(old, work, "old")
        check("control: the old UTF-8 script does not reach the lib directory in this console",
              got is not None and not same_dir(got["lib"], lib_dir),
              "the old script worked, so this console is not reading in code page 437: %r" % (got,))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("\n%s" % ("all passed" if not failures else "%d failed" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
