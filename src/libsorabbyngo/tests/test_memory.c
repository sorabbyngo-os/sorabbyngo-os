/**
 * test_memory.c — libsb_memory test suite
 *
 * All tests run without any external services.
 * Uses a temp directory under /tmp for ledger files.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/memory.h"
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
static sb_mem_ctx_t      *g_mem = NULL;
static char               g_tmpdir[128];

static void setup(void)
{
    sb_identity_init(&g_id, NULL);
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/sb_mem_test_%d", (int)getpid());
    sb_mem_init(&g_mem, g_id, g_tmpdir);
}

static void teardown(void)
{
    if (g_mem) sb_mem_free(g_mem);
    if (g_id)  sb_identity_free(g_id);
    /* Clean up temp files */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmpdir);
    system(cmd);
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_init_free(void)
{
    TEST("init creates storage directory");
    ASSERT(g_mem != NULL, "init failed");
    ASSERT(access(g_tmpdir, F_OK) == 0, "storage dir not created");
    PASS();
}

static void test_ledger_path(void)
{
    TEST("ledger path contains audit.jsonl");
    const char *path = sb_mem_get_ledger_path(g_mem);
    ASSERT(path != NULL, "ledger path is NULL");
    ASSERT(strstr(path, "audit.jsonl") != NULL, "wrong ledger filename");
    PASS();
}

static void test_audit_write_single(void)
{
    TEST("audit_write appends one entry");
    char hash[SB_MEM_CHAIN_HASH_LEN + 1];
    int rc = sb_mem_audit_write(g_mem, SB_MEM_EVENT_SYSTEM,
                                "{\"msg\":\"test event\"}", SB_MEM_PII_NONE, hash);
    ASSERT(rc == SB_OK, sb_mem_strerror(rc));
    ASSERT(strlen(hash) == SB_MEM_CHAIN_HASH_LEN, "hash wrong length");
    ASSERT(strcmp(hash, "0000000000000000000000000000000000000000"
                        "000000000000000000000000") != 0, "hash is all zeros");
    printf("(hash=%.*s...) ", 8, hash);
    PASS();
}

static void test_audit_write_chain(void)
{
    TEST("sequential writes produce different chain hashes");
    char h1[SB_MEM_CHAIN_HASH_LEN + 1];
    char h2[SB_MEM_CHAIN_HASH_LEN + 1];
    char h3[SB_MEM_CHAIN_HASH_LEN + 1];

    sb_mem_audit_write(g_mem, SB_MEM_EVENT_RECORD_WRITE, "{\"a\":1}", SB_MEM_PII_NONE, h1);
    sb_mem_audit_write(g_mem, SB_MEM_EVENT_RECORD_WRITE, "{\"b\":2}", SB_MEM_PII_NONE, h2);
    sb_mem_audit_write(g_mem, SB_MEM_EVENT_RECORD_WRITE, "{\"c\":3}", SB_MEM_PII_NONE, h3);

    ASSERT(strcmp(h1, h2) != 0, "h1 == h2 (chain not progressing)");
    ASSERT(strcmp(h2, h3) != 0, "h2 == h3 (chain not progressing)");
    ASSERT(strcmp(h1, h3) != 0, "h1 == h3");
    PASS();
}

static void test_audit_verify_intact(void)
{
    TEST("audit_verify passes on intact chain");
    int count = 0;
    int rc = sb_mem_audit_verify(g_mem, &count);
    ASSERT(rc == SB_OK, sb_mem_strerror(rc));
    ASSERT(count > 0, "expected entries in chain");
    printf("(%d entries verified) ", count);
    PASS();
}

static void test_audit_verify_tampered(void)
{
    TEST("audit_verify detects tampered entry");

    /* Write one clean entry to a fresh context */
    char tmpdir2[128];
    snprintf(tmpdir2, sizeof(tmpdir2), "/tmp/sb_mem_tamper_%d", (int)getpid());
    sb_mem_ctx_t *ctx2 = NULL;
    sb_mem_init(&ctx2, g_id, tmpdir2);

    sb_mem_audit_write(ctx2, SB_MEM_EVENT_SYSTEM, "{\"x\":1}", SB_MEM_PII_NONE, NULL);

    /* Tamper with the ledger file */
    char audit_path[256];
    snprintf(audit_path, sizeof(audit_path), "%s/audit.jsonl", tmpdir2);
    FILE *f = fopen(audit_path, "r+");
    ASSERT(f != NULL, "could not open audit file for tampering");
    fseek(f, 10, SEEK_SET);
    fwrite("XXXX", 1, 4, f);
    fclose(f);

    int count = 0;
    int rc = sb_mem_audit_verify(ctx2, &count);
    ASSERT(rc == SB_ERR_MEM_CHAIN_BROKEN, "tampered chain should fail verify");

    sb_mem_free(ctx2);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir2);
    system(cmd);
    PASS();
}

static void test_audit_tail(void)
{
    TEST("audit_tail returns last N entries");
    sb_mem_entry_t **entries = NULL;
    int count = 0;
    int rc = sb_mem_audit_tail(g_mem, 3, &entries, &count);
    ASSERT(rc == SB_OK, sb_mem_strerror(rc));
    ASSERT(count > 0, "expected entries");
    ASSERT(entries != NULL, "entries is NULL");

    /* Each entry should have timestamp and event_type */
    for (int i = 0; i < count; i++) {
        ASSERT(entries[i] != NULL, "null entry");
        ASSERT(strlen(entries[i]->timestamp) > 0, "empty timestamp");
        ASSERT(strlen(entries[i]->chain_hash) == SB_MEM_CHAIN_HASH_LEN,
               "bad chain_hash length");
    }
    printf("(%d entries) ", count);
    sb_mem_entries_free(entries, count);
    PASS();
}

static void test_store_and_retrieve(void)
{
    TEST("store + retrieve roundtrip");
    const char *key   = "novela:patient:007:bp";
    const char *value = "{\"systolic\":120,\"diastolic\":80}";

    int rc = sb_mem_store(g_mem, key, (const uint8_t *)value,
                          strlen(value), SB_MEM_PII_NONE);
    ASSERT(rc == SB_OK, sb_mem_strerror(rc));

    uint8_t *out = NULL;
    size_t   out_len = 0;
    rc = sb_mem_retrieve(g_mem, key, &out, &out_len);
    ASSERT(rc == SB_OK, sb_mem_strerror(rc));
    ASSERT(out != NULL, "retrieved value is NULL");
    ASSERT(strstr((char *)out, "120") != NULL, "value content wrong");

    free(out);
    PASS();
}

static void test_store_missing_key(void)
{
    TEST("retrieve returns KEY_NOT_FOUND for unknown key");
    uint8_t *out = NULL;
    size_t   out_len = 0;
    int rc = sb_mem_retrieve(g_mem, "nonexistent:key:xyz", &out, &out_len);
    ASSERT(rc == SB_ERR_MEM_KEY_NOT_FOUND, "expected KEY_NOT_FOUND");
    ASSERT(out == NULL, "out should be NULL");
    PASS();
}

static void test_store_delete(void)
{
    TEST("delete makes key unretrievable");
    const char *key = "novela:temp:session:abc";
    sb_mem_store(g_mem, key, (const uint8_t *)"data", 4, SB_MEM_PII_NONE);

    int rc = sb_mem_delete(g_mem, key);
    ASSERT(rc == SB_OK, "delete failed");

    uint8_t *out = NULL;
    size_t   out_len = 0;
    rc = sb_mem_retrieve(g_mem, key, &out, &out_len);
    ASSERT(rc == SB_ERR_MEM_KEY_NOT_FOUND, "deleted key should not be found");
    PASS();
}

static void test_store_overwrite(void)
{
    TEST("second store overwrites — retrieve returns latest");
    const char *key = "novela:patient:007:spo2";
    sb_mem_store(g_mem, key, (const uint8_t *)"\"98\"", 4, SB_MEM_PII_NONE);
    sb_mem_store(g_mem, key, (const uint8_t *)"\"99\"", 4, SB_MEM_PII_NONE);

    uint8_t *out = NULL;
    size_t   out_len = 0;
    sb_mem_retrieve(g_mem, key, &out, &out_len);
    ASSERT(out != NULL, "retrieve failed");
    ASSERT(strstr((char *)out, "99") != NULL, "expected latest value '99'");
    free(out);
    PASS();
}

static void test_pii_scrub_email(void)
{
    TEST("PII scrubber redacts email address");
    char json[256] = "{\"patient\":\"john.doe@hospital.ke\",\"bp\":120}";
    int n = sb_mem_scrub_pii(json, sizeof(json), SB_MEM_PII_EMAIL);
    ASSERT(n > 0, "expected at least one replacement");
    ASSERT(strstr(json, "john.doe@hospital.ke") == NULL, "email not scrubbed");
    ASSERT(strstr(json, "REDACTED") != NULL, "REDACTED not found");
    printf("(%s) ", json);
    PASS();
}

static void test_pii_scrub_phone(void)
{
    TEST("PII scrubber redacts phone number");
    char json[256] = "{\"contact\":\"+254712345678\",\"name\":\"Jane\"}";
    int n = sb_mem_scrub_pii(json, sizeof(json), SB_MEM_PII_PHONE);
    ASSERT(n > 0, "expected replacement");
    ASSERT(strstr(json, "254712345678") == NULL, "phone not scrubbed");
    PASS();
}

static void test_pii_none_unchanged(void)
{
    TEST("PII_NONE leaves data unchanged");
    char json[256] = "{\"patient\":\"test@test.com\",\"bp\":120}";
    char orig[256];
    strncpy(orig, json, sizeof(orig));
    int n = sb_mem_scrub_pii(json, sizeof(json), SB_MEM_PII_NONE);
    ASSERT(n == 0, "expected 0 replacements");
    ASSERT(strcmp(json, orig) == 0, "data was modified");
    PASS();
}

static void test_resume_chain(void)
{
    TEST("chain resumes correctly after re-init");
    char h1[SB_MEM_CHAIN_HASH_LEN + 1];
    sb_mem_audit_write(g_mem, SB_MEM_EVENT_SYSTEM, "{\"seq\":1}", SB_MEM_PII_NONE, h1);

    /* Re-init same directory */
    sb_mem_ctx_t *ctx2 = NULL;
    sb_mem_init(&ctx2, g_id, g_tmpdir);

    char h2[SB_MEM_CHAIN_HASH_LEN + 1];
    sb_mem_audit_write(ctx2, SB_MEM_EVENT_SYSTEM, "{\"seq\":2}", SB_MEM_PII_NONE, h2);

    /* Verify the full chain across both contexts */
    int count = 0;
    int rc = sb_mem_audit_verify(ctx2, &count);
    ASSERT(rc == SB_OK, "chain broken after re-init");
    ASSERT(count >= 2, "expected at least 2 entries");
    printf("(%d entries intact) ", count);

    sb_mem_free(ctx2);
    PASS();
}

static void test_strerror_coverage(void)
{
    TEST("sb_mem_strerror non-null for all codes");
    ASSERT(sb_mem_strerror(SB_OK)                    != NULL, "SB_OK");
    ASSERT(sb_mem_strerror(SB_ERR_MEM_IO)            != NULL, "IO");
    ASSERT(sb_mem_strerror(SB_ERR_MEM_CHAIN_BROKEN)  != NULL, "CHAIN");
    ASSERT(sb_mem_strerror(SB_ERR_MEM_KEY_NOT_FOUND) != NULL, "KEY");
    ASSERT(sb_mem_strerror(SB_ERR_MEM_VALUE_TOO_BIG) != NULL, "VALUE");
    ASSERT(sb_mem_strerror(SB_ERR_MEM_PII_SCRUBBED)  != NULL, "PII");
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║   SORABBYNGO OS — libsb_memory test suite        ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n\n");

    setup();

    test_init_free();
    test_ledger_path();
    test_audit_write_single();
    test_audit_write_chain();
    test_audit_verify_intact();
    test_audit_verify_tampered();
    test_audit_tail();
    test_store_and_retrieve();
    test_store_missing_key();
    test_store_delete();
    test_store_overwrite();
    test_pii_scrub_email();
    test_pii_scrub_phone();
    test_pii_none_unchanged();
    test_resume_chain();
    test_strerror_coverage();

    teardown();

    printf("\n");
    printf("  ─────────────────────────────────────────────────\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    printf(tests_failed == 0 ? "  ✓  ALL TESTS PASSED\n"
                             : "  ✗  %d FAILED\n", tests_failed);
    printf("  ─────────────────────────────────────────────────\n\n");

    return tests_failed == 0 ? 0 : 1;
}
