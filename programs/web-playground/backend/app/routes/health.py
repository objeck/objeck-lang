from fastapi import APIRouter

from app.config import settings

router = APIRouter()


@router.get("/api/health")
async def health_check():
    return {"status": "ok", "version": settings.objeck_version}
