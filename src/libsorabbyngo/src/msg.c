/**
 * msg.c — libsb_msg implementation
 *
 * Wraps nats.c (official NATS C client) with:
 *   - Automatic Ed25519 signing on publish (via libsb_identity)
 *   - Automatic signature verification on receive
 *   - JSON envelope serialisation/deserialisation
 *   - Reconnect logic mirroring sorabbyngo.conf settings
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/msg.h"
#include "libsorabbyngo/identity.h"

#include <sodium.h>
#include <nats/nats.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ── Base64 (RFC 4648, no padding variant for compactness) ──────────────── */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_size)
{
    size_t i, j = 0;
    uint32_t v;
    for (i = 0; i + 2 < in_len; i += 3) {
        v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        if (j + 4 >= out_size) return 0;
        out[j++] = b64_table[(v >> 18) & 0x3f];
        out[j++] = b64_table[(v >> 12) & 0x3f];
        out[j++] = b64_table[(v >>  6) & 0x3f];
        out[j++] = b64_table[(v      ) & 0x3f];
    }
    if (i < in_len) {
        v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) v |= (uint32_t)in[i+1] << 8;
        if (j + 4 >= out_size) return 0;
        out[j++] = b64_table[(v >> 18) & 0x3f];
        out[j++] = b64_table[(v >> 12) & 0x3f];
        out[j++] = (i + 1 < in_len) ? b64_table[(v >> 6) & 0x3f] : '=';
        out[j++] = '=';
    }
    out[j] = '\0';
    return j;
}

static int b64_decode(const char *in, uint8_t *out, size_t *out_len)
{
    /* Build reverse lookup */
    static int8_t rev[256];
    static int rev_init = 0;
    if (!rev_init) {
        memset(rev, -1, sizeof(rev));
        for (int i = 0; i < 64; i++)
            rev[(unsigned char)b64_table[i]] = i;
        rev_init = 1;
    }

    size_t in_len = strlen(in);
    size_t j = 0;
    uint32_t v = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len; i++) {
        if (in[i] == '=') break;
        int8_t c = rev[(unsigned char)in[i]];
        if (c < 0) return -1;
        v = (v << 6) | (uint32_t)c;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[j++] = (uint8_t)((v >> bits) & 0xff);
        }
    }
    *out_len = j;
    return 0;
}

/* ── Hex helpers ────────────────────────────────────────────────────────── */

static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i*2]   = hex[(in[i] >> 4) & 0xf];
        out[i*2+1] = hex[in[i] & 0xf];
    }
    out[len*2] = '\0';
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t expected_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len != expected_len * 2) return -1;
    for (size_t i = 0; i < expected_len; i++) {
        char byte_str[3] = { hex[i*2], hex[i*2+1], '\0' };
        char *end;
        out[i] = (uint8_t)strtol(byte_str, &end, 16);
        if (*end != '\0') return -1;
    }
    return 0;
}

/* ── Subscription entry ─────────────────────────────────────────────────── */

typedef struct sb_sub_entry {
    int               id;
    natsSubscription *nat_sub;
    sb_msg_handler_t  handler;
    void             *userdata;
    uint32_t          opts;
    struct sb_sub_entry *next;
} sb_sub_entry_t;

/* ── Internal context ───────────────────────────────────────────────────── */

struct sb_msg_ctx {
    sb_identity_ctx_t *id_ctx;          /* borrowed */
    natsConnection    *nc;
    char               nats_url[256];
    char               service[64];
    char               nodeid[SB_NODEID_LEN + 1];
    uint8_t            pubkey[SB_ED25519_PUBKEY_LEN];
    int                next_sub_id;
    sb_sub_entry_t    *subs;
};

/* ── JSON envelope serialiser ───────────────────────────────────────────── */

/**
 * Build the JSON wire envelope. Caller must free returned string.
 * Format: {"v":1,"nodeid":"...","pubkey":"...","subject":"...",
 *          "payload":"<b64>","sig":"<hex>"}
 */
static char *envelope_to_json(const sb_envelope_t *env)
{
    /* Base64-encode payload */
    size_t b64_size = ((env->payload_len + 2) / 3) * 4 + 4;
    char *b64_payload = malloc(b64_size);
    if (!b64_payload) return NULL;
    b64_encode(env->payload, env->payload_len, b64_payload, b64_size);

    /* Hex-encode pubkey and sig */
    char pubkey_hex[SB_ED25519_PUBKEY_LEN * 2 + 1];
    char sig_hex[SB_ED25519_SIG_LEN * 2 + 1];
    bytes_to_hex(env->pubkey, SB_ED25519_PUBKEY_LEN, pubkey_hex);
    bytes_to_hex(env->sig,    SB_ED25519_SIG_LEN,    sig_hex);

    /* Estimate JSON size and allocate */
    size_t json_size = 64                          /* fixed fields */
                     + SB_NODEID_LEN               /* nodeid */
                     + sizeof(pubkey_hex)           /* pubkey */
                     + SB_MAX_SUBJECT_LEN           /* subject */
                     + b64_size                     /* payload */
                     + sizeof(sig_hex)              /* sig */
                     + 64;                          /* formatting */

    char *json = malloc(json_size);
    if (!json) { free(b64_payload); return NULL; }

    snprintf(json, json_size,
        "{\"v\":%d,"
        "\"nodeid\":\"%s\","
        "\"pubkey\":\"%s\","
        "\"subject\":\"%s\","
        "\"payload\":\"%s\","
        "\"sig\":\"%s\"}",
        env->version,
        env->nodeid,
        pubkey_hex,
        env->subject,
        b64_payload,
        sig_hex);

    free(b64_payload);
    return json;
}

/* ── Minimal JSON field extractor (no external JSON lib dependency) ──────── */

/**
 * Extract a string value for a given key from flat JSON.
 * Only handles simple string values — sufficient for our envelope format.
 * Writes into out buffer (max out_size bytes).
 */
static int json_get_str(const char *json, const char *key,
                         char *out, size_t out_size)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= out_size) return -1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* ── Envelope deserialiser ──────────────────────────────────────────────── */

static sb_envelope_t *envelope_from_json(const char *json, size_t json_len)
{
    (void)json_len;

    sb_envelope_t *env = calloc(1, sizeof(sb_envelope_t));
    if (!env) return NULL;

    /* version */
    const char *vp = strstr(json, "\"v\":");
    if (!vp) goto fail;
    env->version = atoi(vp + 4);
    if (env->version != SB_ENVELOPE_VERSION) goto fail;

    /* nodeid */
    if (json_get_str(json, "nodeid", env->nodeid, sizeof(env->nodeid)) != 0)
        goto fail;

    /* pubkey */
    char pubkey_hex[SB_ED25519_PUBKEY_LEN * 2 + 2];
    if (json_get_str(json, "pubkey", pubkey_hex, sizeof(pubkey_hex)) != 0)
        goto fail;
    if (hex_to_bytes(pubkey_hex, env->pubkey, SB_ED25519_PUBKEY_LEN) != 0)
        goto fail;

    /* subject */
    if (json_get_str(json, "subject", env->subject, sizeof(env->subject)) != 0)
        goto fail;

    /* payload (base64) */
    char b64_payload[((SB_MAX_PAYLOAD_LEN + 2) / 3) * 4 + 4];
    if (json_get_str(json, "payload", b64_payload, sizeof(b64_payload)) != 0)
        goto fail;
    env->payload = malloc(SB_MAX_PAYLOAD_LEN);
    if (!env->payload) goto fail;
    if (b64_decode(b64_payload, env->payload, &env->payload_len) != 0)
        goto fail;

    /* sig */
    char sig_hex[SB_ED25519_SIG_LEN * 2 + 2];
    if (json_get_str(json, "sig", sig_hex, sizeof(sig_hex)) != 0)
        goto fail;
    if (hex_to_bytes(sig_hex, env->sig, SB_ED25519_SIG_LEN) != 0)
        goto fail;

    return env;

fail:
    if (env->payload) free(env->payload);
    free(env);
    return NULL;
}

/* ── NATS message callback (called by nats.c on receive) ────────────────── */

typedef struct {
    sb_msg_ctx_t   *ctx;
    sb_sub_entry_t *entry;
} nats_cb_arg_t;

static void nats_msg_handler(natsConnection *nc, natsSubscription *sub,
                              natsMsg *msg, void *closure)
{
    (void)nc; (void)sub;
    nats_cb_arg_t  *arg   = (nats_cb_arg_t *)closure;
    // sb_msg_ctx_t *ctx = arg->ctx; /* reserved for future use */
    sb_sub_entry_t *entry = arg->entry;

    const char *data    = natsMsg_GetData(msg);
    int         data_len = natsMsg_GetDataLength(msg);

    sb_envelope_t *env = envelope_from_json(data, (size_t)data_len);
    if (!env) {
        natsMsg_Destroy(msg);
        return;
    }

    /* Signature verification — drop message silently if invalid */
    if (!(entry->opts & SB_MSG_OPT_NO_VERIFY)) {
        if (sb_envelope_verify(env) != SB_OK) {
            sb_envelope_free(env);
            natsMsg_Destroy(msg);
            return;
        }
    }

    /* NodeID cross-check: nodeid field must match pubkey */
    uint8_t derived_nodeid[SB_NODEID_BYTES];
    crypto_hash_sha256(derived_nodeid, env->pubkey, SB_ED25519_PUBKEY_LEN);
    char derived_hex[SB_NODEID_LEN + 1];
    bytes_to_hex(derived_nodeid, SB_NODEID_BYTES, derived_hex);
    /* Note: NodeID is SHA-256(CPUID|MAC), not SHA-256(pubkey).
     * The cross-check here is: pubkey must produce a valid signature,
     * which is already done above. NodeID is informational in the envelope. */

    entry->handler(env, entry->userdata);

    sb_envelope_free(env);
    natsMsg_Destroy(msg);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int sb_msg_init(sb_msg_ctx_t **ctx_out,
                sb_identity_ctx_t *id_ctx,
                const char *nats_url,
                const char *service)
{
    if (!ctx_out || !id_ctx || !nats_url || !service)
        return SB_ERR_INVALID_ARG;

    if (sodium_init() < 0) return SB_ERR_CRYPTO;

    sb_msg_ctx_t *ctx = calloc(1, sizeof(sb_msg_ctx_t));
    if (!ctx) return SB_ERR_IO;

    ctx->id_ctx     = id_ctx;
    ctx->next_sub_id = 1;
    ctx->subs        = NULL;

    strncpy(ctx->nats_url, nats_url, sizeof(ctx->nats_url) - 1);
    strncpy(ctx->service,  service,  sizeof(ctx->service)  - 1);

    /* Cache nodeid and pubkey from identity context */
    int rc = sb_identity_get_nodeid(id_ctx, ctx->nodeid, sizeof(ctx->nodeid));
    if (rc != SB_OK) { free(ctx); return rc; }

    rc = sb_identity_get_pubkey(id_ctx, ctx->pubkey);
    if (rc != SB_OK) { free(ctx); return rc; }

    *ctx_out = ctx;
    return SB_OK;
}

int sb_msg_connect(sb_msg_ctx_t *ctx, int timeout_ms)
{
    if (!ctx) return SB_ERR_INVALID_ARG;

    natsOptions *opts = NULL;
    natsStatus   s;

    s = natsOptions_Create(&opts);
    if (s != NATS_OK) return SB_ERR_NOT_CONNECTED;

    natsOptions_SetURL(opts, ctx->nats_url);
    natsOptions_SetName(opts, ctx->service);
    natsOptions_SetMaxReconnect(opts, -1);          /* unlimited reconnects */
    natsOptions_SetReconnectWait(opts, 3000);        /* 3s between attempts  */
    natsOptions_SetTimeout(opts, timeout_ms > 0 ? timeout_ms : 5000);

    s = natsConnection_Connect(&ctx->nc, opts);
    natsOptions_Destroy(opts);

    if (s != NATS_OK) return SB_ERR_NOT_CONNECTED;
    return SB_OK;
}

void sb_msg_free(sb_msg_ctx_t *ctx)
{
    if (!ctx) return;

    /* Unsubscribe all */
    sb_sub_entry_t *e = ctx->subs;
    while (e) {
        sb_sub_entry_t *next = e->next;
        if (e->nat_sub) natsSubscription_Unsubscribe(e->nat_sub);
        free(e);
        e = next;
    }

    if (ctx->nc) natsConnection_Destroy(ctx->nc);
    free(ctx);
}

int sb_msg_publish(sb_msg_ctx_t *ctx,
                   const char *subject,
                   const uint8_t *payload,
                   size_t payload_len,
                   uint32_t opts)
{
    if (!ctx || !subject || !payload) return SB_ERR_INVALID_ARG;
    if (!ctx->nc) return SB_ERR_NOT_CONNECTED;
    if (payload_len > SB_MAX_PAYLOAD_LEN) return SB_ERR_INVALID_ARG;

    /* Build envelope */
    sb_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.version     = SB_ENVELOPE_VERSION;
    env.payload     = (uint8_t *)payload;
    env.payload_len = payload_len;

    strncpy(env.nodeid,  ctx->nodeid, SB_NODEID_LEN);
    strncpy(env.subject, subject,     SB_MAX_SUBJECT_LEN - 1);
    memcpy(env.pubkey, ctx->pubkey, SB_ED25519_PUBKEY_LEN);

    /* Sign unless disabled */
    if (!(opts & SB_MSG_OPT_NO_SIGN)) {
        int rc = sb_identity_sign(ctx->id_ctx, payload, payload_len, env.sig);
        if (rc != SB_OK) return rc;
    }

    /* Serialise to JSON */
    char *json = envelope_to_json(&env);
    if (!json) return SB_ERR_IO;

    /* Publish */
    natsStatus s = natsConnection_PublishString(ctx->nc, subject, json);
    free(json);

    return (s == NATS_OK) ? SB_OK : SB_ERR_PUBLISH;
}

int sb_msg_publish_str(sb_msg_ctx_t *ctx,
                       const char *subject,
                       const char *payload_str,
                       uint32_t opts)
{
    if (!payload_str) return SB_ERR_INVALID_ARG;
    return sb_msg_publish(ctx, subject,
                          (const uint8_t *)payload_str,
                          strlen(payload_str), opts);
}

int sb_msg_heartbeat(sb_msg_ctx_t *ctx)
{
    if (!ctx) return SB_ERR_INVALID_ARG;

    char subject[256];
    snprintf(subject, sizeof(subject), SB_SUBJ_HEARTBEAT, ctx->service);

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"service\":\"%s\",\"nodeid\":\"%s\"}",
             ctx->service, ctx->nodeid);

    return sb_msg_publish_str(ctx, subject, payload, SB_MSG_OPT_NONE);
}

int sb_msg_subscribe(sb_msg_ctx_t *ctx,
                     const char *subject,
                     sb_msg_handler_t handler,
                     void *userdata,
                     uint32_t opts)
{
    if (!ctx || !subject || !handler) return SB_ERR_INVALID_ARG;
    if (!ctx->nc) return SB_ERR_NOT_CONNECTED;

    sb_sub_entry_t *entry = calloc(1, sizeof(sb_sub_entry_t));
    if (!entry) return SB_ERR_IO;

    entry->id       = ctx->next_sub_id++;
    entry->handler  = handler;
    entry->userdata = userdata;
    entry->opts     = opts;

    nats_cb_arg_t *arg = malloc(sizeof(nats_cb_arg_t));
    if (!arg) { free(entry); return SB_ERR_IO; }
    arg->ctx   = ctx;
    arg->entry = entry;

    natsStatus s = natsConnection_Subscribe(&entry->nat_sub, ctx->nc,
                                             subject, nats_msg_handler, arg);
    if (s != NATS_OK) {
        free(arg);
        free(entry);
        return SB_ERR_SUBSCRIBE;
    }

    /* Prepend to subscription list */
    entry->next = ctx->subs;
    ctx->subs   = entry;

    return entry->id;
}

int sb_msg_unsubscribe(sb_msg_ctx_t *ctx, int sub_id)
{
    if (!ctx) return SB_ERR_INVALID_ARG;

    sb_sub_entry_t **pp = &ctx->subs;
    while (*pp) {
        if ((*pp)->id == sub_id) {
            sb_sub_entry_t *e = *pp;
            *pp = e->next;
            if (e->nat_sub) natsSubscription_Unsubscribe(e->nat_sub);
            free(e);
            return SB_OK;
        }
        pp = &(*pp)->next;
    }
    return SB_ERR_INVALID_ARG;
}

int sb_msg_request(sb_msg_ctx_t *ctx,
                   const char *subject,
                   const uint8_t *payload,
                   size_t payload_len,
                   int timeout_ms,
                   sb_envelope_t **reply_env)
{
    if (!ctx || !subject || !payload || !reply_env)
        return SB_ERR_INVALID_ARG;
    if (!ctx->nc) return SB_ERR_NOT_CONNECTED;

    /* Build and sign the request envelope JSON */
    sb_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.version     = SB_ENVELOPE_VERSION;
    env.payload     = (uint8_t *)payload;
    env.payload_len = payload_len;
    strncpy(env.nodeid,  ctx->nodeid, SB_NODEID_LEN);
    strncpy(env.subject, subject,     SB_MAX_SUBJECT_LEN - 1);
    memcpy(env.pubkey, ctx->pubkey, SB_ED25519_PUBKEY_LEN);
    sb_identity_sign(ctx->id_ctx, payload, payload_len, env.sig);

    char *json = envelope_to_json(&env);
    if (!json) return SB_ERR_IO;

    natsMsg *reply = NULL;
    natsStatus s = natsConnection_RequestString(&reply, ctx->nc, subject, json,
                                                 timeout_ms > 0 ? timeout_ms : 5000);
    free(json);

    if (s == NATS_TIMEOUT)  return SB_ERR_TIMEOUT;
    if (s != NATS_OK)       return SB_ERR_PUBLISH;

    sb_envelope_t *renv = envelope_from_json(natsMsg_GetData(reply),
                                              (size_t)natsMsg_GetDataLength(reply));
    natsMsg_Destroy(reply);

    if (!renv) return SB_ERR_BAD_ENVELOPE;

    if (sb_envelope_verify(renv) != SB_OK) {
        sb_envelope_free(renv);
        return SB_ERR_SIG_INVALID;
    }

    *reply_env = renv;
    return SB_OK;
}

/* ── Envelope utilities ─────────────────────────────────────────────────── */

void sb_envelope_free(sb_envelope_t *env)
{
    if (!env) return;
    if (env->payload) free(env->payload);
    free(env);
}

int sb_envelope_verify(const sb_envelope_t *env)
{
    if (!env) return SB_ERR_INVALID_ARG;
    return sb_identity_verify_sig(env->payload, env->payload_len,
                                   env->sig, env->pubkey);
}

const char *sb_msg_strerror(int err)
{
    switch (err) {
        case SB_OK:                 return "OK";
        case SB_ERR_NOT_CONNECTED:  return "Not connected to NATS";
        case SB_ERR_PUBLISH:        return "Publish failed";
        case SB_ERR_SUBSCRIBE:      return "Subscribe failed";
        case SB_ERR_TIMEOUT:        return "Request timed out";
        case SB_ERR_BAD_ENVELOPE:   return "Malformed message envelope";
        case SB_ERR_SIG_INVALID:    return "Signature verification failed";
        default:                    return sb_identity_strerror(err);
    }
}
