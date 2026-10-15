#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    EVP_PKEY *host_priv_key;     /* EC P-256 host private key */
    uint8_t *host_cert;          /* Host certificate bytes (184 bytes) */
    size_t host_cert_len;
    uint8_t *server_ecdh_pub;    /* Device ECDH public key (65 bytes uncompressed: 0x04 || X || Y) */
    size_t server_ecdh_pub_len;

    /* Ephemeral session EC key */
    EVP_PKEY *session_key;
    uint8_t client_random[32];
    uint8_t server_random[32];

    /* Handshake transcript SHA-256 context */
    EVP_MD_CTX *transcript_ctx;

    /* Derived session secrets */
    uint8_t master_secret[48];
    uint8_t client_mac_key[32];
    uint8_t server_mac_key[32];
    uint8_t client_enc_key[32];
    uint8_t server_enc_key[32];

    bool secure_tx;
    bool secure_rx;
    bool finished_verified;
    bool close_notify_seen;
} SynaTlsContext;

/**
 * Standard TLS 1.2 PRF (RFC 5246, Section 5) using HMAC-SHA256.
 */
int syna_tls_prf(const uint8_t *secret, size_t secret_len,
                 const char *label,
                 const uint8_t *seed, size_t seed_len,
                 uint8_t *out, size_t out_len);

/**
 * Initialize TLS context with credentials.
 */
int syna_tls_init(SynaTlsContext *ctx,
                  const char *host_key_pem,
                  const uint8_t *host_cert, size_t host_cert_len,
                  const uint8_t *server_ecdh_pub, size_t server_ecdh_pub_len);

/**
 * Free resources in TLS context.
 */
void syna_tls_free(SynaTlsContext *ctx);

/**
 * Build ClientHello packet (78 bytes + 4-byte 0x44 prefix).
 */
int syna_tls_build_client_hello(SynaTlsContext *ctx, uint8_t *out, size_t *out_len);

/**
 * Process ServerHello, CertificateRequest, and ServerHelloDone response.
 */
int syna_tls_handle_server_hello(SynaTlsContext *ctx, const uint8_t *in, size_t in_len);

/**
 * Build client authentication flight (Certificate, ClientKeyExchange,
 * CertificateVerify, ChangeCipherSpec, and Finished).
 */
int syna_tls_build_flight2(SynaTlsContext *ctx, uint8_t *out, size_t *out_len);

/**
 * Process server ChangeCipherSpec and Finished messages.
 */
int syna_tls_handle_flight2_reply(SynaTlsContext *ctx, const uint8_t *in, size_t in_len);

/**
 * Encrypt arbitrary record type (0x15 alert, 0x16 handshake, 0x17 app data) into a TLS 1.2 record.
 */
int syna_tls_encrypt_record(SynaTlsContext *ctx,
                            uint8_t record_type,
                            const uint8_t *plain, size_t plain_len,
                            uint8_t *out, size_t *out_len);

/**
 * Encrypt application data into a TLS 1.2 record (AES-256-CBC + HMAC-SHA256).
 */
int syna_tls_encrypt_app_data(SynaTlsContext *ctx,
                              const uint8_t *plain, size_t plain_len,
                              uint8_t *out, size_t *out_len);

/**
 * Decrypt a TLS 1.2 application data record and verify HMAC.
 */
int syna_tls_decrypt_app_data(SynaTlsContext *ctx,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t max_out_len,
                              size_t *out_len);

/**
 * Build encrypted close_notify alert record (0x15).
 */
int syna_tls_build_close_notify(SynaTlsContext *ctx, uint8_t *out, size_t *out_len);

#ifdef __cplusplus
}
#endif
