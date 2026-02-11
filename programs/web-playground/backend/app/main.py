from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from slowapi import _rate_limit_exceeded_handler
from slowapi.errors import RateLimitExceeded
from slowapi import Limiter
from slowapi.util import get_remote_address

from app.config import settings
from app.routes import run, demos, share, health
from app.services.share_store import share_store


@asynccontextmanager
async def lifespan(app: FastAPI):
    await share_store.init_db()
    yield


app = FastAPI(
    title="Objeck Playground",
    version="1.0.0",
    lifespan=lifespan,
)

# Rate limiter
limiter = Limiter(key_func=get_remote_address)
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

# CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.allowed_origins,
    allow_credentials=False,
    allow_methods=["GET", "POST"],
    allow_headers=["Content-Type"],
)

# API routes
app.include_router(health.router)
app.include_router(run.router)
app.include_router(demos.router)
app.include_router(share.router)

# Serve frontend static files (for development; in production Caddy handles this)
frontend_dir = Path(__file__).parent.parent.parent / "frontend"
if frontend_dir.exists():
    app.mount(
        "/", StaticFiles(directory=str(frontend_dir), html=True), name="frontend"
    )
