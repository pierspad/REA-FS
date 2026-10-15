#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode a 16-byte aligned row buffer using the mode 2 Kronecker transform.
 *
 * @param encoded Non-empty buffer of encoded sample bytes (length must be multiple of 16).
 * @param baseline Optional baseline row (same length as encoded), or NULL.
 * @param out Output buffer of same length as encoded.
 * @param len Length in bytes (must be multiple of 16).
 * @return 0 on success, negative error code on failure.
 */
int syna_decode_mode2_row(const uint8_t *encoded,
                          const uint8_t *baseline,
                          uint8_t *out,
                          size_t len);

/**
 * Validate a raw frame of SYNA_FRAME_BYTES (17024 bytes).
 *
 * Checks row prefixes, row indices (1..112), row trailers, and uniform frame counter.
 *
 * @param frame Buffer of SYNA_FRAME_BYTES.
 * @param counter_out If non-NULL, receives the frame counter (0..255).
 * @return 0 on success, negative error code if invalid.
 */
int syna_validate_frame(const uint8_t *frame, uint8_t *counter_out);

/**
 * Decode an entire validated raw frame of 112x152 bytes into a 56x144 image.
 *
 * Averages adjacent row pairs (0,1), (2,3), ..., (110,111), skips the 8-byte
 * headers, and decodes the 144 samples per row using the mode 2 transform.
 *
 * @param frame Validated raw frame buffer (SYNA_FRAME_BYTES = 17024 bytes).
 * @param baseline Optional 56x144 baseline plane, or NULL.
 * @param out Output buffer for 56x144 pixels (8064 bytes).
 * @return 0 on success, negative error code on failure.
 */
int syna_decode_frame_mode2(const uint8_t *frame,
                            const uint8_t *baseline,
                            uint8_t *out);

#ifdef __cplusplus
}
#endif
