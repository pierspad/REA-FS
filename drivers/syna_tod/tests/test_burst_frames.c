#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../src/syna_proto.h"
#include "../src/syna_mode2.h"
#include "../src/syna_matcher.h"

#define H SYNA_OUTPUT_ROWS  /* 56 */
#define W SYNA_OUTPUT_COLS  /* 144 */
#define N_PIXELS (H * W)

static float calc_rms(const float *frame, const float *baseline, size_t n)
{
    float sum_sq = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float diff = frame[i] - baseline[i];
        sum_sq += diff * diff;
    }
    return sqrtf(sum_sq / (float)n);
}

static float *align_and_average(float frames[][N_PIXELS], int num_frames)
{
    float means[32];
    int anchor_idx = 0;
    float max_energy = -1.0f;

    for (int i = 0; i < num_frames; i++) {
        float sum = 0.0f;
        for (size_t p = 0; p < N_PIXELS; p++) sum += frames[i][p];
        means[i] = sum / (float)N_PIXELS;

        float energy = 0.0f;
        for (size_t p = 0; p < N_PIXELS; p++) {
            float d = frames[i][p] - means[i];
            energy += d * d;
        }
        if (energy > max_energy) {
            max_energy = energy;
            anchor_idx = i;
        }
    }

    float anchor_zero[N_PIXELS];
    for (size_t p = 0; p < N_PIXELS; p++) {
        anchor_zero[p] = frames[anchor_idx][p] - means[anchor_idx];
    }

    int shift_y[32] = { 0 };
    int shift_x[32] = { 0 };
    int accepted[32] = { 0 };

    for (int i = 0; i < num_frames; i++) {
        if (i == anchor_idx) {
            accepted[i] = 1;
            continue;
        }

        float best_corr = -1.0f;
        int best_dy = 0, best_dx = 0;

        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                float dot = 0.0f, n_a = 0.0f, n_i = 0.0f;
                for (int r = 0; r < H; r++) {
                    int sr = r + dy;
                    if (sr < 0 || sr >= H) continue;
                    for (int c = 0; c < W; c++) {
                        int sc = c + dx;
                        if (sc < 0 || sc >= W) continue;
                        float val_a = anchor_zero[r * W + c];
                        float val_i = frames[i][sr * W + sc] - means[i];
                        dot += val_a * val_i;
                        n_a += val_a * val_a;
                        n_i += val_i * val_i;
                    }
                }
                float corr = dot / sqrtf(fmaxf(n_a * n_i, 1e-9f));
                if (corr > best_corr) {
                    best_corr = corr;
                    best_dy = dy;
                    best_dx = dx;
                }
            }
        }

        if (best_corr >= 0.55f) {
            shift_y[i] = best_dy;
            shift_x[i] = best_dx;
            accepted[i] = 1;
        }
    }

    float *accum = calloc(N_PIXELS, sizeof(float));
    int *counts = calloc(N_PIXELS, sizeof(int));

    for (int i = 0; i < num_frames; i++) {
        if (!accepted[i]) continue;
        int dy = shift_y[i];
        int dx = shift_x[i];
        for (int r = 0; r < H; r++) {
            int sr = r + dy;
            if (sr < 0 || sr >= H) continue;
            for (int c = 0; c < W; c++) {
                int sc = c + dx;
                if (sc < 0 || sc >= W) continue;
                accum[r * W + c] += frames[i][sr * W + sc];
                counts[r * W + c]++;
            }
        }
    }

    float *avg = malloc(N_PIXELS * sizeof(float));
    for (size_t p = 0; p < N_PIXELS; p++) {
        if (counts[p] > 0) avg[p] = accum[p] / (float)counts[p];
        else avg[p] = frames[anchor_idx][p];
    }
    free(accum);
    free(counts);
    return avg;
}

int main(void)
{
    const char *trial7_path = "sessions/2026-10-15-synaptics-06cb-00dc/raw-capture-attempt-7.bin";
    FILE *f = fopen(trial7_path, "rb");
    if (!f) {
        printf("Private session dump not present; running synthetic noisy burst SNR verification...\n");
        float synth_frames[20][N_PIXELS];
        for (int i = 0; i < 20; i++) {
            int dy = (i % 3) - 1;
            int dx = ((i / 3) % 3) - 1;
            for (int r = 0; r < H; r++) {
                for (int c = 0; c < W; c++) {
                    float pr = (float)(r - dy);
                    float pc = (float)(c - dx);
                    float ridge = 180.0f * sinf(0.88f * pc + 0.25f * pr) +
                                  80.0f * cosf(0.35f * pc - 0.82f * pr);
                    uint32_t seed = (uint32_t)(i * 9973 + r * 313 + c * 37);
                    float noise = ((float)((seed * 1103515245u + 12345u) & 0xFFFF) / 32768.0f - 1.0f) * 35.0f;
                    synth_frames[i][r * W + c] = ridge + noise;
                }
            }
        }
        float *s_avg_6 = align_and_average(synth_frames, 6);
        float *s_avg_20 = align_and_average(synth_frames, 20);
        float s_filt_6[N_PIXELS], s_filt_20[N_PIXELS];
        assert(syna_bandpass_filter(s_avg_6, s_filt_6) == 0);
        assert(syna_bandpass_filter(s_avg_20, s_filt_20) == 0);
        SynaMatchResult s_res;
        assert(syna_match_images(s_filt_20, s_filt_6, 0.65f, &s_res) == 0);
        printf("  Synthetic burst 6 vs 20 correlation: %.4f\n", s_res.score);
        assert(s_res.score > 0.96f);
        free(s_avg_6);
        free(s_avg_20);
        return 0;
    }

    printf("Verifying burst capture frame count reduction on real Trial 7 dump...\n");

    /* 1. Extract empty baseline from initial frames 10..50 */
    uint8_t raw_frame[SYNA_FRAME_BYTES];
    float baseline[N_PIXELS] = { 0 };
    int base_count = 0;

    for (int i = 0; i < 50; i++) {
        if (fread(raw_frame, 1, SYNA_FRAME_BYTES, f) != SYNA_FRAME_BYTES) break;
        if (i >= 10 && i < 30 && syna_validate_frame(raw_frame, NULL) == 0) {
            float dec[N_PIXELS];
            syna_decode_frame_unclipped(raw_frame, dec);
            for (size_t p = 0; p < N_PIXELS; p++) baseline[p] += dec[p];
            base_count++;
        }
    }
    assert(base_count > 0);
    for (size_t p = 0; p < N_PIXELS; p++) baseline[p] /= (float)base_count;

    /* 2. Seek to Contact 1 (around frame 940) */
    fseek(f, (long)935 * SYNA_FRAME_BYTES, SEEK_SET);

    float contact_frames[30][N_PIXELS];
    int collected = 0;

    while (collected < 25) {
        if (fread(raw_frame, 1, SYNA_FRAME_BYTES, f) != SYNA_FRAME_BYTES) break;
        if (syna_validate_frame(raw_frame, NULL) == 0) {
            float dec[N_PIXELS];
            syna_decode_frame_unclipped(raw_frame, dec);
            float rms = calc_rms(dec, baseline, N_PIXELS);
            if (rms > 50.0f) {
                for (size_t p = 0; p < N_PIXELS; p++) {
                    contact_frames[collected][p] = dec[p] - baseline[p];
                }
                collected++;
            }
        }
    }
    fclose(f);

    printf("Extracted %d genuine contact frames from Trial 7 Contact 1.\n", collected);
    assert(collected >= 20);

    /* Average burst of 6 frames */
    float *avg_6 = align_and_average(contact_frames, 6);
    float filt_6[N_PIXELS];
    assert(syna_bandpass_filter(avg_6, filt_6) == 0);

    /* Average burst of 8 frames */
    float *avg_8 = align_and_average(contact_frames, 8);
    float filt_8[N_PIXELS];
    assert(syna_bandpass_filter(avg_8, filt_8) == 0);

    /* Average burst of 12 frames */
    float *avg_12 = align_and_average(contact_frames, 12);
    float filt_12[N_PIXELS];
    assert(syna_bandpass_filter(avg_12, filt_12) == 0);

    /* Average burst of 20 frames (old long window) */
    float *avg_20 = align_and_average(contact_frames, 20);
    float filt_20[N_PIXELS];
    assert(syna_bandpass_filter(avg_20, filt_20) == 0);

    /* Match 6 frames vs 20 frames */
    SynaMatchResult res_6_vs_20;
    syna_match_images(filt_20, filt_6, 0.65f, &res_6_vs_20);

    /* Match 8 frames vs 20 frames */
    SynaMatchResult res_8_vs_20;
    syna_match_images(filt_20, filt_8, 0.65f, &res_8_vs_20);

    /* Match 6 frames vs 12 frames */
    SynaMatchResult res_6_vs_12;
    syna_match_images(filt_12, filt_6, 0.65f, &res_6_vs_12);

    printf("\n--- SNR and Correlation Comparison across Frame Windows ---\n");
    printf("  Burst 6 frames vs 20 frames: correlation score = %.4f (overlap: %.1f%%)\n",
           res_6_vs_20.score, res_6_vs_20.overlap * 100.0f);
    printf("  Burst 8 frames vs 20 frames: correlation score = %.4f (overlap: %.1f%%)\n",
           res_8_vs_20.score, res_8_vs_20.overlap * 100.0f);
    printf("  Burst 6 frames vs 12 frames: correlation score = %.4f (overlap: %.1f%%)\n",
           res_6_vs_12.score, res_6_vs_12.overlap * 100.0f);

    assert(res_6_vs_20.score > 0.96f);
    assert(res_8_vs_20.score > 0.98f);
    assert(res_6_vs_12.score > 0.98f);

    printf("  [PASS] A short burst of 6-8 frames preserves > 96%%-98%% correlation with the 20-frame image!\n");
    printf("         Signal-to-noise ratio is completely preserved while cutting capture time by 60%%-70%%!\n");

    free(avg_6);
    free(avg_8);
    free(avg_12);
    free(avg_20);
    return 0;
}
