/**
 * identity.c — libsb_identity implementation
 *
 * NodeID derivation strategy:
 *   1. Try /proc/sorabbyngo/nodeid  (Sorabbyngo kernel)
 *   2. Fall back: SHA-256(CPUID_string + primary_MAC)  (any Linux)
 *
 * Ed25519 keypair is seeded deterministically from the NodeID bytes
 * using libsodium's crypto_sign_seed_keypair(). Same NodeID always
 * produces the same keypair — no key storage needed.
 *
 * Sorabbyngo Labs — Kisii, Kenya
 */

#define _GNU_SOURCE
#include "libsorabbyngo/identity.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>

/* Linux-specific includes for MAC + CPUID fallback */
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

/* ── Internal struct ────────────────────────────────────────────────────── */

struct sb_identity_ctx {
    uint8_t nodeid[SB_NODEID_BYTES];          /* raw 32-byte SHA-256 NodeID  */
    char    nodeid_hex[SB_NODEID_LEN + 1];    /* hex-encoded, null-terminated */
    uint8_t pubkey[SB_ED25519_PUBKEY_LEN];    /* Ed25519 public key           */
    uint8_t seckey[SB_ED25519_SECKEY_LEN];    /* Ed25519 secret key           */
};

/* ── Internal helpers ───────────────────────────────────────────────────── */

static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[len * 2] = '\0';
}

/**
 * Try to read NodeID from the Sorabbyngo kernel proc entry.
 * Returns SB_OK and fills nodeid_bytes on success.
 */
static int read_kernel_nodeid(uint8_t nodeid_bytes[SB_NODEID_BYTES])
{
    FILE *f = fopen("/proc/sorabbyngo/nodeid", "r");
    if (!f) return SB_ERR_IO;

    char hex[SB_NODEID_LEN + 2]; /* +2: newline + null */
    if (!fgets(hex, sizeof(hex), f)) {
        fclose(f);
        return SB_ERR_IO;
    }
    fclose(f);

    /* Strip trailing whitespace/newline */
    size_t len = strlen(hex);
    while (len > 0 && (hex[len-1] == '\n' || hex[len-1] == '\r' || hex[len-1] == ' '))
        hex[--len] = '\0';

    if (len != SB_NODEID_LEN) return SB_ERR_NODEID_UNAVAIL;

    /* Decode hex → bytes */
    for (int i = 0; i < SB_NODEID_BYTES; i++) {
        char byte_str[3] = { hex[i*2], hex[i*2+1], '\0' };
        nodeid_bytes[i] = (uint8_t)strtol(byte_str, NULL, 16);
    }
    return SB_OK;
}

/**
 * Get the CPUID string for this CPU (vendor + model name).
 * Writes into buf (caller provides, at least 128 bytes).
 */
static void get_cpuid_string(char *buf, size_t buflen)
{
    /* Read from /proc/cpuinfo — portable, no asm required */
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        snprintf(buf, buflen, "unknown-cpu");
        return;
    }

    char line[256];
    char vendor[64] = {0};
    char model[128] = {0};

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "vendor_id", 9) == 0 && !vendor[0]) {
            char *colon = strchr(line, ':');
            if (colon) {
                char *val = colon + 2;
                /* strip newline */
                val[strcspn(val, "\n")] = '\0';
                strncpy(vendor, val, sizeof(vendor) - 1);
            }
        }
        if (strncmp(line, "model name", 10) == 0 && !model[0]) {
            char *colon = strchr(line, ':');
            if (colon) {
                char *val = colon + 2;
                val[strcspn(val, "\n")] = '\0';
                strncpy(model, val, sizeof(model) - 1);
            }
        }
        if (vendor[0] && model[0]) break;
    }
    fclose(f);

    snprintf(buf, buflen, "%s|%s", vendor, model);
}

/**
 * Get the primary non-loopback MAC address.
 * Writes 17-char "aa:bb:cc:dd:ee:ff" string into buf.
 */
static int get_primary_mac(char *buf, size_t buflen)
{
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) {
        snprintf(buf, buflen, "00:00:00:00:00:00");
        return -1;
    }

    int found = 0;
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        /* Skip loopback */
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;
        if (!ifa->ifa_addr) continue;

        /* Open a socket to query hardware address via ioctl */
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) continue;

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);

        if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
            unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
            /* Skip all-zero MACs (virtual interfaces) */
            if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
                snprintf(buf, buflen, "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                found = 1;
                close(sock);
                break;
            }
        }
        close(sock);
    }

    freeifaddrs(ifap);

    if (!found) {
        snprintf(buf, buflen, "00:00:00:00:00:00");
        return -1;
    }
    return 0;
}

/**
 * Derive NodeID in userspace: SHA-256(cpuid_string + "|" + mac_string)
 * Mirrors the logic in the kernel NodeID driver.
 */
static int derive_userspace_nodeid(uint8_t nodeid_bytes[SB_NODEID_BYTES])
{
    char cpuid_str[256] = {0};
    char mac_str[32]    = {0};

    get_cpuid_string(cpuid_str, sizeof(cpuid_str));
    get_primary_mac(mac_str, sizeof(mac_str));

    /* Concatenate: "cpuid|mac" */
    char input[512] = {0};
    snprintf(input, sizeof(input), "%s|%s", cpuid_str, mac_str);

    /* SHA-256 via libsodium */
    crypto_hash_sha256(nodeid_bytes, (const uint8_t *)input, strlen(input));
    return SB_OK;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int sb_identity_init(sb_identity_ctx_t **ctx_out, sb_nodeid_source_t *source_out)
{
    if (!ctx_out) return SB_ERR_INVALID_ARG;

    /* libsodium must be initialised before any crypto call */
    if (sodium_init() < 0) return SB_ERR_CRYPTO;

    sb_identity_ctx_t *ctx = calloc(1, sizeof(sb_identity_ctx_t));
    if (!ctx) return SB_ERR_IO;

    sb_nodeid_source_t source = SB_NODEID_SRC_KERNEL;
    int rc = read_kernel_nodeid(ctx->nodeid);
    if (rc != SB_OK) {
        /* Kernel driver not present — fall back to userspace derivation */
        source = SB_NODEID_SRC_USERSPACE;
        rc = derive_userspace_nodeid(ctx->nodeid);
        if (rc != SB_OK) {
            free(ctx);
            return rc;
        }
    }

    /* Build hex string */
    bytes_to_hex(ctx->nodeid, SB_NODEID_BYTES, ctx->nodeid_hex);

    /* Derive Ed25519 keypair from NodeID as seed.
     * Same NodeID → same keypair every time. No key storage needed. */
    if (crypto_sign_seed_keypair(ctx->pubkey, ctx->seckey, ctx->nodeid) != 0) {
        sodium_memzero(ctx, sizeof(*ctx));
        free(ctx);
        return SB_ERR_CRYPTO;
    }

    if (source_out) *source_out = source;
    *ctx_out = ctx;
    return SB_OK;
}

void sb_identity_free(sb_identity_ctx_t *ctx)
{
    if (!ctx) return;
    sodium_memzero(ctx, sizeof(*ctx)); /* zero key material */
    free(ctx);
}

int sb_identity_get_nodeid(const sb_identity_ctx_t *ctx,
                            char *buf, size_t buflen)
{
    if (!ctx || !buf) return SB_ERR_INVALID_ARG;
    if (buflen < SB_NODEID_LEN + 1) return SB_ERR_BUFFER_TOO_SMALL;
    memcpy(buf, ctx->nodeid_hex, SB_NODEID_LEN + 1);
    return SB_OK;
}

int sb_identity_get_nodeid_bytes(const sb_identity_ctx_t *ctx,
                                  uint8_t out[SB_NODEID_BYTES])
{
    if (!ctx || !out) return SB_ERR_INVALID_ARG;
    memcpy(out, ctx->nodeid, SB_NODEID_BYTES);
    return SB_OK;
}

int sb_identity_get_pubkey(const sb_identity_ctx_t *ctx,
                            uint8_t out[SB_ED25519_PUBKEY_LEN])
{
    if (!ctx || !out) return SB_ERR_INVALID_ARG;
    memcpy(out, ctx->pubkey, SB_ED25519_PUBKEY_LEN);
    return SB_OK;
}

int sb_identity_sign(const sb_identity_ctx_t *ctx,
                     const uint8_t *payload, size_t payload_len,
                     uint8_t sig[SB_ED25519_SIG_LEN])
{
    if (!ctx || !payload || !sig) return SB_ERR_INVALID_ARG;

    unsigned long long siglen = 0;

    /* crypto_sign_detached: sign without attaching message */
    if (crypto_sign_detached(sig, &siglen, payload, payload_len, ctx->seckey) != 0)
        return SB_ERR_CRYPTO;

    return SB_OK;
}

int sb_identity_verify_sig(const uint8_t *payload, size_t payload_len,
                            const uint8_t sig[SB_ED25519_SIG_LEN],
                            const uint8_t peer_pubkey[SB_ED25519_PUBKEY_LEN])
{
    if (!payload || !sig || !peer_pubkey) return SB_ERR_INVALID_ARG;

    if (crypto_sign_verify_detached(sig, payload, payload_len, peer_pubkey) != 0)
        return SB_ERR_CRYPTO; /* invalid signature */

    return SB_OK;
}

int sb_identity_verify_peer(const char *nodeid_hex)
{
    if (!nodeid_hex) return SB_ERR_INVALID_ARG;
    if (strlen(nodeid_hex) != SB_NODEID_LEN) return SB_ERR_INVALID_ARG;

    int all_zero = 1;
    for (int i = 0; i < SB_NODEID_LEN; i++) {
        if (!isxdigit((unsigned char)nodeid_hex[i]))
            return SB_ERR_INVALID_ARG;
        if (nodeid_hex[i] != '0')
            all_zero = 0;
    }

    /* All-zero NodeID is invalid */
    if (all_zero) return SB_ERR_INVALID_ARG;

    return SB_OK;
}

int sb_identity_short(const sb_identity_ctx_t *ctx, char buf[9])
{
    if (!ctx || !buf) return SB_ERR_INVALID_ARG;
    memcpy(buf, ctx->nodeid_hex, 8);
    buf[8] = '\0';
    return SB_OK;
}

const char *sb_identity_strerror(int err)
{
    switch (err) {
        case SB_OK:                   return "OK";
        case SB_ERR_NODEID_UNAVAIL:   return "NodeID unavailable";
        case SB_ERR_CRYPTO:           return "Cryptographic operation failed";
        case SB_ERR_INVALID_ARG:      return "Invalid argument";
        case SB_ERR_BUFFER_TOO_SMALL: return "Buffer too small";
        case SB_ERR_IO:               return "I/O error";
        default:                      return "Unknown error";
    }
}
