import asyncio
import nats
from nats.js.api import StreamConfig, RetentionPolicy, StorageType
import logging

logging.basicConfig(level=logging.INFO)
log = logging.getLogger(__name__)

STREAMS = [
    StreamConfig(name="HEALTH_STREAM", subjects=["health.>"], retention=RetentionPolicy.WORK_QUEUE, storage=StorageType.FILE, max_age=86400),
    StreamConfig(name="DRUG_STREAM", subjects=["drug.>"], retention=RetentionPolicy.LIMITS, storage=StorageType.FILE, max_age=604800),
    StreamConfig(name="POPULATION_STREAM", subjects=["population.>"], retention=RetentionPolicy.LIMITS, storage=StorageType.FILE, max_age=2592000),
    StreamConfig(name="LEARN_STREAM", subjects=["learn.>"], retention=RetentionPolicy.LIMITS, storage=StorageType.FILE, max_age=604800),
    StreamConfig(name="SYSTEM_STREAM", subjects=["system.>"], retention=RetentionPolicy.LIMITS, storage=StorageType.MEMORY, max_age=3600),
]

async def setup():
    nc = await nats.connect("nats://localhost:4222")
    js = nc.jetstream()
    for stream in STREAMS:
        try:
            await js.add_stream(config=stream)
            log.info(f"Created: {stream.name}")
        except Exception as e:
            log.warning(f"{stream.name} may exist: {e}")
    await nc.close()
    log.info("All streams ready.")

if __name__ == "__main__":
    asyncio.run(setup())
