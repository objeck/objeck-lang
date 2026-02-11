from fastapi import APIRouter, Request
from slowapi import Limiter
from slowapi.util import get_remote_address

from app.config import settings
from app.models.schemas import RunRequest, RunResponse
from app.services.sandbox import run_code
from app.services.validator import validate_code, validate_libs

router = APIRouter()
limiter = Limiter(key_func=get_remote_address)


@router.post("/api/run", response_model=RunResponse)
@limiter.limit(settings.rate_limit)
async def execute_code(request: Request, body: RunRequest):
    # Validate code
    valid, error = validate_code(body.code)
    if not valid:
        return RunResponse(success=False, output="", error=error)

    # Validate libraries
    valid, error, sanitized_libs = validate_libs(body.libs)
    if not valid:
        return RunResponse(success=False, output="", error=error)

    # Execute in sandbox
    return await run_code(body.code, sanitized_libs, body.timeout)
