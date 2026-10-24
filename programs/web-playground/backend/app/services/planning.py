"""Decide, before any container starts, what a run needs and whether it may happen."""

from dataclasses import dataclass, field

from app.config import settings
from app.services.libmap import Analysis, analyze
from app.services.validator import check_code, validate_libs


@dataclass
class RunPlan:
    ok: bool
    error: str = ""                                          # why not, in user-facing words
    libs: list[str] = field(default_factory=list)            # libraries the program uses
    link_libs: list[str] = field(default_factory=list)       # what to pass to obc -lib
    analysis: Analysis = field(default_factory=Analysis)


def plan(code: str, manual_libs: list[str] | None = None) -> RunPlan:
    """Check `code` and work out its libraries.

    `manual_libs` is the optional override from the UI's advanced section: extra
    libraries on top of the detected ones. It never loosens the policy.
    """
    if not code or not code.strip():
        return RunPlan(False, "Code cannot be empty")
    if len(code) > settings.max_code_size:
        return RunPlan(False, f"Code exceeds maximum size of {settings.max_code_size} bytes")
    for ch in code:
        if ord(ch) < 32 and ch not in ("\t", "\n", "\r"):
            return RunPlan(False, "Code contains invalid control characters")

    valid, error, extra = validate_libs(manual_libs or [])
    if not valid:
        return RunPlan(False, error)

    analysis = analyze(code, extra)
    refusals = sorted(analysis.refusals + check_code(code), key=lambda r: r.line or 1 << 30)
    if refusals:
        messages = list(dict.fromkeys(r.message() for r in refusals))
        error = "\n".join(messages[:4])
        return RunPlan(False, error, analysis=analysis)

    return RunPlan(True, libs=analysis.libs, link_libs=analysis.link_libs, analysis=analysis)
