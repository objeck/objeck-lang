from fastapi import APIRouter

from app.services.libmap import library_catalog

router = APIRouter()


@router.get("/api/libs")
async def list_libs():
    """Libraries a program can use. Informational: the server picks them from the
    program's `use` lines, so a client never has to send any."""
    return {"libs": library_catalog()}
