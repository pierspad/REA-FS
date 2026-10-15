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

#include <time.h>

static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void benchmark_5_templates(void)
{
    printf("\n--- Benchmark 1: 1 probe vs 5 templates (legacy API: syna_match_images) ---\n");

    float probe_raw[H * W];
    float probe[H * W];
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            float phase = 2.0f * (float)M_PI * (0.15f * (float)c + 0.06f * (float)r + 0.0004f * (float)(r * c));
            probe_raw[r * W + c] = sinf(phase) + 0.4f * cosf(2.0f * (float)M_PI * 0.12f * (float)r);
        }
    }
    assert(syna_bandpass_filter(probe_raw, probe) == 0);

    /* Generate 5 realistic templates with different slight rotations and offsets */
    float templates[5][H * W];
    float angles[5] = { -12.0f, -4.0f, 0.0f, 8.0f, 15.0f };
    for (int i = 0; i < 5; i++) {
        float rot[H * W];
        rotate_test_image(probe_raw, angles[i], rot);
        assert(syna_bandpass_filter(rot, templates[i]) == 0);
    }

    double t0 = get_time_ms();
    for (int i = 0; i < 5; i++) {
        double t_start = get_time_ms();
        SynaMatchResult res;
        int rc = syna_match_images(probe, templates[i], 0.65f, &res);
        double t_end = get_time_ms();
        assert(rc == 0);
        printf("  Template %d (rot %5.1f°): match score=%.4f angle=%5.1f° overlap=%.2f | time: %7.2f ms\n",
               i, angles[i], res.score, res.angle, res.overlap, t_end - t_start);
    }
    double t1 = get_time_ms();
    printf("  >>> Total time for 5 templates (legacy API): %.2f ms (avg: %.2f ms / template) <<<\n\n",
           t1 - t0, (t1 - t0) / 5.0);

    printf("--- Benchmark 2: 1 probe vs 5 templates with Probe Caching & Early Exit ---\n");
    SynaMatcherContext *ctx = syna_matcher_context_new();
    assert(ctx != NULL);

    double tp0 = get_time_ms();
    syna_matcher_set_probe(ctx, probe);
    double tp1 = get_time_ms();
    printf("  [Probe Setup] Precomputed probe representation in %.2f ms\n", tp1 - tp0);

    double tc0 = get_time_ms();
    for (int i = 0; i < 5; i++) {
        double ts = get_time_ms();
        SynaMatchResult res;
        int rc = syna_matcher_match_template(ctx, templates[i], 0.65f, &res);
        double te = get_time_ms();
        assert(rc == 0);
        printf("  Template %d: score=%.4f angle=%5.1f° | match time: %6.2f ms",
               i, res.score, res.angle, te - ts);
        if (res.score >= 0.70f) {
            printf(" -> [EARLY EXIT on genuine match!]\n");
            break;
        }
        printf("\n");
    }
    double tc1 = get_time_ms();
    printf("  >>> Total verify time with early exit: %.2f ms (probe + match) <<<\n\n",
           (tp1 - tp0) + (tc1 - tc0));

    syna_matcher_context_free(ctx);
}

int main(void)
{
    printf("Running syna_matcher unit tests...\n");
    test_self_match();
    test_known_shift();
    test_uncorrelated_rejection();
    test_odd_rotations();
    benchmark_5_templates();
    printf("All syna_matcher unit tests passed!\n");
    return 0;
}

