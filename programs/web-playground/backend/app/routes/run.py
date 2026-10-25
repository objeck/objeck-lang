from fastapi import APIRouter, Request
from slowapi import Limiter
from slowapi.util import get_remote_address

from app.config import settings
from app.models.schemas import RunRequest, RunResponse
from app.services.runner import execute

router = APIRouter()
limiter = Limiter(key_func=get_remote_address)


@router.post("/api/run", response_model=RunResponse)
@limiter.limit(settings.rate_limit)
async def execute_code(request: Request, body: RunRequest):
    # Libraries are worked out from the program itself. body.libs is only an
    # optional extra (the UI's advanced override, or an old share link).
    return await execute(body.code, body.libs, body.timeout)
