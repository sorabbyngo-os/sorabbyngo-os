/**
 * libsorabbyngo/ai.h
 * Sorabbyngo OS — AI Inference Module (libsb_ai)
 *
 * Tiered inference router:
 *   Tier 1 — Local CPU  (Ollama, phi4-mini / default model)
 *   Tier 2 — Local GPU  (Ollama, larger models, VRAM check)
 *   Tier 3 — Cloud      (DISABLED by default, ENABLE_CLOUD_AI=true opt-in)
 *
 * ENABLE_CLOUD_AI=false is enforced here at the library level.
 * No application code can bypass it — cloud calls return
 * SB_ERR_AI_CLOUD_DISABLED regardless of what the caller requests.
 *
 * Every inference request is tagged with this node's NodeID for
 * audit logging via CML.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#ifndef LIBSORABBYNGO_AI_H
#define LIBSORABBYNGO_AI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "libsorabbyngo/identity.h"

/* ── Tier definitions ───────────────────────────────────────────────────── */

typedef enum {
    SB_AI_TIER_AUTO  = 0,   /* router picks best available tier (default) */
    SB_AI_TIER_1     = 1,   /* local CPU — Ollama, phi4-mini              */
    SB_AI_TIER_2     = 2,   /* local GPU — Ollama, larger model           */
    SB_AI_TIER_3     = 3,   /* cloud opt-in — ENABLE_CLOUD_AI=true only   */
} sb_ai_tier_t;

/* ── Model names (mirrors chameha_firstboot.py + ADR-007 stack) ─────────── */

#define SB_AI_MODEL_PHI4_MINI     "phi4-mini"
#define SB_AI_MODEL_PHI4          "phi4"
#define SB_AI_MODEL_GEMMA3        "gemma3:4b"
#define SB_AI_MODEL_AYA23         "aya:8b"
#define SB_AI_MODEL_MEDITRON      "meditron:7b"
#define SB_AI_MODEL_DEEPSEEK_R1   "deepseek-r1:7b"
#define SB_AI_MODEL_DEFAULT       SB_AI_MODEL_PHI4_MINI

/* ── Ollama endpoints (mirrors chameha_firstboot.py) ────────────────────── */

#define SB_AI_OLLAMA_URL          "http://localhost:11434/api/generate"
#define SB_AI_OLLAMA_HEALTH_URL   "http://localhost:11434/api/tags"
#define SB_AI_OLLAMA_DEFAULT_PORT 11434

/* ── Return codes ───────────────────────────────────────────────────────── */

#define SB_ERR_AI_CLOUD_DISABLED  -20   /* cloud tier blocked by policy     */
#define SB_ERR_AI_TIER_UNAVAIL    -21   /* requested tier not reachable     */
#define SB_ERR_AI_MODEL_UNAVAIL   -22   /* model not loaded in Ollama       */
#define SB_ERR_AI_INFERENCE       -23   /* inference returned error         */
#define SB_ERR_AI_TIMEOUT         -24   /* inference timed out              */
#define SB_ERR_AI_RESPONSE_TOO_BIG -25  /* response exceeded max size       */

/* ── Inference options ──────────────────────────────────────────────────── */

#define SB_AI_DEFAULT_TIMEOUT_MS   60000   /* 60s — mirrors chameha timeout */
#define SB_AI_DEFAULT_MAX_TOKENS   512
#define SB_AI_DEFAULT_TEMPERATURE  70      /* 0.70 * 100, avoids float      */

typedef struct {
    sb_ai_tier_t  tier;           /* SB_AI_TIER_AUTO by default             */
    const char   *model;          /* NULL = use tier default                */
    const char   *system_prompt;  /* NULL = use Chameha system prompt       */
    int           max_tokens;     /* 0 = SB_AI_DEFAULT_MAX_TOKENS           */
    int           temperature_pct;/* 0 = SB_AI_DEFAULT_TEMPERATURE          */
    int           timeout_ms;     /* 0 = SB_AI_DEFAULT_TIMEOUT_MS           */
    int           stream;         /* 1 = stream to stdout (like Chameha)    */
} sb_ai_opts_t;

/* Default options initialiser — use this before customising */
#define SB_AI_OPTS_DEFAULT { \
    .tier            = SB_AI_TIER_AUTO, \
    .model           = NULL, \
    .system_prompt   = NULL, \
    .max_tokens      = SB_AI_DEFAULT_MAX_TOKENS, \
    .temperature_pct = SB_AI_DEFAULT_TEMPERATURE, \
    .timeout_ms      = SB_AI_DEFAULT_TIMEOUT_MS, \
    .stream          = 0, \
}

/* ── Inference result ───────────────────────────────────────────────────── */

typedef struct {
    char        *text;          /* null-terminated response text (heap)    */
    size_t       text_len;      /* length of text in bytes                  */
    sb_ai_tier_t tier_used;     /* which tier actually served the request   */
    char         model_used[64];/* model that served the request            */
    int          tokens_used;   /* token count (from Ollama eval_count)     */
    long         latency_ms;    /* wall-clock time for inference            */
    char         nodeid[SB_NODEID_LEN + 1]; /* NodeID of serving node      */
} sb_ai_result_t;

/* ── AI context ─────────────────────────────────────────────────────────── */

typedef struct sb_ai_ctx sb_ai_ctx_t;

/* ── Tier health ────────────────────────────────────────────────────────── */

typedef struct {
    int tier1_available;    /* Ollama reachable and has Tier 1 model        */
    int tier2_available;    /* Ollama reachable and has Tier 2 model        */
    int tier3_available;    /* cloud enabled AND reachable                  */
    int cloud_enabled;      /* ENABLE_CLOUD_AI env var                      */
    char tier1_model[64];
    char tier2_model[64];
} sb_ai_health_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * sb_ai_init — create an AI inference context.
 *
 * Reads ENABLE_CLOUD_AI environment variable at init time.
 * Cloud remains disabled unless explicitly set to "true".
 *
 * @param ctx_out    output: allocated context
 * @param id_ctx     identity context (borrowed — caller owns)
 * @param ollama_url NULL = "http://localhost:11434" (default)
 * @return SB_OK or negative error code
 */
int sb_ai_init(sb_ai_ctx_t **ctx_out,
               sb_identity_ctx_t *id_ctx,
               const char *ollama_url);

/**
 * sb_ai_free — release all resources.
 */
void sb_ai_free(sb_ai_ctx_t *ctx);

/* ── Health check ───────────────────────────────────────────────────────── */

/**
 * sb_ai_health — check which tiers are available right now.
 * Fast check (< 2s). Does not perform inference.
 */
int sb_ai_health(sb_ai_ctx_t *ctx, sb_ai_health_t *health_out);

/* ── Inference ──────────────────────────────────────────────────────────── */

/**
 * sb_ai_infer — run inference with automatic tier routing.
 *
 * Tier routing (SB_AI_TIER_AUTO):
 *   1. Try Tier 1 (local CPU)
 *   2. If Tier 1 unavailable, try Tier 2 (local GPU)
 *   3. Tier 3 only if ENABLE_CLOUD_AI=true AND explicitly requested
 *
 * Result must be freed with sb_ai_result_free().
 *
 * @param ctx     AI context
 * @param prompt  user prompt string
 * @param opts    inference options (NULL = defaults)
 * @param result  output: allocated result struct
 * @return SB_OK or negative error code
 */
int sb_ai_infer(sb_ai_ctx_t *ctx,
                const char *prompt,
                const sb_ai_opts_t *opts,
                sb_ai_result_t **result);

/**
 * sb_ai_infer_async — non-blocking inference.
 *
 * Spawns a background thread. Callback is called with the result
 * when inference completes or fails. Caller must free result in callback.
 *
 * @param callback  called with (result, userdata) — result may be NULL on error
 */
typedef void (*sb_ai_callback_t)(sb_ai_result_t *result, int err, void *userdata);

int sb_ai_infer_async(sb_ai_ctx_t *ctx,
                      const char *prompt,
                      const sb_ai_opts_t *opts,
                      sb_ai_callback_t callback,
                      void *userdata);

/* ── Tier policy ────────────────────────────────────────────────────────── */

/**
 * sb_ai_set_tier_policy — override the tier routing policy at runtime.
 * This does NOT override ENABLE_CLOUD_AI=false — cloud remains blocked
 * regardless of what tier is requested.
 *
 * @param max_tier  maximum tier allowed (e.g. SB_AI_TIER_2 blocks cloud)
 */
int sb_ai_set_tier_policy(sb_ai_ctx_t *ctx, sb_ai_tier_t max_tier);

/* ── Result utilities ───────────────────────────────────────────────────── */

/**
 * sb_ai_result_free — release a result returned by sb_ai_infer.
 */
void sb_ai_result_free(sb_ai_result_t *result);

/**
 * sb_ai_strerror — human-readable error string.
 */
const char *sb_ai_strerror(int err);

/**
 * sb_ai_tier_name — human-readable tier name.
 */
const char *sb_ai_tier_name(sb_ai_tier_t tier);

#ifdef __cplusplus
}
#endif

#endif /* LIBSORABBYNGO_AI_H */
