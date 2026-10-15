#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float score;      /* Best normalized correlation score (-1.0 .. 1.0) */
    float angle;      /* Rotation angle in degrees (-30.0 .. 30.0) */
    int dy;           /* Translation dy in pixels */
    int dx;           /* Translation dx in pixels */
    float overlap;    /* Overlap fraction (0.0 .. 1.0) */
} SynaMatchResult;

/**
 * Apply radial bandpass filter to a 56x144 image.
 *
 * @param in Input image (56x144 pixels, e.g. float or uint8).
 * @param out Output buffer of 56x144 floats, zero-mean and unit variance.
 * @return 0 on success, negative error code on failure.
 */
int syna_bandpass_filter(const float *in, float *out);

/**
 * Compare two bandpassed 56x144 images using rotation grid and normalized cross-correlation.
 *
 * Searches rotations between -30 and +30 degrees and 2D translations.
 *
 * @param a First bandpassed 56x144 image (unit variance).
 * @param b Second bandpassed 56x144 image (unit variance).
 * @param min_overlap Minimum required overlap fraction (e.g. 0.65).
 * @param result Output struct receiving best score, angle, dy, dx, overlap.
 * @return 0 on success, negative error code on failure.
 */
int syna_match_images(const float *a,
                      const float *b,
                      float min_overlap,
                      SynaMatchResult *result);

/**
 * Context for high-performance matching with pre-allocated scratch buffers
 * and reusable probe representations (avoids recomputing probe FFT and SAT
 * across multiple enrolled templates).
 */
typedef struct SynaMatcherContext SynaMatcherContext;

SynaMatcherContext *syna_matcher_context_new(void);
void syna_matcher_context_free(SynaMatcherContext *ctx);

/**
 * Precompute FFT and Summed-Area Tables for probe image.
 *
 * @param ctx Matcher context.
 * @param probe Bandpassed 56x144 probe image (unit variance).
 */
void syna_matcher_set_probe(SynaMatcherContext *ctx, const float *probe);

/**
 * Match an enrolled template against the currently set probe image.
 *
 * @param ctx Matcher context with probe already set.
 * @param template_b Bandpassed 56x144 template image (unit variance).
 * @param min_overlap Minimum required overlap fraction (e.g. 0.65).
 * @param result Output match result.
 * @return 0 on success, negative error code on failure.
 */
int syna_matcher_match_template(SynaMatcherContext *ctx,
                                const float *template_b,
                                float min_overlap,
                                SynaMatchResult *result);

/**
 * Convenience helper to determine if two 56x144 images match the authorized threshold.
 *
 * @param a First 56x144 image.
 * @param b Second 56x144 image.
 * @return true if score >= SYNA_MATCH_THRESHOLD (0.50f), false otherwise.
 */
bool syna_is_match(const float *a, const float *b);

#ifdef __cplusplus
}
#endif
