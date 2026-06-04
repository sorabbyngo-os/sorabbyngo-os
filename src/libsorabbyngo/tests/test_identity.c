/**
 * test_identity.c — libsb_identity test suite
 *
 * Tests:
 *   1. Init succeeds and returns a context
 *   2. NodeID is 64 hex chars, non-zero
 *   3. Short NodeID is 8 chars
 *   4. Pubkey is non-zero
 *   5. Sign + verify roundtrip succeeds
 *   6. Tampered payload fails verification
 *   7. Tampered signature fails verification
 *   8. Two init calls on same machine produce same NodeID and same keypair
 *   9. sb_identity_verify_peer accepts valid NodeID
 *  10. sb_identity_verify_peer rejects bad inputs
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#include "libsorabbyngo/identity.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ── Minimal test framework ─────────────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  [ TEST %2d ] %s ... ", tests_run, name); \
        fflush(stdout); \
    } while(0)

#define PASS() \
    do { tests_passed++; printf("PASS\n"); } while(0)

#define FAIL(msg) \
    do { tests_failed++; printf("FAIL — %s\n", msg); } while(0)

#define ASSERT(cond, msg) \
    do { if (!(cond)) { FAIL(msg); return; } } while(0)

/* ── Test functions ─────────────────────────────────────────────────────── */

static void test_init_succeeds(void)
{
    TEST("init succeeds");
    sb_identity_ctx_t *ctx = NULL;
    sb_nodeid_source_t src;
    int rc = sb_identity_init(&ctx, &src);
    ASSERT(rc == SB_OK, "sb_identity_init returned error");
    ASSERT(ctx != NULL, "ctx is NULL after init");

    const char *src_str = (src == SB_NODEID_SRC_KERNEL) ? "kernel" : "userspace";
    printf("(NodeID source: %s) ", src_str);

    sb_identity_free(ctx);
    PASS();
}

static void test_nodeid_format(void)
{
    TEST("NodeID is 64-char lowercase hex, non-zero");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    char nodeid[SB_NODEID_LEN + 1];
    ASSERT(sb_identity_get_nodeid(ctx, nodeid, sizeof(nodeid)) == SB_OK,
           "get_nodeid failed");
    ASSERT(strlen(nodeid) == SB_NODEID_LEN, "NodeID wrong length");

    int all_zero = 1;
    for (int i = 0; i < SB_NODEID_LEN; i++) {
        char c = nodeid[i];
        ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
               "NodeID contains non-hex char");
        if (c != '0') all_zero = 0;
    }
    ASSERT(!all_zero, "NodeID is all zeros");

    printf("(%.*s...) ", 16, nodeid);
    sb_identity_free(ctx);
    PASS();
}

static void test_nodeid_short(void)
{
    TEST("short NodeID is 8 chars, prefix of full NodeID");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    char nodeid[SB_NODEID_LEN + 1];
    char shortn[9];
    ASSERT(sb_identity_get_nodeid(ctx, nodeid, sizeof(nodeid)) == SB_OK, "get_nodeid");
    ASSERT(sb_identity_short(ctx, shortn) == SB_OK, "short failed");
    ASSERT(strlen(shortn) == 8, "short NodeID wrong length");
    ASSERT(strncmp(nodeid, shortn, 8) == 0, "short NodeID doesn't match full NodeID prefix");

    sb_identity_free(ctx);
    PASS();
}

static void test_pubkey_nonzero(void)
{
    TEST("pubkey is non-zero");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];
    ASSERT(sb_identity_get_pubkey(ctx, pubkey) == SB_OK, "get_pubkey failed");

    int all_zero = 1;
    for (int i = 0; i < SB_ED25519_PUBKEY_LEN; i++)
        if (pubkey[i]) { all_zero = 0; break; }
    ASSERT(!all_zero, "pubkey is all zeros");

    sb_identity_free(ctx);
    PASS();
}

static void test_sign_verify_roundtrip(void)
{
    TEST("sign + verify roundtrip");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    const uint8_t msg[] = "sorabbyngo-os:health-record:patient-007:v1";
    uint8_t sig[SB_ED25519_SIG_LEN];
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_sign(ctx, msg, sizeof(msg), sig) == SB_OK, "sign failed");
    ASSERT(sb_identity_get_pubkey(ctx, pubkey) == SB_OK, "get_pubkey failed");
    ASSERT(sb_identity_verify_sig(msg, sizeof(msg), sig, pubkey) == SB_OK,
           "verify failed on valid signature");

    sb_identity_free(ctx);
    PASS();
}

static void test_tampered_payload_fails(void)
{
    TEST("tampered payload fails verification");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    uint8_t msg[] = "original-message";
    uint8_t sig[SB_ED25519_SIG_LEN];
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_sign(ctx, msg, sizeof(msg), sig) == SB_OK, "sign failed");
    ASSERT(sb_identity_get_pubkey(ctx, pubkey) == SB_OK, "get_pubkey failed");

    /* Tamper with the payload */
    msg[0] = 'X';

    int rc = sb_identity_verify_sig(msg, sizeof(msg), sig, pubkey);
    ASSERT(rc == SB_ERR_CRYPTO, "tampered payload should fail verify");

    sb_identity_free(ctx);
    PASS();
}

static void test_tampered_sig_fails(void)
{
    TEST("tampered signature fails verification");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    const uint8_t msg[] = "sorabbyngo-node-handshake";
    uint8_t sig[SB_ED25519_SIG_LEN];
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_sign(ctx, msg, sizeof(msg), sig) == SB_OK, "sign failed");
    ASSERT(sb_identity_get_pubkey(ctx, pubkey) == SB_OK, "get_pubkey failed");

    /* Tamper with the signature */
    sig[0] ^= 0xFF;
    sig[31] ^= 0xAB;

    int rc = sb_identity_verify_sig(msg, sizeof(msg), sig, pubkey);
    ASSERT(rc == SB_ERR_CRYPTO, "tampered sig should fail verify");

    sb_identity_free(ctx);
    PASS();
}

static void test_deterministic_identity(void)
{
    TEST("two inits produce identical NodeID and pubkey");
    sb_identity_ctx_t *ctx1 = NULL, *ctx2 = NULL;
    ASSERT(sb_identity_init(&ctx1, NULL) == SB_OK, "init 1 failed");
    ASSERT(sb_identity_init(&ctx2, NULL) == SB_OK, "init 2 failed");

    char nid1[SB_NODEID_LEN + 1], nid2[SB_NODEID_LEN + 1];
    uint8_t pk1[SB_ED25519_PUBKEY_LEN], pk2[SB_ED25519_PUBKEY_LEN];

    ASSERT(sb_identity_get_nodeid(ctx1, nid1, sizeof(nid1)) == SB_OK, "get_nodeid 1");
    ASSERT(sb_identity_get_nodeid(ctx2, nid2, sizeof(nid2)) == SB_OK, "get_nodeid 2");
    ASSERT(strcmp(nid1, nid2) == 0, "NodeIDs differ between inits");

    ASSERT(sb_identity_get_pubkey(ctx1, pk1) == SB_OK, "pubkey 1");
    ASSERT(sb_identity_get_pubkey(ctx2, pk2) == SB_OK, "pubkey 2");
    ASSERT(memcmp(pk1, pk2, SB_ED25519_PUBKEY_LEN) == 0, "pubkeys differ between inits");

    sb_identity_free(ctx1);
    sb_identity_free(ctx2);
    PASS();
}

static void test_cross_node_verify(void)
{
    TEST("cross-context sign/verify (simulated peer)");
    /* Simulate two nodes: ctx_a signs, ctx_b's pubkey is used to verify
     * In real usage ctx_a and ctx_b would be on different machines.
     * Here we just use the same machine's identity for both sides
     * to verify the API contract works correctly. */
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    /* "peer" pubkey is just our own pubkey — same machine */
    uint8_t peer_pubkey[SB_ED25519_PUBKEY_LEN];
    ASSERT(sb_identity_get_pubkey(ctx, peer_pubkey) == SB_OK, "get pubkey");

    const uint8_t handshake[] = "byngonet:mesh:join:v1";
    uint8_t sig[SB_ED25519_SIG_LEN];
    ASSERT(sb_identity_sign(ctx, handshake, sizeof(handshake), sig) == SB_OK, "sign");
    ASSERT(sb_identity_verify_sig(handshake, sizeof(handshake), sig, peer_pubkey) == SB_OK,
           "cross-ctx verify failed");

    sb_identity_free(ctx);
    PASS();
}

static void test_verify_peer_valid(void)
{
    TEST("verify_peer accepts valid NodeID");
    sb_identity_ctx_t *ctx = NULL;
    ASSERT(sb_identity_init(&ctx, NULL) == SB_OK, "init failed");

    char nodeid[SB_NODEID_LEN + 1];
    ASSERT(sb_identity_get_nodeid(ctx, nodeid, sizeof(nodeid)) == SB_OK, "get_nodeid");
    ASSERT(sb_identity_verify_peer(nodeid) == SB_OK, "verify_peer rejected valid nodeid");

    sb_identity_free(ctx);
    PASS();
}

static void test_verify_peer_rejects_bad(void)
{
    TEST("verify_peer rejects bad inputs");
    ASSERT(sb_identity_verify_peer(NULL)   != SB_OK, "should reject NULL");
    ASSERT(sb_identity_verify_peer("")     != SB_OK, "should reject empty string");
    ASSERT(sb_identity_verify_peer("abc")  != SB_OK, "should reject short string");
    ASSERT(sb_identity_verify_peer(
        "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ")
        != SB_OK, "should reject non-hex chars");
    /* All-zero NodeID */
    ASSERT(sb_identity_verify_peer(
        "0000000000000000000000000000000000000000000000000000000000000000")
        != SB_OK, "should reject all-zero NodeID");
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║   SORABBYNGO OS — libsb_identity test suite      ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n");
    printf("\n");

    test_init_succeeds();
    test_nodeid_format();
    test_nodeid_short();
    test_pubkey_nonzero();
    test_sign_verify_roundtrip();
    test_tampered_payload_fails();
    test_tampered_sig_fails();
    test_deterministic_identity();
    test_cross_node_verify();
    test_verify_peer_valid();
    test_verify_peer_rejects_bad();

    printf("\n");
    printf("  ─────────────────────────────────────────────────\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed == 0) {
        printf("  ✓  ALL TESTS PASSED\n");
    } else {
        printf("  ✗  %d FAILED\n", tests_failed);
    }
    printf("  ─────────────────────────────────────────────────\n\n");

    return tests_failed == 0 ? 0 : 1;
}
