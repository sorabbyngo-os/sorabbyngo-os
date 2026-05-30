/*
 * libsorabbyngo — Sorabbyngo C library
 * Low-level system interface for kernel modules and C components.
 *
 * Copyright (c) 2026 Sorabbyngo Labs, Kisii, Kenya
 * SPDX-License-Identifier: MIT
 */

#ifndef SORABBYNGO_H
#define SORABBYNGO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Version ---- */
#define SORABBYNGO_VERSION_MAJOR 0
#define SORABBYNGO_VERSION_MINOR 1
#define SORABBYNGO_VERSION_PATCH 0
#define SORABBYNGO_VERSION "0.1.0"

/* ---- Error codes ---- */
typedef enum {
    SBYNGO_OK               =  0,
    SBYNGO_ERR_IO           = -1,
    SBYNGO_ERR_NOT_FOUND    = -2,
    SBYNGO_ERR_MISMATCH     = -3,
    SBYNGO_ERR_PERM         = -4,
    SBYNGO_ERR_INVAL        = -5,
    SBYNGO_ERR_INTEGRITY    = -6,
    SBYNGO_ERR_NOMEM        = -7,
} sbyngo_err_t;

/* ---- NodeID ---- */
#define SBYNGO_NODEID_LEN 64

typedef struct {
    char id[SBYNGO_NODEID_LEN + 1];
    bool sealed;
    bool verified;
} sbyngo_nodeid_t;

/**
 * Load NodeID from sealed storage or environment.
 * @param out  Output NodeID struct
 * @return     SBYNGO_OK on success, error code otherwise
 */
sbyngo_err_t sbyngo_nodeid_load(sbyngo_nodeid_t *out);

/**
 * Verify live kernel NodeID against sealed NodeID.
 * @param nodeid  Loaded NodeID to verify
 * @return        SBYNGO_OK if match, SBYNGO_ERR_MISMATCH if tampered
 */
sbyngo_err_t sbyngo_nodeid_verify(const sbyngo_nodeid_t *nodeid);

/**
 * Seal NodeID to persistent storage.
 * @param nodeid  NodeID to seal
 * @return        SBYNGO_OK on success
 */
sbyngo_err_t sbyngo_nodeid_seal(const sbyngo_nodeid_t *nodeid);

/* ---- Hardening ---- */

/**
 * Apply PR_SET_NO_NEW_PRIVS to current process.
 * @return SBYNGO_OK on success
 */
sbyngo_err_t sbyngo_harden_no_new_privs(void);

/**
 * Disable core dumps for current process.
 * @return SBYNGO_OK on success
 */
sbyngo_err_t sbyngo_harden_no_core_dumps(void);

/**
 * Set memory limit for current process.
 * @param bytes  Maximum virtual memory in bytes
 * @return       SBYNGO_OK on success
 */
sbyngo_err_t sbyngo_harden_memory_limit(uint64_t bytes);

/* ---- Integrity ---- */

/**
 * Compute SHA-256 hash of a file.
 * @param path      File path
 * @param out_hex   Output buffer (65 bytes minimum)
 * @return          SBYNGO_OK on success
 */
sbyngo_err_t sbyngo_sha256_file(const char *path, char *out_hex);

/**
 * Verify file against expected SHA-256.
 * @param path      File path
 * @param expected  Expected hex hash
 * @return          SBYNGO_OK if match, SBYNGO_ERR_INTEGRITY if mismatch
 */
sbyngo_err_t sbyngo_verify_file(const char *path, const char *expected);

/* ---- Audit ---- */

typedef struct sbyngo_audit_s sbyngo_audit_t;

/**
 * Open an audit ledger at the given path.
 * @param path  Path to JSONL audit file
 * @return      Audit handle or NULL on failure
 */
sbyngo_audit_t *sbyngo_audit_open(const char *path);

/**
 * Write an event to the audit ledger.
 * @param audit       Audit handle
 * @param event_type  Event type string
 * @param data_json   JSON string of event data
 * @return            SBYNGO_OK on success
 */
sbyngo_err_t sbyngo_audit_write(sbyngo_audit_t *audit,
                                 const char *event_type,
                                 const char *data_json);

/**
 * Close and free the audit handle.
 * @param audit  Audit handle
 */
void sbyngo_audit_close(sbyngo_audit_t *audit);

/* ---- Utilities ---- */

/**
 * Return string description of error code.
 */
const char *sbyngo_strerror(sbyngo_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* SORABBYNGO_H */
