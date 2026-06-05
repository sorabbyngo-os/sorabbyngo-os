/**
 * libsorabbyngo/msg.h
 * Sorabbyngo OS — Messaging Module (libsb_msg)
 *
 * Thin C wrapper over NATS with NodeID-signed message envelopes.
 * Every published message is automatically signed with the node's
 * Ed25519 key (via libsb_identity). Subscribers can verify the
 * sender's identity before processing.
 *
 * Depends on: libsb_identity, nats.c client library
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#ifndef LIBSORABBYNGO_MSG_H
#define LIBSORABBYNGO_MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "libsorabbyngo/identity.h"

/* ── Stream names (mirrors sorabbyngo.conf) ────────────────────────────── */

#define SB_STREAM_SENTINEL  "SENTINEL"
#define SB_STREAM_BYNGOX    "BYNGOX"
#define SB_STREAM_BYNGONET  "BYNGONET"
#define SB_STREAM_KCP       "KCP"

/* ── Standard subjects ──────────────────────────────────────────────────── */

#define SB_SUBJ_HEARTBEAT      "sorabbyngo.%s.heartbeat"
#define SB_SUBJ_CW_ALERT       "sorabbyngo.cyberwall.alert"
#define SB_SUBJ_SENTINEL_CMD   "sorabbyngo.sentinel.command"
#define SB_SUBJ_SENTINEL_HEALTH "sorabbyngo.sentinel.health"
#define SB_SUBJ_MESH_PEER      "sorabbyngo.byngonet.peer"
#define SB_SUBJ_AI_REQUEST     "sorabbyngo.byngox.request"
#define SB_SUBJ_AI_RESPONSE    "sorabbyngo.byngox.response"
#define SB_SUBJ_NOVELA_RECORD  "sorabbyngo.novela.record"

/* ── Return codes (extends identity.h codes) ────────────────────────────── */

#define SB_ERR_NOT_CONNECTED  -10
#define SB_ERR_PUBLISH        -11
#define SB_ERR_SUBSCRIBE      -12
#define SB_ERR_TIMEOUT        -13
#define SB_ERR_BAD_ENVELOPE   -14
#define SB_ERR_SIG_INVALID    -15

/* ── Message envelope ───────────────────────────────────────────────────── */

/**
 * Every message on the Sorabbyngo bus is wrapped in this envelope.
 * JSON wire format:
 * {
 *   "v":       1,
 *   "nodeid":  "<64-char hex>",
 *   "pubkey":  "<64-char hex of 32-byte Ed25519 pubkey>",
 *   "subject": "<nats subject>",
 *   "payload": "<base64-encoded payload bytes>",
 *   "sig":     "<128-char hex of 64-byte Ed25519 sig over payload bytes>"
 * }
 */
#define SB_ENVELOPE_VERSION 1
#define SB_MAX_SUBJECT_LEN  256
#define SB_MAX_PAYLOAD_LEN  (1024 * 1024)   /* 1MB — mirrors NATS max_payload */

typedef struct {
    int      version;
    char     nodeid[SB_NODEID_LEN + 1];
    uint8_t  pubkey[SB_ED25519_PUBKEY_LEN];
    char     subject[SB_MAX_SUBJECT_LEN];
    uint8_t *payload;
    size_t   payload_len;
    uint8_t  sig[SB_ED25519_SIG_LEN];
} sb_envelope_t;

/* ── Messaging context ──────────────────────────────────────────────────── */

typedef struct sb_msg_ctx sb_msg_ctx_t;

/* ── Callback type for subscriptions ───────────────────────────────────── */

/**
 * Called for every received message.
 *
 * @param env      parsed and signature-verified envelope
 * @param userdata caller-supplied pointer passed to sb_msg_subscribe
 *
 * If signature verification fails, the callback is NOT called —
 * the message is silently dropped. Set SB_MSG_OPT_NO_VERIFY to
 * disable this (dev/debug only).
 */
typedef void (*sb_msg_handler_t)(const sb_envelope_t *env, void *userdata);

/* ── Options ────────────────────────────────────────────────────────────── */

#define SB_MSG_OPT_NONE       0x00
#define SB_MSG_OPT_NO_VERIFY  0x01   /* skip sig verification on recv (debug) */
#define SB_MSG_OPT_NO_SIGN    0x02   /* skip signing on send (debug) */

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * sb_msg_init — create a messaging context.
 *
 * @param ctx_out     output: allocated context
 * @param id_ctx      initialised identity context (borrowed — caller owns)
 * @param nats_url    NATS server URL, e.g. "nats://localhost:4222"
 * @param service     service name for heartbeats and logging
 * @return SB_OK or negative error code
 */
int sb_msg_init(sb_msg_ctx_t **ctx_out,
                sb_identity_ctx_t *id_ctx,
                const char *nats_url,
                const char *service);

/**
 * sb_msg_connect — connect to NATS server.
 * Blocks until connected or timeout_ms elapses (0 = 5s default).
 */
int sb_msg_connect(sb_msg_ctx_t *ctx, int timeout_ms);

/**
 * sb_msg_free — disconnect and release all resources.
 */
void sb_msg_free(sb_msg_ctx_t *ctx);

/* ── Publishing ─────────────────────────────────────────────────────────── */

/**
 * sb_msg_publish — publish a signed message envelope to a NATS subject.
 *
 * Automatically:
 *   1. Wraps payload in sb_envelope_t
 *   2. Signs payload with this node's Ed25519 key
 *   3. Serialises to JSON
 *   4. Publishes to NATS
 *
 * @param ctx         messaging context
 * @param subject     NATS subject string
 * @param payload     raw bytes to publish
 * @param payload_len length of payload
 * @param opts        SB_MSG_OPT_* flags
 * @return SB_OK or negative error code
 */
int sb_msg_publish(sb_msg_ctx_t *ctx,
                   const char *subject,
                   const uint8_t *payload,
                   size_t payload_len,
                   uint32_t opts);

/**
 * sb_msg_publish_str — convenience: publish a null-terminated string.
 */
int sb_msg_publish_str(sb_msg_ctx_t *ctx,
                       const char *subject,
                       const char *payload_str,
                       uint32_t opts);

/**
 * sb_msg_heartbeat — publish a standard heartbeat on
 * sorabbyngo.<service>.heartbeat
 */
int sb_msg_heartbeat(sb_msg_ctx_t *ctx);

/* ── Subscribing ────────────────────────────────────────────────────────── */

/**
 * sb_msg_subscribe — subscribe to a NATS subject.
 *
 * Incoming messages are signature-verified before handler is called.
 * Returns a subscription ID (> 0) on success.
 *
 * @param ctx      messaging context
 * @param subject  NATS subject (wildcards supported: *, >)
 * @param handler  callback invoked for each verified message
 * @param userdata passed through to handler
 * @param opts     SB_MSG_OPT_* flags
 * @return subscription ID (> 0) or negative error code
 */
int sb_msg_subscribe(sb_msg_ctx_t *ctx,
                     const char *subject,
                     sb_msg_handler_t handler,
                     void *userdata,
                     uint32_t opts);

/**
 * sb_msg_unsubscribe — remove a subscription by ID.
 */
int sb_msg_unsubscribe(sb_msg_ctx_t *ctx, int sub_id);

/* ── Request / Reply ────────────────────────────────────────────────────── */

/**
 * sb_msg_request — synchronous request-reply.
 *
 * Publishes to subject and waits up to timeout_ms for a reply.
 * Caller must free reply_env->payload with sb_envelope_free().
 *
 * @param ctx        messaging context
 * @param subject    request subject
 * @param payload    request payload
 * @param payload_len length of payload
 * @param timeout_ms milliseconds to wait for reply
 * @param reply_env  output: verified reply envelope
 * @return SB_OK, SB_ERR_TIMEOUT, or negative error code
 */
int sb_msg_request(sb_msg_ctx_t *ctx,
                   const char *subject,
                   const uint8_t *payload,
                   size_t payload_len,
                   int timeout_ms,
                   sb_envelope_t **reply_env);

/* ── Envelope utilities ─────────────────────────────────────────────────── */

/**
 * sb_envelope_free — release an envelope allocated by sb_msg_request.
 */
void sb_envelope_free(sb_envelope_t *env);

/**
 * sb_envelope_verify — verify the signature on an envelope.
 * Called automatically on receive; exposed for manual use.
 */
int sb_envelope_verify(const sb_envelope_t *env);

/**
 * sb_msg_strerror — human-readable string for msg error codes.
 */
const char *sb_msg_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBSORABBYNGO_MSG_H */
