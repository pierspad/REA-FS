#include "syna_matcher.h"
#include "syna_proto.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IMG_H SYNA_OUTPUT_ROWS  /* 56 */
#define IMG_W SYNA_OUTPUT_COLS  /* 144 */
#define FFT_H 128
#define FFT_W 256

typedef struct {
    float r;
    float i;
} Complex;

/* Precomputed Twiddle Factors and Bit Reversal Tables */
static Complex twiddles_512[512], twiddles_inv_512[512];
static Complex twiddles_256[256], twiddles_inv_256[256];
static Complex twiddles_128[128], twiddles_inv_128[128];
static Complex twiddles_64[64],   twiddles_inv_64[64];
static int bitrev_512[512], bitrev_256[256], bitrev_128[128], bitrev_64[64];
static int tables_initialized = 0;

static void init_tables(void)
{
    if (tables_initialized) return;

    for (int i = 0; i < 512; i++) {
        int rev = 0;
        for (int b = 0; b < 9; b++) if (i & (1 << b)) rev |= (1 << (8 - b));
        bitrev_512[i] = rev;
        float a = -2.0f * (float)M_PI * (float)i / 512.0f;
        twiddles_512[i].r = cosf(a); twiddles_512[i].i = sinf(a);
        twiddles_inv_512[i].r = cosf(-a); twiddles_inv_512[i].i = sinf(-a);
    }
    for (int i = 0; i < 256; i++) {
        int rev = 0;
        for (int b = 0; b < 8; b++) if (i & (1 << b)) rev |= (1 << (7 - b));
        bitrev_256[i] = rev;
        float a = -2.0f * (float)M_PI * (float)i / 256.0f;
        twiddles_256[i].r = cosf(a); twiddles_256[i].i = sinf(a);
        twiddles_inv_256[i].r = cosf(-a); twiddles_inv_256[i].i = sinf(-a);
    }
    for (int i = 0; i < 128; i++) {
        int rev = 0;
        for (int b = 0; b < 7; b++) if (i & (1 << b)) rev |= (1 << (6 - b));
        bitrev_128[i] = rev;
        float a = -2.0f * (float)M_PI * (float)i / 128.0f;
        twiddles_128[i].r = cosf(a); twiddles_128[i].i = sinf(a);
        twiddles_inv_128[i].r = cosf(-a); twiddles_inv_128[i].i = sinf(-a);
    }
    for (int i = 0; i < 64; i++) {
        int rev = 0;
        for (int b = 0; b < 6; b++) if (i & (1 << b)) rev |= (1 << (5 - b));
        bitrev_64[i] = rev;
        float a = -2.0f * (float)M_PI * (float)i / 64.0f;
        twiddles_64[i].r = cosf(a); twiddles_64[i].i = sinf(a);
        twiddles_inv_64[i].r = cosf(-a); twiddles_inv_64[i].i = sinf(-a);
    }

    tables_initialized = 1;
}

static inline void fft_radix2_fast(Complex *x, int n, int invert, const int *bitrev, const Complex *twiddles)
{
    for (int i = 0; i < n; i++) {
        int j = bitrev[i];
        if (i < j) {
            Complex tmp = x[i]; x[i] = x[j]; x[j] = tmp;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                Complex w = twiddles[j * step];
                Complex v;
                v.r = x[i + j + half].r * w.r - x[i + j + half].i * w.i;
                v.i = x[i + j + half].r * w.i + x[i + j + half].i * w.r;

                Complex u = x[i + j];
                x[i + j].r = u.r + v.r;
                x[i + j].i = u.i + v.i;
                x[i + j + half].r = u.r - v.r;
                x[i + j + half].i = u.i - v.i;
            }
        }
    }

    if (invert) {
        float inv = 1.0f / (float)n;
        for (int i = 0; i < n; i++) {
            x[i].r *= inv;
            x[i].i *= inv;
        }
    }
}

static void fft2d_fast(Complex *grid, int rows, int cols, int invert, Complex *scratch)
{
    const int *bitrev_r = (rows == 128) ? bitrev_128 : (rows == 64 ? bitrev_64 : bitrev_512);
    const int *bitrev_c = (cols == 256) ? bitrev_256 : (cols == 512 ? bitrev_512 : bitrev_128);
    const Complex *tw_r = invert ? ((rows == 128) ? twiddles_inv_128 : twiddles_inv_64)
                                 : ((rows == 128) ? twiddles_128 : twiddles_64);
    const Complex *tw_c = invert ? ((cols == 256) ? twiddles_inv_256 : (cols == 512 ? twiddles_inv_512 : twiddles_inv_128))
                                 : ((cols == 256) ? twiddles_256 : (cols == 512 ? twiddles_512 : twiddles_128));

    for (int r = 0; r < rows; r++) {
        fft_radix2_fast(&grid[r * cols], cols, invert, bitrev_c, tw_c);
    }

    const int BS = 16;
    for (int r = 0; r < rows; r += BS) {
        for (int c = 0; c < cols; c += BS) {
            int r_end = (r + BS < rows) ? r + BS : rows;
            int c_end = (c + BS < cols) ? c + BS : cols;
            for (int ir = r; ir < r_end; ir++) {
                for (int ic = c; ic < c_end; ic++) {
                    scratch[ic * rows + ir] = grid[ir * cols + ic];
                }
            }
        }
    }

    for (int c = 0; c < cols; c++) {
        fft_radix2_fast(&scratch[c * rows], rows, invert, bitrev_r, tw_r);
    }

    for (int c = 0; c < cols; c += BS) {
        for (int r = 0; r < rows; r += BS) {
            int c_end = (c + BS < cols) ? c + BS : cols;
            int r_end = (r + BS < rows) ? r + BS : rows;
            for (int ic = c; ic < c_end; ic++) {
                for (int ir = r; ir < r_end; ir++) {
                    grid[ir * cols + ic] = scratch[ic * rows + ir];
                }
            }
        }
    }
}

int syna_bandpass_filter(const float *in, float *out)
{
    if (!in || !out) return -1;
    init_tables();

    const int pad_h = 64;
    const int pad_w = 256;
    Complex *grid = calloc(pad_h * pad_w, sizeof(Complex));
    Complex *scratch = malloc(pad_h * pad_w * sizeof(Complex));
    if (!grid || !scratch) {
        free(grid);
        free(scratch);
        return -2;
    }

    float mean = 0.0f;
    for (int i = 0; i < IMG_H * IMG_W; i++) {
        mean += in[i];
    }
    mean /= (float)(IMG_H * IMG_W);

    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++) {
            grid[r * pad_w + c].r = in[r * IMG_W + c] - mean;
        }
    }

    fft2d_fast(grid, pad_h, pad_w, 0, scratch);

    for (int r = 0; r < pad_h; r++) {
        float fy = (r < pad_h / 2) ? (float)r / (float)pad_h : (float)(r - pad_h) / (float)pad_h;
        for (int c = 0; c < pad_w; c++) {
            float fx = (c < pad_w / 2) ? (float)c / (float)pad_w : (float)(c - pad_w) / (float)pad_w;
            float rad = sqrtf(fy * fy + fx * fx);
            if (rad < 0.08f || rad >= 0.30f) {
                grid[r * pad_w + c].r = 0.0f;
                grid[r * pad_w + c].i = 0.0f;
            }
        }
    }

    fft2d_fast(grid, pad_h, pad_w, 1, scratch);

    float var = 0.0f;
    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++) {
            float val = grid[r * pad_w + c].r;
            out[r * IMG_W + c] = val;
            var += val * val;
        }
    }

    free(grid);
    free(scratch);

    float std = sqrtf(var / (float)(IMG_H * IMG_W));
    if (std <= 1e-7f) return -3;

    float inv_std = 1.0f / std;
    for (int i = 0; i < IMG_H * IMG_W; i++) {
        out[i] *= inv_std;
    }

    return 0;
}

static void rotate_nearest(const float *in, float deg, float *out, float *mask)
{
    float rad = deg * (float)M_PI / 180.0f;
    float cos_t = cosf(rad);
    float sin_t = sinf(rad);
    float cy = (float)(IMG_H - 1) * 0.5f;
    float cx = (float)(IMG_W - 1) * 0.5f;

    for (int r = 0; r < IMG_H; r++) {
        float yr = (float)r - cy;
        for (int c = 0; c < IMG_W; c++) {
            float xc = (float)c - cx;
            float ys = cy + yr * cos_t - xc * sin_t;
            float xs = cx + yr * sin_t + xc * cos_t;

            int iy = (int)roundf(ys);
            int ix = (int)roundf(xs);

            if (iy >= 0 && iy < IMG_H && ix >= 0 && ix < IMG_W) {
                out[r * IMG_W + c] = in[iy * IMG_W + ix];
                mask[r * IMG_W + c] = 1.0f;
            } else {
                out[r * IMG_W + c] = 0.0f;
                mask[r * IMG_W + c] = 0.0f;
            }
        }
    }
}

static void compute_sat(const float *img, int rows, int cols, float *sat)
{
    int sat_stride = cols + 1;
    for (int c = 0; c <= cols; c++) sat[c] = 0.0f;

    for (int r = 0; r < rows; r++) {
        float row_sum = 0.0f;
        sat[(r + 1) * sat_stride + 0] = 0.0f;
        for (int c = 0; c < cols; c++) {
            row_sum += img[r * cols + c];
            sat[(r + 1) * sat_stride + (c + 1)] = sat[r * sat_stride + (c + 1)] + row_sum;
        }
    }
}

static inline float query_sat(const float *sat, int cols, int r0, int r1, int c0, int c1)
{
    if (r1 <= r0 || c1 <= c0) return 0.0f;
    int sat_stride = cols + 1;
    return sat[r1 * sat_stride + c1]
         - sat[r0 * sat_stride + c1]
         - sat[r1 * sat_stride + c0]
         + sat[r0 * sat_stride + c0];
}

struct SynaMatcherContext {
    Complex fa[FFT_H * FFT_W];
    Complex fb[FFT_H * FFT_W];
    Complex num[FFT_H * FFT_W];
    Complex scratch[FFT_H * FFT_W];

    float sat_a2[(IMG_H + 1) * (IMG_W + 1)];
    float sat_b2[(IMG_H + 1) * (IMG_W + 1)];
    float sat_m[(IMG_H + 1) * (IMG_W + 1)];

    float br[IMG_H * IMG_W];
    float mask[IMG_H * IMG_W];
    float b2[IMG_H * IMG_W];

    bool probe_set;
};

SynaMatcherContext *syna_matcher_context_new(void)
{
    init_tables();
    SynaMatcherContext *ctx = calloc(1, sizeof(SynaMatcherContext));
    return ctx;
}

void syna_matcher_context_free(SynaMatcherContext *ctx)
{
    if (ctx) {
        free(ctx);
    }
}

void syna_matcher_set_probe(SynaMatcherContext *ctx, const float *probe)
{
    if (!ctx || !probe) return;
    init_tables();

    /* Compute horizontal 1D FFTs on the 56 valid rows, then store transposed [c * FFT_H + r] in ctx->fa */
    memset(ctx->fa, 0, sizeof(ctx->fa));
    float a2[IMG_H * IMG_W];
    for (int r = 0; r < IMG_H; r++) {
        Complex row_buf[FFT_W];
        memset(row_buf, 0, sizeof(row_buf));
        for (int c = 0; c < IMG_W; c++) {
            float val = probe[r * IMG_W + c];
            row_buf[c].r = val;
            a2[r * IMG_W + c] = val * val;
        }
        fft_radix2_fast(row_buf, FFT_W, 0, bitrev_256, twiddles_256);
        for (int c = 0; c < FFT_W; c++) {
            ctx->fa[c * FFT_H + r] = row_buf[c];
        }
    }
    for (int c = 0; c < FFT_W; c++) {
        fft_radix2_fast(&ctx->fa[c * FFT_H], FFT_H, 0, bitrev_128, twiddles_128);
    }
    compute_sat(a2, IMG_H, IMG_W, ctx->sat_a2);
    ctx->probe_set = true;
}

static void eval_single_angle_fast(SynaMatcherContext *ctx, int deg, const float *b,
                                   float min_ov_pixels, int step_r, int step_c,
                                   SynaMatchResult *res)
{
    res->score = -1.0f;
    res->angle = (float)deg;
    res->dy = 0;
    res->dx = 0;
    res->overlap = 0.0f;

    rotate_nearest(b, (float)deg, ctx->br, ctx->mask);

    for (int i = 0; i < IMG_H * IMG_W; i++) {
        float v = ctx->br[i];
        ctx->b2[i] = v * v;
    }
    compute_sat(ctx->mask, IMG_H, IMG_W, ctx->sat_m);
    compute_sat(ctx->b2, IMG_H, IMG_W, ctx->sat_b2);

    /* 1. Forward 1D horizontal FFTs only on the 56 non-zero image rows */
    memset(ctx->scratch, 0, sizeof(ctx->scratch));
    for (int r = 0; r < IMG_H; r++) {
        Complex *row = &ctx->fb[r * FFT_W];
        for (int c = 0; c < IMG_W; c++) {
            row[c].r = ctx->br[r * IMG_W + c];
            row[c].i = 0.0f;
        }
        memset(&row[IMG_W], 0, (FFT_W - IMG_W) * sizeof(Complex));
        fft_radix2_fast(row, FFT_W, 0, bitrev_256, twiddles_256);
    }

    /* Transpose 56 x 256 into ctx->scratch [c * FFT_H + r] */
    const int BS = 16;
    for (int r = 0; r < IMG_H; r += BS) {
        int r_end = (r + BS < IMG_H) ? r + BS : IMG_H;
        for (int c = 0; c < FFT_W; c += BS) {
            int c_end = (c + BS < FFT_W) ? c + BS : FFT_W;
            for (int ir = r; ir < r_end; ir++) {
                for (int ic = c; ic < c_end; ic++) {
                    ctx->scratch[ic * FFT_H + ir] = ctx->fb[ir * FFT_W + ic];
                }
            }
        }
    }

    /* 2. Fused L1-cache column loop: vertical FFT -> cross-spectrum multiply -> vertical IFFT */
    for (int c = 0; c < FFT_W; c++) {
        Complex *col = &ctx->scratch[c * FFT_H];
        const Complex *fa_col = &ctx->fa[c * FFT_H];
        fft_radix2_fast(col, FFT_H, 0, bitrev_128, twiddles_128);
        for (int r = 0; r < FFT_H; r++) {
            float ar = fa_col[r].r, ai = fa_col[r].i;
            float br = col[r].r,    bi = col[r].i;
            col[r].r = ar * br + ai * bi;
            col[r].i = ai * br - ar * bi;
        }
        fft_radix2_fast(col, FFT_H, 1, bitrev_128, twiddles_inv_128);
    }

    /* 3. Inverse 1D horizontal FFTs ONLY on active shift rows (|r_shift| <= 22: 45 rows out of 128) */
    for (int r = 0; r < FFT_H; r += step_r) {
        int r_shift = (r < IMG_H) ? r : (r - FFT_H);
        if (r_shift < -22 || r_shift > 22) continue;

        Complex *num_row = &ctx->num[r * FFT_W];
        for (int c = 0; c < FFT_W; c++) {
            num_row[c] = ctx->scratch[c * FFT_H + r];
        }
        fft_radix2_fast(num_row, FFT_W, 1, bitrev_256, twiddles_inv_256);

        for (int c = 0; c < FFT_W; c += step_c) {
            int c_shift = (c < IMG_W) ? c : (c - FFT_W);
            if (c_shift < -52 || c_shift > 52) continue;

            float num_val = num_row[c].r;
            if (num_val <= 0.0f) continue;

            /* Intersect intervals */
            int ub0 = (r_shift > 0) ? 0 : -r_shift;
            int ub1 = (r_shift > 0) ? (IMG_H - r_shift) : IMG_H;
            int vb0 = (c_shift > 0) ? 0 : -c_shift;
            int vb1 = (c_shift > 0) ? (IMG_W - c_shift) : IMG_W;

            float cur_ov = query_sat(ctx->sat_m, IMG_W, ub0, ub1, vb0, vb1);
            if (cur_ov > min_ov_pixels) {
                float cur_eb = query_sat(ctx->sat_b2, IMG_W, ub0, ub1, vb0, vb1);

                int ua0 = (r_shift > 0) ? r_shift : 0;
                int ua1 = (r_shift > 0) ? IMG_H : (IMG_H + r_shift);
                int va0 = (c_shift > 0) ? c_shift : 0;
                int va1 = (c_shift > 0) ? IMG_W : (IMG_W + c_shift);

                float cur_ea = query_sat(ctx->sat_a2, IMG_W, ua0, ua1, va0, va1);
                float box_area = (float)((ub1 - ub0) * (vb1 - vb0));
                if (box_area > 0.0f && cur_ov < box_area) {
                    cur_ea *= (cur_ov / box_area);
                }

                float denom = sqrtf(fmaxf(cur_ea * cur_eb, 1e-9f));
                float score = num_val / denom;
                float ov_ratio = cur_ov / (float)(IMG_H * IMG_W);
                if (ov_ratio < 0.65f) {
                    score *= sqrtf(ov_ratio / 0.65f);
                }

                if (score > res->score) {
                    res->score = score;
                    res->angle = (float)deg;
                    res->dy = -r_shift;
                    res->dx = -c_shift;
                    res->overlap = ov_ratio;
                }
            }
        }
    }
}

static void insert_peak(SynaMatchResult top_peaks[3], const SynaMatchResult *cand)
{
    if (cand->score <= top_peaks[2].score) return;
    if (cand->score > top_peaks[0].score) {
        top_peaks[2] = top_peaks[1];
        top_peaks[1] = top_peaks[0];
        top_peaks[0] = *cand;
    } else if (cand->score > top_peaks[1].score) {
        top_peaks[2] = top_peaks[1];
        top_peaks[1] = *cand;
    } else {
        top_peaks[2] = *cand;
    }
}

int syna_matcher_match_template(SynaMatcherContext *ctx,
                                const float *template_b,
                                float min_overlap,
                                SynaMatchResult *result)
{
    if (!ctx || !ctx->probe_set || !template_b || !result) return -1;

    float min_ov_pixels = min_overlap * (float)(IMG_H * IMG_W);
    bool evaluated[61] = { false };
    SynaMatchResult top_peaks[3];
    for (int k = 0; k < 3; k++) {
        top_peaks[k].score = -1.0f;
        top_peaks[k].angle = 0.0f;
        top_peaks[k].dy = 0;
        top_peaks[k].dx = 0;
        top_peaks[k].overlap = 0.0f;
    }

    /* Phase 1: Search step 2 deg across -30..+30 at full shift resolution (step 1, 1) */
    for (int deg = -30; deg <= 30; deg += 2) {
        evaluated[deg + 30] = true;
        SynaMatchResult cur;
        eval_single_angle_fast(ctx, deg, template_b, min_ov_pixels, 1, 1, &cur);
        if (cur.score > -0.5f) {
            insert_peak(top_peaks, &cur);
        }
    }

    *result = top_peaks[0];

    /* Phase 2: Fine refinement step 1 deg (+/- 1 deg) around top peaks */
    for (int k = 0; k < 3; k++) {
        if (top_peaks[k].score < 0.20f) continue;
        int peak_deg = (int)roundf(top_peaks[k].angle);

        for (int d = -1; d <= 1; d += 2) {
            int fine_deg = peak_deg + d;
            if (fine_deg < -30 || fine_deg > 30) continue;
            if (evaluated[fine_deg + 30]) continue;
            evaluated[fine_deg + 30] = true;

            SynaMatchResult cur;
            eval_single_angle_fast(ctx, fine_deg, template_b, min_ov_pixels, 1, 1, &cur);
            if (cur.score > result->score) {
                *result = cur;
            }
        }
        if (result->score >= 0.92f) break;
    }

    return 0;
}

int syna_match_images(const float *a,
                      const float *b,
                      float min_overlap,
                      SynaMatchResult *result)
{
    if (!a || !b || !result) return -1;

    SynaMatcherContext *ctx = syna_matcher_context_new();
    if (!ctx) return -2;

    syna_matcher_set_probe(ctx, a);
    int ret = syna_matcher_match_template(ctx, b, min_overlap, result);
    syna_matcher_context_free(ctx);
    return ret;
}

bool syna_is_match(const float *a, const float *b)
{
    SynaMatchResult res;
    if (syna_match_images(a, b, 0.65f, &res) != 0)
        return false;

    return (res.score >= SYNA_MATCH_THRESHOLD);
}
