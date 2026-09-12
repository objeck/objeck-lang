import json
import logging
from pathlib import Path

from fastapi import APIRouter, HTTPException

from app.services.validator import validate_demo_id

router = APIRouter()

DEMOS_DIR = Path(__file__).parent.parent.parent.parent / "demos"
_demos_cache: dict | None = None


def load_demos() -> dict:
    global _demos_cache
    if _demos_cache is not None:
        return _demos_cache

    index_path = DEMOS_DIR / "index.json"
    if not index_path.exists():
        # Shape must match the success path below: list_demos returns
        # data["index"], so the old {"categories", "demos"} fallback made
        # every /api/demos call raise KeyError -- permanently, since the
        # bad value was cached.
        _demos_cache = {"index": {"categories": []}, "demos": {}}
        return _demos_cache

    with open(index_path) as f:
        index = json.load(f)

    demos = {}
    for category in index["categories"]:
        for demo in category["demos"]:
            demo_path = DEMOS_DIR / category["folder"] / demo["file"]
            code = ""
            if demo_path.exists():
                code = demo_path.read_text(encoding="utf-8")
            else:
                # A renamed or deleted demo used to load an empty editor with no
                # error anywhere. Still serve the index, but say so in the log.
                logging.getLogger(__name__).warning(
                    "demo %s: missing file %s", demo["id"], demo_path
                )
            demos[demo["id"]] = {
                **demo,
                "category": category["name"],
                "code": code,
            }

    _demos_cache = {"index": index, "demos": demos}
    return _demos_cache


@router.get("/api/demos")
async def list_demos():
    data = load_demos()
    return data["index"]


@router.get("/api/demos/{demo_id}")
async def get_demo(demo_id: str):
    if not validate_demo_id(demo_id):
        raise HTTPException(status_code=400, detail="Invalid demo ID")

    data = load_demos()
    if demo_id not in data["demos"]:
        raise HTTPException(status_code=404, detail="Demo not found")
    return data["demos"][demo_id]
