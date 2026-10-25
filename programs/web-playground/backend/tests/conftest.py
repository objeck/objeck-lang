import os
import sys
import tempfile
from pathlib import Path

# make `import app...` work no matter where pytest is started from
BACKEND = Path(__file__).resolve().parents[1]
if str(BACKEND) not in sys.path:
    sys.path.insert(0, str(BACKEND))
TESTS = Path(__file__).resolve().parent
if str(TESTS) not in sys.path:
    sys.path.insert(0, str(TESTS))

# Settings are read when app.config is first imported, so set them before any
# test module does: a generous rate limit (the API tests make many calls) and a
# scratch share database instead of ./data/shares.db.
os.environ.setdefault("PLAYGROUND_RATE_LIMIT", "10000/minute")
os.environ.setdefault("PLAYGROUND_SHARE_DB_PATH", str(Path(tempfile.mkdtemp(prefix="pg_shares_")) / "shares.db"))
