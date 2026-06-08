/**
 * memory.c — libsb_memory implementation
 *
 * Hash-chained audit ledger mirrors AuditLedger in audit.py exactly:
 *   chain_hash = SHA-256(prev_hash + json.dumps(entry, sort_keys=True))
 *
 * Key-value store is a separate JSONL file — each line is one write op.
 * Reads scan backwards for the latest value (tombstone for deletes).
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/memory.h"
#include "libsorabbyngo/identity.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <regex.h>
#include <pthread.h>

/* ── Internal context ───────────────────────────────────────────────────── */

struct sb_mem_ctx {
    sb_identity_ctx_t *id_ctx;
    char               nodeid[SB_NODEID_LEN + 1];
    char               data_dir[512];
    char               audit_path[560];
    char               store_path[560];
    char               prev_hash[SB_MEM_CHAIN_HASH_LEN + 1];
    pthread_mutex_t    lock;
};

/* ── Helpers ────────────────────────────────────────────────────────────── */

static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i*2]   = hex[(in[i] >> 4) & 0xf];
        out[i*2+1] = hex[in[i] & 0xf];
    }
    out[len*2] = '\0';
}

static void get_iso8601(char *buf, size_t len)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm);
    /* append microseconds + Z */
    size_t pos = strlen(buf);
    snprintf(buf + pos, len - pos, ".%06ldZ", ts.tv_nsec / 1000);
}

static int mkdir_p(const char *path)
{
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

/* ── Kernel audit tail ──────────────────────────────────────────────────── */

static int read_kernel_audit_tail(char *out, size_t out_len)
{
    FILE *f = fopen(SB_MEM_KERNEL_AUDIT_TAIL, "r");
    if (!f) return -1;
    if (!fgets(out, (int)out_len, f)) { fclose(f); return -1; }
    fclose(f);
    /* strip newline */
    out[strcspn(out, "\r\n")] = '\0';
    return 0;
}

/* ── Chain hash computation (mirrors audit.py exactly) ─────────────────── */

/**
 * Compute SHA-256(prev_hash + entry_json_without_chain_hash)
 * entry_json must NOT contain "chain_hash" field.
 */
static void compute_chain_hash(const char *prev_hash,
                                const char *entry_json,
                                char out[SB_MEM_CHAIN_HASH_LEN + 1])
{
    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    crypto_hash_sha256_update(&state,
        (const uint8_t *)prev_hash, strlen(prev_hash));
    crypto_hash_sha256_update(&state,
        (const uint8_t *)entry_json, strlen(entry_json));
    uint8_t hash[32];
    crypto_hash_sha256_final(&state, hash);
    bytes_to_hex(hash, 32, out);
}

/* ── PII scrubbing ──────────────────────────────────────────────────────── */

/* Simple pattern-based scrubber using regcomp/regexec */
typedef struct { const char *pattern; uint32_t flag; } pii_pattern_t;

static const pii_pattern_t PII_PATTERNS[] = {
    /* Phone: +254XXXXXXXXX, 07XXXXXXXX, 01XXXXXXXX */
    { "\\+?[0-9]{1,3}[-. ]?\\(?[0-9]{3}\\)?[-. ]?[0-9]{3}[-. ]?[0-9]{4,6}",
      SB_MEM_PII_PHONE },
    /* Email */
    { "[a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,}",
      SB_MEM_PII_EMAIL },
    /* Kenya National ID: 8 digits */
    { "\"[0-9]{7,9}\"",
      SB_MEM_PII_ID_NUMBER },
};

int sb_mem_scrub_pii(char *json, size_t json_size, uint32_t flags)
{
    if (!json || !flags) return 0;

    int total_replacements = 0;
    static const char REDACTED[] = "\"[REDACTED]\"";

    for (size_t p = 0; p < sizeof(PII_PATTERNS)/sizeof(PII_PATTERNS[0]); p++) {
        if (!(flags & PII_PATTERNS[p].flag)) continue;

        regex_t re;
        if (regcomp(&re, PII_PATTERNS[p].pattern, REG_EXTENDED) != 0) continue;

        regmatch_t match;
        char *pos = json;
        char  tmp[SB_MEM_MAX_VALUE_LEN + SB_MEM_MAX_EVENT_LEN];
        size_t tmp_pos = 0;
        int replacements = 0;

        while (*pos && regexec(&re, pos, 1, &match, 0) == 0) {
            /* Copy everything before the match */
            size_t before = (size_t)match.rm_so;
            if (tmp_pos + before >= sizeof(tmp)) break;
            memcpy(tmp + tmp_pos, pos, before);
            tmp_pos += before;

            /* Insert REDACTED */
            size_t rlen = strlen(REDACTED);
            if (tmp_pos + rlen >= sizeof(tmp)) break;
            memcpy(tmp + tmp_pos, REDACTED, rlen);
            tmp_pos += rlen;

            pos += match.rm_eo;
            replacements++;
        }

        if (replacements > 0) {
            /* Copy remainder */
            size_t rem = strlen(pos);
            if (tmp_pos + rem < sizeof(tmp)) {
                memcpy(tmp + tmp_pos, pos, rem);
                tmp_pos += rem;
                tmp[tmp_pos] = '\0';
                strncpy(json, tmp, json_size - 1);
                json[json_size - 1] = '\0';
                total_replacements += replacements;
            }
        }

        regfree(&re);
    }

    return total_replacements;
}

/* ── Resume chain from last entry in audit.jsonl ────────────────────────── */

static void resume_chain(sb_mem_ctx_t *ctx)
{
    strncpy(ctx->prev_hash, "0000000000000000000000000000000000000000"
                            "000000000000000000000000", SB_MEM_CHAIN_HASH_LEN);

    FILE *f = fopen(ctx->audit_path, "r");
    if (!f) return;

    char line[SB_MEM_MAX_EVENT_LEN + 256];
    char last_line[SB_MEM_MAX_EVENT_LEN + 256] = {0};

    while (fgets(line, sizeof(line), f))
        if (strlen(line) > 2)
            strncpy(last_line, line, sizeof(last_line) - 1);
    fclose(f);

    if (!last_line[0]) return;

    /* Extract chain_hash from last entry */
    const char *key = "\"chain_hash\":\"";
    const char *p   = strstr(last_line, key);
    if (!p) return;
    p += strlen(key);
    const char *end = strchr(p, '"');
    if (!end || (size_t)(end - p) != SB_MEM_CHAIN_HASH_LEN) return;
    memcpy(ctx->prev_hash, p, SB_MEM_CHAIN_HASH_LEN);
    ctx->prev_hash[SB_MEM_CHAIN_HASH_LEN] = '\0';
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int sb_mem_init(sb_mem_ctx_t **ctx_out,
                sb_identity_ctx_t *id_ctx,
                const char *data_dir)
{
    if (!ctx_out || !id_ctx) return SB_ERR_INVALID_ARG;
    if (sodium_init() < 0) return SB_ERR_CRYPTO;

    sb_mem_ctx_t *ctx = calloc(1, sizeof(sb_mem_ctx_t));
    if (!ctx) return SB_ERR_MEM_IO;

    ctx->id_ctx = id_ctx;
    sb_identity_get_nodeid(id_ctx, ctx->nodeid, sizeof(ctx->nodeid));
    pthread_mutex_init(&ctx->lock, NULL);

    /* Determine storage directory */
    if (data_dir) {
        strncpy(ctx->data_dir, data_dir, sizeof(ctx->data_dir) - 1);
    } else {
        /* Try /data/cml first (production), fall back to ~/.sorabbyngo/cml */
        struct stat st;
        if (stat("/data", &st) == 0 && S_ISDIR(st.st_mode)) {
            strncpy(ctx->data_dir, SB_MEM_DATA_DIR, sizeof(ctx->data_dir) - 1);
        } else {
            const char *home = getenv("HOME");
            if (!home) home = "/tmp";
            snprintf(ctx->data_dir, sizeof(ctx->data_dir),
                     "%s/%s", home, SB_MEM_FALLBACK_DIR);
        }
    }

    if (mkdir_p(ctx->data_dir) != 0 && errno != EEXIST) {
        free(ctx);
        return SB_ERR_MEM_IO;
    }

    snprintf(ctx->audit_path, sizeof(ctx->audit_path),
             "%s/%s", ctx->data_dir, SB_MEM_AUDIT_FILE);
    snprintf(ctx->store_path, sizeof(ctx->store_path),
             "%s/%s", ctx->data_dir, SB_MEM_STORE_FILE);

    resume_chain(ctx);

    *ctx_out = ctx;
    return SB_OK;
}

void sb_mem_free(sb_mem_ctx_t *ctx)
{
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
}

/* ── Audit ledger write ─────────────────────────────────────────────────── */

int sb_mem_audit_write(sb_mem_ctx_t *ctx,
                       const char *event_type,
                       const char *data_json,
                       uint32_t pii_flags,
                       char chain_hash_out[SB_MEM_CHAIN_HASH_LEN + 1])
{
    if (!ctx || !event_type || !data_json) return SB_ERR_INVALID_ARG;

    pthread_mutex_lock(&ctx->lock);

    /* PII scrub a copy of data_json */
    char data_scrubbed[SB_MEM_MAX_EVENT_LEN];
    strncpy(data_scrubbed, data_json, sizeof(data_scrubbed) - 1);
    data_scrubbed[sizeof(data_scrubbed) - 1] = '\0';
    if (pii_flags) sb_mem_scrub_pii(data_scrubbed, sizeof(data_scrubbed), pii_flags);

    /* Timestamp */
    char ts[40];
    get_iso8601(ts, sizeof(ts));

    /* Kernel audit tail */
    char kernel_hash[SB_MEM_CHAIN_HASH_LEN + 4] = "null";
    char kh[SB_MEM_CHAIN_HASH_LEN + 2];
    if (read_kernel_audit_tail(kh, sizeof(kh)) == 0)
        snprintf(kernel_hash, sizeof(kernel_hash), "\"%s\"", kh);

    /* Build entry JSON (sort_keys=True order: data, event_type, kernel_hash,
     * node_id, prev_hash, timestamp — alphabetical, mirrors Python) */
    char entry_json[SB_MEM_MAX_EVENT_LEN + 512];
    snprintf(entry_json, sizeof(entry_json),
        "{"
        "\"data\":%s,"
        "\"event_type\":\"%s\","
        "\"kernel_hash\":%s,"
        "\"node_id\":\"%s\","
        "\"prev_hash\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        data_scrubbed,
        event_type,
        kernel_hash,
        ctx->nodeid,
        ctx->prev_hash,
        ts);

    /* Compute chain hash */
    char chain_hash[SB_MEM_CHAIN_HASH_LEN + 1];
    compute_chain_hash(ctx->prev_hash, entry_json, chain_hash);

    /* Append chain_hash to entry and write to ledger */
    FILE *f = fopen(ctx->audit_path, "a");
    if (!f) {
        pthread_mutex_unlock(&ctx->lock);
        return SB_ERR_MEM_IO;
    }

    /* Final JSON with chain_hash appended */
    char final_json[SB_MEM_MAX_EVENT_LEN + 600];
    snprintf(final_json, sizeof(final_json),
        "{"
        "\"data\":%s,"
        "\"event_type\":\"%s\","
        "\"kernel_hash\":%s,"
        "\"node_id\":\"%s\","
        "\"prev_hash\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"chain_hash\":\"%s\""
        "}\n",
        data_scrubbed, event_type, kernel_hash,
        ctx->nodeid, ctx->prev_hash, ts, chain_hash);

    fputs(final_json, f);
    fclose(f);

    strncpy(ctx->prev_hash, chain_hash, SB_MEM_CHAIN_HASH_LEN);
    if (chain_hash_out)
        strncpy(chain_hash_out, chain_hash, SB_MEM_CHAIN_HASH_LEN + 1);

    pthread_mutex_unlock(&ctx->lock);
    return SB_OK;
}

/* ── Audit chain verification ───────────────────────────────────────────── */

int sb_mem_audit_verify(sb_mem_ctx_t *ctx, int *entry_count)
{
    if (!ctx) return SB_ERR_INVALID_ARG;

    FILE *f = fopen(ctx->audit_path, "r");
    if (!f) {
        if (entry_count) *entry_count = 0;
        return SB_OK; /* empty ledger is valid */
    }

    char prev[SB_MEM_CHAIN_HASH_LEN + 1];
    strncpy(prev, "0000000000000000000000000000000000000000"
                  "000000000000000000000000", SB_MEM_CHAIN_HASH_LEN);
    prev[SB_MEM_CHAIN_HASH_LEN] = '\0';

    char line[SB_MEM_MAX_EVENT_LEN + 600];
    int  count = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (!line[0]) continue;

        /* Extract stored chain_hash */
        const char *ch_key = "\"chain_hash\":\"";
        const char *ch_p   = strstr(line, ch_key);
        if (!ch_p) { fclose(f); return SB_ERR_MEM_CHAIN_BROKEN; }
        ch_p += strlen(ch_key);
        const char *ch_end = strchr(ch_p, '"');
        if (!ch_end || (size_t)(ch_end - ch_p) != SB_MEM_CHAIN_HASH_LEN) {
            fclose(f);
            return SB_ERR_MEM_CHAIN_BROKEN;
        }
        char stored_hash[SB_MEM_CHAIN_HASH_LEN + 1];
        memcpy(stored_hash, ch_p, SB_MEM_CHAIN_HASH_LEN);
        stored_hash[SB_MEM_CHAIN_HASH_LEN] = '\0';

        /* Reconstruct entry without chain_hash field for verification.
         * Find the ",\"chain_hash\":\"...\"}" suffix and strip it. */
        char entry_without_ch[SB_MEM_MAX_EVENT_LEN + 600];
        strncpy(entry_without_ch, line, sizeof(entry_without_ch) - 1);
        /* Remove the chain_hash field: find last occurrence */
        char *strip = strstr(entry_without_ch, ",\"chain_hash\":\"");
        if (!strip) { fclose(f); return SB_ERR_MEM_CHAIN_BROKEN; }
        /* Close the JSON object at the comma */
        *strip = '}';
        *(strip + 1) = '\0';

        char expected[SB_MEM_CHAIN_HASH_LEN + 1];
        compute_chain_hash(prev, entry_without_ch, expected);

        if (strcmp(expected, stored_hash) != 0) {
            fclose(f);
            if (entry_count) *entry_count = count;
            return SB_ERR_MEM_CHAIN_BROKEN;
        }

        strncpy(prev, stored_hash, SB_MEM_CHAIN_HASH_LEN);
        count++;
    }

    fclose(f);
    if (entry_count) *entry_count = count;
    return SB_OK;
}

/* ── Audit tail ─────────────────────────────────────────────────────────── */

int sb_mem_audit_tail(sb_mem_ctx_t *ctx, int n,
                      sb_mem_entry_t ***entries_out, int *count_out)
{
    if (!ctx || !entries_out || !count_out) return SB_ERR_INVALID_ARG;
    if (n <= 0) n = 20;

    *entries_out = NULL;
    *count_out   = 0;

    FILE *f = fopen(ctx->audit_path, "r");
    if (!f) return SB_OK;

    /* Collect all lines (ring buffer of last n) */
    char **lines = calloc((size_t)n, sizeof(char *));
    if (!lines) { fclose(f); return SB_ERR_MEM_IO; }

    int   total = 0;
    char  buf[SB_MEM_MAX_EVENT_LEN + 600];

    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = '\0';
        if (!buf[0]) continue;
        int slot = total % n;
        free(lines[slot]);
        lines[slot] = strdup(buf);
        total++;
    }
    fclose(f);

    int actual = total < n ? total : n;
    sb_mem_entry_t **entries = calloc((size_t)actual, sizeof(sb_mem_entry_t *));
    if (!entries) {
        for (int i = 0; i < n; i++) free(lines[i]);
        free(lines);
        return SB_ERR_MEM_IO;
    }

    /* Reorder from ring buffer */
    int start = total < n ? 0 : total % n;
    for (int i = 0; i < actual; i++) {
        int slot = (start + i) % n;
        if (!lines[slot]) continue;

        sb_mem_entry_t *e = calloc(1, sizeof(sb_mem_entry_t));
        if (!e) continue;

        /* Parse key fields */
        const char *line = lines[slot];

        auto void extract(const char *key, char *out, size_t out_len);
        void extract(const char *key, char *out, size_t out_len) {
            char search[128];
            snprintf(search, sizeof(search), "\"%s\":\"", key);
            const char *p = strstr(line, search);
            if (!p) return;
            p += strlen(search);
            const char *end = strchr(p, '"');
            if (!end) return;
            size_t len = (size_t)(end - p);
            if (len >= out_len) len = out_len - 1;
            memcpy(out, p, len);
            out[len] = '\0';
        }

        extract("timestamp",   e->timestamp,   sizeof(e->timestamp));
        extract("event_type",  e->event_type,  sizeof(e->event_type));
        extract("node_id",     e->node_id,     sizeof(e->node_id));
        extract("prev_hash",   e->prev_hash,   sizeof(e->prev_hash));
        extract("chain_hash",  e->chain_hash,  sizeof(e->chain_hash));
        extract("kernel_hash", e->kernel_hash, sizeof(e->kernel_hash));

        /* data field (object, not quoted string) */
        const char *dp = strstr(line, "\"data\":");
        if (dp) {
            dp += 7;
            strncpy(e->data, dp, sizeof(e->data) - 1);
        }

        entries[i] = e;
    }

    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);

    *entries_out = entries;
    *count_out   = actual;
    return SB_OK;
}

void sb_mem_entries_free(sb_mem_entry_t **entries, int count)
{
    if (!entries) return;
    for (int i = 0; i < count; i++) free(entries[i]);
    free(entries);
}

/* ── Key-value store ────────────────────────────────────────────────────── */

int sb_mem_store(sb_mem_ctx_t *ctx,
                 const char *key,
                 const uint8_t *value,
                 size_t value_len,
                 uint32_t pii_flags)
{
    if (!ctx || !key || !value) return SB_ERR_INVALID_ARG;
    if (strlen(key) > SB_MEM_MAX_KEY_LEN) return SB_ERR_INVALID_ARG;
    if (value_len > SB_MEM_MAX_VALUE_LEN) return SB_ERR_MEM_VALUE_TOO_BIG;

    pthread_mutex_lock(&ctx->lock);

    /* PII scrub value copy */
    char val_str[SB_MEM_MAX_VALUE_LEN + 2];
    memcpy(val_str, value, value_len);
    val_str[value_len] = '\0';
    int scrubbed = 0;
    if (pii_flags) scrubbed = sb_mem_scrub_pii(val_str, sizeof(val_str), pii_flags);

    /* Timestamp */
    char ts[40];
    get_iso8601(ts, sizeof(ts));

    /* Write to store.jsonl */
    FILE *f = fopen(ctx->store_path, "a");
    if (!f) { pthread_mutex_unlock(&ctx->lock); return SB_ERR_MEM_IO; }

    fprintf(f, "{\"op\":\"set\",\"key\":\"%s\",\"value\":%s,\"ts\":\"%s\",\"node\":\"%s\"}\n",
            key, val_str, ts, ctx->nodeid);
    fclose(f);

    pthread_mutex_unlock(&ctx->lock);

    /* Also write to audit ledger */
    char audit_data[SB_MEM_MAX_KEY_LEN + 64];
    snprintf(audit_data, sizeof(audit_data),
             "{\"key\":\"%s\",\"value_len\":%zu}", key, value_len);
    sb_mem_audit_write(ctx, SB_MEM_EVENT_RECORD_WRITE,
                       audit_data, SB_MEM_PII_NONE, NULL);

    return scrubbed ? SB_ERR_MEM_PII_SCRUBBED : SB_OK;
}

int sb_mem_retrieve(sb_mem_ctx_t *ctx,
                    const char *key,
                    uint8_t **value_out,
                    size_t *value_len_out)
{
    if (!ctx || !key || !value_out || !value_len_out) return SB_ERR_INVALID_ARG;

    FILE *f = fopen(ctx->store_path, "r");
    if (!f) return SB_ERR_MEM_KEY_NOT_FOUND;

    char line[SB_MEM_MAX_VALUE_LEN + 512];
    char last_value[SB_MEM_MAX_VALUE_LEN + 2] = {0};
    int  found = 0;
    int  deleted = 0;

    char key_search[SB_MEM_MAX_KEY_LEN + 16];
    snprintf(key_search, sizeof(key_search), "\"key\":\"%s\"", key);

    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, key_search)) continue;

        if (strstr(line, "\"op\":\"delete\"")) {
            deleted = 1;
            found   = 1;
            last_value[0] = '\0';
            continue;
        }

        /* Extract value */
        const char *vp = strstr(line, "\"value\":");
        if (!vp) continue;
        vp += 8;

        /* Find end of value (next ,"ts": marker) */
        const char *vend = strstr(vp, ",\"ts\":");
        if (!vend) continue;

        size_t vlen = (size_t)(vend - vp);
        if (vlen >= sizeof(last_value)) vlen = sizeof(last_value) - 1;
        memcpy(last_value, vp, vlen);
        last_value[vlen] = '\0';
        deleted = 0;
        found   = 1;
    }
    fclose(f);

    if (!found || deleted) return SB_ERR_MEM_KEY_NOT_FOUND;

    /* Log read to audit ledger */
    char audit_data[SB_MEM_MAX_KEY_LEN + 32];
    snprintf(audit_data, sizeof(audit_data), "{\"key\":\"%s\"}", key);
    sb_mem_audit_write(ctx, SB_MEM_EVENT_RECORD_READ,
                       audit_data, SB_MEM_PII_NONE, NULL);

    size_t vlen = strlen(last_value);
    *value_out     = (uint8_t *)strndup(last_value, vlen);
    *value_len_out = vlen;
    return SB_OK;
}

int sb_mem_delete(sb_mem_ctx_t *ctx, const char *key)
{
    if (!ctx || !key) return SB_ERR_INVALID_ARG;

    pthread_mutex_lock(&ctx->lock);

    char ts[40];
    get_iso8601(ts, sizeof(ts));

    FILE *f = fopen(ctx->store_path, "a");
    if (!f) { pthread_mutex_unlock(&ctx->lock); return SB_ERR_MEM_IO; }

    fprintf(f, "{\"op\":\"delete\",\"key\":\"%s\",\"ts\":\"%s\",\"node\":\"%s\"}\n",
            key, ts, ctx->nodeid);
    fclose(f);

    pthread_mutex_unlock(&ctx->lock);

    char audit_data[SB_MEM_MAX_KEY_LEN + 32];
    snprintf(audit_data, sizeof(audit_data), "{\"key\":\"%s\"}", key);
    sb_mem_audit_write(ctx, SB_MEM_EVENT_RECORD_DELETE,
                       audit_data, SB_MEM_PII_NONE, NULL);

    return SB_OK;
}

/* ── Utilities ──────────────────────────────────────────────────────────── */

const char *sb_mem_get_ledger_path(const sb_mem_ctx_t *ctx)
{
    return ctx ? ctx->audit_path : NULL;
}

const char *sb_mem_strerror(int err)
{
    switch (err) {
        case SB_OK:                    return "OK";
        case SB_ERR_MEM_IO:            return "Memory I/O error";
        case SB_ERR_MEM_CHAIN_BROKEN:  return "Audit chain integrity failure";
        case SB_ERR_MEM_KEY_NOT_FOUND: return "Key not found in store";
        case SB_ERR_MEM_VALUE_TOO_BIG: return "Value exceeds maximum size";
        case SB_ERR_MEM_PII_SCRUBBED:  return "Value stored (PII scrubbed)";
        default:                       return "Unknown memory error";
    }
}
