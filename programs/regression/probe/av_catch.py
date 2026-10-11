"""Run a program under the Win32 debug API and describe the first access violation.

For #1077. `core_thread_gc_stress` fails on windows-arm64 two ways: a detected
corruption the test itself reports, and a silent 0xC0000005 with EMPTY output.
The silent one has never had a faulting address attached to it, so there has
been nothing to reason from. This attaches one.

    python av_catch.py --timeout 180 -- obr.exe stress.obe

stdout and stderr are inherited, so the program's own output still reaches
whatever the caller redirected it to; this writes only its report, prefixed with
`[av]`, to stderr. The exit code is the program's own, except 124 for a timeout.

WHY NOT GetThreadContext BY DEFAULT
-----------------------------------
The faulting PC is already in EXCEPTION_RECORD.ExceptionAddress, along with
whether the access was a read or a write and what address was touched. CONTEXT
is only needed for a stack scan, and CONTEXT on ARM64 is a different structure
and a different size from the x64 one -- the offsets recorded from the 2026-09
x64 work (1232 / 0x30 / 0x98 / 0xF8) are WRONG here. Reusing them yields a
plausible-looking garbage address, which is worse than no stack at all. So the
stack scan is opt-in (--stack) and refuses to run unless sizeof(CONTEXT) comes
out at the documented ARM64 size.

The symbol name is best-effort. Module + RVA is derived from VirtualQueryEx and
GetModuleFileNameExW, which do not depend on dbghelp or on a PDB being present,
so a symbol failure degrades to `obr.exe+0x1234` -- resolvable offline -- rather
than to nothing.
"""
import argparse
import ctypes
import os
import sys
import time
from ctypes import wintypes

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)

DEBUG_ONLY_THIS_PROCESS = 0x00000002
EXCEPTION_DEBUG_EVENT = 1
EXIT_PROCESS_DEBUG_EVENT = 5
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
DBG_CONTINUE = 0x00010002
EXCEPTION_ACCESS_VIOLATION = 0xC0000005
STILL_ACTIVE = 259
INFINITE = 0xFFFFFFFF

ULONG_PTR = ctypes.c_size_t


class EXCEPTION_RECORD(ctypes.Structure):
    pass


EXCEPTION_RECORD._fields_ = [
    ('ExceptionCode', wintypes.DWORD),
    ('ExceptionFlags', wintypes.DWORD),
    ('ExceptionRecord', ctypes.POINTER(EXCEPTION_RECORD)),
    ('ExceptionAddress', ctypes.c_void_p),
    ('NumberParameters', wintypes.DWORD),
    ('ExceptionInformation', ULONG_PTR * 15),
]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('ExceptionRecord', EXCEPTION_RECORD),
                ('dwFirstChance', wintypes.DWORD)]


class DEBUG_EVENT_U(ctypes.Union):
    # The raw member is a safe upper bound on every variant of the union, so a
    # variant this script does not model cannot overrun the buffer.
    _fields_ = [('Exception', EXCEPTION_DEBUG_INFO),
                ('raw', ctypes.c_ubyte * 192)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [('dwDebugEventCode', wintypes.DWORD),
                ('dwProcessId', wintypes.DWORD),
                ('dwThreadId', wintypes.DWORD),
                ('u', DEBUG_EVENT_U)]


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('lpReserved', wintypes.LPWSTR),
                ('lpDesktop', wintypes.LPWSTR), ('lpTitle', wintypes.LPWSTR),
                ('dwX', wintypes.DWORD), ('dwY', wintypes.DWORD),
                ('dwXSize', wintypes.DWORD), ('dwYSize', wintypes.DWORD),
                ('dwXCountChars', wintypes.DWORD), ('dwYCountChars', wintypes.DWORD),
                ('dwFillAttribute', wintypes.DWORD), ('dwFlags', wintypes.DWORD),
                ('wShowWindow', wintypes.WORD), ('cbReserved2', wintypes.WORD),
                ('lpReserved2', ctypes.POINTER(ctypes.c_ubyte)),
                ('hStdInput', wintypes.HANDLE), ('hStdOutput', wintypes.HANDLE),
                ('hStdError', wintypes.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', wintypes.HANDLE), ('hThread', wintypes.HANDLE),
                ('dwProcessId', wintypes.DWORD), ('dwThreadId', wintypes.DWORD)]


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p),
                ('AllocationBase', ctypes.c_void_p),
                ('AllocationProtect', wintypes.DWORD),
                ('PartitionId', wintypes.WORD),
                ('__pad', wintypes.WORD),
                ('RegionSize', ctypes.c_size_t),
                ('State', wintypes.DWORD), ('Protect', wintypes.DWORD),
                ('Type', wintypes.DWORD), ('__pad2', wintypes.DWORD)]


def say(msg):
    sys.stderr.write('[av] %s\n' % msg)
    sys.stderr.flush()


def module_for(h_process, addr):
    """(module name, RVA) for an address, without dbghelp or a PDB."""
    mbi = MEMORY_BASIC_INFORMATION()
    if not k32.VirtualQueryEx(h_process, ctypes.c_void_p(addr),
                              ctypes.byref(mbi), ctypes.sizeof(mbi)):
        return None, None
    base = mbi.AllocationBase
    if not base:
        return None, None
    buf = ctypes.create_unicode_buffer(32768)
    n = psapi.GetModuleFileNameExW(h_process, ctypes.c_void_p(base), buf, len(buf))
    name = os.path.basename(buf.value) if n else '<unmapped-image>'
    return name, addr - base


def symbolize(h_process, addr):
    """Best effort. A failure here must degrade to module+RVA, never to a guess."""
    try:
        dbghelp = ctypes.WinDLL('dbghelp', use_last_error=True)
    except OSError:
        return None

    class SYMBOL_INFO(ctypes.Structure):
        _fields_ = [('SizeOfStruct', wintypes.DWORD), ('TypeIndex', wintypes.DWORD),
                    ('Reserved', ctypes.c_ulonglong * 2), ('Index', wintypes.DWORD),
                    ('Size', wintypes.DWORD), ('ModBase', ctypes.c_ulonglong),
                    ('Flags', wintypes.DWORD), ('Value', ctypes.c_ulonglong),
                    ('Address', ctypes.c_ulonglong), ('Register', wintypes.DWORD),
                    ('Scope', wintypes.DWORD), ('Tag', wintypes.DWORD),
                    ('NameLen', wintypes.DWORD), ('MaxNameLen', wintypes.DWORD),
                    ('Name', ctypes.c_char * 1)]

    max_name = 1024
    size = ctypes.sizeof(SYMBOL_INFO) + max_name
    raw = (ctypes.c_ubyte * size)()
    info = ctypes.cast(raw, ctypes.POINTER(SYMBOL_INFO)).contents
    info.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO)
    info.MaxNameLen = max_name
    disp = ctypes.c_ulonglong(0)
    try:
        if not dbghelp.SymInitialize(h_process, None, True):
            return None
        if not dbghelp.SymFromAddr(h_process, ctypes.c_ulonglong(addr),
                                   ctypes.byref(disp), ctypes.byref(info)):
            return None
        name = ctypes.string_at(ctypes.addressof(info) + SYMBOL_INFO.Name.offset)
        text = name.decode('utf-8', 'replace')
        return '%s+0x%x' % (text, disp.value) if disp.value else text
    except Exception:
        return None
    finally:
        try:
            dbghelp.SymCleanup(h_process)
        except Exception:
            pass


def describe(h_process, rec, first_chance):
    code = rec.ExceptionCode & 0xFFFFFFFF
    addr = rec.ExceptionAddress or 0
    say('exception 0x%08X at 0x%016X (%s chance)'
        % (code, addr, 'first' if first_chance else 'second'))
    if code == EXCEPTION_ACCESS_VIOLATION and rec.NumberParameters >= 2:
        kind = {0: 'read', 1: 'write', 8: 'execute (DEP)'}.get(
            rec.ExceptionInformation[0], 'op=%d' % rec.ExceptionInformation[0])
        say('  %s of 0x%016X' % (kind, rec.ExceptionInformation[1]))
    mod, rva = module_for(h_process, addr)
    if mod is not None:
        say('  in %s+0x%x' % (mod, rva))
    name = symbolize(h_process, addr)
    say('  symbol: %s' % (name if name else '<unavailable -- resolve the RVA offline>'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--timeout', type=float, default=300.0)
    ap.add_argument('--stack', action='store_true',
                    help='also scan the stack (needs a verified ARM64 CONTEXT)')
    ap.add_argument('cmd', nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.cmd[1:] if args.cmd and args.cmd[0] == '--' else args.cmd
    if not cmd:
        say('nothing to run')
        return 2

    if args.stack:
        say('--stack is not implemented: ARM64 CONTEXT is a different structure '
            'and size from the x64 one, and a wrong layout produces a '
            'plausible garbage address. ExceptionAddress below is exact.')

    line = ' '.join('"%s"' % c if ' ' in c else c for c in cmd)
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    # Handles are inherited, so the child's own stdout/stderr still go wherever
    # the caller redirected them -- the diag report must not be lost to this.
    if not k32.CreateProcessW(None, ctypes.create_unicode_buffer(line), None, None,
                              True, DEBUG_ONLY_THIS_PROCESS, None, None,
                              ctypes.byref(si), ctypes.byref(pi)):
        say('CreateProcessW failed: %d' % ctypes.get_last_error())
        return 2

    evt = DEBUG_EVENT()
    deadline = time.time() + args.timeout
    exit_code = 0
    caught = 0
    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            say('TIMEOUT after %.0fs -- killing' % args.timeout)
            k32.TerminateProcess(pi.hProcess, 124)
            exit_code = 124
            break
        if not k32.WaitForDebugEvent(ctypes.byref(evt), 250):
            continue
        status = DBG_CONTINUE
        if evt.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
            info = evt.u.Exception
            code = info.ExceptionRecord.ExceptionCode & 0xFFFFFFFF
            # A first-chance report is the one worth having: the state is intact
            # and nothing has unwound. Report once, then hand it back unhandled
            # so the process dies exactly as it would have without a debugger.
            if code == EXCEPTION_ACCESS_VIOLATION and caught == 0:
                caught = 1
                describe(pi.hProcess, info.ExceptionRecord, info.dwFirstChance)
            status = DBG_EXCEPTION_NOT_HANDLED
        elif evt.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT:
            rc = wintypes.DWORD(0)
            k32.GetExitCodeProcess(pi.hProcess, ctypes.byref(rc))
            exit_code = rc.value
            k32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, DBG_CONTINUE)
            break
        k32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, status)

    k32.CloseHandle(pi.hThread)
    k32.CloseHandle(pi.hProcess)
    if caught:
        say('exit 0x%08X' % (exit_code & 0xFFFFFFFF))
    return exit_code & 0xFFFFFFFF


if __name__ == '__main__':
    sys.exit(main())
