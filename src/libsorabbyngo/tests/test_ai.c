/**
 * test_ai.c — libsb_ai test suite
 *
 * Group A: Unit tests (no Ollama required)
 *   - Init/free lifecycle
 *   - ENABLE_CLOUD_AI=false enforcement
 *   - Tier policy validation
 *   - strerror / tier_name coverage
 *
 * Group B: Integration tests (require Ollama on localhost:11434)
 *   - Health check
 *   - Tier 1 inference roundtrip
 *   - Async inference
 *   Skipped automatically if Ollama is not reachable.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _POSIX_C_SOURCE 200809L
#include "libsorabbyngo/ai.h"
#include "libsorabbyngo/identity.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ── Test framework ─────────────────────────────────────────────────────── */

static int tests_run = 0, tests_passed = 0, tests_failed = 0, tests_skipped = 0;

#define TEST(name) do { tests_run++; \
    printf("  [ TEST %2d ] %s ... ", tests_run, name); fflush(stdout); } while(0)
#define PASS()        do { tests_passed++;  printf("PASS\n"); } while(0)
#define SKIP(r)       do { tests_skipped++; printf("SKIP — %s\n", r); return; } while(0)
#define FAIL(m)       do { tests_failed++;  printf("FAIL — %s\n", m); return; } while(0)
#define ASSERT(c, m)  do { if (!(c)) { FAIL(m); } } while(0)

/* ── Fixtures ───────────────────────────────────────────────────────────── */

static sb_identity_ctx_t *g_id  = NULL;
static sb_ai_ctx_t       *g_ai  = NULL;
static int                g_ollama_ok = 0;

static void setup(void)
{
    sb_identity_init(&g_id, NULL);
    if (sb_ai_init(&g_ai, g_id, NULL) == SB_OK) {
        sb_ai_health_t h;
        sb_ai_health(g_ai, &h);
        g_ollama_ok = h.tier1_available;
    }
}

static void teardown(void)
{
    if (g_ai) sb_ai_free(g_ai);
    if (g_id) sb_identity_free(g_id);
}

/* ══════════════════════════════════════════════════════════════════════════
 * GROUP A — Unit tests
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_init_free(void)
{
    TEST("ai_init and free succeed");
    sb_identity_ctx_t *id = NULL;
    sb_ai_ctx_t       *ai = NULL;
    ASSERT(sb_identity_init(&id, NULL) == SB_OK, "identity init");
    ASSERT(sb_ai_init(&ai, id, NULL) == SB_OK, "ai init");
    sb_ai_free(ai);
    sb_identity_free(id);
    PASS();
}

static void test_init_null_args(void)
{
    TEST("ai_init rejects null arguments");
    ASSERT(sb_ai_init(NULL, g_id, NULL) != SB_OK, "null ctx_out");
    ASSERT(sb_ai_init((sb_ai_ctx_t**)&(void*){NULL}, NULL, NULL) != SB_OK,
           "null id_ctx");
    PASS();
}

static void test_cloud_disabled_by_default(void)
{
    TEST("cloud tier blocked when ENABLE_CLOUD_AI not set");
    /* Ensure env var is not set */
    unsetenv("ENABLE_CLOUD_AI");

    sb_identity_ctx_t *id = NULL;
    sb_ai_ctx_t       *ai = NULL;
    sb_identity_init(&id, NULL);
    sb_ai_init(&ai, id, NULL);

    sb_ai_opts_t opts = SB_AI_OPTS_DEFAULT;
    opts.tier = SB_AI_TIER_3;

    sb_ai_result_t *result = NULL;
    int rc = sb_ai_infer(ai, "hello", &opts, &result);

    ASSERT(rc == SB_ERR_AI_CLOUD_DISABLED, "expected CLOUD_DISABLED");
    ASSERT(result == NULL, "result should be NULL on cloud block");

    sb_ai_free(ai);
    sb_identity_free(id);
    PASS();
}

static void test_cloud_disabled_env_false(void)
{
    TEST("cloud tier blocked when ENABLE_CLOUD_AI=false");
    setenv("ENABLE_CLOUD_AI", "false", 1);

    sb_identity_ctx_t *id = NULL;
    sb_ai_ctx_t       *ai = NULL;
    sb_identity_init(&id, NULL);
    sb_ai_init(&ai, id, NULL);

    sb_ai_opts_t opts = SB_AI_OPTS_DEFAULT;
    opts.tier = SB_AI_TIER_3;
    sb_ai_result_t *result = NULL;
    int rc = sb_ai_infer(ai, "hello", &opts, &result);

    ASSERT(rc == SB_ERR_AI_CLOUD_DISABLED, "expected CLOUD_DISABLED");

    unsetenv("ENABLE_CLOUD_AI");
    sb_ai_free(ai);
    sb_identity_free(id);
    PASS();
}

static void test_tier_policy_blocks_cloud(void)
{
    TEST("set_tier_policy blocks cloud when ENABLE_CLOUD_AI=false");
    unsetenv("ENABLE_CLOUD_AI");

    sb_identity_ctx_t *id = NULL;
    sb_ai_ctx_t       *ai = NULL;
    sb_identity_init(&id, NULL);
    sb_ai_init(&ai, id, NULL);

    int rc = sb_ai_set_tier_policy(ai, SB_AI_TIER_3);
    ASSERT(rc == SB_ERR_AI_CLOUD_DISABLED,
           "policy should not allow tier3 when cloud disabled");

    sb_ai_free(ai);
    sb_identity_free(id);
    PASS();
}

static void test_tier_policy_allows_tier2(void)
{
    TEST("set_tier_policy to tier2 succeeds");
    sb_identity_ctx_t *id = NULL;
    sb_ai_ctx_t       *ai = NULL;
    sb_identity_init(&id, NULL);
    sb_ai_init(&ai, id, NULL);

    ASSERT(sb_ai_set_tier_policy(ai, SB_AI_TIER_2) == SB_OK,
           "tier2 policy should succeed");

    sb_ai_free(ai);
    sb_identity_free(id);
    PASS();
}

static void test_infer_null_args(void)
{
    TEST("infer rejects null arguments");
    sb_ai_result_t *r = NULL;
    ASSERT(sb_ai_infer(NULL, "hello", NULL, &r) != SB_OK, "null ctx");
    ASSERT(sb_ai_infer(g_ai, NULL,    NULL, &r) != SB_OK, "null prompt");
    ASSERT(sb_ai_infer(g_ai, "hello", NULL, NULL) != SB_OK, "null result_out");
    PASS();
}

static void test_strerror_coverage(void)
{
    TEST("sb_ai_strerror returns non-null for all codes");
    ASSERT(sb_ai_strerror(SB_OK)                     != NULL, "SB_OK");
    ASSERT(sb_ai_strerror(SB_ERR_AI_CLOUD_DISABLED)  != NULL, "CLOUD_DISABLED");
    ASSERT(sb_ai_strerror(SB_ERR_AI_TIER_UNAVAIL)    != NULL, "TIER_UNAVAIL");
    ASSERT(sb_ai_strerror(SB_ERR_AI_MODEL_UNAVAIL)   != NULL, "MODEL_UNAVAIL");
    ASSERT(sb_ai_strerror(SB_ERR_AI_INFERENCE)       != NULL, "INFERENCE");
    ASSERT(sb_ai_strerror(SB_ERR_AI_TIMEOUT)         != NULL, "TIMEOUT");
    PASS();
}

static void test_tier_name_coverage(void)
{
    TEST("sb_ai_tier_name returns non-null for all tiers");
    ASSERT(sb_ai_tier_name(SB_AI_TIER_AUTO) != NULL, "AUTO");
    ASSERT(sb_ai_tier_name(SB_AI_TIER_1)    != NULL, "TIER_1");
    ASSERT(sb_ai_tier_name(SB_AI_TIER_2)    != NULL, "TIER_2");
    ASSERT(sb_ai_tier_name(SB_AI_TIER_3)    != NULL, "TIER_3");
    PASS();
}

static void test_result_free_null(void)
{
    TEST("sb_ai_result_free handles NULL gracefully");
    sb_ai_result_free(NULL); /* should not crash */
    PASS();
}

/* ══════════════════════════════════════════════════════════════════════════
 * GROUP B — Integration tests (require Ollama + phi4-mini)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_health_check(void)
{
    TEST("health check reports tier availability");
    sb_ai_health_t h;
    int rc = sb_ai_health(g_ai, &h);
    ASSERT(rc == SB_OK, "health check failed");
    printf("(tier1=%d tier2=%d cloud=%d) ",
           h.tier1_available, h.tier2_available, h.cloud_enabled);
    PASS();
}

static void test_tier1_inference(void)
{
    TEST("Tier 1 inference — phi4-mini responds");
    if (!g_ollama_ok) SKIP("Ollama/phi4-mini not available");

    sb_ai_opts_t opts = SB_AI_OPTS_DEFAULT;
    opts.tier       = SB_AI_TIER_1;
    opts.max_tokens = 32;

    sb_ai_result_t *result = NULL;
    int rc = sb_ai_infer(g_ai, "Say exactly: SORABBYNGO", &opts, &result);

    ASSERT(rc == SB_OK,        sb_ai_strerror(rc));
    ASSERT(result != NULL,     "result is NULL");
    ASSERT(result->text != NULL && result->text_len > 0, "empty response");
    ASSERT(result->tier_used == SB_AI_TIER_1, "wrong tier");
    ASSERT(result->latency_ms > 0, "latency not recorded");

    printf("(%zuB in %ldms) ", result->text_len, result->latency_ms);

    sb_ai_result_free(result);
    PASS();
}

static void test_auto_tier_routing(void)
{
    TEST("AUTO tier routes to Tier 1 when available");
    if (!g_ollama_ok) SKIP("Ollama/phi4-mini not available");

    sb_ai_opts_t opts = SB_AI_OPTS_DEFAULT;
    opts.tier       = SB_AI_TIER_AUTO;
    opts.max_tokens = 16;

    sb_ai_result_t *result = NULL;
    int rc = sb_ai_infer(g_ai, "Reply: OK", &opts, &result);

    ASSERT(rc == SB_OK, sb_ai_strerror(rc));
    ASSERT(result->tier_used == SB_AI_TIER_1, "should have used tier 1");
    printf("(tier=%s) ", sb_ai_tier_name(result->tier_used));

    sb_ai_result_free(result);
    PASS();
}

static volatile int g_async_done = 0;
static volatile int g_async_rc   = -1;

static void async_callback(sb_ai_result_t *result, int err, void *userdata)
{
    (void)userdata;
    g_async_rc   = err;
    g_async_done = 1;
    if (result) sb_ai_result_free(result);
}

static void test_async_inference(void)
{
    TEST("async inference callback fires");
    if (!g_ollama_ok) SKIP("Ollama/phi4-mini not available");

    g_async_done = 0;
    g_async_rc   = -1;

    sb_ai_opts_t opts = SB_AI_OPTS_DEFAULT;
    opts.tier       = SB_AI_TIER_1;
    opts.max_tokens = 16;

    int rc = sb_ai_infer_async(g_ai, "Reply: ASYNC_OK", &opts,
                                async_callback, NULL);
    ASSERT(rc == SB_OK, "infer_async launch failed");

    /* Wait up to 30s for callback */
    int waited = 0;
    while (!g_async_done && waited < 300) {
        usleep(100000); /* 100ms */
        waited++;
    }

    ASSERT(g_async_done, "async callback never fired");
    ASSERT(g_async_rc == SB_OK, sb_ai_strerror(g_async_rc));
    printf("(fired after ~%dms) ", waited * 100);
    PASS();
}

static void test_nodeid_in_result(void)
{
    TEST("result carries this node's NodeID");
    if (!g_ollama_ok) SKIP("Ollama/phi4-mini not available");

    sb_ai_opts_t opts = SB_AI_OPTS_DEFAULT;
    opts.tier       = SB_AI_TIER_1;
    opts.max_tokens = 8;

    sb_ai_result_t *result = NULL;
    ASSERT(sb_ai_infer(g_ai, "Hi", &opts, &result) == SB_OK, "infer");

    char nodeid[SB_NODEID_LEN + 1];
    sb_identity_get_nodeid(g_id, nodeid, sizeof(nodeid));
    ASSERT(strcmp(result->nodeid, nodeid) == 0, "NodeID mismatch in result");

    sb_ai_result_free(result);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║   SORABBYNGO OS — libsb_ai test suite            ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n\n");

    setup();

    printf("  ── Group A: Unit tests (no Ollama required) ──\n");
    test_init_free();
    test_init_null_args();
    test_cloud_disabled_by_default();
    test_cloud_disabled_env_false();
    test_tier_policy_blocks_cloud();
    test_tier_policy_allows_tier2();
    test_infer_null_args();
    test_strerror_coverage();
    test_tier_name_coverage();
    test_result_free_null();

    printf("\n  ── Group B: Integration tests (Ollama: %s) ──\n",
           g_ollama_ok ? "localhost:11434 ✓" : "not available — skipping");
    test_health_check();
    test_tier1_inference();
    test_auto_tier_routing();
    test_async_inference();
    test_nodeid_in_result();

    teardown();

    printf("\n");
    printf("  ─────────────────────────────────────────────────\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_skipped) printf(", %d skipped", tests_skipped);
    printf(tests_failed == 0 ? "  ✓  ALL TESTS PASSED\n"
                             : "  ✗  %d FAILED\n", tests_failed);
    printf("  ─────────────────────────────────────────────────\n\n");

    return tests_failed == 0 ? 0 : 1;
}
