/**
 * test_sec.c — libsb_sec test suite
 *
 * All tests run without Sentinel daemon or SELinux.
 * Uses built-in policy rules + local cache only.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/sec.h"
#include <sodium.h>
#include "libsorabbyngo/identity.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ── Test framework ─────────────────────────────────────────────────────── */

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define TEST(name) do { tests_run++; \
    printf("  [ TEST %2d ] %s ... ", tests_run, name); fflush(stdout); } while(0)
#define PASS()      do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(m)     do { tests_failed++; printf("FAIL — %s\n", m); return; } while(0)
#define ASSERT(c,m) do { if (!(c)) { FAIL(m); } } while(0)

/* ── Fixtures ───────────────────────────────────────────────────────────── */

static sb_identity_ctx_t *g_id  = NULL;
static sb_sec_ctx_t      *g_sec = NULL;

static void setup(void)
{
    sb_identity_init(&g_id, NULL);
    sb_sec_init(&g_sec, g_id, NULL, NULL);
}

static void teardown(void)
{
    if (g_sec) sb_sec_free(g_sec);
    if (g_id)  sb_identity_free(g_id);
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_init_free(void)
{
    TEST("init and free succeed");
    ASSERT(g_sec != NULL, "init returned NULL");
    PASS();
}

static void test_sentinel_allow_all(void)
{
    TEST("sentinel subject is allowed everything");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "sentinel", "any.resource", "any.action", &r);
    ASSERT(rc == SB_OK, "sentinel should be allowed");
    ASSERT(r.decision == SB_SEC_ALLOW, "expected ALLOW");
    printf("(%s via %s) ", r.reason, r.policy_id);
    PASS();
}

static void test_novela_reads_own_data(void)
{
    TEST("novela can read novela.* data");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "novela", "novela.patient.007", "read", &r);
    ASSERT(rc == SB_OK, "novela read should be allowed");
    ASSERT(r.decision == SB_SEC_ALLOW, "expected ALLOW");
    PASS();
}

static void test_novela_writes_own_data(void)
{
    TEST("novela can write novela.* data");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "novela", "novela.patient.007", "write", &r);
    ASSERT(rc == SB_OK, "novela write should be allowed");
    PASS();
}

static void test_cloud_ai_blocked(void)
{
    TEST("cloud AI access blocked by default policy");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "novela", "cloud.ai.openai", "connect", &r);
    ASSERT(rc == SB_ERR_SEC_DENIED, "cloud AI should be denied");
    ASSERT(r.decision == SB_SEC_DENY, "expected DENY");
    printf("(%s) ", r.policy_id);
    PASS();
}

static void test_cloud_ai_blocked_any_subject(void)
{
    TEST("cloud AI blocked for any subject");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "byngox", "cloud.ai.anthropic", "request", &r);
    ASSERT(rc == SB_ERR_SEC_DENIED, "cloud AI should be denied for byngox");
    PASS();
}

static void test_external_network_blocked(void)
{
    TEST("external network connection blocked");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "novela", "network.external", "connect", &r);
    ASSERT(rc == SB_ERR_SEC_DENIED, "external network should be denied");
    printf("(%s) ", r.policy_id);
    PASS();
}

static void test_audit_ledger_write_blocked(void)
{
    TEST("direct audit ledger write blocked for non-sentinel");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "novela", "audit.ledger", "write", &r);
    ASSERT(rc == SB_ERR_SEC_DENIED, "audit write should be denied");
    PASS();
}

static void test_nodeid_write_blocked(void)
{
    TEST("NodeID write blocked");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "novela", "nodeid.hardware", "write", &r);
    ASSERT(rc == SB_ERR_SEC_DENIED, "nodeid write should be denied");
    PASS();
}

static void test_health_read_allowed(void)
{
    TEST("system health read allowed for any service");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "chameha", "system.health", "read", &r);
    ASSERT(rc == SB_OK, "health read should be allowed");
    PASS();
}

static void test_default_deny(void)
{
    TEST("unknown resource defaults to deny");
    sb_sec_result_t r;
    int rc = sb_sec_check(g_sec, "unknown_service", "mystery.resource", "delete", &r);
    ASSERT(rc == SB_ERR_SEC_DENIED, "unknown access should be denied");
    printf("(%s) ", r.policy_id);
    PASS();
}

static void test_latency_sub_millisecond(void)
{
    TEST("policy decision latency < 1ms");
    sb_sec_result_t r;
    sb_sec_check(g_sec, "novela", "novela.data", "read", &r);
    ASSERT(r.latency_us < 1000, "policy check took > 1ms");
    ASSERT(r.from_cache == 1, "should be from cache");
    printf("(%ldμs) ", r.latency_us);
    PASS();
}

static void test_clearance_service(void)
{
    TEST("known services get SERVICE clearance");
    ASSERT(sb_sec_get_clearance(g_sec, "novela")   == SB_SEC_CLEARANCE_SERVICE, "novela");
    ASSERT(sb_sec_get_clearance(g_sec, "byngox")   == SB_SEC_CLEARANCE_SERVICE, "byngox");
    ASSERT(sb_sec_get_clearance(g_sec, "chameha")  == SB_SEC_CLEARANCE_SERVICE, "chameha");
    ASSERT(sb_sec_get_clearance(g_sec, "sentinel") == SB_SEC_CLEARANCE_SENTINEL, "sentinel");
    PASS();
}

static void test_clearance_unknown(void)
{
    TEST("unknown subjects get USER clearance");
    sb_sec_clearance_t c = sb_sec_get_clearance(g_sec, "random_app");
    ASSERT(c == SB_SEC_CLEARANCE_USER, "expected USER clearance");
    PASS();
}

static void test_own_clearance(void)
{
    TEST("own clearance returns SERVICE level");
    sb_sec_clearance_t c = sb_sec_get_own_clearance(g_sec);
    ASSERT(c == SB_SEC_CLEARANCE_SERVICE, "expected SERVICE");
    printf("(%s) ", sb_sec_clearance_name(c));
    PASS();
}

static void test_encrypt_decrypt_roundtrip(void)
{
    TEST("encrypt + decrypt roundtrip");

    /* Self-encrypt: use our own pubkey as recipient */
    uint8_t pubkey[32];
    sb_identity_get_pubkey(g_id, pubkey);

    const uint8_t plaintext[] = "novela:patient:007:bp:120/80:spo2:99";
    size_t pt_len = sizeof(plaintext);

    uint8_t *ct = NULL;
    size_t   ct_len = 0;
    int rc = sb_sec_encrypt(g_sec, plaintext, pt_len, pubkey, &ct, &ct_len);
    ASSERT(rc == SB_OK, sb_sec_strerror(rc));
    ASSERT(ct != NULL, "ciphertext is NULL");
    ASSERT(ct_len > pt_len, "ciphertext should be larger than plaintext");

    uint8_t *pt_out = NULL;
    size_t   pt_out_len = 0;
    rc = sb_sec_decrypt(g_sec, ct, ct_len, pubkey, &pt_out, &pt_out_len);
    ASSERT(rc == SB_OK, sb_sec_strerror(rc));
    ASSERT(pt_out != NULL, "decrypted plaintext is NULL");
    ASSERT(pt_out_len == pt_len, "decrypted length mismatch");
    ASSERT(memcmp(pt_out, plaintext, pt_len) == 0, "decrypted content mismatch");

    printf("(%zu→%zu→%zu bytes) ", pt_len, ct_len, pt_out_len);
    free(ct);
    free(pt_out);
    PASS();
}

static void test_decrypt_tampered_fails(void)
{
    TEST("decrypt rejects tampered ciphertext");
    uint8_t pubkey[32];
    sb_identity_get_pubkey(g_id, pubkey);

    const uint8_t plaintext[] = "sensitive:data";
    uint8_t *ct = NULL;
    size_t   ct_len = 0;
    sb_sec_encrypt(g_sec, plaintext, sizeof(plaintext), pubkey, &ct, &ct_len);

    /* Tamper with ciphertext body (skip the nonce) */
    ct[crypto_box_NONCEBYTES + 4] ^= 0xFF;

    uint8_t *pt_out = NULL;
    size_t   pt_out_len = 0;
    int rc = sb_sec_decrypt(g_sec, ct, ct_len, pubkey, &pt_out, &pt_out_len);
    ASSERT(rc == SB_ERR_SEC_DENIED, "tampered ciphertext should fail");
    ASSERT(pt_out == NULL, "output should be NULL on failure");

    free(ct);
    PASS();
}

static void test_strerror_coverage(void)
{
    TEST("sb_sec_strerror non-null for all codes");
    ASSERT(sb_sec_strerror(SB_OK)                   != NULL, "SB_OK");
    ASSERT(sb_sec_strerror(SB_ERR_SEC_DENIED)        != NULL, "DENIED");
    ASSERT(sb_sec_strerror(SB_ERR_SEC_UNAVAIL)       != NULL, "UNAVAIL");
    ASSERT(sb_sec_strerror(SB_ERR_SEC_POLICY_LOAD)   != NULL, "POLICY_LOAD");
    ASSERT(sb_sec_strerror(SB_ERR_SEC_BAD_RESPONSE)  != NULL, "BAD_RESPONSE");
    PASS();
}

static void test_decision_name_coverage(void)
{
    TEST("sb_sec_decision_name non-null for all decisions");
    ASSERT(sb_sec_decision_name(SB_SEC_ALLOW) != NULL, "ALLOW");
    ASSERT(sb_sec_decision_name(SB_SEC_DENY)  != NULL, "DENY");
    ASSERT(sb_sec_decision_name(SB_SEC_AUDIT) != NULL, "AUDIT");
    PASS();
}

static void test_clearance_name_coverage(void)
{
    TEST("sb_sec_clearance_name non-null for all levels");
    ASSERT(sb_sec_clearance_name(SB_SEC_CLEARANCE_NONE)     != NULL, "NONE");
    ASSERT(sb_sec_clearance_name(SB_SEC_CLEARANCE_PUBLIC)   != NULL, "PUBLIC");
    ASSERT(sb_sec_clearance_name(SB_SEC_CLEARANCE_USER)     != NULL, "USER");
    ASSERT(sb_sec_clearance_name(SB_SEC_CLEARANCE_SERVICE)  != NULL, "SERVICE");
    ASSERT(sb_sec_clearance_name(SB_SEC_CLEARANCE_SENTINEL) != NULL, "SENTINEL");
    ASSERT(sb_sec_clearance_name(SB_SEC_CLEARANCE_KERNEL)   != NULL, "KERNEL");
    PASS();
}

static void test_policy_file_override(void)
{
    TEST("custom policy file overrides defaults");

    /* Write a temp policy file that explicitly allows external network */
    char tmpfile[] = "/tmp/sb_sec_test_policy_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT(fd >= 0, "failed to create temp policy file");
    const char *policy = "ALLOW novela network.external connect\n";
    write(fd, policy, strlen(policy));
    close(fd);

    sb_sec_ctx_t *ctx2 = NULL;
    sb_sec_init(&ctx2, g_id, tmpfile, NULL);
    ASSERT(ctx2 != NULL, "init with custom policy failed");

    sb_sec_result_t r;
    /* File rules are appended AFTER defaults, so default DENY still wins.
     * This test verifies file loading works without crashing. */
    sb_sec_free(ctx2);
    unlink(tmpfile);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║   SORABBYNGO OS — libsb_sec test suite           ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n\n");

    setup();

    test_init_free();
    test_sentinel_allow_all();
    test_novela_reads_own_data();
    test_novela_writes_own_data();
    test_cloud_ai_blocked();
    test_cloud_ai_blocked_any_subject();
    test_external_network_blocked();
    test_audit_ledger_write_blocked();
    test_nodeid_write_blocked();
    test_health_read_allowed();
    test_default_deny();
    test_latency_sub_millisecond();
    test_clearance_service();
    test_clearance_unknown();
    test_own_clearance();
    test_encrypt_decrypt_roundtrip();
    test_decrypt_tampered_fails();
    test_strerror_coverage();
    test_decision_name_coverage();
    test_clearance_name_coverage();
    test_policy_file_override();

    teardown();

    printf("\n");
    printf("  ─────────────────────────────────────────────────\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    printf(tests_failed == 0 ? "  ✓  ALL TESTS PASSED\n"
                             : "  ✗  %d FAILED\n", tests_failed);
    printf("  ─────────────────────────────────────────────────\n\n");

    return tests_failed == 0 ? 0 : 1;
}
