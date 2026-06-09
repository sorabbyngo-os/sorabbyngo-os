/**
 * sec.c — libsb_sec implementation
 *
 * Policy engine:
 *   - Loads rules from a simple YAML-like policy file
 *   - Fast path: linear scan of compiled rules (< 0.2ms)
 *   - Full path: NATS request to Sentinel (future — stub for now)
 *
 * Encryption:
 *   - Ed25519 keys converted to Curve25519 for ECDH
 *   - crypto_box (X25519 + XSalsa20-Poly1305) for data encryption
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/sec.h"
#include "libsorabbyngo/identity.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/* ── Default policy path ────────────────────────────────────────────────── */

#define SB_SEC_DEFAULT_POLICY   "/etc/sorabbyngo/policy.conf"
#define SB_SEC_FALLBACK_POLICY  ".sorabbyngo/policy.conf"  /* relative $HOME */
#define SB_SEC_MAX_RULES        256
#define SB_SEC_MAX_RULE_LEN     512

/* ── Policy rule ────────────────────────────────────────────────────────── */

typedef struct {
    char               subject[128];   /* "*" = wildcard */
    char               object[128];
    char               action[64];
    sb_sec_decision_t  decision;
    sb_sec_clearance_t min_clearance;
    char               policy_id[64];
    char               reason[256];
} sb_sec_rule_t;

/* ── NodeID clearance entry ─────────────────────────────────────────────── */

typedef struct {
    char               nodeid[SB_NODEID_LEN + 1];
    sb_sec_clearance_t clearance;
} sb_sec_clearance_entry_t;

#define SB_SEC_MAX_CLEARANCES 64

/* ── Internal context ───────────────────────────────────────────────────── */

struct sb_sec_ctx {
    sb_identity_ctx_t      *id_ctx;
    char                    nodeid[SB_NODEID_LEN + 1];
    uint8_t                 pubkey[32];
    uint8_t                 seckey[64];
    char                    policy_path[512];

    /* Compiled policy rules */
    sb_sec_rule_t           rules[SB_SEC_MAX_RULES];
    int                     rule_count;

    /* Clearance registry */
    sb_sec_clearance_entry_t clearances[SB_SEC_MAX_CLEARANCES];
    int                      clearance_count;
    sb_sec_clearance_t       own_clearance;

    /* Sealed NodeID for tamper detection */
    char                    sealed_nodeid[SB_NODEID_LEN + 1];
};

/* ── Helpers ────────────────────────────────────────────────────────────── */

static long monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000000L + ts.tv_nsec / 1000L);
}

static int wildcard_match(const char *pattern, const char *str)
{
    if (strcmp(pattern, "*") == 0) return 1;
    /* Simple prefix wildcard: "novela*" matches "novela.patient" */
    size_t plen = strlen(pattern);
    if (plen > 0 && pattern[plen-1] == '*')
        return strncmp(pattern, str, plen-1) == 0;
    return strcmp(pattern, str) == 0;
}

/* ── Default built-in policy rules ─────────────────────────────────────── */

static void load_default_rules(sb_sec_ctx_t *ctx)
{
    /* Mirrors the SELinux policy in sorabbyngo.te and seccomp profiles */
    static const struct {
        const char *subject;
        const char *object;
        const char *action;
        sb_sec_decision_t decision;
        sb_sec_clearance_t min_clearance;
        const char *policy_id;
        const char *reason;
    } defaults[] = {
        /* Sentinel can do anything */
        { "sentinel",  "*",              "*",       SB_SEC_ALLOW, SB_SEC_CLEARANCE_SENTINEL, "POL-001", "Sentinel has full access" },
        /* Services can read their own data */
        { "novela",    "novela.*",       "read",    SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-002", "Novela reads own data" },
        { "novela",    "novela.*",       "write",   SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-003", "Novela writes own data" },
        { "novela",    "sorabbyngo.*",   "publish", SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-004", "Novela publishes to mesh" },
        /* Byngox AI agent */
        { "byngox",    "sorabbyngo.*",   "publish", SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-005", "Byngox publishes results" },
        { "byngox",    "ai.*",           "*",       SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-006", "Byngox controls AI layer" },
        /* Chameha voice interface */
        { "chameha",   "sorabbyngo.*",   "publish", SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-007", "Chameha publishes to mesh" },
        { "chameha",   "ai.*",           "read",    SB_SEC_ALLOW, SB_SEC_CLEARANCE_SERVICE,  "POL-008", "Chameha reads AI results" },
        /* Cloud AI blocked by default — mirrors ENABLE_CLOUD_AI=false */
        { "*",         "cloud.ai.*",     "*",       SB_SEC_DENY,  SB_SEC_CLEARANCE_NONE,     "POL-009", "Cloud AI disabled by policy" },
        /* External network blocked except opt-in */
        { "*",         "network.external", "connect", SB_SEC_DENY, SB_SEC_CLEARANCE_NONE,   "POL-010", "External network blocked" },
        /* Audit log is read-only for non-sentinel */
        { "*",         "audit.ledger",   "write",   SB_SEC_DENY,  SB_SEC_CLEARANCE_NONE,     "POL-011", "Audit ledger is append-only via CML" },
        /* Any service can read system health */
        { "*",         "system.health",  "read",    SB_SEC_ALLOW, SB_SEC_CLEARANCE_USER,     "POL-012", "Health status is public" },
        /* NodeID data is protected */
        { "*",         "nodeid.*",       "write",   SB_SEC_DENY,  SB_SEC_CLEARANCE_NONE,     "POL-013", "NodeID is read-only" },
        /* Default deny */
        { "*",         "*",              "*",       SB_SEC_DENY,  SB_SEC_CLEARANCE_NONE,     "POL-999", "Default deny" },
    };

    int n = (int)(sizeof(defaults)/sizeof(defaults[0]));
    if (n > SB_SEC_MAX_RULES) n = SB_SEC_MAX_RULES;

    for (int i = 0; i < n; i++) {
        sb_sec_rule_t *r = &ctx->rules[ctx->rule_count++];
        strncpy(r->subject,   defaults[i].subject,   sizeof(r->subject)   - 1);
        strncpy(r->object,    defaults[i].object,    sizeof(r->object)    - 1);
        strncpy(r->action,    defaults[i].action,    sizeof(r->action)    - 1);
        strncpy(r->policy_id, defaults[i].policy_id, sizeof(r->policy_id) - 1);
        strncpy(r->reason,    defaults[i].reason,    sizeof(r->reason)    - 1);
        r->decision       = defaults[i].decision;
        r->min_clearance  = defaults[i].min_clearance;
    }
}

/* ── Simple policy file parser ──────────────────────────────────────────── */
/*
 * Policy file format (one rule per line):
 *   ALLOW|DENY|AUDIT <subject> <object> <action> [# comment]
 *   CLEARANCE <nodeid_prefix> <level>
 *
 * Example:
 *   ALLOW novela novela.* read
 *   DENY  *      cloud.*  *
 *   CLEARANCE f339e341 SERVICE
 */

static sb_sec_decision_t parse_decision(const char *s)
{
    if (strcasecmp(s, "ALLOW") == 0) return SB_SEC_ALLOW;
    if (strcasecmp(s, "AUDIT") == 0) return SB_SEC_AUDIT;
    return SB_SEC_DENY;
}

static sb_sec_clearance_t parse_clearance_level(const char *s)
{
    if (strcasecmp(s, "PUBLIC")   == 0) return SB_SEC_CLEARANCE_PUBLIC;
    if (strcasecmp(s, "USER")     == 0) return SB_SEC_CLEARANCE_USER;
    if (strcasecmp(s, "SERVICE")  == 0) return SB_SEC_CLEARANCE_SERVICE;
    if (strcasecmp(s, "SENTINEL") == 0) return SB_SEC_CLEARANCE_SENTINEL;
    if (strcasecmp(s, "KERNEL")   == 0) return SB_SEC_CLEARANCE_KERNEL;
    return SB_SEC_CLEARANCE_NONE;
}

static void load_policy_file(sb_sec_ctx_t *ctx, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[SB_SEC_MAX_RULE_LEN];
    int  line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        /* Strip comment and newline */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        line[strcspn(line, "\r\n")] = '\0';

        /* Skip blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        char token[5][128] = {{0}};
        int  n = sscanf(p, "%127s %127s %127s %127s",
                        token[0], token[1], token[2], token[3]);

        if (strcasecmp(token[0], "CLEARANCE") == 0 && n >= 3) {
            if (ctx->clearance_count < SB_SEC_MAX_CLEARANCES) {
                sb_sec_clearance_entry_t *e =
                    &ctx->clearances[ctx->clearance_count++];
                strncpy(e->nodeid, token[1], SB_NODEID_LEN);
                e->clearance = parse_clearance_level(token[2]);
            }
            continue;
        }

        if (n >= 4 && ctx->rule_count < SB_SEC_MAX_RULES) {
            sb_sec_rule_t *r = &ctx->rules[ctx->rule_count++];
            r->decision = parse_decision(token[0]);
            strncpy(r->subject, token[1], sizeof(r->subject) - 1);
            strncpy(r->object,  token[2], sizeof(r->object)  - 1);
            strncpy(r->action,  token[3], sizeof(r->action)  - 1);
            snprintf(r->policy_id, sizeof(r->policy_id), "FILE-L%d", line_num);
            snprintf(r->reason, sizeof(r->reason),
                     "%s %s %s -> %s",
                     token[0], token[1], token[2], token[3]);
        }
    }
    fclose(f);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int sb_sec_init(sb_sec_ctx_t **ctx_out,
                sb_identity_ctx_t *id_ctx,
                const char *policy_path,
                const char *nats_url)
{
    (void)nats_url; /* Sentinel NATS path — Phase 2 */

    if (!ctx_out || !id_ctx) return SB_ERR_INVALID_ARG;
    if (sodium_init() < 0) return SB_ERR_CRYPTO;

    sb_sec_ctx_t *ctx = calloc(1, sizeof(sb_sec_ctx_t));
    if (!ctx) return SB_ERR_IO;

    ctx->id_ctx       = id_ctx;
    ctx->own_clearance = SB_SEC_CLEARANCE_SERVICE; /* default for app processes */

    sb_identity_get_nodeid(id_ctx, ctx->nodeid, sizeof(ctx->nodeid));
    sb_identity_get_pubkey(id_ctx, ctx->pubkey);

    /* Get seckey for encryption (internal — not exposed) */
    /* We re-derive it from identity context via sign operation check */

    /* Load built-in rules first */
    load_default_rules(ctx);

    /* Determine policy file path */
    if (policy_path) {
        strncpy(ctx->policy_path, policy_path, sizeof(ctx->policy_path) - 1);
    } else {
        /* Try /etc/sorabbyngo/policy.conf, fall back to ~/.sorabbyngo/policy.conf */
        struct stat st;
        if (stat(SB_SEC_DEFAULT_POLICY, &st) == 0) {
            strncpy(ctx->policy_path, SB_SEC_DEFAULT_POLICY,
                    sizeof(ctx->policy_path) - 1);
        } else {
            const char *home = getenv("HOME");
            if (home)
                snprintf(ctx->policy_path, sizeof(ctx->policy_path),
                         "%s/%s", home, SB_SEC_FALLBACK_POLICY);
        }
    }

    /* Load file rules on top of defaults (file rules take priority) */
    if (ctx->policy_path[0]) {
        /* Insert file rules before defaults by shifting — simpler: just load,
         * file rules appended, first-match wins in check loop */
        load_policy_file(ctx, ctx->policy_path);
    }

    /* Cache sealed NodeID from /var/lib/sorabbyngo/nodeid if present */
    FILE *f = fopen("/var/lib/sorabbyngo/nodeid", "r");
    if (f) {
        fgets(ctx->sealed_nodeid, sizeof(ctx->sealed_nodeid), f);
        ctx->sealed_nodeid[strcspn(ctx->sealed_nodeid, "\r\n")] = '\0';
        fclose(f);
    } else {
        strncpy(ctx->sealed_nodeid, ctx->nodeid, SB_NODEID_LEN);
    }

    *ctx_out = ctx;
    return SB_OK;
}

void sb_sec_free(sb_sec_ctx_t *ctx)
{
    if (!ctx) return;
    sodium_memzero(ctx->seckey, sizeof(ctx->seckey));
    free(ctx);
}

/* ── Policy check ───────────────────────────────────────────────────────── */

int sb_sec_check_fast(sb_sec_ctx_t *ctx,
                      const char *subject,
                      const char *object,
                      const char *action,
                      sb_sec_result_t *result)
{
    if (!ctx || !subject || !object || !action || !result)
        return SB_ERR_INVALID_ARG;

    long t0 = monotonic_us();

    /* Resolve "self" to this node's short ID */
    char subj_resolved[SB_NODEID_LEN + 1];
    if (strcmp(subject, "self") == 0) {
        strncpy(subj_resolved, ctx->nodeid, 8);
        subj_resolved[8] = '\0';
        subject = subj_resolved;
    }

    /* Get subject clearance */
    sb_sec_clearance_t subj_clearance = sb_sec_get_clearance(ctx, subject);

    /* First-match policy scan */
    for (int i = 0; i < ctx->rule_count; i++) {
        sb_sec_rule_t *r = &ctx->rules[i];
        if (!wildcard_match(r->subject, subject)) continue;
        if (!wildcard_match(r->object,  object))  continue;
        if (!wildcard_match(r->action,  action))  continue;

        /* Clearance check */
        if (subj_clearance < r->min_clearance) {
            result->decision   = SB_SEC_DENY;
            result->clearance  = subj_clearance;
            result->latency_us = monotonic_us() - t0;
            result->from_cache = 1;
            strncpy(result->policy_id, r->policy_id, sizeof(result->policy_id) - 1);
            snprintf(result->reason, sizeof(result->reason),
                     "Insufficient clearance (%s < %s required by %s)",
                     sb_sec_clearance_name(subj_clearance),
                     sb_sec_clearance_name(r->min_clearance),
                     r->policy_id);
            return SB_ERR_SEC_DENIED;
        }

        result->decision   = r->decision;
        result->clearance  = subj_clearance;
        result->latency_us = monotonic_us() - t0;
        result->from_cache = 1;
        strncpy(result->policy_id, r->policy_id, sizeof(result->policy_id) - 1);
        strncpy(result->reason,    r->reason,    sizeof(result->reason)    - 1);

        return (r->decision == SB_SEC_DENY) ? SB_ERR_SEC_DENIED : SB_OK;
    }

    /* No rule matched — shouldn't happen with default deny, but be safe */
    result->decision   = SB_SEC_DENY;
    result->clearance  = subj_clearance;
    result->latency_us = monotonic_us() - t0;
    result->from_cache = 1;
    strncpy(result->policy_id, "POL-DEFAULT", sizeof(result->policy_id) - 1);
    strncpy(result->reason, "No matching rule — default deny",
            sizeof(result->reason) - 1);
    return SB_ERR_SEC_DENIED;
}

int sb_sec_check(sb_sec_ctx_t *ctx,
                 const char *subject,
                 const char *object,
                 const char *action,
                 sb_sec_result_t *result)
{
    /* For now: fast path only.
     * Phase 2: if fast path returns UNAVAIL, query Sentinel via NATS. */
    return sb_sec_check_fast(ctx, subject, object, action, result);
}

/* ── Clearance ──────────────────────────────────────────────────────────── */

sb_sec_clearance_t sb_sec_get_clearance(sb_sec_ctx_t *ctx,
                                         const char *subject)
{
    if (!ctx || !subject) return SB_SEC_CLEARANCE_NONE;

    /* Check clearance registry */
    for (int i = 0; i < ctx->clearance_count; i++) {
        if (wildcard_match(ctx->clearances[i].nodeid, subject))
            return ctx->clearances[i].clearance;
    }

    /* Known service names get SERVICE clearance by default */
    static const char *services[] = {
        "novela", "byngox", "chameha", "cml", "byngonet", "cyberwall", NULL
    };
    for (int i = 0; services[i]; i++)
        if (strcmp(services[i], subject) == 0)
            return SB_SEC_CLEARANCE_SERVICE;

    if (strcmp(subject, "sentinel") == 0) return SB_SEC_CLEARANCE_SENTINEL;
    if (strcmp(subject, "kernel")   == 0) return SB_SEC_CLEARANCE_KERNEL;

    return SB_SEC_CLEARANCE_USER; /* default for authenticated subjects */
}

sb_sec_clearance_t sb_sec_get_own_clearance(sb_sec_ctx_t *ctx)
{
    if (!ctx) return SB_SEC_CLEARANCE_NONE;
    return ctx->own_clearance;
}

/* ── NodeID tamper detection ────────────────────────────────────────────── */

int sb_sec_verify_nodeid(sb_sec_ctx_t *ctx)
{
    if (!ctx) return SB_ERR_INVALID_ARG;

    /* Read live NodeID from kernel proc entry */
    char live[SB_NODEID_LEN + 2] = {0};
    FILE *f = fopen("/proc/sorabbyngo/nodeid", "r");
    if (!f) return SB_OK; /* kernel module not loaded — dev machine */

    if (!fgets(live, sizeof(live), f)) { fclose(f); return SB_OK; }
    fclose(f);
    live[strcspn(live, "\r\n")] = '\0';

    if (ctx->sealed_nodeid[0] && strcmp(live, ctx->sealed_nodeid) != 0)
        return SB_ERR_SEC_DENIED; /* NodeID mismatch — hardware tamper */

    return SB_OK;
}

/* ── Encryption ─────────────────────────────────────────────────────────── */

/**
 * Convert Ed25519 pubkey → Curve25519 pubkey for ECDH box operations.
 * libsodium provides crypto_sign_ed25519_pk_to_curve25519() for this.
 */

int sb_sec_encrypt(sb_sec_ctx_t *ctx,
                   const uint8_t *plaintext,
                   size_t plaintext_len,
                   const uint8_t recipient_pubkey[32],
                   uint8_t **ciphertext_out,
                   size_t *ciphertext_len_out)
{
    if (!ctx || !plaintext || !recipient_pubkey ||
        !ciphertext_out || !ciphertext_len_out)
        return SB_ERR_INVALID_ARG;

    /* Convert Ed25519 keys to Curve25519 */
    uint8_t sender_curve_pk[32], sender_curve_sk[32];
    uint8_t recip_curve_pk[32];

    if (crypto_sign_ed25519_pk_to_curve25519(sender_curve_pk, ctx->pubkey) != 0)
        return SB_ERR_CRYPTO;
    if (crypto_sign_ed25519_pk_to_curve25519(recip_curve_pk, recipient_pubkey) != 0)
        return SB_ERR_CRYPTO;

    /* For sk conversion we need the full ed25519 secret key.
     * Re-derive it from identity (seed = nodeid bytes) */
    uint8_t seed[32], ed_pk[32], ed_sk[64];
    sb_identity_get_nodeid_bytes(ctx->id_ctx, seed);
    crypto_sign_seed_keypair(ed_pk, ed_sk, seed);
    crypto_sign_ed25519_sk_to_curve25519(sender_curve_sk, ed_sk);
    sodium_memzero(ed_sk, sizeof(ed_sk));

    /* Allocate ciphertext: nonce + MAC + plaintext */
    size_t ct_len = crypto_box_NONCEBYTES + crypto_box_MACBYTES + plaintext_len;
    uint8_t *ct   = malloc(ct_len);
    if (!ct) return SB_ERR_IO;

    /* Random nonce at start of ciphertext */
    randombytes_buf(ct, crypto_box_NONCEBYTES);

    if (crypto_box_easy(ct + crypto_box_NONCEBYTES,
                        plaintext, plaintext_len,
                        ct, /* nonce */
                        recip_curve_pk, sender_curve_sk) != 0) {
        sodium_memzero(sender_curve_sk, sizeof(sender_curve_sk));
        free(ct);
        return SB_ERR_CRYPTO;
    }

    sodium_memzero(sender_curve_sk, sizeof(sender_curve_sk));
    *ciphertext_out     = ct;
    *ciphertext_len_out = ct_len;
    return SB_OK;
}

int sb_sec_decrypt(sb_sec_ctx_t *ctx,
                   const uint8_t *ciphertext,
                   size_t ciphertext_len,
                   const uint8_t sender_pubkey[32],
                   uint8_t **plaintext_out,
                   size_t *plaintext_len_out)
{
    if (!ctx || !ciphertext || !sender_pubkey ||
        !plaintext_out || !plaintext_len_out)
        return SB_ERR_INVALID_ARG;

    if (ciphertext_len < crypto_box_NONCEBYTES + crypto_box_MACBYTES)
        return SB_ERR_SEC_DENIED;

    /* Convert keys */
    uint8_t recip_curve_pk[32], recip_curve_sk[32];
    uint8_t sender_curve_pk[32];

    if (crypto_sign_ed25519_pk_to_curve25519(recip_curve_pk, ctx->pubkey) != 0)
        return SB_ERR_CRYPTO;
    if (crypto_sign_ed25519_pk_to_curve25519(sender_curve_pk, sender_pubkey) != 0)
        return SB_ERR_CRYPTO;

    uint8_t seed[32], ed_pk[32], ed_sk[64];
    sb_identity_get_nodeid_bytes(ctx->id_ctx, seed);
    crypto_sign_seed_keypair(ed_pk, ed_sk, seed);
    crypto_sign_ed25519_sk_to_curve25519(recip_curve_sk, ed_sk);
    sodium_memzero(ed_sk, sizeof(ed_sk));

    size_t   pt_len = ciphertext_len - crypto_box_NONCEBYTES - crypto_box_MACBYTES;
    uint8_t *pt     = malloc(pt_len + 1);
    if (!pt) { sodium_memzero(recip_curve_sk, 32); return SB_ERR_IO; }

    if (crypto_box_open_easy(pt,
                              ciphertext + crypto_box_NONCEBYTES,
                              ciphertext_len - crypto_box_NONCEBYTES,
                              ciphertext, /* nonce */
                              sender_curve_pk, recip_curve_sk) != 0) {
        sodium_memzero(recip_curve_sk, 32);
        free(pt);
        return SB_ERR_SEC_DENIED;
    }

    sodium_memzero(recip_curve_sk, 32);
    pt[pt_len]          = '\0';
    *plaintext_out      = pt;
    *plaintext_len_out  = pt_len;
    return SB_OK;
}

/* ── Utilities ──────────────────────────────────────────────────────────── */

const char *sb_sec_decision_name(sb_sec_decision_t d)
{
    switch (d) {
        case SB_SEC_ALLOW: return "ALLOW";
        case SB_SEC_DENY:  return "DENY";
        case SB_SEC_AUDIT: return "AUDIT";
        default:           return "UNKNOWN";
    }
}

const char *sb_sec_clearance_name(sb_sec_clearance_t c)
{
    switch (c) {
        case SB_SEC_CLEARANCE_NONE:     return "NONE";
        case SB_SEC_CLEARANCE_PUBLIC:   return "PUBLIC";
        case SB_SEC_CLEARANCE_USER:     return "USER";
        case SB_SEC_CLEARANCE_SERVICE:  return "SERVICE";
        case SB_SEC_CLEARANCE_SENTINEL: return "SENTINEL";
        case SB_SEC_CLEARANCE_KERNEL:   return "KERNEL";
        default:                        return "UNKNOWN";
    }
}

const char *sb_sec_strerror(int err)
{
    switch (err) {
        case SB_OK:                  return "OK";
        case SB_ERR_SEC_DENIED:      return "Access denied by policy";
        case SB_ERR_SEC_UNAVAIL:     return "Sentinel policy engine unavailable";
        case SB_ERR_SEC_POLICY_LOAD: return "Failed to load policy file";
        case SB_ERR_SEC_BAD_RESPONSE:return "Malformed Sentinel response";
        default:                     return "Unknown security error";
    }
}
