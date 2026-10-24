"""The whole run pipeline, shared by /api/run and the demo test.

    plan (libraries + policy) -> sandbox -> diagnostics and hints
"""

import re

from app.models.schemas import RunResponse
from app.services.diagnostics import parse_diagnostics, tidy_output
from app.services.libmap import missing_bundle_hint
from app.services.planning import plan
from app.services.sandbox import run_code


_CRLF = re.compile(r"\r+\n")


async def execute(code: str, manual_libs: list[str] | None, timeout: int) -> RunResponse:
    run_plan = plan(code, manual_libs)
    if not run_plan.ok:
        # Refused before any container starts: the message says what and why.
        return RunResponse(
            success=False, output="", error=run_plan.error,
            blocked=True, libs=run_plan.analysis.libs,
        )

    result = await run_code(code, run_plan.link_libs, timeout)
    result.libs = run_plan.libs

    # CSV and Windows-style text end lines with CR LF, which the console on a
    # Windows dev machine doubles into blank lines. A browser wants plain LF.
    result.output = _CRLF.sub("\n", result.output)

    if result.compile_error:
        result.output = tidy_output(result.output)
        result.diagnostics = parse_diagnostics(result.output)
        result.hint = missing_bundle_hint(result.output, run_plan.analysis)

    return result
