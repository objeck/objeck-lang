from fastapi import APIRouter, HTTPException, Request
from slowapi import Limiter
from slowapi.util import get_remote_address

from app.config import settings
from app.models.schemas import ShareRequest, ShareResponse, ShareGetResponse
from app.services.share_store import share_store
from app.services.validator import validate_code, validate_libs, validate_share_id

router = APIRouter()
limiter = Limiter(key_func=get_remote_address)

# Allowed origins for share URL construction
_ALLOWED_SHARE_ORIGINS = {
    "https://playground.objeck.org",
    "https://www.objeck.org",
    "https://objeck.org",
    "http://localhost:8080",
    "http://localhost:8000",
}
_DEFAULT_SHARE_ORIGIN = "https://playground.objeck.org"


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

    # Only use origin if it's in the allow-list
    origin = request.headers.get("origin", "")
    if origin not in _ALLOWED_SHARE_ORIGINS:
        origin = _DEFAULT_SHARE_ORIGIN
    url = f"{origin}?s={share_id}"

    return ShareResponse(id=share_id, url=url)


@router.get("/api/share/{share_id}", response_model=ShareGetResponse)
async def get_share(share_id: str):
    if not validate_share_id(share_id):
        raise HTTPException(status_code=400, detail="Invalid share ID")

    result = await share_store.get_share(share_id)
    if result is None:
        raise HTTPException(status_code=404, detail="Share not found")

    code, libs = result
    return ShareGetResponse(code=code, libs=libs)
