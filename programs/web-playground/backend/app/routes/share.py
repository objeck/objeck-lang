from fastapi import APIRouter, HTTPException, Request
from slowapi import Limiter
from slowapi.util import get_remote_address

from app.config import settings
from app.models.schemas import ShareRequest, ShareResponse, ShareGetResponse
from app.services.share_store import share_store
from app.services.validator import validate_code, validate_libs

router = APIRouter()
limiter = Limiter(key_func=get_remote_address)


@router.post("/api/share", response_model=ShareResponse)
@limiter.limit("30/minute")
async def create_share(request: Request, body: ShareRequest):
    valid, error = validate_code(body.code)
    if not valid:
        raise HTTPException(status_code=400, detail=error)

    valid, error, sanitized_libs = validate_libs(body.libs)
    if not valid:
        raise HTTPException(status_code=400, detail=error)

    share_id = await share_store.create_share(body.code, sanitized_libs)

    # Build URL based on request origin
    origin = request.headers.get("origin", "https://playground.objeck.org")
    url = f"{origin}?s={share_id}"

    return ShareResponse(id=share_id, url=url)


@router.get("/api/share/{share_id}", response_model=ShareGetResponse)
async def get_share(share_id: str):
    result = await share_store.get_share(share_id)
    if result is None:
        raise HTTPException(status_code=404, detail="Share not found")

    code, libs = result
    return ShareGetResponse(code=code, libs=libs)
