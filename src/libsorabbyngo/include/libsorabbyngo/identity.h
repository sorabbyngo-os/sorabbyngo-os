/**
 * libsorabbyngo/identity.h
 * Sorabbyngo OS — Identity Module (libsb_identity)
 *
 * Public API for NodeID-based hardware identity, Ed25519 signing,
 * and peer verification. Works on both the custom Sorabbyngo kernel
 * (reads /proc/sorabbyngo/nodeid) and standard Linux (derives NodeID
 * from CPU CPUID + MAC address in userspace — same SHA-256 logic as
 * the kernel driver).
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#ifndef LIBSORABBYNGO_IDENTITY_H
#define LIBSORABBYNGO_IDENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define SB_NODEID_LEN        64      /* hex-encoded SHA-256, null-terminated needs +1 */
#define SB_NODEID_BYTES      32      /* raw SHA-256 bytes */
#define SB_ED25519_PUBKEY_LEN  32
#define SB_ED25519_SECKEY_LEN  64
#define SB_ED25519_SIG_LEN     64
#define SB_ED25519_SEED_LEN    32

/* ── Return Codes ───────────────────────────────────────────────────────── */

#define SB_OK                  0
#define SB_ERR_NODEID_UNAVAIL -1   /* cannot derive NodeID */
#define SB_ERR_CRYPTO         -2   /* libsodium operation failed */
#define SB_ERR_INVALID_ARG    -3   /* null pointer or bad size */
#define SB_ERR_BUFFER_TOO_SMALL -4 /* output buffer insufficient */
#define SB_ERR_IO             -5   /* filesystem / proc read failure */

/* ── NodeID Source (informational) ─────────────────────────────────────── */

typedef enum {
    SB_NODEID_SRC_KERNEL   = 0,  /* read from /proc/sorabbyngo/nodeid     */
    SB_NODEID_SRC_USERSPACE = 1, /* derived in userspace (dev / no kernel) */
} sb_nodeid_source_t;

/* ── Identity Context ───────────────────────────────────────────────────── */

/**
 * Opaque identity context. Allocated with sb_identity_init(),
 * freed with sb_identity_free(). Not thread-safe — use one context
 * per thread or protect with a mutex.
 */
typedef struct sb_identity_ctx sb_identity_ctx_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * sb_identity_init — initialise an identity context.
 *
 * Derives or reads the NodeID, generates Ed25519 keypair seeded from
 * the NodeID. On a Sorabbyngo kernel, reads /proc/sorabbyngo/nodeid.
 * On standard Linux, derives NodeID from CPUID + primary MAC address.
 *
 * @param ctx     output: pointer to allocated context (caller must free)
 * @param source  output (nullable): set to indicate where NodeID came from
 * @return SB_OK on success, negative error code on failure
 */
int sb_identity_init(sb_identity_ctx_t **ctx, sb_nodeid_source_t *source);

/**
 * sb_identity_free — release all resources held by a context.
 * Securely zeroes key material before freeing.
 */
void sb_identity_free(sb_identity_ctx_t *ctx);

/* ── NodeID ─────────────────────────────────────────────────────────────── */

/**
 * sb_identity_get_nodeid — get this node's hex-encoded NodeID string.
 *
 * @param ctx    initialised identity context
 * @param buf    output buffer, must be >= SB_NODEID_LEN + 1 bytes
 * @param buflen size of buf
 * @return SB_OK, or SB_ERR_BUFFER_TOO_SMALL / SB_ERR_INVALID_ARG
 */
int sb_identity_get_nodeid(const sb_identity_ctx_t *ctx,
                            char *buf, size_t buflen);

/**
 * sb_identity_get_nodeid_bytes — get raw 32-byte NodeID.
 *
 * @param ctx    initialised identity context
 * @param out    output buffer, must be exactly SB_NODEID_BYTES
 * @return SB_OK or SB_ERR_INVALID_ARG
 */
int sb_identity_get_nodeid_bytes(const sb_identity_ctx_t *ctx,
                                  uint8_t out[SB_NODEID_BYTES]);

/**
 * sb_identity_get_pubkey — get this node's Ed25519 public key.
 * This is the key peers use to verify signatures from this node.
 *
 * @param ctx    initialised identity context
 * @param out    output buffer, must be SB_ED25519_PUBKEY_LEN bytes
 * @return SB_OK or SB_ERR_INVALID_ARG
 */
int sb_identity_get_pubkey(const sb_identity_ctx_t *ctx,
                            uint8_t out[SB_ED25519_PUBKEY_LEN]);

/* ── Signing ────────────────────────────────────────────────────────────── */

/**
 * sb_identity_sign — sign a payload with this node's Ed25519 secret key.
 *
 * @param ctx        initialised identity context
 * @param payload    data to sign
 * @param payload_len length of payload in bytes
 * @param sig        output: 64-byte Ed25519 signature
 * @return SB_OK or negative error code
 */
int sb_identity_sign(const sb_identity_ctx_t *ctx,
                     const uint8_t *payload, size_t payload_len,
                     uint8_t sig[SB_ED25519_SIG_LEN]);

/* ── Verification ───────────────────────────────────────────────────────── */

/**
 * sb_identity_verify_sig — verify a signature from a peer.
 *
 * @param payload      original data that was signed
 * @param payload_len  length of payload
 * @param sig          64-byte signature to verify
 * @param peer_pubkey  32-byte Ed25519 public key of the signing node
 * @return SB_OK if valid, SB_ERR_CRYPTO if invalid
 */
int sb_identity_verify_sig(const uint8_t *payload, size_t payload_len,
                            const uint8_t sig[SB_ED25519_SIG_LEN],
                            const uint8_t peer_pubkey[SB_ED25519_PUBKEY_LEN]);

/**
 * sb_identity_verify_peer — check if a hex NodeID string is well-formed
 * and non-zero. Does NOT contact a mesh registry (that is libsb_msg's job).
 *
 * @param nodeid_hex  null-terminated hex string, expected SB_NODEID_LEN chars
 * @return SB_OK if well-formed, SB_ERR_INVALID_ARG otherwise
 */
int sb_identity_verify_peer(const char *nodeid_hex);

/* ── Utilities ──────────────────────────────────────────────────────────── */

/**
 * sb_identity_strerror — human-readable error string for a return code.
 */
const char *sb_identity_strerror(int err);

/**
 * sb_identity_short — first 8 hex chars of the NodeID (for prompts/logs).
 * Writes 9 bytes (8 chars + null) into buf.
 */
int sb_identity_short(const sb_identity_ctx_t *ctx, char buf[9]);

#ifdef __cplusplus
}
#endif

#endif /* LIBSORABBYNGO_IDENTITY_H */
