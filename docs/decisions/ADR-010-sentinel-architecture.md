# ADR-010: Sentinel Architecture

## Status
Accepted

## Components

| Process | Runs as | Purpose |
|---|---|---|
| sentinel_daemon.py | user | NATS subscriber, audit ledger, service watchdog |
| sentinel_watcher.py | root | Kernel log watcher, module integrity checker |

## Separation of Privilege
sentinel_watcher.py runs as root solely to read dmesg and /proc/modules.
It only publishes to NATS — it never acts on the filesystem directly.
All response actions go through cyberwall via NATS.

## Audit Chain
Append-only hash-chained JSONL at /var/log/sorabbyngo/sentinel_audit.jsonl.
Verify via: sorabbyngo.sentinel.command → {"command": "verify_audit"}

## NodeID Integrity
On boot and every 30 seconds:
  /proc/sorabbyngo/nodeid (live) vs /var/lib/sorabbyngo/nodeid (sealed)
Mismatch → CRITICAL → audit entry → escalation broadcast.

## Escalation Policy
3x HIGH/CRITICAL from same source → sorabbyngo.sentinel.escalation broadcast.

## NATS Subjects
| Subject | Direction | Who |
|---|---|---|
| sorabbyngo.cyberwall.alert | Subscribe | From cyberwall + watcher |
| sorabbyngo.cyberwall.block | Subscribe | From cyberwall |
| sorabbyngo.cyberwall.status | Subscribe | From cyberwall |
| sorabbyngo.*.heartbeat | Subscribe | All services |
| sorabbyngo.sentinel.command | Subscribe | Operators |
| sorabbyngo.sentinel.health | Publish | Sentinel |
| sorabbyngo.sentinel.escalation | Publish | Sentinel |
