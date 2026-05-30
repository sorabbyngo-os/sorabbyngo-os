"""
AuditLedger — hash-chained append-only audit log.
Extracted from sentinel_daemon.py into shared package.
"""
from __future__ import annotations
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Tuple


class AuditLedger:
    """
    Append-only, hash-chained JSONL audit ledger.
    Each entry contains a SHA-256 chain hash linking to the previous entry,
    making tampering detectable.
    """

    def __init__(self, path: Path):
        self.path = Path(path)
        self._prev_hash = "0" * 64
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self.path.exists():
            try:
                lines = self.path.read_text().strip().splitlines()
                if lines:
                    last = json.loads(lines[-1])
                    self._prev_hash = last.get("chain_hash", self._prev_hash)
            except Exception:
                pass

    def write(self, event_type: str, data: dict) -> str:
        """Append a chained entry. Returns the chain hash."""
        entry = {
            "timestamp":  datetime.now(timezone.utc).isoformat(),
            "event_type": event_type,
            "data":       data,
            "prev_hash":  self._prev_hash,
        }
        entry_str  = json.dumps(entry, sort_keys=True)
        chain_hash = hashlib.sha256(
            (self._prev_hash + entry_str).encode()
        ).hexdigest()
        entry["chain_hash"] = chain_hash
        self._prev_hash = chain_hash

        with open(self.path, "a") as f:
            f.write(json.dumps(entry) + "\n")

        return chain_hash

    def verify(self) -> Tuple[bool, int]:
        """Verify chain integrity. Returns (ok, entry_count)."""
        if not self.path.exists():
            return True, 0
        prev  = "0" * 64
        count = 0
        for line in self.path.read_text().strip().splitlines():
            try:
                entry       = json.loads(line)
                stored_hash = entry.pop("chain_hash")
                entry_str   = json.dumps(entry, sort_keys=True)
                expected    = hashlib.sha256(
                    (prev + entry_str).encode()
                ).hexdigest()
                if expected != stored_hash:
                    return False, count
                prev = stored_hash
                count += 1
            except Exception:
                return False, count
        return True, count

    def tail(self, n: int = 20) -> list:
        """Return last n entries."""
        if not self.path.exists():
            return []
        lines = self.path.read_text().strip().splitlines()
        return [json.loads(l) for l in lines[-n:]]
