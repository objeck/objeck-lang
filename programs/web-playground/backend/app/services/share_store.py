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
        # The id is content-derived, so re-sharing identical code is idempotent.
        # It was 6 bytes (48 bits) with INSERT OR IGNORE, which meant a collision
        # silently dropped the second program and served the FIRST one's code
        # under its URL -- a wrong answer with no error anywhere. 12 bytes makes
        # a collision negligible, and the read-back below turns any remaining one
        # into a distinct id rather than wrong content.
        content = code + json.dumps(sorted(libs))
        payload = json.dumps(libs)
        digest = hashlib.sha256(content.encode()).digest()

        async with aiosqlite.connect(self.db_path) as db:
            await db.execute("PRAGMA journal_mode=WAL")
            await db.execute("PRAGMA busy_timeout=5000")
            for attempt in range(4):
                share_id = base64.urlsafe_b64encode(
                    digest[attempt * 2 : attempt * 2 + 12]
                ).decode().rstrip("=")
                async with db.execute(
                    "SELECT code, libs FROM shares WHERE id = ?", (share_id,)
                ) as cursor:
                    row = await cursor.fetchone()
                if row is None:
                    await db.execute(
                        "INSERT OR IGNORE INTO shares (id, code, libs) VALUES (?, ?, ?)",
                        (share_id, code, payload),
                    )
                    await db.commit()
                    return share_id
                if row[0] == code and row[1] == payload:
                    return share_id  # same content, same link
                # a genuine collision: try the next window of the digest
            raise RuntimeError("share id collision could not be resolved")

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
