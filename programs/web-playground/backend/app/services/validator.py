import re

from app.config import settings

# Normalize library name aliases
LIB_ALIASES = {
    "collect": "gen_collect",
}

# Only allow simple alphanumeric lib names
_LIB_NAME_RE = re.compile(r"^[a-z][a-z0-9_]{0,30}$")

# Only allow alphanumeric share IDs (base64url chars)
SHARE_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,20}$")

# Only allow simple demo IDs
DEMO_ID_RE = re.compile(r"^[a-z][a-z0-9-]{0,40}$")

MAX_LIBS = 10


def validate_libs(requested_libs: list[str]) -> tuple[bool, str, list[str]]:
    """Validate and sanitize requested libraries."""
    if len(requested_libs) > MAX_LIBS:
        return False, f"Too many libraries (max {MAX_LIBS})", []

    sanitized = []
    for lib in requested_libs:
        lib = lib.strip().lower()
        if not lib:
            continue

        if not _LIB_NAME_RE.match(lib):
            return False, f"Invalid library name: '{lib[:20]}'", []

        # Normalize aliases
        lib = LIB_ALIASES.get(lib, lib)

        if lib in settings.blocked_libs:
            return False, f"Library '{lib}' is not available in the playground", []

        if lib not in settings.allowed_libs:
            return False, f"Unknown library '{lib}'", []

        sanitized.append(lib)

    return True, "", list(set(sanitized))


_BLOCKED_PATTERNS = [
    "Runtime->Execute",
    "Runtime->Exit",
    "System.IO.File",
    "System.IO.FileReader",
    "System.IO.FileWriter",
    "System.IO.Directory",
    "System.IO.Pipe",
    "TCPSocket",
    "UDPSocket",
    "SecureSocket",
    "HttpClient",
    "HttpsClient",
]


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

    # Block dangerous API calls
    for pattern in _BLOCKED_PATTERNS:
        if pattern in code:
            return False, f"'{pattern}' is not allowed in the playground"

    return True, ""


def validate_share_id(share_id: str) -> bool:
    """Validate share ID format."""
    return bool(SHARE_ID_RE.match(share_id))


def validate_demo_id(demo_id: str) -> bool:
    """Validate demo ID format."""
    return bool(DEMO_ID_RE.match(demo_id))
