"""
NodeIdentity — Hardware node identity management for Sorabbyngo OS.
"""
from __future__ import annotations
import hashlib
import os
import socket
from pathlib import Path
from typing import Optional


NODEID_PATH  = Path("/var/lib/sorabbyngo/nodeid")
PROC_NODEID  = Path("/proc/sorabbyngo/nodeid")


class NodeIdentity:
    """
    Manages the Sorabbyngo hardware node identity.
    NodeID is derived from hardware characteristics and sealed at first boot.
    """

    def __init__(self):
        self._id: Optional[str] = None

    @property
    def node_id(self) -> str:
        if self._id is None:
            self._id = self._load_or_generate()
        return self._id

    def _load_or_generate(self) -> str:
        """Load sealed NodeID or generate from env/hostname."""
        if NODEID_PATH.exists():
            return NODEID_PATH.read_text().strip()
        env_id = os.environ.get("SORABBYNGO_NODE_ID")
        if env_id:
            return env_id
        # Fallback — derive from hostname
        return self._derive_from_hostname()

    def _derive_from_hostname(self) -> str:
        hostname = socket.gethostname()
        return hashlib.sha256(hostname.encode()).hexdigest()[:32]

    def verify(self) -> bool:
        """
        Verify live kernel NodeID matches sealed NodeID.
        Returns True if match or if kernel module not loaded.
        """
        if not PROC_NODEID.exists():
            return True
        try:
            live   = PROC_NODEID.read_text().strip()
            sealed = self.node_id
            return live == sealed
        except FileNotFoundError:
            return True

    def short_id(self) -> str:
        return self.node_id[:12]

    def __str__(self) -> str:
        return self.node_id
