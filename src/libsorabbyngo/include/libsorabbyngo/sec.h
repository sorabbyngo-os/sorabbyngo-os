/**
 * libsorabbyngo/sec.h
 * Sorabbyngo OS — Security Module (libsb_sec)
 *
 * Policy query interface to the Sorabbyngo Sentinel daemon.
 * Provides synchronous policy decisions for C applications —
 * no direct NATS knowledge required.
 *
 * Two query paths:
 *   Fast path: local YAML policy cache (< 0.2ms, no network)
 *   Full path:  NATS request to Sentinel daemon (< 5ms, verified response)
 *
 * Every policy decision is logged to the CML audit ledger via libsb_memory.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#ifndef LIBSORABBYNGO_SEC_H
#define LIBSORABBYNGO_SEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "libsorabbyngo/identity.h"

/* ── Policy decision ────────────────────────────────────────────────────── */

typedef enum {
    SB_SEC_DENY  = 0,
    SB_SEC_ALLOW = 1,
    SB_SEC_AUDIT = 2,   /* allow but log for review                        */
} sb_sec_decision_t;

/* ── Clearance levels (mirrors 7-ring defence architecture) ─────────────── */

typedef enum {
    SB_SEC_CLEARANCE_NONE      = 0,
    SB_SEC_CLEARANCE_PUBLIC    = 1,
    SB_SEC_CLEARANCE_USER      = 2,
    SB_SEC_CLEARANCE_SERVICE   = 3,
    SB_SEC_CLEARANCE_SENTINEL  = 4,
    SB_SEC_CLEARANCE_KERNEL    = 5,
} sb_sec_clearance_t;

/* ── Return codes ───────────────────────────────────────────────────────── */

#define SB_ERR_SEC_DENIED        -40
#define SB_ERR_SEC_UNAVAIL       -41   /* Sentinel not reachable            */
#define SB_ERR_SEC_POLICY_LOAD   -42   /* failed to load policy file        */
#define SB_ERR_SEC_BAD_RESPONSE  -43   /* Sentinel returned malformed reply */

/* ── Policy check result ────────────────────────────────────────────────── */

typedef struct {
    sb_sec_decision_t  decision;
    sb_sec_clearance_t clearance;
    char               reason[256];     /* human-readable decision reason   */
    char               policy_id[64];   /* which rule matched               */
    long               latency_us;      /* decision latency in microseconds */
    int                from_cache;      /* 1 = local cache, 0 = Sentinel    */
} sb_sec_result_t;

/* ── Security context ───────────────────────────────────────────────────── */

typedef struct sb_sec_ctx sb_sec_ctx_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * sb_sec_init — create a security context.
 *
 * Loads local policy cache from policy_path (YAML).
 * Attempts connection to Sentinel via msg_ctx if provided.
 * Falls back to local cache only if Sentinel unavailable.
 *
 * @param ctx_out     output: allocated context
 * @param id_ctx      identity context (borrowed)
 * @param policy_path path to local policy YAML (NULL = default)
 * @param nats_url    NATS URL for Sentinel queries (NULL = local cache only)
 * @return SB_OK or negative error code
 */
int sb_sec_init(sb_sec_ctx_t **ctx_out,
                sb_identity_ctx_t *id_ctx,
                const char *policy_path,
                const char *nats_url);

/**
 * sb_sec_free — release all resources.
 */
void sb_sec_free(sb_sec_ctx_t *ctx);

/* ── Policy queries ─────────────────────────────────────────────────────── */

/**
 * sb_sec_check — check if subject can perform action on object.
 *
 * Fast path: checks local policy cache first (< 0.2ms).
 * Falls back to Sentinel via NATS if local policy has no matching rule.
 *
 * @param ctx        security context
 * @param subject    who is requesting (NodeID, service name, or "self")
 * @param object     what is being accessed (path, resource, or service)
 * @param action     what action (read, write, exec, connect, publish, etc.)
 * @param result     output: full decision with reason and latency
 * @return SB_OK (allow/audit) or SB_ERR_SEC_DENIED
 */
int sb_sec_check(sb_sec_ctx_t *ctx,
                 const char *subject,
                 const char *object,
                 const char *action,
                 sb_sec_result_t *result);

/**
 * sb_sec_check_fast — local cache only, never contacts Sentinel.
 * Returns SB_ERR_SEC_UNAVAIL if no matching rule in cache.
 * Use for hot paths where < 0.2ms latency is required.
 */
int sb_sec_check_fast(sb_sec_ctx_t *ctx,
                      const char *subject,
                      const char *object,
                      const char *action,
                      sb_sec_result_t *result);

/* ── Clearance ──────────────────────────────────────────────────────────── */

/**
 * sb_sec_get_clearance — get the clearance level for a NodeID.
 * Queries local policy cache; falls back to SB_SEC_CLEARANCE_NONE.
 */
sb_sec_clearance_t sb_sec_get_clearance(sb_sec_ctx_t *ctx,
                                         const char *nodeid);

/**
 * sb_sec_get_own_clearance — get this node's clearance level.
 */
sb_sec_clearance_t sb_sec_get_own_clearance(sb_sec_ctx_t *ctx);

/* ── NodeID verification ────────────────────────────────────────────────── */

/**
 * sb_sec_verify_nodeid — cross-check live /proc NodeID against sealed value.
 * Mirrors SentinelDaemon._verify_node_id().
 *
 * @return SB_OK if match, SB_ERR_SEC_DENIED if mismatch (hardware tamper)
 */
int sb_sec_verify_nodeid(sb_sec_ctx_t *ctx);

/* ── Encryption ─────────────────────────────────────────────────────────── */

/**
 * sb_sec_encrypt — encrypt data addressed to a specific NodeID.
 * Uses X25519 ECDH + XSalsa20-Poly1305 (libsodium box).
 * Only the holder of recipient_nodeid's keypair can decrypt.
 *
 * @param plaintext      data to encrypt
 * @param plaintext_len  length of plaintext
 * @param recipient_pubkey  32-byte Ed25519 pubkey of recipient
 * @param ciphertext_out    output: allocated ciphertext (caller frees)
 * @param ciphertext_len    output: length of ciphertext
 * @return SB_OK or negative error code
 */
int sb_sec_encrypt(sb_sec_ctx_t *ctx,
                   const uint8_t *plaintext,
                   size_t plaintext_len,
                   const uint8_t recipient_pubkey[32],
                   uint8_t **ciphertext_out,
                   size_t *ciphertext_len);

/**
 * sb_sec_decrypt — decrypt data addressed to this node.
 *
 * @param ciphertext      encrypted data
 * @param ciphertext_len  length of ciphertext
 * @param sender_pubkey   32-byte Ed25519 pubkey of sender
 * @param plaintext_out   output: allocated plaintext (caller frees)
 * @param plaintext_len   output: length of plaintext
 * @return SB_OK or SB_ERR_SEC_DENIED (decryption failed)
 */
int sb_sec_decrypt(sb_sec_ctx_t *ctx,
                   const uint8_t *ciphertext,
                   size_t ciphertext_len,
                   const uint8_t sender_pubkey[32],
                   uint8_t **plaintext_out,
                   size_t *plaintext_len);

/* ── Utilities ──────────────────────────────────────────────────────────── */

/**
 * sb_sec_decision_name — human-readable decision string.
 */
const char *sb_sec_decision_name(sb_sec_decision_t decision);

/**
 * sb_sec_clearance_name — human-readable clearance level string.
 */
const char *sb_sec_clearance_name(sb_sec_clearance_t clearance);

/**
 * sb_sec_strerror — human-readable error string.
 */
const char *sb_sec_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBSORABBYNGO_SEC_H */
