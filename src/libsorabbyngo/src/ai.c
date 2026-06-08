/**
 * ai.c — libsb_ai implementation
 *
 * HTTP calls to Ollama via libcurl (same endpoints as chameha_firstboot.py).
 * Tier routing: AUTO → Tier1 → Tier2 → Tier3 (cloud blocked by default).
 * ENABLE_CLOUD_AI=false enforced at context init — immutable for context lifetime.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/ai.h"
#include "libsorabbyngo/identity.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/* ── Chameha system prompt (mirrors chameha_firstboot.py) ───────────────── */

static const char *DEFAULT_SYSTEM_PROMPT =
    "You are Chameha, the AI core of SORABBYNGO OS — Africa's sovereign AI "
    "operating system. Warm, direct, highly capable. You speak English and "
    "Swahili fluently. You know this system deeply: kernel, security, mesh "
    "networking, health platform. You never diagnose medical conditions. "
    "Keep responses concise — this is a terminal. "
    "Greeting: 'Sorabbyngo — Rhythm of Digital Life.'";

/* ── Dynamic string buffer for curl response ────────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} sb_strbuf_t;

static int strbuf_init(sb_strbuf_t *b, size_t initial)
{
    b->data = malloc(initial);
    if (!b->data) return -1;
    b->data[0] = '\0';
    b->len = 0;
    b->cap = initial;
    return 0;
}

static int strbuf_append(sb_strbuf_t *b, const char *src, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t new_cap = (b->len + n + 1) * 2;
        char *new_data = realloc(b->data, new_cap);
        if (!new_data) return -1;
        b->data = new_data;
        b->cap  = new_cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static void strbuf_free(sb_strbuf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ── Streaming state (for stream=1 mode) ────────────────────────────────── */

typedef struct {
    sb_strbuf_t  full_text;   /* accumulated response text */
    int          stream;      /* write tokens to stdout as they arrive */
    int          done;
    int          tokens;
} stream_state_t;

/* ── curl write callback ────────────────────────────────────────────────── */

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    stream_state_t *state = (stream_state_t *)userdata;
    size_t bytes = size * nmemb;

    /* Ollama streams one JSON object per line */
    /* ptr may contain multiple lines — process line by line */
    char *buf = malloc(bytes + 1);
    if (!buf) return 0;
    memcpy(buf, ptr, bytes);
    buf[bytes] = '\0';

    char *line = buf;
    char *nl;
    while ((nl = strchr(line, '\n')) || *line) {
        if (nl) *nl = '\0';

        /* Skip empty lines */
        if (*line == '\0') {
            if (nl) { line = nl + 1; continue; }
            else break;
        }

        /* Minimal JSON field extraction — same approach as identity.c */
        /* Extract "response":"..." */
        const char *resp_key = "\"response\":\"";
        const char *rp = strstr(line, resp_key);
        if (rp) {
            rp += strlen(resp_key);
            /* Find closing quote, handling escape sequences */
            const char *end = rp;
            while (*end && !(*end == '"' && *(end-1) != '\\')) end++;
            size_t token_len = (size_t)(end - rp);
            if (token_len > 0) {
                strbuf_append(&state->full_text, rp, token_len);
                if (state->stream) {
                    fwrite(rp, 1, token_len, stdout);
                    fflush(stdout);
                }
            }
        }

        /* Check "done":true */
        if (strstr(line, "\"done\":true")) {
            state->done = 1;
            /* Extract eval_count for token count */
            const char *ec = strstr(line, "\"eval_count\":");
            if (ec) state->tokens = atoi(ec + 13);
        }

        if (nl) line = nl + 1;
        else break;
    }

    free(buf);
    return bytes;
}

/* ── Internal context ───────────────────────────────────────────────────── */

struct sb_ai_ctx {
    sb_identity_ctx_t *id_ctx;
    char               ollama_base[256];
    char               ollama_gen_url[320];
    char               ollama_health_url[320];
    char               nodeid[SB_NODEID_LEN + 1];
    int                cloud_enabled;    /* immutable after init */
    sb_ai_tier_t       max_tier;         /* policy ceiling       */
    CURL              *curl_handle;
    pthread_mutex_t    lock;
};

/* ── Ollama health check ────────────────────────────────────────────────── */

static int ollama_is_alive(sb_ai_ctx_t *ctx)
{
    CURL *c = curl_easy_init();
    if (!c) return 0;

    long   http_code = 0;
    curl_easy_setopt(c, CURLOPT_URL, ctx->ollama_health_url);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);          /* HEAD request */
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);

    /* Dummy write state — we just need the HTTP status */
    stream_state_t dummy;
    strbuf_init(&dummy.full_text, 16);
    dummy.stream = 0; dummy.done = 0; dummy.tokens = 0;
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &dummy);

    CURLcode res = curl_easy_perform(c);
    if (res == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(c);
    strbuf_free(&dummy.full_text);
    return (http_code == 200);
}

static int ollama_has_model(sb_ai_ctx_t *ctx, const char *model)
{
    CURL *c = curl_easy_init();
    if (!c) return 0;

    sb_strbuf_t body;
    strbuf_init(&body, 4096);

    stream_state_t state;
    strbuf_init(&state.full_text, 4096);
    state.stream = 0; state.done = 0; state.tokens = 0;

    curl_easy_setopt(c, CURLOPT_URL, ctx->ollama_health_url);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &state);

    curl_easy_perform(c);
    curl_easy_cleanup(c);

    /* Check if model name appears in the tags response */
    int found = (strstr(state.full_text.data, model) != NULL);
    strbuf_free(&state.full_text);
    strbuf_free(&body);
    return found;
}

/* ── Build Ollama request JSON ──────────────────────────────────────────── */

static char *build_ollama_json(const char *prompt,
                                const char *model,
                                const char *system_prompt,
                                int max_tokens,
                                int temperature_pct)
{
    /* Escape quotes in prompt and system_prompt */
    size_t json_size = strlen(prompt) * 2
                     + strlen(system_prompt) * 2
                     + strlen(model)
                     + 256;
    char *json = malloc(json_size);
    if (!json) return NULL;

    snprintf(json, json_size,
        "{"
        "\"model\":\"%s\","
        "\"prompt\":\"%s\\n\\nUser: %s\\nChameha:\","
        "\"stream\":true,"
        "\"options\":{"
            "\"temperature\":%.2f,"
            "\"num_predict\":%d"
        "}"
        "}",
        model,
        system_prompt,
        prompt,
        (double)temperature_pct / 100.0,
        max_tokens);

    return json;
}

/* ── Core inference call ────────────────────────────────────────────────── */

static int do_inference(sb_ai_ctx_t *ctx,
                         const char *prompt,
                         const char *model,
                         const char *system_prompt,
                         int max_tokens,
                         int temperature_pct,
                         int timeout_ms,
                         int stream,
                         sb_ai_result_t *result)
{
    char *json_body = build_ollama_json(prompt, model, system_prompt,
                                         max_tokens, temperature_pct);
    if (!json_body) return SB_ERR_IO;

    stream_state_t state;
    if (strbuf_init(&state.full_text, 4096) != 0) {
        free(json_body);
        return SB_ERR_IO;
    }
    state.stream = stream;
    state.done   = 0;
    state.tokens = 0;

    CURL *c = curl_easy_init();
    if (!c) {
        strbuf_free(&state.full_text);
        free(json_body);
        return SB_ERR_IO;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL,           ctx->ollama_gen_url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,    json_body);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     &state);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS,    (long)(timeout_ms > 0
                                                       ? timeout_ms
                                                       : SB_AI_DEFAULT_TIMEOUT_MS));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    CURLcode res = curl_easy_perform(c);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    long latency_ms = (long)((t1.tv_sec - t0.tv_sec) * 1000
                     + (t1.tv_nsec - t0.tv_nsec) / 1000000);

    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    free(json_body);

    if (res == CURLE_OPERATION_TIMEDOUT) {
        strbuf_free(&state.full_text);
        return SB_ERR_AI_TIMEOUT;
    }
    if (res != CURLE_OK || http_code != 200) {
        strbuf_free(&state.full_text);
        return SB_ERR_AI_INFERENCE;
    }

    if (stream) printf("\n"); /* newline after streamed output */

    /* Populate result */
    result->text      = state.full_text.data; /* transfer ownership */
    result->text_len  = state.full_text.len;
    result->tokens_used = state.tokens;
    result->latency_ms  = latency_ms;
    strncpy(result->model_used, model, sizeof(result->model_used) - 1);
    strncpy(result->nodeid, ctx->nodeid, SB_NODEID_LEN);

    /* Don't free state.full_text.data — transferred to result->text */
    return SB_OK;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int sb_ai_init(sb_ai_ctx_t **ctx_out,
               sb_identity_ctx_t *id_ctx,
               const char *ollama_url)
{
    if (!ctx_out || !id_ctx) return SB_ERR_INVALID_ARG;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return SB_ERR_IO;

    sb_ai_ctx_t *ctx = calloc(1, sizeof(sb_ai_ctx_t));
    if (!ctx) return SB_ERR_IO;

    ctx->id_ctx   = id_ctx;
    ctx->max_tier = SB_AI_TIER_3; /* policy ceiling — all tiers by default */

    /* ENABLE_CLOUD_AI enforcement — read once, immutable */
    const char *cloud_env = getenv("ENABLE_CLOUD_AI");
    ctx->cloud_enabled = (cloud_env && strcmp(cloud_env, "true") == 0) ? 1 : 0;

    /* Build URLs */
    const char *base = ollama_url ? ollama_url : "http://localhost:11434";
    strncpy(ctx->ollama_base, base, sizeof(ctx->ollama_base) - 1);
    snprintf(ctx->ollama_gen_url, sizeof(ctx->ollama_gen_url),
             "%s/api/generate", base);
    snprintf(ctx->ollama_health_url, sizeof(ctx->ollama_health_url),
             "%s/api/tags", base);

    /* Cache NodeID */
    sb_identity_get_nodeid(id_ctx, ctx->nodeid, sizeof(ctx->nodeid));

    pthread_mutex_init(&ctx->lock, NULL);

    *ctx_out = ctx;
    return SB_OK;
}

void sb_ai_free(sb_ai_ctx_t *ctx)
{
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    curl_global_cleanup();
}

int sb_ai_health(sb_ai_ctx_t *ctx, sb_ai_health_t *h)
{
    if (!ctx || !h) return SB_ERR_INVALID_ARG;
    memset(h, 0, sizeof(*h));

    h->cloud_enabled = ctx->cloud_enabled;
    strncpy(h->tier1_model, SB_AI_MODEL_PHI4_MINI, sizeof(h->tier1_model) - 1);
    strncpy(h->tier2_model, SB_AI_MODEL_PHI4,      sizeof(h->tier2_model) - 1);

    if (!ollama_is_alive(ctx)) return SB_OK; /* all tiers unavailable */

    h->tier1_available = ollama_has_model(ctx, SB_AI_MODEL_PHI4_MINI);
    h->tier2_available = ollama_has_model(ctx, SB_AI_MODEL_PHI4);
    h->tier3_available = ctx->cloud_enabled; /* cloud: policy check only */

    return SB_OK;
}

int sb_ai_set_tier_policy(sb_ai_ctx_t *ctx, sb_ai_tier_t max_tier)
{
    if (!ctx) return SB_ERR_INVALID_ARG;
    /* Cannot raise ceiling above Tier 2 if cloud is disabled */
    if (max_tier == SB_AI_TIER_3 && !ctx->cloud_enabled)
        return SB_ERR_AI_CLOUD_DISABLED;
    ctx->max_tier = max_tier;
    return SB_OK;
}

int sb_ai_infer(sb_ai_ctx_t *ctx,
                const char *prompt,
                const sb_ai_opts_t *opts,
                sb_ai_result_t **result_out)
{
    if (!ctx || !prompt || !result_out) return SB_ERR_INVALID_ARG;

    /* Apply defaults */
    sb_ai_opts_t o = SB_AI_OPTS_DEFAULT;
    if (opts) o = *opts;
    if (o.max_tokens      <= 0) o.max_tokens      = SB_AI_DEFAULT_MAX_TOKENS;
    if (o.temperature_pct <= 0) o.temperature_pct = SB_AI_DEFAULT_TEMPERATURE;
    if (o.timeout_ms      <= 0) o.timeout_ms      = SB_AI_DEFAULT_TIMEOUT_MS;
    if (!o.system_prompt)       o.system_prompt   = DEFAULT_SYSTEM_PROMPT;

    /* ── ENABLE_CLOUD_AI enforcement ──────────────────────────────────── */
    if (o.tier == SB_AI_TIER_3 && !ctx->cloud_enabled)
        return SB_ERR_AI_CLOUD_DISABLED;
    if (o.tier == SB_AI_TIER_3 && ctx->max_tier < SB_AI_TIER_3)
        return SB_ERR_AI_CLOUD_DISABLED;

    /* Allocate result */
    sb_ai_result_t *result = calloc(1, sizeof(sb_ai_result_t));
    if (!result) return SB_ERR_IO;

    pthread_mutex_lock(&ctx->lock);

    int rc = SB_ERR_AI_TIER_UNAVAIL;

    /* ── Tier routing ─────────────────────────────────────────────────── */
    if (o.tier == SB_AI_TIER_AUTO || o.tier == SB_AI_TIER_1) {
        const char *model = o.model ? o.model : SB_AI_MODEL_PHI4_MINI;
        if (ollama_is_alive(ctx)) {
            result->tier_used = SB_AI_TIER_1;
            rc = do_inference(ctx, prompt, model, o.system_prompt,
                              o.max_tokens, o.temperature_pct,
                              o.timeout_ms, o.stream, result);
            if (rc == SB_OK) goto done;
        }
        /* Fall through to Tier 2 if AUTO */
        if (o.tier == SB_AI_TIER_1) { rc = SB_ERR_AI_TIER_UNAVAIL; goto done; }
    }

    if (o.tier == SB_AI_TIER_AUTO || o.tier == SB_AI_TIER_2) {
        const char *model = o.model ? o.model : SB_AI_MODEL_PHI4;
        if (ollama_is_alive(ctx) && ollama_has_model(ctx, model)) {
            result->tier_used = SB_AI_TIER_2;
            rc = do_inference(ctx, prompt, model, o.system_prompt,
                              o.max_tokens, o.temperature_pct,
                              o.timeout_ms, o.stream, result);
            if (rc == SB_OK) goto done;
        }
        if (o.tier == SB_AI_TIER_2) { rc = SB_ERR_AI_TIER_UNAVAIL; goto done; }
    }

    if (o.tier == SB_AI_TIER_3) {
        /* Cloud tier — double-check policy (defence in depth) */
        if (!ctx->cloud_enabled) {
            rc = SB_ERR_AI_CLOUD_DISABLED;
            goto done;
        }
        /* Cloud inference implementation goes here in Phase 4 */
        rc = SB_ERR_AI_TIER_UNAVAIL;
    }

done:
    pthread_mutex_unlock(&ctx->lock);

    if (rc != SB_OK) {
        free(result);
        return rc;
    }

    *result_out = result;
    return SB_OK;
}

/* ── Async inference ────────────────────────────────────────────────────── */

typedef struct {
    sb_ai_ctx_t      *ctx;
    char             *prompt;
    sb_ai_opts_t      opts;
    sb_ai_callback_t  callback;
    void             *userdata;
} async_args_t;

static void *async_thread(void *arg)
{
    async_args_t   *a   = (async_args_t *)arg;
    sb_ai_result_t *res = NULL;
    int rc = sb_ai_infer(a->ctx, a->prompt, &a->opts, &res);
    a->callback(res, rc, a->userdata);
    free(a->prompt);
    free(a);
    return NULL;
}

int sb_ai_infer_async(sb_ai_ctx_t *ctx,
                      const char *prompt,
                      const sb_ai_opts_t *opts,
                      sb_ai_callback_t callback,
                      void *userdata)
{
    if (!ctx || !prompt || !callback) return SB_ERR_INVALID_ARG;

    async_args_t *a = malloc(sizeof(async_args_t));
    if (!a) return SB_ERR_IO;

    a->ctx      = ctx;
    a->prompt   = strdup(prompt);
    a->opts     = opts ? *opts : (sb_ai_opts_t)SB_AI_OPTS_DEFAULT;
    a->callback = callback;
    a->userdata = userdata;

    pthread_t thread;
    if (pthread_create(&thread, NULL, async_thread, a) != 0) {
        free(a->prompt);
        free(a);
        return SB_ERR_IO;
    }
    pthread_detach(thread);
    return SB_OK;
}

/* ── Result utilities ───────────────────────────────────────────────────── */

void sb_ai_result_free(sb_ai_result_t *result)
{
    if (!result) return;
    free(result->text);
    free(result);
}

const char *sb_ai_strerror(int err)
{
    switch (err) {
        case SB_OK:                    return "OK";
        case SB_ERR_AI_CLOUD_DISABLED: return "Cloud AI disabled (ENABLE_CLOUD_AI=false)";
        case SB_ERR_AI_TIER_UNAVAIL:   return "Requested AI tier unavailable";
        case SB_ERR_AI_MODEL_UNAVAIL:  return "Model not loaded in Ollama";
        case SB_ERR_AI_INFERENCE:      return "Inference failed";
        case SB_ERR_AI_TIMEOUT:        return "Inference timed out";
        case SB_ERR_AI_RESPONSE_TOO_BIG: return "Response too large";
        default:                       return "Unknown AI error";
    }
}

const char *sb_ai_tier_name(sb_ai_tier_t tier)
{
    switch (tier) {
        case SB_AI_TIER_AUTO: return "auto";
        case SB_AI_TIER_1:    return "tier1 (local CPU)";
        case SB_AI_TIER_2:    return "tier2 (local GPU)";
        case SB_AI_TIER_3:    return "tier3 (cloud)";
        default:              return "unknown";
    }
}
