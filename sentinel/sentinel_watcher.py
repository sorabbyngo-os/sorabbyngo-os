#!/usr/bin/env python3
"""
Sorabbyngo OS — Sentinel Watcher (root)
Watches kernel ring buffer and /proc for OS-level threats.
Publishes findings to NATS for sentinel_daemon to act on.
Requires root for dmesg and /proc/modules access.
"""
import asyncio
import json
import logging
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import nats
    NATS_AVAILABLE = True
except ImportError:
    NATS_AVAILABLE = False

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s — %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
logger = logging.getLogger("sentinel.watcher")

NODEID_PATH = Path("/var/lib/sorabbyngo/nodeid")

KERNEL_THREAT_PATTERNS = [
    (r"CYBERWALL_DROP.*SRC=(\S+)",           "firewall_drop",        3.0),
    (r"sorabbyngo: WARNING NodeID mismatch", "nodeid_tamper",       10.0),
    (r"Oops|kernel BUG|BUG: unable",         "kernel_panic",         8.0),
    (r"Out of memory.*Kill",                  "oom_kill",             5.0),
    (r"audit.*denied.*\{.*\}",               "selinux_denial",       6.0),
    (r"possible SYN flooding",                "syn_flood",            7.0),
    (r"nf_conntrack: table full",             "conntrack_exhaustion", 6.5),
    (r"martian source",                       "ip_spoofing",          5.0),
]

SUSPICIOUS_MODULE_PATTERNS = [
    r"hide_", r"hook_", r"rootkit", r"inject", r"keylog", r"backdoor",
]


class SentinelWatcher:
    def __init__(self, nats_url: str = "nats://localhost:4222"):
        self.nats_url = nats_url
        self._nc      = None
        self._running = False
        self.node_id  = self._read_node_id()

    def _read_node_id(self) -> str:
        try:
            return NODEID_PATH.read_text().strip()
        except FileNotFoundError:
            return os.environ.get("SORABBYNGO_NODE_ID", "unknown")

    async def start(self):
        if os.geteuid() != 0:
            logger.warning("Not running as root — kernel checks limited")
        self._running = True
        logger.info(f"SentinelWatcher starting — NodeID: {self.node_id}")
        if NATS_AVAILABLE:
            try:
                self._nc = await nats.connect(
                    self.nats_url,
                    name=f"sentinel-watcher-{self.node_id[:8]}",
                )
                logger.info("Connected to NATS")
            except Exception as e:
                logger.error(f"NATS failed: {e}")
        await asyncio.gather(
            self._kernel_log_watcher(),
            self._module_integrity_loop(),
        )

    async def _kernel_log_watcher(self):
        logger.info("Watching kernel ring buffer...")
        try:
            proc = await asyncio.create_subprocess_exec(
                "dmesg", "-w",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
            while self._running and proc.stdout:
                try:
                    line = await asyncio.wait_for(
                        proc.stdout.readline(), timeout=2.0
                    )
                    if line:
                        await self._parse_kernel_line(line.decode(errors="replace"))
                except asyncio.TimeoutError:
                    continue
        except FileNotFoundError:
            logger.warning("dmesg not available")

    async def _parse_kernel_line(self, line: str):
        for pattern, threat_type, score in KERNEL_THREAT_PATTERNS:
            if re.search(pattern, line):
                logger.warning(f"KERNEL [{threat_type}]: {line.strip()[:120]}")
                await self._publish("sorabbyngo.cyberwall.alert", {
                    "source":      "sentinel_watcher",
                    "threat_type": threat_type,
                    "level":       "HIGH" if score >= 7 else "MEDIUM",
                    "score":       score,
                    "description": line.strip()[:200],
                    "data":        {"raw": line.strip()[:200]},
                    "node_id":     self.node_id,
                    "timestamp":   datetime.now(timezone.utc).isoformat(),
                })
                break

    async def _module_integrity_loop(self):
        while self._running:
            await self._check_modules()
            await asyncio.sleep(60)

    async def _check_modules(self):
        try:
            modules_path = Path("/proc/modules")
            if not modules_path.exists():
                return
            for line in modules_path.read_text().splitlines():
                mod = line.split()[0]
                for p in SUSPICIOUS_MODULE_PATTERNS:
                    if re.search(p, mod, re.IGNORECASE):
                        logger.critical(f"SUSPICIOUS MODULE: {mod}")
                        await self._publish("sorabbyngo.cyberwall.alert", {
                            "source":      "sentinel_watcher",
                            "threat_type": "rootkit",
                            "level":       "CRITICAL",
                            "score":       9.5,
                            "description": f"Suspicious kernel module: {mod}",
                            "data":        {"module": mod},
                            "node_id":     self.node_id,
                            "timestamp":   datetime.now(timezone.utc).isoformat(),
                        })
        except Exception as e:
            logger.debug(f"Module check error: {e}")

    async def _publish(self, subject: str, data: dict):
        if self._nc:
            try:
                await self._nc.publish(subject, json.dumps(data).encode())
            except Exception as e:
                logger.error(f"Publish error: {e}")
        logger.info(f"[{subject}] {data.get('threat_type')} | {data.get('level')}")

    def stop(self):
        self._running = False


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Sorabbyngo Sentinel Watcher")
    parser.add_argument("--nats", default="nats://localhost:4222")
    args = parser.parse_args()
    asyncio.run(SentinelWatcher(nats_url=args.nats).start())
