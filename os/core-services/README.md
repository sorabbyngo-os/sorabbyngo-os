# SORABBYNGO Core Services — Layer 2

Foundation services every component depends on.
Must be running before Byngox, CML, Novela, or Chameha.

## Services
| Service | Port | Purpose |
|---|---|---|
| NATS JetStream | 4222 | Message bus — nervous system of the OS |
| SurrealDB | 8000 | Device, agent, config storage |
| PostgreSQL | 5432 | Novela health records |
| Redis | 6379 | Cache and task queue |

## Boot Order
sorabbyngo-nodeid.service sorabbyngo-nats.service sorabbyngo-byngox.service sorabbyngo-novela.service sorabbyngo-chameha.service graphical.target

## Development Setup
```bash
docker-compose -f os/core-services/docker-compose.dev.yml up -d
python3 os/core-services/nats/setup_streams.py
curl http://localhost:8222/healthz
```
