#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/syna_mode2.h"
#include "../src/syna_proto.h"

static void test_basis_vector_0(void)
{
    uint8_t enc[16] = {144, 128, 128, 128, 128, 128, 128, 128,
                       128, 128, 128, 128, 128, 128, 128, 128};
    uint8_t expected[16] = {136, 120, 120, 120, 120, 136, 136, 136,
                            120, 136, 136, 136, 120, 136, 136, 136};
    uint8_t out[16] = {0};

    int rc = syna_decode_mode2_row(enc, NULL, out, 16);
    assert(rc == 0);
    assert(memcmp(out, expected, 16) == 0);
    printf("  [PASS] test_basis_vector_0\n");
}

static void test_constant_levels(void)
{
    uint8_t zero[16] = {0};
    uint8_t mid[16];
    uint8_t high[16];
    memset(mid, 128, 16);
    memset(high, 255, 16);

    uint8_t out[16];

    assert(syna_decode_mode2_row(zero, NULL, out, 16) == 0);
    assert(memcmp(out, zero, 16) == 0);

    assert(syna_decode_mode2_row(mid, NULL, out, 16) == 0);
    assert(memcmp(out, mid, 16) == 0);

    assert(syna_decode_mode2_row(high, NULL, out, 16) == 0);
    assert(memcmp(out, high, 16) == 0);

    printf("  [PASS] test_constant_levels\n");
}

static void test_baseline_offset(void)
{
    uint8_t enc[16];
    uint8_t base[16];
    uint8_t out[16];
    memset(enc, 128, 16);
    memset(base, 130, 16); // baseline offset +2 -> total -= 8 -> total>>1 -= 4 -> out should be 124

    assert(syna_decode_mode2_row(enc, base, out, 16) == 0);
    for (int i = 0; i < 16; i++) {
        assert(out[i] == 124);
    }
    printf("  [PASS] test_baseline_offset\n");
}

static void test_frame_validation(void)
{
    uint8_t frame[SYNA_FRAME_BYTES];

    /* Construct valid frame */
    for (int r = 0; r < SYNA_ROWS_PER_FRAME; r++) {
        uint8_t *row = frame + r * SYNA_ROW_BYTES;
        row[0] = 0x01;
        row[1] = 0xfe;
        row[2] = (uint8_t)(r + 1);
        row[3] = 42; // counter
        row[4] = 0xf0;
        row[5] = 0xf7;
        row[6] = 0x01;
        row[7] = 0x00;
        memset(row + 8, 128, SYNA_SAMPLE_BYTES);
    }

    uint8_t counter = 0;
    assert(syna_validate_frame(frame, &counter) == 0);
    assert(counter == 42);

    /* Corrupt header prefix */
    frame[0] = 0x00;
    assert(syna_validate_frame(frame, NULL) == -2);
    frame[0] = 0x01;

    /* Corrupt row index */
    frame[2] = 99;
    assert(syna_validate_frame(frame, NULL) == -3);
    frame[2] = 1;

    /* Corrupt counter mid-frame */
    frame[1 * SYNA_ROW_BYTES + 3] = 43;
    assert(syna_validate_frame(frame, NULL) == -4);
    frame[1 * SYNA_ROW_BYTES + 3] = 42;

    /* Full decode test */
    uint8_t decoded_img[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
    assert(syna_decode_frame_mode2(frame, NULL, decoded_img) == 0);
    for (size_t i = 0; i < sizeof(decoded_img); i++) {
        assert(decoded_img[i] == 128);
    }

    printf("  [PASS] test_frame_validation & test_decode_frame_mode2\n");
}

static void test_unclipped_frame(void)
{
    uint8_t frame[SYNA_FRAME_BYTES];
    for (int r = 0; r < SYNA_ROWS_PER_FRAME; r++) {
        uint8_t *row = frame + r * SYNA_ROW_BYTES;
        const uint8_t header[] = {1, 254, (uint8_t)(r + 1), 42, 240, 247, 1, 0};
        memcpy(row, header, 8);
        memset(row + 8, 255, SYNA_SAMPLE_BYTES);
    }
    float out[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
    /* Each transform row sums to four: 4*255/2-128 = 382. */
    assert(syna_decode_frame_unclipped(frame, out) == 0);
    for (size_t i = 0; i < SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES; i++)
        assert(out[i] == 382.0f);
    frame[0] = 0;
    assert(syna_decode_frame_unclipped(frame, out) < 0);
}

int main(void)
{
    printf("Running syna_mode2 unit tests...\n");
    test_unclipped_frame();
    test_basis_vector_0();
    test_constant_levels();
    test_baseline_offset();
    test_frame_validation();
    printf("All syna_mode2 unit tests passed!\n");
    return 0;
}
