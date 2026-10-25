import re

from app.config import settings
from app.services.libmap import (
    Refusal,
    block_reason,
    canonical_lib,
    lib_status,
)
from app.services.source_scan import scan

# Only allow simple alphanumeric lib names (plus the .obl a user might paste)
_LIB_NAME_RE = re.compile(r"^[a-z][a-z0-9_]{0,30}(\.obl)?$")

# Only allow alphanumeric share IDs (base64url chars)
SHARE_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,20}$")

# Only allow simple demo IDs
DEMO_ID_RE = re.compile(r"^[a-z][a-z0-9-]{0,40}$")

MAX_LIBS = 10


def validate_libs(requested_libs: list[str]) -> tuple[bool, str, list[str]]:
    """Validate the optional manual library override ('advanced' in the UI).

    Normal use never reaches this with anything: libraries are read off the
    program's `use` lines. Old share links stored the user's ticked libraries
    here, and those still load.
    """
    if len(requested_libs) > MAX_LIBS:
        return False, f"Too many libraries (max {MAX_LIBS})", []

    sanitized: list[str] = []
    for lib in requested_libs:
        lib = lib.strip().lower()
        if not lib:
            continue

        if not _LIB_NAME_RE.match(lib):
            return False, f"Invalid library name: '{lib[:20]}'", []

        lib = canonical_lib(lib)
        status = lib_status(lib)

        if status == "blocked":
            if lib in settings.blocked_libs:
                return False, (
                    f"Library '{lib}' isn't available in the playground: {block_reason(lib)}."
                ), []
            return False, f"Unknown library '{lib}'", []

        if status == "dependency":
            return False, (
                f"Library '{lib}' isn't available in the playground: {block_reason(lib)}."
            ), []

        if status == "default":
            continue  # the compiler always links it

        if lib not in sanitized:
            sanitized.append(lib)

    return True, "", sanitized


# ---------------------------------------------------------------- source policy
#
# The container is the security boundary: no network, a read-only root, a 10 MB
# noexec tmpfs, no capabilities, a pid cap. This list is the courtesy layer in
# front of it. It exists so that a program reaching for the file system, a
# process or a socket gets "X isn't available in the playground" instead of an
# obscure failure inside the sandbox -- and so the common ways out are closed
# even if the container configuration regresses.
#
# Bundles that live inside lang.obl (System.IO.Filesystem, System.IO.Net,
# System.API) are refused by name in libmap.py; whole libraries by the allow list.
# What is left is individual calls and classes in bundles the program may use.
#
# Matched against the source with comments removed (strings kept, so a call
# hidden inside a "{$ ... }" interpolation is still seen).

_PROCESS = "it would run programs on the host"
_FILES = "it reads or writes files"

_BLOCKED_CODE: list[tuple[re.Pattern, str, str]] = [
    # processes and the environment (Runtime->Execute is the pre-2026 spelling)
    (re.compile(r"\bRuntime\s*->\s*(Command|CommandOutput|Execute|IsProcessAlive)\b"), "Runtime->{0}", _PROCESS),
    (re.compile(r"\bCommandOutput\b"), "CommandOutput", _PROCESS),
    (re.compile(r"\bRuntime\s*->\s*Exit\b"), "Runtime->Exit", "a program ends by returning from Main"),
    (re.compile(r"\bRuntime\s*->\s*(GetEnvironmentVariable|SetEnvironmentVariable)\b"), "Runtime->{0}", "the environment belongs to the host"),
    # native code
    (re.compile(r"\bDllProxy\b"), "DllProxy", "it loads native libraries"),
    # named pipes live in System.IO, which every program imports implicitly
    (re.compile(r"\bPipe\s*->"), "Pipe", "it talks to other processes"),
    # classes that touch sockets or files, whether or not the bundle was named
    (re.compile(r"\b(TCPSocket|TCPSecureSocket|UDPSocket|DTLSSocket|SecureSocket|HttpClient|HttpsClient|Http2Client|Http3Client)\b"), "{0}", "the sandbox has no network access"),
    # Query.Structured sources that open a path or a URL, and its on-disk store
    (re.compile(r"\b(TableStore|OtabFile|FilesystemSource|RemoteSource|HttpJsonSource|CsvSource|TextSource|JsonLinesSource|JsonSource|XmlSource|ArchiveSource|XlsxSource|Finder)\b"), "{0}", "it reads files or URLs"),
]

# 'select * from 'runs.csv'' / 'join 'meta.csv' m': a quoted name after FROM or
# JOIN is a file or a URL, not a table. Catalog->SetFileAccess(false) would turn
# this off, but only a program can call it on its own catalog, so it cannot be
# enforced from outside.
_SQL_FILE_SOURCE = re.compile(r"\b(from|join)\s+'", re.I)


def check_code(code: str) -> list[Refusal]:
    """Everything in `code` that the sandbox will not allow, with line numbers."""
    scanned = scan(code)
    refusals: list[Refusal] = []
    seen: set[str] = set()

    for pattern, label, reason in _BLOCKED_CODE:
        for m in pattern.finditer(scanned.code):
            what = label.format(*(g for g in m.groups() if g)) if "{" in label else label
            if what in seen:
                continue
            if what == "CommandOutput" and "Runtime->CommandOutput" in seen:
                continue  # the same finding, reported once
            seen.add(what)
            refusals.append(Refusal(what, reason, scanned.line_of(m.start())))

    for line, text in scanned.strings:
        if _SQL_FILE_SOURCE.search(text) and "query file" not in seen:
            seen.add("query file")
            refusals.append(Refusal("A query that names a file or URL", _FILES, line))

    return refusals


def validate_code(code: str) -> tuple[bool, str]:
    """Validate source code before execution."""
    if not code or not code.strip():
        return False, "Code cannot be empty"

    if len(code) > settings.max_code_size:
        return False, f"Code exceeds maximum size of {settings.max_code_size} bytes"

    # Reject control characters (allow tab, newline, carriage return)
    for ch in code:
        if ord(ch) < 32 and ch not in ('\t', '\n', '\r'):
            return False, "Code contains invalid control characters"

    refusals = check_code(code)
    if refusals:
        return False, refusals[0].message()

    return True, ""


def validate_share_id(share_id: str) -> bool:
    """Validate share ID format."""
    return bool(SHARE_ID_RE.match(share_id))


def validate_demo_id(demo_id: str) -> bool:
    """Validate demo ID format."""
    return bool(DEMO_ID_RE.match(demo_id))
