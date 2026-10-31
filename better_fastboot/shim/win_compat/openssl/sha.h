/*
 * openssl/sha.h shim for the Windows (mingw) build — MeowArchMobile glue.
 *
 * liblp's utility.cpp is the only consumer in the fastboot chain and uses
 * exactly SHA256_CTX / SHA256_Init / SHA256_Update / SHA256_Final. On Linux
 * these come from the system OpenSSL; for Windows we back them with libavb's
 * dependency-free SHA256 (already compiled for fastboot's AVB footer work),
 * producing byte-identical digests. This is NOT a general OpenSSL shim.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <libavb/avb_sha.h>

#define SHA256_DIGEST_LENGTH 32

typedef AvbSHA256Ctx SHA256_CTX;

#ifdef __cplusplus
extern "C" {
#endif

static inline int SHA256_Init(SHA256_CTX* c) {
    avb_sha256_init(c);
    return 1;
}
static inline int SHA256_Update(SHA256_CTX* c, const void* data, size_t len) {
    avb_sha256_update(c, (const uint8_t*)data, len);
    return 1;
}
static inline int SHA256_Final(unsigned char* md, SHA256_CTX* c) {
    uint8_t* digest = avb_sha256_final(c);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) md[i] = digest[i];
    return 1;
}

#ifdef __cplusplus
}
#endif
