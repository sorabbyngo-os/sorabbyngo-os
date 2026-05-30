/*
 * libsorabbyngo — Sorabbyngo C library implementation
 * Copyright (c) 2026 Sorabbyngo Labs, Kisii, Kenya
 */

#include "sorabbyngo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <openssl/sha.h>
#include <time.h>

/* ---- NodeID paths ---- */
#define NODEID_PATH  "/var/lib/sorabbyngo/nodeid"
#define PROC_NODEID  "/proc/sorabbyngo/nodeid"
#define ENV_NODEID   "SORABBYNGO_NODE_ID"

/* ---- NodeID ---- */

sbyngo_err_t sbyngo_nodeid_load(sbyngo_nodeid_t *out) {
    if (!out) return SBYNGO_ERR_INVAL;
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(NODEID_PATH, "r");
    if (f) {
        if (fgets(out->id, sizeof(out->id), f)) {
            /* Strip newline */
            size_t len = strlen(out->id);
            if (len > 0 && out->id[len-1] == '\n')
                out->id[len-1] = '\0';
            out->sealed = true;
        }
        fclose(f);
        return SBYNGO_OK;
    }

    /* Try environment variable */
    const char *env = getenv(ENV_NODEID);
    if (env) {
        strncpy(out->id, env, SBYNGO_NODEID_LEN);
        out->id[SBYNGO_NODEID_LEN] = '\0';
        return SBYNGO_OK;
    }

    return SBYNGO_ERR_NOT_FOUND;
}

sbyngo_err_t sbyngo_nodeid_verify(const sbyngo_nodeid_t *nodeid) {
    if (!nodeid) return SBYNGO_ERR_INVAL;

    FILE *f = fopen(PROC_NODEID, "r");
    if (!f) return SBYNGO_OK; /* Module not loaded */

    char live[SBYNGO_NODEID_LEN + 2];
    memset(live, 0, sizeof(live));
    if (!fgets(live, sizeof(live), f)) {
        fclose(f);
        return SBYNGO_ERR_IO;
    }
    fclose(f);

    /* Strip newline */
    size_t len = strlen(live);
    if (len > 0 && live[len-1] == '\n') live[len-1] = '\0';

    if (strcmp(live, nodeid->id) != 0) {
        return SBYNGO_ERR_MISMATCH;
    }
    return SBYNGO_OK;
}

sbyngo_err_t sbyngo_nodeid_seal(const sbyngo_nodeid_t *nodeid) {
    if (!nodeid) return SBYNGO_ERR_INVAL;

    /* Ensure directory exists */
    system("mkdir -p /var/lib/sorabbyngo");

    FILE *f = fopen(NODEID_PATH, "w");
    if (!f) return SBYNGO_ERR_PERM;
    fprintf(f, "%s\n", nodeid->id);
    fclose(f);
    return SBYNGO_OK;
}

/* ---- Hardening ---- */

sbyngo_err_t sbyngo_harden_no_new_privs(void) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return SBYNGO_ERR_PERM;
    return SBYNGO_OK;
}

sbyngo_err_t sbyngo_harden_no_core_dumps(void) {
    struct rlimit rl = {0, 0};
    if (setrlimit(RLIMIT_CORE, &rl) != 0)
        return SBYNGO_ERR_PERM;
    return SBYNGO_OK;
}

sbyngo_err_t sbyngo_harden_memory_limit(uint64_t bytes) {
    struct rlimit rl = {bytes, bytes};
    if (setrlimit(RLIMIT_AS, &rl) != 0)
        return SBYNGO_ERR_PERM;
    return SBYNGO_OK;
}

/* ---- Integrity ---- */

sbyngo_err_t sbyngo_sha256_file(const char *path, char *out_hex) {
    if (!path || !out_hex) return SBYNGO_ERR_INVAL;

    FILE *f = fopen(path, "rb");
    if (!f) return SBYNGO_ERR_NOT_FOUND;

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        SHA256_Update(&ctx, buf, n);
    }
    fclose(f);

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(out_hex + i*2, "%02x", hash[i]);
    }
    out_hex[64] = '\0';
    return SBYNGO_OK;
}

sbyngo_err_t sbyngo_verify_file(const char *path, const char *expected) {
    char actual[65];
    sbyngo_err_t err = sbyngo_sha256_file(path, actual);
    if (err != SBYNGO_OK) return err;
    if (strcmp(actual, expected) != 0) return SBYNGO_ERR_INTEGRITY;
    return SBYNGO_OK;
}

/* ---- Audit ---- */

struct sbyngo_audit_s {
    char  path[4096];
    char  prev_hash[65];
};

sbyngo_audit_t *sbyngo_audit_open(const char *path) {
    sbyngo_audit_t *a = calloc(1, sizeof(sbyngo_audit_t));
    if (!a) return NULL;
    strncpy(a->path, path, sizeof(a->path) - 1);
    memset(a->prev_hash, '0', 64);
    a->prev_hash[64] = '\0';
    return a;
}

sbyngo_err_t sbyngo_audit_write(sbyngo_audit_t *audit,
                                 const char *event_type,
                                 const char *data_json) {
    if (!audit || !event_type || !data_json) return SBYNGO_ERR_INVAL;

    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    /* Build entry JSON */
    char entry[8192];
    snprintf(entry, sizeof(entry),
        "{\"timestamp\":\"%s\",\"event_type\":\"%s\",\"data\":%s,\"prev_hash\":\"%s\"}",
        ts, event_type, data_json, audit->prev_hash
    );

    /* Compute chain hash */
    char to_hash[9216];
    snprintf(to_hash, sizeof(to_hash), "%s%s", audit->prev_hash, entry);

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, to_hash, strlen(to_hash));
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);

    char chain_hash[65];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(chain_hash + i*2, "%02x", hash[i]);
    chain_hash[64] = '\0';
    memcpy(audit->prev_hash, chain_hash, 65);

    /* Write to file */
    FILE *f = fopen(audit->path, "a");
    if (!f) return SBYNGO_ERR_IO;
    char final_entry[8320];
    /* Insert chain_hash before closing brace */
    size_t len = strlen(entry);
    entry[len-1] = '\0'; /* Remove closing } */
    snprintf(final_entry, sizeof(final_entry),
        "%s,\"chain_hash\":\"%s\"}\n", entry, chain_hash);
    fputs(final_entry, f);
    fclose(f);
    return SBYNGO_OK;
}

void sbyngo_audit_close(sbyngo_audit_t *audit) {
    free(audit);
}

/* ---- Utilities ---- */

const char *sbyngo_strerror(sbyngo_err_t err) {
    switch (err) {
        case SBYNGO_OK:             return "OK";
        case SBYNGO_ERR_IO:         return "I/O error";
        case SBYNGO_ERR_NOT_FOUND:  return "Not found";
        case SBYNGO_ERR_MISMATCH:   return "NodeID mismatch";
        case SBYNGO_ERR_PERM:       return "Permission denied";
        case SBYNGO_ERR_INVAL:      return "Invalid argument";
        case SBYNGO_ERR_INTEGRITY:  return "Integrity violation";
        case SBYNGO_ERR_NOMEM:      return "Out of memory";
        default:                    return "Unknown error";
    }
}
