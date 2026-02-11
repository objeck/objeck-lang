import hashlib
import base64
import json
from pathlib import Path

import aiosqlite

from app.config import settings


class ShareStore:
    def __init__(self):
        self.db_path = settings.share_db_path

    async def init_db(self):
        Path(self.db_path).parent.mkdir(parents=True, exist_ok=True)
        async with aiosqlite.connect(self.db_path) as db:
            await db.execute(
                """
                CREATE TABLE IF NOT EXISTS shares (
                    id TEXT PRIMARY KEY,
                    code TEXT NOT NULL,
                    libs TEXT NOT NULL DEFAULT '[]',
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
                """
            )
            await db.commit()

    async def create_share(self, code: str, libs: list[str]) -> str:
        content = code + json.dumps(sorted(libs))
        hash_bytes = hashlib.sha256(content.encode()).digest()
        share_id = base64.urlsafe_b64encode(hash_bytes[:6]).decode().rstrip("=")

        async with aiosqlite.connect(self.db_path) as db:
            await db.execute(
                "INSERT OR IGNORE INTO shares (id, code, libs) VALUES (?, ?, ?)",
                (share_id, code, json.dumps(libs)),
            )
            await db.commit()

        return share_id

    async def get_share(self, share_id: str) -> tuple[str, list[str]] | None:
        async with aiosqlite.connect(self.db_path) as db:
            async with db.execute(
                "SELECT code, libs FROM shares WHERE id = ?", (share_id,)
            ) as cursor:
                row = await cursor.fetchone()
                if row:
                    return row[0], json.loads(row[1])
                return None


share_store = ShareStore()
