/**
 * libsorabbyngo/memory.h
 * Sorabbyngo OS — Memory Module (libsb_memory)
 *
 * C interface to the Sorabbyngo Contextual Memory Layer (CML).
 * Wraps the AuditLedger chain (mirrors audit.py) with:
 *   - Hash-chained JSONL append-only ledger
 *   - Kernel audit tail anchoring (/proc/sorabbyngo/audit_tail)
 *   - PII field scrubbing before persistence
 *   - NodeID-stamped entries for Kenya DPA 2019 forensic queries
 *   - Simple key-value store on top of the ledger
 *
 * Storage layout (mirrors ADR-012):
 *   /data/cml/audit.jsonl   — hash-chained ledger
 *   /data/cml/store.jsonl   — key-value store entries
 *
 * Falls back to ~/.sorabbyngo/cml/ on dev machines (no /data mount).
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#ifndef LIBSORABBYNGO_MEMORY_H
#define LIBSORABBYNGO_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "libsorabbyngo/identity.h"

/* ── Storage paths ──────────────────────────────────────────────────────── */

#define SB_MEM_DATA_DIR          "/data/cml"
#define SB_MEM_FALLBACK_DIR      ".sorabbyngo/cml"   /* relative to $HOME */
#define SB_MEM_AUDIT_FILE        "audit.jsonl"
#define SB_MEM_STORE_FILE        "store.jsonl"
#define SB_MEM_KERNEL_AUDIT_TAIL "/proc/sorabbyngo/audit_tail"

/* ── Limits ─────────────────────────────────────────────────────────────── */

#define SB_MEM_MAX_KEY_LEN       128
#define SB_MEM_MAX_VALUE_LEN     (64 * 1024)   /* 64KB per value */
#define SB_MEM_MAX_EVENT_LEN     (8 * 1024)    /* 8KB per audit event */
#define SB_MEM_CHAIN_HASH_LEN    64             /* SHA-256 hex */

/* ── Return codes ───────────────────────────────────────────────────────── */

#define SB_ERR_MEM_IO            -30
#define SB_ERR_MEM_CHAIN_BROKEN  -31   /* audit chain integrity failure    */
#define SB_ERR_MEM_KEY_NOT_FOUND -32
#define SB_ERR_MEM_VALUE_TOO_BIG -33
#define SB_ERR_MEM_PII_SCRUBBED  -34   /* value was modified by PII filter */

/* ── PII scrubbing flags ────────────────────────────────────────────────── */

#define SB_MEM_PII_NONE          0x00
#define SB_MEM_PII_PHONE         0x01   /* scrub phone numbers             */
#define SB_MEM_PII_EMAIL         0x02   /* scrub email addresses           */
#define SB_MEM_PII_ID_NUMBER     0x04   /* scrub national ID patterns      */
#define SB_MEM_PII_ALL           0xFF   /* scrub all known PII patterns    */

/* ── Audit event types (mirrors sentinel_daemon.py event taxonomy) ──────── */

#define SB_MEM_EVENT_RECORD_WRITE  "record.write"
#define SB_MEM_EVENT_RECORD_READ   "record.read"
#define SB_MEM_EVENT_RECORD_DELETE "record.delete"
#define SB_MEM_EVENT_AUTH          "auth.event"
#define SB_MEM_EVENT_POLICY        "policy.decision"
#define SB_MEM_EVENT_AI_INFERENCE  "ai.inference"
#define SB_MEM_EVENT_SYSTEM        "system.event"

/* ── Chain entry (one line in audit.jsonl) ──────────────────────────────── */

typedef struct {
    char timestamp[32];
    char event_type[64];
    char node_id[SB_NODEID_LEN + 1];
    char data[SB_MEM_MAX_EVENT_LEN];
    char prev_hash[SB_MEM_CHAIN_HASH_LEN + 1];
    char kernel_hash[SB_MEM_CHAIN_HASH_LEN + 1]; /* NULL if no kernel module */
    char chain_hash[SB_MEM_CHAIN_HASH_LEN + 1];
} sb_mem_entry_t;

/* ── Memory context ─────────────────────────────────────────────────────── */

typedef struct sb_mem_ctx sb_mem_ctx_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * sb_mem_init — initialise a CML memory context.
 *
 * Creates the storage directory if it doesn't exist.
 * Resumes the hash chain from the last entry in audit.jsonl.
 * Tries /data/cml first; falls back to ~/.sorabbyngo/cml on dev machines.
 *
 * @param ctx_out   output: allocated context
 * @param id_ctx    identity context (borrowed)
 * @param data_dir  NULL = auto-detect (/data/cml or ~/.sorabbyngo/cml)
 * @return SB_OK or negative error code
 */
int sb_mem_init(sb_mem_ctx_t **ctx_out,
                sb_identity_ctx_t *id_ctx,
                const char *data_dir);

/**
 * sb_mem_free — release all resources. Flushes pending writes.
 */
void sb_mem_free(sb_mem_ctx_t *ctx);

/* ── Audit ledger ───────────────────────────────────────────────────────── */

/**
 * sb_mem_audit_write — append a hash-chained entry to the audit ledger.
 *
 * Mirrors AuditLedger.write() from audit.py.
 * Anchors to /proc/sorabbyngo/audit_tail if kernel module is loaded.
 *
 * @param ctx         memory context
 * @param event_type  SB_MEM_EVENT_* constant or custom string
 * @param data_json   JSON string with event data (will be PII-scrubbed)
 * @param pii_flags   SB_MEM_PII_* flags for scrubbing
 * @param chain_hash_out  output: SHA-256 chain hash of this entry (nullable)
 * @return SB_OK or negative error code
 */
int sb_mem_audit_write(sb_mem_ctx_t *ctx,
                       const char *event_type,
                       const char *data_json,
                       uint32_t pii_flags,
                       char chain_hash_out[SB_MEM_CHAIN_HASH_LEN + 1]);

/**
 * sb_mem_audit_verify — verify the full chain integrity.
 * Mirrors AuditLedger.verify() from audit.py.
 *
 * @param ctx          memory context
 * @param entry_count  output: number of entries verified
 * @return SB_OK if chain is intact, SB_ERR_MEM_CHAIN_BROKEN if tampered
 */
int sb_mem_audit_verify(sb_mem_ctx_t *ctx, int *entry_count);

/**
 * sb_mem_audit_tail — read the last N entries from the audit ledger.
 * Caller must free each entry and the array with sb_mem_entries_free().
 *
 * @param ctx      memory context
 * @param n        number of entries to read (0 = last 20)
 * @param entries  output: allocated array of entry pointers
 * @param count    output: number of entries returned
 */
int sb_mem_audit_tail(sb_mem_ctx_t *ctx, int n,
                      sb_mem_entry_t ***entries, int *count);

void sb_mem_entries_free(sb_mem_entry_t **entries, int count);

/* ── Key-value store ────────────────────────────────────────────────────── */

/**
 * sb_mem_store — persist a key-value pair to the CML store.
 *
 * Every store operation is also written to the audit ledger.
 * Values are PII-scrubbed before persistence.
 *
 * @param ctx       memory context
 * @param key       null-terminated key string (max SB_MEM_MAX_KEY_LEN)
 * @param value     value bytes
 * @param value_len length of value
 * @param pii_flags PII scrubbing flags
 * @return SB_OK, SB_ERR_MEM_PII_SCRUBBED (value stored but modified),
 *         or negative error code
 */
int sb_mem_store(sb_mem_ctx_t *ctx,
                 const char *key,
                 const uint8_t *value,
                 size_t value_len,
                 uint32_t pii_flags);

/**
 * sb_mem_retrieve — fetch the latest value for a key.
 * Caller must free *value_out with free().
 *
 * @param ctx        memory context
 * @param key        key to look up
 * @param value_out  output: allocated value bytes (null-terminated)
 * @param value_len  output: length of value
 * @return SB_OK, SB_ERR_MEM_KEY_NOT_FOUND, or negative error code
 */
int sb_mem_retrieve(sb_mem_ctx_t *ctx,
                    const char *key,
                    uint8_t **value_out,
                    size_t *value_len);

/**
 * sb_mem_delete — mark a key as deleted in the store.
 * Does NOT remove from audit ledger (append-only by design).
 */
int sb_mem_delete(sb_mem_ctx_t *ctx, const char *key);

/* ── PII scrubbing ──────────────────────────────────────────────────────── */

/**
 * sb_mem_scrub_pii — apply PII scrubbing to a JSON string in-place.
 * Replaces matched patterns with "[REDACTED]".
 *
 * @param json      input/output buffer (modified in place)
 * @param json_size size of buffer
 * @param flags     SB_MEM_PII_* flags
 * @return number of replacements made (0 = nothing scrubbed)
 */
int sb_mem_scrub_pii(char *json, size_t json_size, uint32_t flags);

/* ── Utilities ──────────────────────────────────────────────────────────── */

/**
 * sb_mem_get_ledger_path — returns the path to the active audit.jsonl file.
 */
const char *sb_mem_get_ledger_path(const sb_mem_ctx_t *ctx);

/**
 * sb_mem_strerror — human-readable error string.
 */
const char *sb_mem_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBSORABBYNGO_MEMORY_H */
