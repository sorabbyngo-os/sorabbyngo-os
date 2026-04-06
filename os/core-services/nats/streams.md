# SORABBYNGO JetStream Streams

Streams persist messages for offline-first operation.
When a service is offline, messages queue here and deliver on reconnect.

| Stream | Subjects | Retention | Max Age | Storage |
|---|---|---|---|---|
| HEALTH_STREAM | health.> | WorkQueue | 24 hours | File |
| DRUG_STREAM | drug.> | Limits | 7 days | File |
| POPULATION_STREAM | population.> | Limits | 30 days | File |
| LEARN_STREAM | learn.> | Limits | 7 days | File |
| SYSTEM_STREAM | system.> | Limits | 1 hour | Memory |
