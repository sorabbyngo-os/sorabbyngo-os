#!/usr/bin/env python3
"""
Sorabbyngo OS — Sentinel Daemon
Central watchdog for the entire Sorabbyngo ecosystem.
"""
import asyncio
import argparse
import json
import logging
import os
import signal
import sys
import hashlib
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import nats
    NATS_AVAILABLE = True
except ImportError:
    NATS_AVAILABLE = False

LOG_DIR = Path("/var/log/sorabbyngo")
LOG_DIR.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s — %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(LOG_DIR / "sentinel.log"),
    ]
)
logger = logging.getLogger("sentinel")

BANNER = """
╔══════════════════════════════════════════════════════════╗
║       Sorabbyngo Sentinel — System Watchdog              ║
║       Rhythm of Digital Life                             ║
╚══════════════════════════════════════════════════════════╝
"""

NODEID_PATH = Path("/var/lib/sorabbyngo/nodeid")
PROC_NODEID = Path("/proc/sorabbyngo/nodeid")
AUDIT_LOG   = LOG_DIR / "sentinel_audit.jsonl"

WATCHED_SERVICES = [
    "cyberwall", "byngox", "chameha",
    "cml", "novela", "byngonet",
]

SUBJECTS = {
    "cw_alert":   "sorabbyngo.cyberwall.alert",
    "cw_block":   "sorabbyngo.cyberwall.block",
    "cw_status":  "sorabbyngo.cyberwall.status",
    "heartbeat":  "sorabbyngo.*.heartbeat",
    "cmd":        "sorabbyngo.sentinel.command",
    "health":     "sorabbyngo.sentinel.health",
    "escalation": "sorabbyngo.sentinel.escalation",
}


class AuditLedger:
    def __init__(self, path: Path):
        self.path = path
        self._prev_hash = "0" * 64
        if path.exists():
            try:
                lines = path.read_text().strip().splitlines()
                if lines:
                    last = json.loads(lines[-1])
                    self._prev_hash = last.get("chain_hash", self._prev_hash)
            except Exception:
                pass

    def write(self, event_type: str, data: dict):
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

    def verify(self) -> tuple:
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


class ServiceRegistry:
    def __init__(self, timeout_seconds: int = 90):
        self._services: dict = {}
        self._timeout = timeout_seconds

    def heartbeat(self, service: str):
        self._services[service] = time.monotonic()

    def is_alive(self, service: str) -> bool:
        last = self._services.get(service)
        if last is None:
            return False
        return (time.monotonic() - last) < self._timeout

    def dead_services(self) -> list:
        return [s for s in WATCHED_SERVICES if not self.is_alive(s)]

    def status(self) -> dict:
        now = time.monotonic()
        return {
            s: {
                "alive": self.is_alive(s),
                "last_seen_seconds_ago": round(now - self._services[s], 1)
                    if s in self._services else None,
            }
            for s in WATCHED_SERVICES
        }


class SentinelDaemon:
    def __init__(self, nats_url: str = "nats://localhost:4222"):
        self.nats_url      = nats_url
        self._nc           = None
        self._running      = False
        self.node_id       = self._read_node_id()
        self.ledger        = AuditLedger(AUDIT_LOG)
        self.registry      = ServiceRegistry()
        self._threat_counts: dict = {}

    def _read_node_id(self) -> str:
        try:
            return NODEID_PATH.read_text().strip()
        except FileNotFoundError:
            return os.environ.get("SORABBYNGO_NODE_ID", "unknown")

    def _verify_node_id(self) -> bool:
        if not PROC_NODEID.exists():
            return True
        try:
            live   = PROC_NODEID.read_text().strip()
            sealed = NODEID_PATH.read_text().strip()
            if live != sealed:
                logger.critical("NODE ID MISMATCH — hardware may have changed")
                self.ledger.write("nodeid_mismatch", {"live": live, "sealed": sealed})
                return False
            return True
        except FileNotFoundError:
            return True

    async def _connect(self) -> bool:
        if not NATS_AVAILABLE:
            logger.warning("nats-py not installed — log-only mode")
            return False
        try:
            self._nc = await nats.connect(
                self.nats_url,
                name=f"sentinel-{self.node_id[:8]}",
                reconnect_time_wait=3,
                max_reconnect_attempts=-1,
            )
            logger.info(f"Connected to NATS at {self.nats_url}")
            return True
        except Exception as e:
            logger.error(f"NATS connection failed: {e}")
            return False

    async def _subscribe_all(self):
        if not self._nc:
            return
        await self._nc.subscribe(SUBJECTS["cw_alert"],  cb=self._on_cyberwall_alert)
        await self._nc.subscribe(SUBJECTS["cw_block"],  cb=self._on_cyberwall_block)
        await self._nc.subscribe(SUBJECTS["cw_status"], cb=self._on_cyberwall_status)
        await self._nc.subscribe(SUBJECTS["heartbeat"], cb=self._on_heartbeat)
        await self._nc.subscribe(SUBJECTS["cmd"],       cb=self._on_command)
        logger.info("Subscribed to all NATS subjects")

    async def _on_cyberwall_alert(self, msg):
        try:
            data = json.loads(msg.data.decode())
        except Exception:
            return
        level       = data.get("level", "INFO")
        threat_type = data.get("threat_type", "unknown")
        score       = data.get("score", 0)
        description = data.get("description", "")
        logger.warning(f"THREAT [{level}] score={score} | {threat_type} | {description}")
        self.ledger.write("threat_alert", data)

        src = data.get("data", {}).get("src_ip", data.get("source", "unknown"))
        self._threat_counts[src] = self._threat_counts.get(src, 0) + 1
        if self._threat_counts[src] >= 3 and level in ("HIGH", "CRITICAL"):
            await self._escalate(data, reason=f"Repeated threats from {src}")

        await self._publish_health()

    async def _on_cyberwall_block(self, msg):
        try:
            data = json.loads(msg.data.decode())
        except Exception:
            return
        ip = data.get("data", {}).get("ip", "unknown")
        logger.warning(f"BLOCK: {ip} — {data.get('description', '')}")
        self.ledger.write("block_event", data)

    async def _on_cyberwall_status(self, msg):
        self.registry.heartbeat("cyberwall")
        logger.debug("Cyberwall heartbeat")

    async def _on_heartbeat(self, msg):
        try:
            data    = json.loads(msg.data.decode())
            service = data.get("service", "unknown")
            self.registry.heartbeat(service)
            logger.debug(f"Heartbeat: {service}")
        except Exception:
            pass

    async def _on_command(self, msg):
        try:
            data = json.loads(msg.data.decode())
        except Exception:
            return
        cmd = data.get("command", "")
        logger.info(f"Command: {cmd}")
        if cmd == "status":
            await self._publish_health()
        elif cmd == "verify_audit":
            ok, count = self.ledger.verify()
            logger.info(f"Audit chain: {'INTACT' if ok else 'TAMPERED'} ({count} entries)")
            self.ledger.write("audit_verify", {"ok": ok, "entries": count})
        elif cmd == "verify_nodeid":
            ok = self._verify_node_id()
            logger.info(f"NodeID: {'OK' if ok else 'MISMATCH'}")
        elif cmd == "list_dead":
            dead = self.registry.dead_services()
            logger.warning(f"Dead: {dead}" if dead else "All services alive")

    async def _escalate(self, data: dict, reason: str):
        logger.critical(f"ESCALATION: {reason}")
        self.ledger.write("escalation", {"reason": reason, "original": data})
        if self._nc:
            await self._nc.publish(
                SUBJECTS["escalation"],
                json.dumps({
                    "node_id":   self.node_id,
                    "reason":    reason,
                    "timestamp": datetime.now(timezone.utc).isoformat(),
                    "data":      data,
                }).encode()
            )

    async def _publish_health(self):
        if not self._nc:
            return
        dead = self.registry.dead_services()
        await self._nc.publish(
            SUBJECTS["health"],
            json.dumps({
                "node_id":   self.node_id,
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "services":  self.registry.status(),
                "dead":      dead,
                "nodeid_ok": self._verify_node_id(),
            }).encode()
        )
        if dead:
            logger.warning(f"Dead services: {dead}")

    async def _watchdog_loop(self):
        while self._running:
            await asyncio.sleep(30)
            self._verify_node_id()
            await self._publish_health()
            dead = self.registry.dead_services()
            if dead:
                self.ledger.write("services_dead", {"services": dead})
                logger.warning(f"Services not reporting: {dead}")

    async def start(self):
        self._running = True
        print(BANNER)
        logger.info(f"Sentinel starting — NodeID: {self.node_id}")
        self._verify_node_id()
        ok, count = self.ledger.verify()
        logger.info(f"Audit chain: {'INTACT' if ok else 'TAMPERED'} ({count} entries)")
        self.ledger.write("sentinel_start", {
            "node_id": self.node_id, "audit_ok": ok, "audit_count": count,
        })
        connected = await self._connect()
        if connected:
            await self._subscribe_all()

        tasks = [asyncio.create_task(self._watchdog_loop(), name="watchdog")]
        loop  = asyncio.get_event_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, lambda: [t.cancel() for t in tasks])

        logger.info("Sentinel online")
        try:
            await asyncio.gather(*tasks)
        except asyncio.CancelledError:
            logger.info("Sentinel shutting down...")
        finally:
            self.ledger.write("sentinel_stop", {"node_id": self.node_id})
            if self._nc:
                await self._nc.close()
            logger.info("Sentinel stopped.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Sorabbyngo Sentinel Daemon")
    parser.add_argument("--nats", default="nats://localhost:4222")
    args = parser.parse_args()
    asyncio.run(SentinelDaemon(nats_url=args.nats).start())
