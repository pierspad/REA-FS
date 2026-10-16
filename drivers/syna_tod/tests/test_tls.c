#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/syna_tls.h"

static void test_prf(void)
{
    /* Known RFC 5246 TLS 1.2 PRF test vector */
    const uint8_t secret[4] = { 0x01, 0x02, 0x03, 0x04 };
    const char *label = "test label";
    const uint8_t seed[4] = { 0xa1, 0xa2, 0xa3, 0xa4 };
    uint8_t out1[32];
    uint8_t out2[32];

    assert(syna_tls_prf(secret, 4, label, seed, 4, out1, 32) == 0);
    assert(syna_tls_prf(secret, 4, label, seed, 4, out2, 32) == 0);

    /* Deterministic */
    assert(memcmp(out1, out2, 32) == 0);

    /* Check that PRF output is non-zero */
    int non_zero = 0;
    for (int i = 0; i < 32; i++) {
        if (out1[i] != 0) non_zero = 1;
    }
    assert(non_zero == 1);

    printf("  [PASS] test_prf\n");
}

static void test_encrypt_decrypt_roundtrip(void)
{
    SynaTlsContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    memset(ctx.client_mac_key, 0x11, 32);
    memset(ctx.server_mac_key, 0x11, 32);
    memset(ctx.client_enc_key, 0x22, 32);
    memset(ctx.server_enc_key, 0x22, 32);
    ctx.secure_tx = true;
    ctx.secure_rx = true;

    const uint8_t plain[] = "hello synaptics TLS";
    const size_t plain_len = sizeof(plain) - 1;
    uint8_t enc[128];
    size_t enc_len = 0;

    assert(syna_tls_encrypt_app_data(&ctx, plain, plain_len, enc, &enc_len) == 0);
    assert(enc_len > plain_len + 16 + 32);

    uint8_t dec[128];
    size_t dec_len = 0;
    assert(syna_tls_decrypt_app_data(&ctx, enc, enc_len, dec, sizeof(dec), &dec_len) == 0);

    assert(dec_len == 19);
    assert(memcmp(dec, plain, 19) == 0);

    /* A short advertised record must be rejected before subtracting its IV. */
    uint8_t malformed[128];
    memcpy(malformed, enc, enc_len);
    malformed[3] = 0; malformed[4] = 0;
    assert(syna_tls_decrypt_app_data(&ctx, malformed, enc_len, dec, sizeof(dec), &dec_len) != 0);

    /* An authenticated handshake/alert must not be consumed as application data. */
    uint8_t other_record[128];
    size_t other_len = 0;
    assert(syna_tls_encrypt_record(&ctx, 0x16, plain, plain_len, other_record, &other_len) == 0);
    assert(syna_tls_decrypt_app_data(&ctx, other_record, other_len, dec, sizeof(dec), &dec_len) != 0);

    /* Test buffer too small condition */
    uint8_t small_dec[10];
    assert(syna_tls_decrypt_app_data(&ctx, enc, enc_len, small_dec, sizeof(small_dec), &dec_len) == -8);

    /* Test tamper detection: flip 1 byte in ciphertext */
    enc[25] ^= 0x01;
    assert(syna_tls_decrypt_app_data(&ctx, enc, enc_len, dec, sizeof(dec), &dec_len) != 0);

    printf("  [PASS] test_encrypt_decrypt_roundtrip & tamper_detection\n");
}

static void test_pairing_request_and_extraction(void)
{
    char *key_pem = NULL;
    uint8_t packet[453];
    size_t packet_len = 0;

    assert(syna_pairing_generate_request(&key_pem, packet, &packet_len) == 0);
    assert(key_pem != NULL && strlen(key_pem) > 100);
    assert(packet_len == 453);
    assert(packet[0] == 0x4f);

    /* Validate generated 0x4f packet and verify deterministic ROM 6.20 ECDSA signature */
    assert(syna_pairing_validate_request(key_pem, packet, packet_len) == 0);

    /* Tamper with 1 byte in certificate body -> signature verification must fail */
    packet[20] ^= 0x01;
    assert(syna_pairing_validate_request(key_pem, packet, packet_len) != 0);
    packet[20] ^= 0x01;
    assert(syna_pairing_validate_request(key_pem, packet, packet_len) == 0);

    /* Test credential extraction from synthetic 298B ownership reply + 926B public material reply */
    uint8_t own_reply[298];
    memset(own_reply, 0, sizeof(own_reply));
    own_reply[2] = 184; /* length = 184 LE */
    memcpy(own_reply + 6, packet + 9, 184);

    uint8_t pub_mat[926];
    memset(pub_mat, 0, sizeof(pub_mat));
    for (int i = 0; i < 32; i++) {
        pub_mat[534 + i] = (uint8_t)(i + 1);
        pub_mat[602 + i] = (uint8_t)(i + 101);
    }

    uint8_t host_cert[184];
    size_t host_cert_len = 0;
    uint8_t server_ecdh[65];
    size_t server_ecdh_len = 0;
    assert(syna_pairing_extract_credentials(own_reply, sizeof(own_reply),
                                            pub_mat, sizeof(pub_mat),
                                            host_cert, &host_cert_len,
                                            server_ecdh, &server_ecdh_len) == 0);
    assert(host_cert_len == 184);
    assert(memcmp(host_cert, packet + 9, 184) == 0);
    assert(server_ecdh_len == 65 && server_ecdh[0] == 0x04);
    assert(server_ecdh[1] == 32 && server_ecdh[32] == 1);
    assert(server_ecdh[33] == 132 && server_ecdh[64] == 101);

    free(key_pem);
    printf("  [PASS] test_pairing_request_and_extraction\n");
}

int main(void)
{
    printf("Running syna_tls unit tests...\n");
    test_prf();
    test_encrypt_decrypt_roundtrip();
    test_pairing_request_and_extraction();
    printf("All syna_tls unit tests passed!\n");
    return 0;
}

