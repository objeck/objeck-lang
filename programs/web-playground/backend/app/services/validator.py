from app.config import settings

# Normalize library name aliases
LIB_ALIASES = {
    "collect": "gen_collect",
}


def validate_libs(requested_libs: list[str]) -> tuple[bool, str, list[str]]:
    """Validate and sanitize requested libraries.

    Returns (is_valid, error_message, sanitized_libs).
    """
    sanitized = []
    for lib in requested_libs:
        lib = lib.strip().lower()
        if not lib:
            continue

        # Normalize aliases
        lib = LIB_ALIASES.get(lib, lib)

        if lib in settings.blocked_libs:
            return False, f"Library '{lib}' is not available in the playground", []

        if lib not in settings.allowed_libs:
            return False, f"Unknown library '{lib}'", []

        sanitized.append(lib)

    return True, "", list(set(sanitized))


def validate_code(code: str) -> tuple[bool, str]:
    """Validate source code before execution."""
    if not code or not code.strip():
        return False, "Code cannot be empty"

    if len(code) > settings.max_code_size:
        return False, f"Code exceeds maximum size of {settings.max_code_size} bytes"

    if "\x00" in code:
        return False, "Code contains invalid characters"

    return True, ""
