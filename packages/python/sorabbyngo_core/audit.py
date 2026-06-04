"""
AuditLedger — hash-chained append-only audit log.

Two-tier audit chain:
  Tier 1 (userspace): SHA-256 JSONL chain in /var/log/sorabbyngo/
  Tier 2 (kernel):    SHA-256 chain from patch 0013, read via
                      /proc/sorabbyngo/audit_tail

Both chains include the NodeID so cross-tier correlation is possible
for Kenya DPA 2019 forensic queries.
"""
from __future__ import annotations
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional, Tuple

KERNEL_AUDIT_TAIL = Path("/proc/sorabbyngo/audit_tail")

class AuditLedger:
    def __init__(self, path: Path, node_id: Optional[str] = None):
        self.path    = Path(path)
        self.node_id = node_id or "unknown"
        self._prev_hash = "0" * 64
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # Resume chain from last entry
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
        # Anchor to kernel chain if available
        kernel_hash = self._read_kernel_tail()

        entry = {
            "timestamp":   datetime.now(timezone.utc).isoformat(),
            "event_type":  event_type,
            "node_id":     self.node_id,
            "data":        data,
            "prev_hash":   self._prev_hash,
            "kernel_hash": kernel_hash,   # None if kernel module not loaded
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
        if not self.path.exists():
            return []
        lines = self.path.read_text().strip().splitlines()
        return [json.loads(l) for l in lines[-n:]]

    @staticmethod
    def _read_kernel_tail() -> Optional[str]:
        """Read latest kernel audit chain hash for cross-tier anchoring."""
        if not KERNEL_AUDIT_TAIL.exists():
            return None
        try:
            return KERNEL_AUDIT_TAIL.read_text().strip()
        except OSError:
            return None
