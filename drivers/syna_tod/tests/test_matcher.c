#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../src/syna_matcher.h"
#include "../src/syna_proto.h"

#define H 56
#define W 144

static void generate_synthetic_ridges(float *img, float angle_rad, float freq)
{
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            float u = (float)c * cosf(angle_rad) + (float)r * sinf(angle_rad);
            img[r * W + c] = sinf(2.0f * (float)M_PI * freq * u);
        }
    }
}

static void test_self_match(void)
{
    float img[H * W];
    float filtered[H * W];
    generate_synthetic_ridges(img, 0.2f, 0.15f);

    assert(syna_bandpass_filter(img, filtered) == 0);

    SynaMatchResult res;
    assert(syna_match_images(filtered, filtered, 0.70f, &res) == 0);

    printf("  Self match -> score: %.4f, angle: %.1f deg, dy: %d, dx: %d, overlap: %.2f\n",
           res.score, res.angle, res.dy, res.dx, res.overlap);

    assert(res.score > 0.98f);
    assert(fabsf(res.angle) < 1e-3f);
    assert(res.dy == 0 && res.dx == 0);
    assert(syna_is_match(filtered, filtered) == true);
    printf("  [PASS] test_self_match\n");
}

static void test_known_shift(void)
{
    const int big_h = 80;
    const int big_w = 200;
    float *big = malloc(big_h * big_w * sizeof(float));

    /* 2D non-linear chirp + localized texture */
    for (int r = 0; r < big_h; r++) {
        for (int c = 0; c < big_w; c++) {
            float phase = 2.0f * (float)M_PI * (0.15f * (float)c + 0.05f * (float)r + 0.0005f * (float)(r * c));
            big[r * big_w + c] = sinf(phase);
        }
    }

    float img_a[H * W];
    float img_b[H * W];
    float filt_a[H * W];
    float filt_b[H * W];

    /* a is at [10..66, 20..164], b is at [4..60, 12..156] -> content of b moved by (+6, +8) */
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            img_a[r * W + c] = big[(10 + r) * big_w + (20 + c)];
            img_b[r * W + c] = big[(4 + r) * big_w + (12 + c)];
        }
    }
    free(big);

    assert(syna_bandpass_filter(img_a, filt_a) == 0);
    assert(syna_bandpass_filter(img_b, filt_b) == 0);

    SynaMatchResult res;
    assert(syna_match_images(filt_a, filt_b, 0.65f, &res) == 0);

    printf("  Shift match -> score: %.4f, angle: %.1f deg, dy: %d, dx: %d, overlap: %.2f\n",
           res.score, res.angle, res.dy, res.dx, res.overlap);

    assert(res.score > 0.90f);
    assert(res.dy == 6 && res.dx == 8);
    printf("  [PASS] test_known_shift\n");
}

static void test_uncorrelated_rejection(void)
{
    float img_a[H * W];
    float img_b[H * W];
    float filt_a[H * W];
    float filt_b[H * W];

    generate_synthetic_ridges(img_a, 0.0f, 0.15f);
    generate_synthetic_ridges(img_b, (float)M_PI * 0.5f, 0.15f); /* Perpendicular ridges */

    assert(syna_bandpass_filter(img_a, filt_a) == 0);
    assert(syna_bandpass_filter(img_b, filt_b) == 0);

    SynaMatchResult res;
    assert(syna_match_images(filt_a, filt_b, 0.65f, &res) == 0);

    printf("  Perpendicular match -> score: %.4f (threshold: %.2f)\n",
           res.score, (float)SYNA_MATCH_THRESHOLD);

    assert(res.score < SYNA_MATCH_THRESHOLD);
    assert(syna_is_match(filt_a, filt_b) == false);
    printf("  [PASS] test_uncorrelated_rejection\n");
}

static void rotate_test_image(const float *in, float deg, float *out)
{
    float rad = deg * (float)M_PI / 180.0f;
    float cos_t = cosf(rad);
    float sin_t = sinf(rad);
    float cy = (float)(H - 1) * 0.5f;
    float cx = (float)(W - 1) * 0.5f;

    for (int r = 0; r < H; r++) {
        float yr = (float)r - cy;
        for (int c = 0; c < W; c++) {
            float xc = (float)c - cx;
            float ys = cy + yr * cos_t - xc * sin_t;
            float xs = cx + yr * sin_t + xc * cos_t;

            int iy = (int)roundf(ys);
            int ix = (int)roundf(xs);

            if (iy >= 0 && iy < H && ix >= 0 && ix < W) {
                out[r * W + c] = in[iy * W + ix];
            } else {
                out[r * W + c] = 0.0f;
            }
        }
    }
}

static void test_odd_rotations(void)
{
    float img_orig[H * W];
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            float phase = 2.0f * (float)M_PI * (0.15f * (float)c + 0.08f * (float)r + 0.0003f * (float)(r * c));
            img_orig[r * W + c] = sinf(phase) + 0.5f * cosf(2.0f * (float)M_PI * 0.12f * (float)r);
        }
    }

    float filt_orig[H * W];
    assert(syna_bandpass_filter(img_orig, filt_orig) == 0);

    /* Test +7 degrees */
    {
        float img_rot[H * W];
        rotate_test_image(img_orig, -7.0f, img_rot);
        float filt_rot[H * W];
        assert(syna_bandpass_filter(img_rot, filt_rot) == 0);

        SynaMatchResult res;
        assert(syna_match_images(filt_orig, filt_rot, 0.60f, &res) == 0);
        printf("  +7 deg match -> score: %.4f, angle: %.1f deg (expected ~7.0)\n", res.score, res.angle);
        assert(res.score > 0.85f);
        assert(fabsf(res.angle - 7.0f) <= 1.0f);
        assert(syna_is_match(filt_orig, filt_rot) == true);
    }

    /* Test -13 degrees */
    {
        float img_rot[H * W];
        rotate_test_image(img_orig, 13.0f, img_rot);
        float filt_rot[H * W];
        assert(syna_bandpass_filter(img_rot, filt_rot) == 0);

        SynaMatchResult res;
        assert(syna_match_images(filt_orig, filt_rot, 0.60f, &res) == 0);
        printf("  -13 deg match -> score: %.4f, angle: %.1f deg (expected ~ -13.0)\n", res.score, res.angle);
        assert(res.score > 0.85f);
        assert(fabsf(res.angle - (-13.0f)) <= 1.0f);
        assert(syna_is_match(filt_orig, filt_rot) == true);
    }
    printf("  [PASS] test_odd_rotations (+7 deg and -13 deg)\n");
}

int main(void)
{
    printf("Running syna_matcher unit tests...\n");
    test_self_match();
    test_known_shift();
    test_uncorrelated_rejection();
    test_odd_rotations();
    printf("All syna_matcher unit tests passed!\n");
    return 0;
}
