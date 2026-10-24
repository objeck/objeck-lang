"""Read obc's error output.

obc prints `<path>program.obs:(LINE,COL): message`, and may follow a message with
tab-indented lines of detail ("'Vector' is defined in bundle 'Collection' -- add:
use Collection;"). The path differs between the sandbox (/tmp/program.obs) and
local development, so only the file name is relied on.
"""

import re

from app.models.schemas import Diagnostic

_DIAG = re.compile(r"program\.obs:\((\d+),\s*(\d+)\):\s*(.*)$")
_PATH_PREFIX = re.compile(r"[^\r\n]*?[\\/](?=program\.obs:\()")


def tidy_output(text: str) -> str:
    """Show `program.obs:(3,10): ...` rather than the server's temp path."""
    return _PATH_PREFIX.sub("", text)


def parse_diagnostics(text: str) -> list[Diagnostic]:
    result: list[Diagnostic] = []
    current: Diagnostic | None = None

    for raw in text.splitlines():
        m = _DIAG.search(raw)
        if m:
            message = m.group(3).strip()
            severity = "error"
            if message.startswith("Warning:"):
                severity = "warning"
                message = message[len("Warning:"):].strip()
            current = Diagnostic(
                line=int(m.group(1)), column=int(m.group(2)),
                severity=severity, message=message,
            )
            result.append(current)
        elif current is not None and raw[:1] in ("\t", " ") and raw.strip():
            current.message += "\n" + raw.strip()
        else:
            current = None

    return result
