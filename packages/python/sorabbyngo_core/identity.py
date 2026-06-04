"""
NodeIdentity — Hardware node identity for Sorabbyngo OS.
All three bindings (C, Rust, Python) read the same paths in the same order.
Fallback to hostname is DISABLED in prod — raises if kernel module absent.
"""
from __future__ import annotations
import hashlib
import os
import socket
from pathlib import Path
from typing import Optional

NODEID_PATH = Path("/var/lib/sorabbyngo/nodeid")
PROC_NODEID = Path("/proc/sorabbyngo/nodeid")
ENV_NODEID  = "SORABBYNGO_NODE_ID"

class NodeIDError(Exception):
    pass

class NodeIdentity:
    def __init__(self, allow_fallback: bool = False):
        """
        allow_fallback: permit hostname-derived ID when module not loaded.
        Always False in prod. Set True only in dev/test environments.
        """
        self._id: Optional[str] = None
        self._allow_fallback = allow_fallback
        self._source: Optional[str] = None

    @property
    def node_id(self) -> str:
        if self._id is None:
            self._id, self._source = self._load()
        return self._id

    @property
    def source(self) -> str:
        """Where the NodeID came from: 'sealed', 'env', 'fallback'"""
        if self._source is None:
            _ = self.node_id
        return self._source

    def _load(self) -> tuple[str, str]:
        # 1. Sealed storage (first boot writes this from kernel)
        if NODEID_PATH.exists():
            return NODEID_PATH.read_text().strip(), "sealed"
        # 2. Environment override (dev/CI only)
        env_id = os.environ.get(ENV_NODEID)
        if env_id:
            return env_id.strip(), "env"
        # 3. Fallback — only in non-prod
        if self._allow_fallback:
            return self._derive_from_hostname(), "fallback"
        raise NodeIDError(
            f"NodeID unavailable: {NODEID_PATH} not found and "
            f"SORABBYNGO_NODE_ID not set. "
            f"Is the sorabbyngo_nodeid kernel module loaded?"
        )

    def verify(self) -> bool:
        """
        Verify live kernel NodeID matches sealed NodeID.
        Returns True on match. Raises NodeIDError on mismatch.
        Returns True if kernel module not loaded (non-prod only).
        """
        if not PROC_NODEID.exists():
            if self._allow_fallback:
                return True
            raise NodeIDError(
                f"{PROC_NODEID} not present — sorabbyngo_nodeid not loaded"
            )
        live   = PROC_NODEID.read_text().strip()
        sealed = self.node_id
        if live != sealed:
            raise NodeIDError(
                f"NodeID MISMATCH: live={live[:16]}... sealed={sealed[:16]}..."
            )
        return True

    def short_id(self) -> str:
        return self.node_id[:12]

    @staticmethod
    def _derive_from_hostname() -> str:
        """
        Deterministic fallback — SHA-256(hostname), same algorithm
        as C and Rust fallback paths for cross-language consistency.
        """
        hostname = socket.gethostname()
        return hashlib.sha256(hostname.encode()).hexdigest()

    def __str__(self) -> str:
        return self.node_id
