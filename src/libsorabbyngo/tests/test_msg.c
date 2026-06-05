/**
 * test_msg.c — libsb_msg test suite
 *
 * Tests split into two groups:
 *   A) Unit tests — no NATS server needed (envelope, signing, JSON)
 *   B) Integration tests — requires NATS on localhost:4222
 *      Skipped automatically if NATS is not reachable.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#include "libsorabbyngo/msg.h"
#include "libsorabbyngo/identity.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

/* ── Minimal test framework ─────────────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

#define TEST(name) \
    do { tests_run++; \
         printf("  [ TEST %2d ] %s ... ", tests_run, name); \
         fflush(stdout); } while(0)

#define PASS()       do { tests_passed++;  printf("PASS\n"); } while(0)
#define SKIP(reason) do { tests_skipped++; printf("SKIP — %s\n", reason); return; } while(0)
#define FAIL(msg)    do { tests_failed++;  printf("FAIL — %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while(0)

/* ── Shared fixtures ────────────────────────────────────────────────────── */

static sb_identity_ctx_t *g_id  = NULL;
static sb_msg_ctx_t      *g_msg = NULL;
static int                g_nats_ok = 0;

static void setup(void)
{
    sb_identity_init(&g_id, NULL);

    /* Try connecting to NATS — if it fails, integration tests are skipped */
    if (sb_msg_init(&g_msg, g_id, "nats://localhost:4222", "test-suite") == SB_OK) {
        if (sb_msg_connect(g_msg, 2000) == SB_OK) {
            g_nats_ok = 1;
        }
    }
}

static void teardown(void)
{
    if (g_msg) sb_msg_free(g_msg);
    if (g_id)  sb_identity_free(g_id);
}

/* ══════════════════════════════════════════════════════════════════════════
 * GROUP A — Unit tests (no NATS required)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_msg_init(void)
{
    TEST("msg_init succeeds with valid identity context");
    sb_identity_ctx_t *id = NULL;
    sb_msg_ctx_t      *msg = NULL;
    ASSERT(sb_identity_init(&id, NULL) == SB_OK, "identity init");
    ASSERT(sb_msg_init(&msg, id, "nats://localhost:4222", "test") == SB_OK,
           "msg init failed");
    sb_msg_free(msg);
    sb_identity_free(id);
    PASS();
}

static void test_msg_init_null_args(void)
{
    TEST("msg_init rejects null arguments");
    ASSERT(sb_msg_init(NULL, g_id, "nats://localhost:4222", "t") != SB_OK,
           "should reject null ctx_out");
    ASSERT(sb_msg_init((sb_msg_ctx_t**)&(void*){NULL}, NULL,
                        "nats://localhost:4222", "t") != SB_OK,
           "should reject null id_ctx");
    PASS();
}

static void test_envelope_verify_roundtrip(void)
{
    TEST("envelope sign + verify roundtrip");

    /* Manually exercise the sign/verify path that msg.c uses internally */
    const uint8_t payload[] = "novela:patient:007:vitals:ok";

    uint8_t sig[SB_ED25519_SIG_LEN];
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_sign(g_id, payload, sizeof(payload), sig) == SB_OK,
           "sign failed");
    ASSERT(sb_identity_get_pubkey(g_id, pubkey) == SB_OK, "get pubkey");

    /* Build a fake envelope and verify it */
    sb_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.version     = 1;
    env.payload     = (uint8_t *)payload;
    env.payload_len = sizeof(payload);
    memcpy(env.sig,    sig,    SB_ED25519_SIG_LEN);
    memcpy(env.pubkey, pubkey, SB_ED25519_PUBKEY_LEN);

    ASSERT(sb_envelope_verify(&env) == SB_OK, "envelope verify failed");
    PASS();
}

static void test_envelope_verify_tampered_payload(void)
{
    TEST("envelope verify rejects tampered payload");

    uint8_t payload[] = "byngonet:peer:join:node-a3f2";
    uint8_t sig[SB_ED25519_SIG_LEN];
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_sign(g_id, payload, sizeof(payload), sig) == SB_OK, "sign");
    ASSERT(sb_identity_get_pubkey(g_id, pubkey) == SB_OK, "pubkey");

    payload[0] = 'X'; /* tamper */

    sb_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.version = 1; env.payload = payload; env.payload_len = sizeof(payload);
    memcpy(env.sig, sig, SB_ED25519_SIG_LEN);
    memcpy(env.pubkey, pubkey, SB_ED25519_PUBKEY_LEN);

    ASSERT(sb_envelope_verify(&env) != SB_OK, "tampered payload should fail");
    PASS();
}

static void test_envelope_verify_wrong_pubkey(void)
{
    TEST("envelope verify rejects wrong pubkey");

    const uint8_t payload[] = "sentinel:policy:enforce:v1";
    uint8_t sig[SB_ED25519_SIG_LEN];
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_sign(g_id, payload, sizeof(payload), sig) == SB_OK, "sign");
    ASSERT(sb_identity_get_pubkey(g_id, pubkey) == SB_OK, "pubkey");

    /* Corrupt the pubkey */
    pubkey[0] ^= 0xFF;

    sb_envelope_t env;
    memset(&env, 0, sizeof(env));
    env.version = 1; env.payload = (uint8_t*)payload; env.payload_len = sizeof(payload);
    memcpy(env.sig, sig, SB_ED25519_SIG_LEN);
    memcpy(env.pubkey, pubkey, SB_ED25519_PUBKEY_LEN);

    ASSERT(sb_envelope_verify(&env) != SB_OK, "wrong pubkey should fail");
    PASS();
}

static void test_strerror(void)
{
    TEST("sb_msg_strerror returns non-null for all codes");
    ASSERT(sb_msg_strerror(SB_OK)                != NULL, "SB_OK");
    ASSERT(sb_msg_strerror(SB_ERR_NOT_CONNECTED) != NULL, "NOT_CONNECTED");
    ASSERT(sb_msg_strerror(SB_ERR_PUBLISH)       != NULL, "PUBLISH");
    ASSERT(sb_msg_strerror(SB_ERR_SUBSCRIBE)     != NULL, "SUBSCRIBE");
    ASSERT(sb_msg_strerror(SB_ERR_TIMEOUT)       != NULL, "TIMEOUT");
    ASSERT(sb_msg_strerror(SB_ERR_BAD_ENVELOPE)  != NULL, "BAD_ENVELOPE");
    ASSERT(sb_msg_strerror(SB_ERR_SIG_INVALID)   != NULL, "SIG_INVALID");
    PASS();
}

static void test_publish_without_connect(void)
{
    TEST("publish without connect returns SB_ERR_NOT_CONNECTED");
    sb_identity_ctx_t *id  = NULL;
    sb_msg_ctx_t      *msg = NULL;
    sb_identity_init(&id, NULL);
    sb_msg_init(&msg, id, "nats://localhost:4222", "test");

    /* Do NOT call sb_msg_connect */
    int rc = sb_msg_publish_str(msg, "test.subject", "hello", SB_MSG_OPT_NONE);
    ASSERT(rc == SB_ERR_NOT_CONNECTED, "expected NOT_CONNECTED");

    sb_msg_free(msg);
    sb_identity_free(id);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════════════
 * GROUP B — Integration tests (require NATS on localhost:4222)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_nats_publish(void)
{
    TEST("publish signed message to NATS");
    if (!g_nats_ok) SKIP("NATS not available");

    const char *payload = "{\"type\":\"test\",\"data\":\"libsb_msg live\"}";
    int rc = sb_msg_publish_str(g_msg, "sorabbyngo.test.msg",
                                 payload, SB_MSG_OPT_NONE);
    ASSERT(rc == SB_OK, sb_msg_strerror(rc));
    PASS();
}

static void test_nats_heartbeat(void)
{
    TEST("heartbeat publishes to sorabbyngo.test-suite.heartbeat");
    if (!g_nats_ok) SKIP("NATS not available");

    int rc = sb_msg_heartbeat(g_msg);
    ASSERT(rc == SB_OK, sb_msg_strerror(rc));
    PASS();
}

/* Shared state for subscribe test */
static volatile int g_recv_count = 0;
static char g_recv_nodeid[SB_NODEID_LEN + 1];

static void sub_handler(const sb_envelope_t *env, void *userdata)
{
    (void)userdata;
    g_recv_count++;
    strncpy(g_recv_nodeid, env->nodeid, SB_NODEID_LEN);
}

static void test_nats_subscribe_and_receive(void)
{
    TEST("subscribe + publish + receive with sig verification");
    if (!g_nats_ok) SKIP("NATS not available");

    g_recv_count = 0;

    int sub_id = sb_msg_subscribe(g_msg, "sorabbyngo.test.roundtrip",
                                   sub_handler, NULL, SB_MSG_OPT_NONE);
    ASSERT(sub_id > 0, "subscribe failed");

    /* Small sleep to let subscription register */
    usleep(50000);

    int rc = sb_msg_publish_str(g_msg, "sorabbyngo.test.roundtrip",
                                 "sorabbyngo:test:roundtrip:payload",
                                 SB_MSG_OPT_NONE);
    ASSERT(rc == SB_OK, "publish failed");

    /* Wait for delivery */
    usleep(200000);

    ASSERT(g_recv_count == 1, "expected 1 message received");

    /* Verify the received NodeID matches ours */
    char our_nodeid[SB_NODEID_LEN + 1];
    sb_identity_get_nodeid(g_id, our_nodeid, sizeof(our_nodeid));
    ASSERT(strcmp(g_recv_nodeid, our_nodeid) == 0,
           "received NodeID doesn't match sender");

    sb_msg_unsubscribe(g_msg, sub_id);
    PASS();
}

static volatile int g_tamper_recv = 0;
static void tamper_handler(const sb_envelope_t *env, void *userdata)
{
    (void)env; (void)userdata;
    g_tamper_recv++; /* should never be incremented */
}

static void test_nats_tampered_message_dropped(void)
{
    TEST("tampered message dropped by subscriber (sig verification)");
    if (!g_nats_ok) SKIP("NATS not available");

    g_tamper_recv = 0;
    int sub_id = sb_msg_subscribe(g_msg, "sorabbyngo.test.tamper",
        tamper_handler, NULL, SB_MSG_OPT_NONE);

    /* Publish a raw malformed JSON (no valid sig) directly via NATS */
    /* We use NO_SIGN flag to send an unsigned envelope */
    int rc = sb_msg_publish_str(g_msg, "sorabbyngo.test.tamper",
                                 "raw:unsigned:payload",
                                 SB_MSG_OPT_NO_SIGN);
    ASSERT(rc == SB_OK, "raw publish should succeed at transport level");

    usleep(200000);

    /* recv should still be 0 — message dropped by verifier */
    ASSERT(g_tamper_recv == 0, "unsigned message should have been dropped");
    sb_msg_unsubscribe(g_msg, sub_id);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║   SORABBYNGO OS — libsb_msg test suite           ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n\n");

    setup();

    printf("  ── Group A: Unit tests (no NATS required) ──\n");
    test_msg_init();
    test_msg_init_null_args();
    test_envelope_verify_roundtrip();
    test_envelope_verify_tampered_payload();
    test_envelope_verify_wrong_pubkey();
    test_strerror();
    test_publish_without_connect();

    printf("\n  ── Group B: Integration tests (NATS: %s) ──\n",
           g_nats_ok ? "localhost:4222 ✓" : "not available — skipping");
    test_nats_publish();
    test_nats_heartbeat();
    test_nats_subscribe_and_receive();
    test_nats_tampered_message_dropped();

    teardown();

    printf("\n");
    printf("  ─────────────────────────────────────────────────\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_skipped) printf(", %d skipped", tests_skipped);
    if (tests_failed == 0)
        printf("  ✓  ALL TESTS PASSED\n");
    else
        printf("  ✗  %d FAILED\n", tests_failed);
    printf("  ─────────────────────────────────────────────────\n\n");

    return tests_failed == 0 ? 0 : 1;
}
