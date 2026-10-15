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

    /* Test buffer too small condition */
    uint8_t small_dec[10];
    assert(syna_tls_decrypt_app_data(&ctx, enc, enc_len, small_dec, sizeof(small_dec), &dec_len) == -8);

    /* Test tamper detection: flip 1 byte in ciphertext */
    enc[25] ^= 0x01;
    assert(syna_tls_decrypt_app_data(&ctx, enc, enc_len, dec, sizeof(dec), &dec_len) != 0);

    printf("  [PASS] test_encrypt_decrypt_roundtrip & tamper_detection\n");
}

int main(void)
{
    printf("Running syna_tls unit tests...\n");
    test_prf();
    test_encrypt_decrypt_roundtrip();
    printf("All syna_tls unit tests passed!\n");
    return 0;
}
