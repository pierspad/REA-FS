#include "syna_tls.h"
#include "syna_proto.h"
#include <string.h>
#include <stdlib.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>

static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u24_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static uint16_t get_u16_be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t get_u24_be(const uint8_t *p)
{
    return (uint32_t)((p[0] << 16) | (p[1] << 8) | p[2]);
}

int syna_tls_prf(const uint8_t *secret, size_t secret_len,
                 const char *label,
                 const uint8_t *seed, size_t seed_len,
                 uint8_t *out, size_t out_len)
{
    size_t label_len = strlen(label);
    size_t full_seed_len = label_len + seed_len;
    uint8_t *full_seed = malloc(full_seed_len);
    if (!full_seed) return -1;

    memcpy(full_seed, label, label_len);
    memcpy(full_seed + label_len, seed, seed_len);

    uint8_t a[SHA256_DIGEST_LENGTH];
    unsigned int a_len = 0;
    HMAC(EVP_sha256(), secret, secret_len, full_seed, full_seed_len, a, &a_len);

    size_t generated = 0;
    while (generated < out_len) {
        size_t chunk_in_len = sizeof(a) + full_seed_len;
        uint8_t *chunk_in = malloc(chunk_in_len);
        if (!chunk_in) {
            free(full_seed);
            return -2;
        }

        memcpy(chunk_in, a, sizeof(a));
        memcpy(chunk_in + sizeof(a), full_seed, full_seed_len);

        uint8_t chunk_out[SHA256_DIGEST_LENGTH];
        unsigned int chunk_out_len = 0;
        HMAC(EVP_sha256(), secret, secret_len, chunk_in, chunk_in_len, chunk_out, &chunk_out_len);
        free(chunk_in);

        size_t to_copy = (out_len - generated < chunk_out_len) ? (out_len - generated) : chunk_out_len;
        memcpy(out + generated, chunk_out, to_copy);
        generated += to_copy;

        /* A(i+1) = HMAC(secret, A(i)) */
        HMAC(EVP_sha256(), secret, secret_len, a, sizeof(a), a, &a_len);
    }

    free(full_seed);
    return 0;
}

int syna_tls_init(SynaTlsContext *ctx,
                  const char *host_key_pem,
                  const uint8_t *host_cert, size_t host_cert_len,
                  const uint8_t *server_ecdh_pub, size_t server_ecdh_pub_len)
{
    if (!ctx || !host_key_pem || !host_cert || !server_ecdh_pub)
        return -1;

    memset(ctx, 0, sizeof(*ctx));

    BIO *bio = BIO_new_mem_buf(host_key_pem, -1);
    if (!bio) return -2;

    ctx->host_priv_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!ctx->host_priv_key) return -3;

    ctx->host_cert = malloc(host_cert_len);
    if (!ctx->host_cert) {
        EVP_PKEY_free(ctx->host_priv_key);
        return -4;
    }
    memcpy(ctx->host_cert, host_cert, host_cert_len);
    ctx->host_cert_len = host_cert_len;

    ctx->server_ecdh_pub = malloc(server_ecdh_pub_len);
    if (!ctx->server_ecdh_pub) {
        free(ctx->host_cert);
        EVP_PKEY_free(ctx->host_priv_key);
        return -5;
    }
    memcpy(ctx->server_ecdh_pub, server_ecdh_pub, server_ecdh_pub_len);
    ctx->server_ecdh_pub_len = server_ecdh_pub_len;

    ctx->transcript_ctx = EVP_MD_CTX_new();
    if (!ctx->transcript_ctx) {
        syna_tls_free(ctx);
        return -6;
    }
    EVP_DigestInit_ex(ctx->transcript_ctx, EVP_sha256(), NULL);

    return 0;
}

void syna_tls_free(SynaTlsContext *ctx)
{
    if (!ctx) return;

    if (ctx->host_priv_key) {
        EVP_PKEY_free(ctx->host_priv_key);
        ctx->host_priv_key = NULL;
    }
    if (ctx->session_key) {
        EVP_PKEY_free(ctx->session_key);
        ctx->session_key = NULL;
    }
    if (ctx->transcript_ctx) {
        EVP_MD_CTX_free(ctx->transcript_ctx);
        ctx->transcript_ctx = NULL;
    }
    if (ctx->host_cert) {
        free(ctx->host_cert);
        ctx->host_cert = NULL;
    }
    if (ctx->server_ecdh_pub) {
        free(ctx->server_ecdh_pub);
        ctx->server_ecdh_pub = NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
}

int syna_tls_build_client_hello(SynaTlsContext *ctx, uint8_t *out, size_t *out_len)
{
    if (!ctx || !out || !out_len) return -1;

    RAND_bytes(ctx->client_random, 32);

    /* 4-byte 0x44 prefix */
    out[0] = 0x44;
    out[1] = 0x00;
    out[2] = 0x00;
    out[3] = 0x00;

    /* Handshake Record Header: Type 0x16, Version 0x0303 */
    uint8_t *rec = out + 4;
    rec[0] = 0x16;
    rec[1] = 0x03;
    rec[2] = 0x03;

    /* ClientHello Handshake Message: Type 0x01 */
    uint8_t *hs = rec + 5;
    hs[0] = 0x01;

    uint8_t *body = hs + 4;
    body[0] = 0x03;
    body[1] = 0x03;
    memcpy(body + 2, ctx->client_random, 32);

    /* Session ID: 7 bytes zeros */
    body[34] = 0x07;
    memset(body + 35, 0, 7);

    /* Cipher suites: length 6 bytes (c005, 003d, 008d) */
    put_u16_be(body + 42, 6);
    body[44] = 0xc0; body[45] = 0x05;
    body[46] = 0x00; body[47] = 0x3d;
    body[48] = 0x00; body[49] = 0x8d;

    /* Compression method: OEM single 0x00 byte */
    body[50] = 0x00;

    /* Extensions: length 10 (0x000a) followed by 12 bytes */
    put_u16_be(body + 51, 10);
    const uint8_t oem_ext[12] = { 0x00, 0x04, 0x00, 0x02, 0x00, 0x17, 0x00, 0x0b, 0x00, 0x02, 0x01, 0x00 };
    memcpy(body + 53, oem_ext, 12);

    size_t body_len = 65;
    put_u24_be(hs + 1, (uint32_t)body_len);

    size_t hs_len = 4 + body_len;
    put_u16_be(rec + 3, (uint16_t)hs_len);

    /* Add ClientHello handshake message (hs) to transcript hash */
    EVP_DigestUpdate(ctx->transcript_ctx, hs, hs_len);

    *out_len = 4 + 5 + hs_len; /* 78 bytes exactly */
    return 0;
}

int syna_tls_handle_server_hello(SynaTlsContext *ctx, const uint8_t *in, size_t in_len)
{
    if (!ctx || !in || in_len < 5) return -1;

    if (in[0] != 0x16 || in[1] != 0x03 || in[2] != 0x03)
        return -2;

    uint16_t rec_len = get_u16_be(in + 3);
    if (in_len < (size_t)(5 + rec_len)) return -3;

    const uint8_t *hs = in + 5;
    size_t remaining = rec_len;

    /* Add ServerHandshake messages to transcript */
    EVP_DigestUpdate(ctx->transcript_ctx, hs, remaining);

    while (remaining >= 4) {
        uint8_t type = hs[0];
        uint32_t len = get_u24_be(hs + 1);
        if (remaining < 4 + len) return -4;

        if (type == 0x02) { /* ServerHello */
            if (len < 38) return -5;
            if (hs[4] != 0x03 || hs[5] != 0x03) return -6;
            memcpy(ctx->server_random, hs + 6, 32);
        }
        hs += 4 + len;
        remaining -= (4 + len);
    }

    return 0;
}

int syna_tls_build_flight2(SynaTlsContext *ctx, uint8_t *out, size_t *out_len)
{
    if (!ctx || !out || !out_len) return -1;

    /* Generate ephemeral session key P-256 */
    EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!kctx) return -2;
    EVP_PKEY_keygen_init(kctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1);
    EVP_PKEY_keygen(kctx, &ctx->session_key);
    EVP_PKEY_CTX_free(kctx);
    if (!ctx->session_key) return -3;

    /* Derive ECDH pre-master secret using server ECDH pubkey */
    EC_KEY *peer_ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    const uint8_t *p = ctx->server_ecdh_pub;
    o2i_ECPublicKey(&peer_ec, &p, ctx->server_ecdh_pub_len);
    EVP_PKEY *peer_pkey = EVP_PKEY_new();
    EVP_PKEY_set1_EC_KEY(peer_pkey, peer_ec);
    EC_KEY_free(peer_ec);

    EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(ctx->session_key, NULL);
    EVP_PKEY_derive_init(dctx);
    EVP_PKEY_derive_set_peer(dctx, peer_pkey);
    size_t secret_len = 0;
    EVP_PKEY_derive(dctx, NULL, &secret_len);
    uint8_t *pre_master = malloc(secret_len);
    EVP_PKEY_derive(dctx, pre_master, &secret_len);
    EVP_PKEY_CTX_free(dctx);
    EVP_PKEY_free(peer_pkey);

    /* Master secret */
    uint8_t seed[64];
    memcpy(seed, ctx->client_random, 32);
    memcpy(seed + 32, ctx->server_random, 32);
    syna_tls_prf(pre_master, secret_len, "master secret", seed, 64, ctx->master_secret, 48);
    free(pre_master);

    /* Key expansion: 288 bytes */
    uint8_t key_block[288];
    uint8_t exp_seed[64];
    memcpy(exp_seed, ctx->client_random, 32);
    memcpy(exp_seed + 32, ctx->server_random, 32);
    syna_tls_prf(ctx->master_secret, 48, "key expansion", exp_seed, 64, key_block, 288);

    memcpy(ctx->client_mac_key, key_block, 32);
    memcpy(ctx->server_mac_key, key_block + 32, 32);
    memcpy(ctx->client_enc_key, key_block + 64, 32);
    memcpy(ctx->server_enc_key, key_block + 96, 32);

    /* Build Flight 2 buffer */
    out[0] = 0x44; out[1] = 0; out[2] = 0; out[3] = 0;
    uint8_t *buf = out + 4;
    size_t pos = 0;

    /* 1. Handshake record (Certificate + ClientKeyExchange + CertificateVerify) */
    buf[pos++] = 0x16; buf[pos++] = 0x03; buf[pos++] = 0x03;
    size_t hs_rec_len_pos = pos;
    pos += 2; /* length placeholder */

    size_t hs_content_start = pos;

    /* Certificate (Type 0x0b) */
    buf[pos++] = 0x0b;
    size_t cert_len_pos = pos;
    pos += 3;

    /* Validity OEM specific certificate wrapper */
    buf[pos++] = 0; put_u16_be(buf + pos, (uint16_t)ctx->host_cert_len); pos += 2;
    buf[pos++] = 0; put_u16_be(buf + pos, (uint16_t)ctx->host_cert_len); pos += 2;
    buf[pos++] = 0xac; buf[pos++] = 0x16;
    memcpy(buf + pos, ctx->host_cert, ctx->host_cert_len); pos += ctx->host_cert_len;
    put_u24_be(buf + cert_len_pos, (uint32_t)(pos - (cert_len_pos + 3)));

    /* ClientKeyExchange (Type 0x10) */
    buf[pos++] = 0x10;
    size_t kex_len_pos = pos;
    pos += 3;
    /* Point 65 bytes (0x04 || X || Y) */
    const EC_KEY *my_ec = EVP_PKEY_get0_EC_KEY(ctx->session_key);
    const EC_POINT *point = EC_KEY_get0_public_key(my_ec);
    const EC_GROUP *group = EC_KEY_get0_group(my_ec);
    uint8_t point_buf[65];
    EC_POINT_point2oct(group, point, POINT_CONVERSION_UNCOMPRESSED, point_buf, 65, NULL);
    memcpy(buf + pos, point_buf, 65); pos += 65;
    put_u24_be(buf + kex_len_pos, 65);

    /* Update transcript before CertificateVerify */
    EVP_DigestUpdate(ctx->transcript_ctx, buf + hs_content_start, pos - hs_content_start);

    /* CertificateVerify (Type 0x0f): ECDSA signature over transcript SHA256 */
    EVP_MD_CTX *t_copy = EVP_MD_CTX_new();
    EVP_MD_CTX_copy_ex(t_copy, ctx->transcript_ctx);
    uint8_t transcript_hash[SHA256_DIGEST_LENGTH];
    unsigned int thash_len = 0;
    EVP_DigestFinal_ex(t_copy, transcript_hash, &thash_len);
    EVP_MD_CTX_free(t_copy);

    buf[pos++] = 0x0f;
    size_t cv_len_pos = pos;
    pos += 3;
    unsigned int sig_len = 0;
    const EC_KEY *host_ec = EVP_PKEY_get0_EC_KEY(ctx->host_priv_key);
    ECDSA_sign(0, transcript_hash, thash_len, buf + pos, &sig_len, (EC_KEY *)host_ec);
    put_u24_be(buf + cv_len_pos, sig_len);

    /* Add CertificateVerify to transcript */
    EVP_DigestUpdate(ctx->transcript_ctx, buf + (cv_len_pos - 1), 4 + sig_len);
    pos += sig_len;

    put_u16_be(buf + hs_rec_len_pos, (uint16_t)(pos - hs_content_start));

    /* 2. ChangeCipherSpec: 14 03 03 00 01 01 */
    buf[pos++] = 0x14; buf[pos++] = 0x03; buf[pos++] = 0x03;
    buf[pos++] = 0x00; buf[pos++] = 0x01; buf[pos++] = 0x01;

    /* 3. Encrypted Finished Message */
    EVP_MD_CTX *fin_copy = EVP_MD_CTX_new();
    EVP_MD_CTX_copy_ex(fin_copy, ctx->transcript_ctx);
    uint8_t fin_hash[SHA256_DIGEST_LENGTH];
    unsigned int fin_hash_len = 0;
    EVP_DigestFinal_ex(fin_copy, fin_hash, &fin_hash_len);
    EVP_MD_CTX_free(fin_copy);

    uint8_t fin_data[12];
    syna_tls_prf(ctx->master_secret, 48, "client finished", fin_hash, fin_hash_len, fin_data, 12);

    /* Finished Handshake payload: Type 0x14, length 12 */
    uint8_t fin_hs[16];
    fin_hs[0] = 0x14;
    put_u24_be(fin_hs + 1, 12);
    memcpy(fin_hs + 4, fin_data, 12);

    /* Encrypt record type 22 (0x16 Handshake) */
    uint8_t enc_fin[128];
    size_t enc_fin_len = 0;
    ctx->secure_tx = true;
    syna_tls_encrypt_record(ctx, 0x16, fin_hs, 16, enc_fin, &enc_fin_len);
    memcpy(buf + pos, enc_fin, enc_fin_len);
    pos += enc_fin_len;

    *out_len = 4 + pos;
    return 0;
}

int syna_tls_handle_flight2_reply(SynaTlsContext *ctx, const uint8_t *in, size_t in_len)
{
    if (!ctx || !in || in_len < 6) return -1;

    /* Verify ChangeCipherSpec (14 03 03 00 01 01) */
    if (in[0] != 0x14 || in[5] != 0x01) return -2;

    const uint8_t *fin_rec = in + 6;
    size_t fin_len = in_len - 6;
    if (fin_len < 5) return -3;

    uint8_t dec[128];
    size_t dec_len = 0;
    ctx->secure_rx = true;
    if (syna_tls_decrypt_app_data(ctx, fin_rec, fin_len, dec, sizeof(dec), &dec_len) != 0)
        return -4;

    if (dec_len < 16 || dec[0] != 0x14) return -5;

    /* Verify server finished verify_data */
    EVP_MD_CTX *srv_copy = EVP_MD_CTX_new();
    EVP_MD_CTX_copy_ex(srv_copy, ctx->transcript_ctx);
    uint8_t srv_hash[SHA256_DIGEST_LENGTH];
    unsigned int srv_hash_len = 0;
    EVP_DigestFinal_ex(srv_copy, srv_hash, &srv_hash_len);
    EVP_MD_CTX_free(srv_copy);

    uint8_t expected_fin[12];
    syna_tls_prf(ctx->master_secret, 48, "server finished", srv_hash, srv_hash_len, expected_fin, 12);
    if (CRYPTO_memcmp(dec + 4, expected_fin, 12) != 0) return -6;

    ctx->finished_verified = true;
    return 0;
}

int syna_tls_encrypt_record(SynaTlsContext *ctx,
                            uint8_t record_type,
                            const uint8_t *plain, size_t plain_len,
                            uint8_t *out, size_t *out_len)
{
    if (!ctx || !plain || !out || !out_len) return -1;

    /* TLS MAC: HMAC(type || 0x0303 || len || plain) */
    uint8_t hdr[5] = { record_type, 0x03, 0x03, (uint8_t)(plain_len >> 8), (uint8_t)plain_len };
    uint8_t mac[SHA256_DIGEST_LENGTH];
    unsigned int mac_len = 0;

    HMAC_CTX *hctx = HMAC_CTX_new();
    HMAC_Init_ex(hctx, ctx->client_mac_key, 32, EVP_sha256(), NULL);
    HMAC_Update(hctx, hdr, 5);
    HMAC_Update(hctx, plain, plain_len);
    HMAC_Final(hctx, mac, &mac_len);
    HMAC_CTX_free(hctx);

    size_t payload_len = plain_len + 32;
    uint8_t *payload = malloc(payload_len + 16);
    memcpy(payload, plain, plain_len);
    memcpy(payload + plain_len, mac, 32);

    /* PKCS#7 / TLS padding */
    size_t pad_len = 16 - (payload_len % 16);
    for (size_t i = 0; i < pad_len; i++) {
        payload[payload_len + i] = (uint8_t)(pad_len - 1);
    }
    payload_len += pad_len;

    /* AES-128-CBC encryption */
    uint8_t iv[16];
    RAND_bytes(iv, 16);

    out[0] = record_type;
    out[1] = 0x03;
    out[2] = 0x03;
    put_u16_be(out + 3, (uint16_t)(16 + payload_len));
    memcpy(out + 5, iv, 16);

    EVP_CIPHER_CTX *cctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(cctx, EVP_aes_256_cbc(), NULL, ctx->client_enc_key, iv);
    EVP_CIPHER_CTX_set_padding(cctx, 0);

    int outl1 = 0, outl2 = 0;
    EVP_EncryptUpdate(cctx, out + 21, &outl1, payload, payload_len);
    EVP_EncryptFinal_ex(cctx, out + 21 + outl1, &outl2);
    EVP_CIPHER_CTX_free(cctx);
    free(payload);

    *out_len = 21 + outl1 + outl2;
    return 0;
}

int syna_tls_encrypt_app_data(SynaTlsContext *ctx,
                              const uint8_t *plain, size_t plain_len,
                              uint8_t *out, size_t *out_len)
{
    return syna_tls_encrypt_record(ctx, 0x17, plain, plain_len, out, out_len);
}

int syna_tls_decrypt_app_data(SynaTlsContext *ctx,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t max_out_len,
                              size_t *out_len)
{
    if (!ctx || !in || in_len < 5 + 16 + 32 + 16) return -1;

    uint8_t type = in[0];
    uint16_t rec_len = get_u16_be(in + 3);
    if (in_len < (size_t)(5 + rec_len)) return -2;

    const uint8_t *iv = in + 5;
    const uint8_t *cipher = in + 21;
    size_t cipher_len = rec_len - 16;
    if (cipher_len % 16 != 0) return -3;

    uint8_t *plain = malloc(cipher_len);
    EVP_CIPHER_CTX *cctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(cctx, EVP_aes_256_cbc(), NULL, ctx->server_enc_key, iv);
    EVP_CIPHER_CTX_set_padding(cctx, 0);

    int outl1 = 0, outl2 = 0;
    EVP_DecryptUpdate(cctx, plain, &outl1, cipher, cipher_len);
    EVP_DecryptFinal_ex(cctx, plain + outl1, &outl2);
    EVP_CIPHER_CTX_free(cctx);

    size_t dec_total = outl1 + outl2;
    if (dec_total < 33) {
        free(plain);
        return -4;
    }

    uint8_t pad_val = plain[dec_total - 1];
    size_t pad_count = pad_val + 1;
    if (pad_count > 16 || pad_count > dec_total) {
        free(plain);
        return -5;
    }

    size_t content_and_mac = dec_total - pad_count;
    if (content_and_mac < 32) {
        free(plain);
        return -6;
    }

    size_t content_len = content_and_mac - 32;
    const uint8_t *received_mac = plain + content_len;

    /* Verify HMAC */
    uint8_t hdr[5] = { type, 0x03, 0x03, (uint8_t)(content_len >> 8), (uint8_t)content_len };
    uint8_t expected_mac[SHA256_DIGEST_LENGTH];
    unsigned int emac_len = 0;

    HMAC_CTX *hctx = HMAC_CTX_new();
    HMAC_Init_ex(hctx, ctx->server_mac_key, 32, EVP_sha256(), NULL);
    HMAC_Update(hctx, hdr, 5);
    HMAC_Update(hctx, plain, content_len);
    HMAC_Final(hctx, expected_mac, &emac_len);
    HMAC_CTX_free(hctx);

    if (CRYPTO_memcmp(received_mac, expected_mac, 32) != 0) {
        free(plain);
        return -7;
    }

    if (content_len > max_out_len) {
        free(plain);
        return -8;
    }

    memcpy(out, plain, content_len);
    *out_len = content_len;
    free(plain);
    return 0;
}

int syna_tls_build_close_notify(SynaTlsContext *ctx, uint8_t *out, size_t *out_len)
{
    if (!ctx || !out || !out_len) return -1;

    /* Alert record warning close_notify: 0x01 0x00 */
    uint8_t alert[2] = { 0x01, 0x00 };
    return syna_tls_encrypt_record(ctx, 0x15, alert, 2, out, out_len);
}
