"""
SorabbyngoNats — Thin async NATS wrapper for all Sorabbyngo services.
"""
from __future__ import annotations
import json
import logging
from typing import Any, Callable, Optional

logger = logging.getLogger("sorabbyngo.nats")

try:
    import nats as nats_lib
    NATS_AVAILABLE = True
except ImportError:
    NATS_AVAILABLE = False


class SorabbyngoNats:
    """
    Async NATS client wrapper.
    All services use this instead of raw nats-py to ensure
    consistent reconnect behavior, error handling, and message format.
    """

    # Standard subjects
    SUBJECTS = {
        "cw_alert":   "sorabbyngo.cyberwall.alert",
        "cw_block":   "sorabbyngo.cyberwall.block",
        "cw_status":  "sorabbyngo.cyberwall.status",
        "heartbeat":  "sorabbyngo.{service}.heartbeat",
        "cmd":        "sorabbyngo.sentinel.command",
        "health":     "sorabbyngo.sentinel.health",
        "escalation": "sorabbyngo.sentinel.escalation",
        "agent_alert":"sorabbyngo.agent.alert",
        "mesh_peer":  "sorabbyngo.byngonet.peer",
        "ai_request": "sorabbyngo.byngox.request",
        "ai_response":"sorabbyngo.byngox.response",
    }

    def __init__(self, url: str = "nats://localhost:4222", service_name: str = "unknown"):
        self._url          = url
        self._service_name = service_name
        self._nc           = None

    async def connect(self) -> bool:
        if not NATS_AVAILABLE:
            logger.warning("nats-py not installed — offline mode")
            return False
        try:
            self._nc = await nats_lib.connect(
                self._url,
                name=self._service_name,
                reconnect_time_wait=3,
                max_reconnect_attempts=-1,
            )
            logger.info(f"Connected to NATS: {self._url}")
            return True
        except Exception as e:
            logger.error(f"NATS connection failed: {e}")
            return False

    async def publish(self, subject: str, data: dict) -> bool:
        if not self._nc:
            return False
        try:
            await self._nc.publish(subject, json.dumps(data).encode())
            return True
        except Exception as e:
            logger.error(f"Publish failed [{subject}]: {e}")
            return False

    async def subscribe(self, subject: str, callback: Callable) -> bool:
        if not self._nc:
            return False
        try:
            await self._nc.subscribe(subject, cb=callback)
            return True
        except Exception as e:
            logger.error(f"Subscribe failed [{subject}]: {e}")
            return False

    async def heartbeat(self, extra: Optional[dict] = None) -> bool:
        """Publish a standard heartbeat for this service."""
        subject = self.SUBJECTS["heartbeat"].format(service=self._service_name)
        data = {"service": self._service_name, **(extra or {})}
        return await self.publish(subject, data)

    async def alert(self, level: str, threat_type: str, description: str,
                    score: int = 0, data: Optional[dict] = None) -> bool:
        """Publish a cyberwall alert."""
        return await self.publish(self.SUBJECTS["cw_alert"], {
            "level":       level,
            "threat_type": threat_type,
            "description": description,
            "score":       score,
            "source":      self._service_name,
            "data":        data or {},
        })

    async def close(self):
        if self._nc:
            await self._nc.close()
            self._nc = None
