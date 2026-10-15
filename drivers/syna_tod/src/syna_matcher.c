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
#define FFT_W 512

typedef struct {
    float r;
    float i;
} Complex;

static void fft_radix2(Complex *x, int n, int invert)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            Complex temp = x[i];
            x[i] = x[j];
            x[j] = temp;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        float angle = 2.0f * (float)M_PI / (float)len * (invert ? -1.0f : 1.0f);
        Complex wlen = { cosf(angle), sinf(angle) };
        for (int i = 0; i < n; i += len) {
            Complex w = { 1.0f, 0.0f };
            for (int j = 0; j < len / 2; j++) {
                Complex u = x[i + j];
                Complex v = {
                    x[i + j + len / 2].r * w.r - x[i + j + len / 2].i * w.i,
                    x[i + j + len / 2].r * w.i + x[i + j + len / 2].i * w.r
                };
                x[i + j].r = u.r + v.r;
                x[i + j].i = u.i + v.i;
                x[i + j + len / 2].r = u.r - v.r;
                x[i + j + len / 2].i = u.i - v.i;

                float next_wr = w.r * wlen.r - w.i * wlen.i;
                float next_wi = w.r * wlen.i + w.i * wlen.r;
                w.r = next_wr;
                w.i = next_wi;
            }
        }
    }

    if (invert) {
        float inv_n = 1.0f / (float)n;
        for (int i = 0; i < n; i++) {
            x[i].r *= inv_n;
            x[i].i *= inv_n;
        }
    }
}

static void fft2d(Complex *grid, int rows, int cols, int invert)
{
    Complex *buf = malloc((rows > cols ? rows : cols) * sizeof(Complex));
    if (!buf) return;

    /* Rows */
    for (int r = 0; r < rows; r++) {
        fft_radix2(&grid[r * cols], cols, invert);
    }

    /* Columns */
    for (int c = 0; c < cols; c++) {
        for (int r = 0; r < rows; r++) {
            buf[r] = grid[r * cols + c];
        }
        fft_radix2(buf, rows, invert);
        for (int r = 0; r < rows; r++) {
            grid[r * cols + c] = buf[r];
        }
    }

    free(buf);
}

int syna_bandpass_filter(const float *in, float *out)
{
    if (!in || !out) return -1;

    /* Pad to next power of 2 for filtering: 64x256 */
    const int pad_h = 64;
    const int pad_w = 256;
    Complex *grid = calloc(pad_h * pad_w, sizeof(Complex));
    if (!grid) return -2;

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

    fft2d(grid, pad_h, pad_w, 0);

    /* Filter in frequency domain: 0.08 <= radius < 0.30 cycles/pixel */
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

    fft2d(grid, pad_h, pad_w, 1);

    /* Extract original image size, compute variance */
    float var = 0.0f;
    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++) {
            float val = grid[r * pad_w + c].r;
            out[r * IMG_W + c] = val;
            var += val * val;
        }
    }

    free(grid);

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

int syna_match_images(const float *a,
                      const float *b,
                      float min_overlap,
                      SynaMatchResult *result)
{
    if (!a || !b || !result) return -1;

    size_t sz = FFT_H * FFT_W * sizeof(Complex);
    Complex *fa  = calloc(FFT_H * FFT_W, sizeof(Complex));
    Complex *fa2 = calloc(FFT_H * FFT_W, sizeof(Complex));
    Complex *f1  = calloc(FFT_H * FFT_W, sizeof(Complex));
    Complex *fb  = calloc(FFT_H * FFT_W, sizeof(Complex));
    Complex *fb2 = calloc(FFT_H * FFT_W, sizeof(Complex));
    Complex *fm  = calloc(FFT_H * FFT_W, sizeof(Complex));

    Complex *num = malloc(sz);
    Complex *ov  = malloc(sz);
    Complex *ea  = malloc(sz);
    Complex *eb  = malloc(sz);

    float *br = malloc(IMG_H * IMG_W * sizeof(float));
    float *mask = malloc(IMG_H * IMG_W * sizeof(float));

    if (!fa || !fa2 || !f1 || !fb || !fb2 || !fm || !num || !ov || !ea || !eb || !br || !mask) {
        free(fa); free(fa2); free(f1); free(fb); free(fb2); free(fm);
        free(num); free(ov); free(ea); free(eb); free(br); free(mask);
        return -2;
    }

    /* Compute fa, fa2, f1 once */
    for (int r = 0; r < IMG_H; r++) {
        for (int c = 0; c < IMG_W; c++) {
            float val = a[r * IMG_W + c];
            fa[r * FFT_W + c].r = val;
            fa2[r * FFT_W + c].r = val * val;
            f1[r * FFT_W + c].r = 1.0f;
        }
    }

    fft2d(fa, FFT_H, FFT_W, 0);
    fft2d(fa2, FFT_H, FFT_W, 0);
    fft2d(f1, FFT_H, FFT_W, 0);

    result->score = -1.0f;
    result->angle = 0.0f;
    result->dy = 0;
    result->dx = 0;
    result->overlap = 0.0f;

    float min_ov_pixels = min_overlap * (float)(IMG_H * IMG_W);

    for (int deg = -30; deg <= 30; deg += 2) {
        rotate_nearest(b, (float)deg, br, mask);

        memset(fb, 0, sz);
        memset(fb2, 0, sz);
        memset(fm, 0, sz);

        for (int r = 0; r < IMG_H; r++) {
            for (int c = 0; c < IMG_W; c++) {
                float v = br[r * IMG_W + c];
                float m = mask[r * IMG_W + c];
                fb[r * FFT_W + c].r = v;
                fb2[r * FFT_W + c].r = v * v;
                fm[r * FFT_W + c].r = m;
            }
        }

        fft2d(fb, FFT_H, FFT_W, 0);
        fft2d(fb2, FFT_H, FFT_W, 0);
        fft2d(fm, FFT_H, FFT_W, 0);

        /* Cross-correlations in freq domain */
        for (int i = 0; i < FFT_H * FFT_W; i++) {
            /* num = fa * conj(fb) */
            num[i].r = fa[i].r * fb[i].r + fa[i].i * fb[i].i;
            num[i].i = fa[i].i * fb[i].r - fa[i].r * fb[i].i;

            /* ov = f1 * conj(fm) */
            ov[i].r = f1[i].r * fm[i].r + f1[i].i * fm[i].i;
            ov[i].i = f1[i].i * fm[i].r - f1[i].r * fm[i].i;

            /* ea = fa2 * conj(fm) */
            ea[i].r = fa2[i].r * fm[i].r + fa2[i].i * fm[i].i;
            ea[i].i = fa2[i].i * fm[i].r - fa2[i].r * fm[i].i;

            /* eb = f1 * conj(fb2) */
            eb[i].r = f1[i].r * fb2[i].r + f1[i].i * fb2[i].i;
            eb[i].i = f1[i].i * fb2[i].r - f1[i].r * fb2[i].i;
        }

        fft2d(num, FFT_H, FFT_W, 1);
        fft2d(ov, FFT_H, FFT_W, 1);
        fft2d(ea, FFT_H, FFT_W, 1);
        fft2d(eb, FFT_H, FFT_W, 1);

        for (int r = 0; r < FFT_H; r++) {
            for (int c = 0; c < FFT_W; c++) {
                int idx = r * FFT_W + c;
                float cur_ov = ov[idx].r;
                if (cur_ov > min_ov_pixels) {
                    float cur_ea = ea[idx].r;
                    float cur_eb = eb[idx].r;
                    float denom = sqrtf(fmaxf(cur_ea * cur_eb, 1e-9f));
                    float score = num[idx].r / denom;

                    if (score > result->score) {
                        int dy = (r < IMG_H) ? r : (r - FFT_H);
                        int dx = (c < IMG_W) ? c : (c - FFT_W);
                        result->score = score;
                        result->angle = (float)deg;
                        result->dy = -dy;
                        result->dx = -dx;
                        result->overlap = cur_ov / (float)(IMG_H * IMG_W);
                    }
                }
            }
        }
    }

    free(fa); free(fa2); free(f1); free(fb); free(fb2); free(fm);
    free(num); free(ov); free(ea); free(eb); free(br); free(mask);
    return 0;
}

bool syna_is_match(const float *a, const float *b)
{
    SynaMatchResult res;
    if (syna_match_images(a, b, 0.65f, &res) != 0)
        return false;

    return (res.score >= SYNA_MATCH_THRESHOLD);
}
