#include "syna_mode2.h"
#include "syna_proto.h"
#include <string.h>

/*
 * Kronecker product M = (2*I4 - J4) (x) (2*I4 - J4)
 * For 4x4 H: H_ij = (i == j) ? +1 : -1
 * M[4*a + b][4*c + d] = H_ac * H_bd
 */
static const int8_t KRON_M[16][16] = {
    { 1, -1, -1, -1, -1,  1,  1,  1, -1,  1,  1,  1, -1,  1,  1,  1},
    {-1,  1, -1, -1,  1, -1,  1,  1,  1, -1,  1,  1,  1, -1,  1,  1},
    {-1, -1,  1, -1,  1,  1, -1,  1,  1,  1, -1,  1,  1,  1, -1,  1},
    {-1, -1, -1,  1,  1,  1,  1, -1,  1,  1,  1, -1,  1,  1,  1, -1},
    {-1,  1,  1,  1,  1, -1, -1, -1, -1,  1,  1,  1, -1,  1,  1,  1},
    { 1, -1,  1,  1, -1,  1, -1, -1,  1, -1,  1,  1,  1, -1,  1,  1},
    { 1,  1, -1,  1, -1, -1,  1, -1,  1,  1, -1,  1,  1,  1, -1,  1},
    { 1,  1,  1, -1, -1, -1, -1,  1,  1,  1,  1, -1,  1,  1,  1, -1},
    {-1,  1,  1,  1, -1,  1,  1,  1,  1, -1, -1, -1, -1,  1,  1,  1},
    { 1, -1,  1,  1,  1, -1,  1,  1, -1,  1, -1, -1,  1, -1,  1,  1},
    { 1,  1, -1,  1,  1,  1, -1,  1, -1, -1,  1, -1,  1,  1, -1,  1},
    { 1,  1,  1, -1,  1,  1,  1, -1, -1, -1, -1,  1,  1,  1,  1, -1},
    {-1,  1,  1,  1, -1,  1,  1,  1, -1,  1,  1,  1,  1, -1, -1, -1},
    { 1, -1,  1,  1,  1, -1,  1,  1,  1, -1,  1,  1, -1,  1, -1, -1},
    { 1,  1, -1,  1,  1,  1, -1,  1,  1,  1, -1,  1, -1, -1,  1, -1},
    { 1,  1,  1, -1,  1,  1,  1, -1,  1,  1,  1, -1, -1, -1, -1,  1}
};

int syna_decode_mode2_row(const uint8_t *encoded,
                          const uint8_t *baseline,
                          uint8_t *out,
                          size_t len)
{
    if (!encoded || !out || len == 0 || (len % 16) != 0)
        return -1;

    for (size_t blk = 0; blk < len; blk += 16) {
        for (int k = 0; k < 16; k++) {
            int32_t total = 0;
            const int8_t *row_m = KRON_M[k];
            const uint8_t *blk_enc = &encoded[blk];

            for (int j = 0; j < 16; j++) {
                total += (int32_t)row_m[j] * (int32_t)blk_enc[j];
            }

            if (baseline) {
                total -= 4 * ((int32_t)baseline[blk + k] - 128);
            }

            int32_t val = (total >> 1) - 128;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            out[blk + k] = (uint8_t)val;
        }
    }

    return 0;
}

int syna_validate_frame(const uint8_t *frame, uint8_t *counter_out)
{
    if (!frame)
        return -1;

    uint8_t counter = 0;
    for (size_t row = 0; row < SYNA_ROWS_PER_FRAME; row++) {
        const uint8_t *row_ptr = frame + (row * SYNA_ROW_BYTES);

        if (row_ptr[0] != SYNA_ROW_PREFIX_0 || row_ptr[1] != SYNA_ROW_PREFIX_1)
            return -2;

        if (row_ptr[2] != (uint8_t)(row + 1))
            return -3;

        if (row == 0) {
            counter = row_ptr[3];
        } else if (row_ptr[3] != counter) {
            return -4;
        }

        if (row_ptr[4] != SYNA_ROW_SUFFIX_0 ||
            row_ptr[5] != SYNA_ROW_SUFFIX_1 ||
            row_ptr[6] != SYNA_ROW_SUFFIX_2 ||
            row_ptr[7] != SYNA_ROW_SUFFIX_3)
            return -5;
    }

    if (counter_out)
        *counter_out = counter;

    return 0;
}

int syna_decode_frame_mode2(const uint8_t *frame,
                            const uint8_t *baseline,
                            uint8_t *out)
{
    if (!frame || !out)
        return -1;

    int ret = syna_validate_frame(frame, NULL);
    if (ret != 0)
        return ret;

    uint8_t pair_row[SYNA_SAMPLE_BYTES];

    for (size_t pair = 0; pair < SYNA_OUTPUT_ROWS; pair++) {
        const uint8_t *row_a = frame + (pair * 2 * SYNA_ROW_BYTES) + SYNA_HEADER_BYTES;
        const uint8_t *row_b = frame + ((pair * 2 + 1) * SYNA_ROW_BYTES) + SYNA_HEADER_BYTES;

        for (size_t c = 0; c < SYNA_SAMPLE_BYTES; c++) {
            pair_row[c] = (uint8_t)(((uint16_t)row_a[c] + (uint16_t)row_b[c]) / 2);
        }

        const uint8_t *base_row = baseline ? (baseline + pair * SYNA_SAMPLE_BYTES) : NULL;
        uint8_t *out_row = out + (pair * SYNA_SAMPLE_BYTES);

        ret = syna_decode_mode2_row(pair_row, base_row, out_row, SYNA_SAMPLE_BYTES);
        if (ret != 0)
            return ret;
    }

    return 0;
}
