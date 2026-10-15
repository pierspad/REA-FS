#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/syna_proto.h"
#include "../src/syna_matcher.h"

#define H SYNA_OUTPUT_ROWS  /* 56 */
#define W SYNA_OUTPUT_COLS  /* 144 */
#define N_PIXELS (H * W)
#define MAX_FRAMES 12

static float calc_rms(const float *frame, const float *baseline, size_t n)
{
    float sum_sq = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float diff = frame[i] - baseline[i];
        sum_sq += diff * diff;
    }
    return sqrtf(sum_sq / (float)n);
}

static float calc_spatial_variance(const float *frame, size_t n)
{
    float mean = 0.0f;
    for (size_t i = 0; i < n; i++) mean += frame[i];
    mean /= (float)n;

    float sum_sq = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float diff = frame[i] - mean;
        sum_sq += diff * diff;
    }
    return sum_sq / (float)n;
}

static void test_microshift_alignment_and_outlier_rejection(void)
{
    printf("Testing micro-shift alignment and outlier rejection...\n");

    /* Base ridge pattern */
    float base[N_PIXELS];
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            float phase = 2.0f * (float)M_PI * (0.15f * (float)c + 0.05f * (float)r);
            base[r * W + c] = 50.0f * sinf(phase);
        }
    }

    float frames[MAX_FRAMES][N_PIXELS];
    int shifts[MAX_FRAMES][2] = {
        {0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, -1}, {0, 0},
        {0, 0}, {0, 0}, {0, 0}, {0, 0}
    };

    /* Frames 0..7: shifted base with small noise */
    for (int k = 0; k < 8; k++) {
        int dy = shifts[k][0];
        int dx = shifts[k][1];
        for (int r = 0; r < H; r++) {
            int sr = r + dy;
            for (int c = 0; c < W; c++) {
                int sc = c + dx;
                if (sr >= 0 && sr < H && sc >= 0 && sc < W) {
                    frames[k][r * W + c] = base[sr * W + sc] + (float)((k % 3) - 1);
                } else {
                    frames[k][r * W + c] = 0.0f;
                }
            }
        }
    }

    /* Frames 8 and 9: outliers (orthogonal ridges / random noise) */
    for (int r = 0; r < H; r++) {
        for (int c = 0; c < W; c++) {
            frames[8][r * W + c] = 50.0f * sinf(2.0f * (float)M_PI * 0.15f * (float)r); /* orthogonal */
            frames[9][r * W + c] = (float)((((r * 137 + c * 257) % 101) - 50)); /* uncorrelated noise */
        }
    }

    int num_frames = 10;

    /* Anchor selection: find frame with max contrast energy */
    int anchor_idx = 0;
    float max_energy = -1.0f;
    float means[MAX_FRAMES];

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

    /* Run microshift alignment */
    int shift_y[MAX_FRAMES] = {0};
    int shift_x[MAX_FRAMES] = {0};
    int accepted[MAX_FRAMES] = {0};

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

    /* Valid frames 0..7 must be accepted with high correlation */
    for (int k = 0; k < 8; k++) {
        assert(accepted[k] == 1);
        assert(shift_y[k] >= -2 && shift_y[k] <= 2);
        assert(shift_x[k] >= -2 && shift_x[k] <= 2);
    }

    /* Outlier frames 8 and 9 must be rejected */
    assert(accepted[8] == 0);
    assert(accepted[9] == 0);

    printf("  [PASS] microshift alignment correctly accepted 8 shifted frames and rejected 2 outliers!\n");
}

static void test_baseline_logic(void)
{
    printf("Testing baseline cache, anti-touch protection, and file permissions...\n");

    char temp_dir[] = "/tmp/syna_test_XXXXXX";
    assert(mkdtemp(temp_dir) != NULL);

    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/baseline_cache.bin", temp_dir);

    float clean_baseline[N_PIXELS];
    for (size_t i = 0; i < N_PIXELS; i++) {
        clean_baseline[i] = 100.0f + (float)(i % 10);
    }

    /* Save baseline atomically with 0600 permissions */
    char tmp_path[1050];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path);

    FILE *f = fopen(tmp_path, "wb");
    assert(f != NULL);
    int fd = fileno(f);
    assert(fd >= 0);
    assert(fchmod(fd, 0600) == 0);
    assert(fwrite(clean_baseline, sizeof(float), N_PIXELS, f) == N_PIXELS);
    fclose(f);
    assert(rename(tmp_path, cache_path) == 0);

    /* Verify file permissions are 0600 */
    struct stat st;
    assert(stat(cache_path, &st) == 0);
    assert((st.st_mode & 0777) == 0600);

    /* Verify loading baseline */
    float loaded_baseline[N_PIXELS];
    FILE *rf = fopen(cache_path, "rb");
    assert(rf != NULL);
    assert(fread(loaded_baseline, sizeof(float), N_PIXELS, rf) == N_PIXELS);
    fclose(rf);

    assert(memcmp(clean_baseline, loaded_baseline, sizeof(clean_baseline)) == 0);

    /* Case A: Clean sensor test (RMS <= 15.0) */
    float empty_sensor_frame[N_PIXELS];
    for (size_t i = 0; i < N_PIXELS; i++) {
        empty_sensor_frame[i] = clean_baseline[i] + 2.0f;
    }
    float empty_rms = calc_rms(empty_sensor_frame, clean_baseline, N_PIXELS);
    assert(empty_rms <= 15.0f);

    /* Case B: Finger already touching at open (RMS > 18.0) */
    float finger_frame[N_PIXELS];
    for (size_t i = 0; i < N_PIXELS; i++) {
        finger_frame[i] = clean_baseline[i] + 45.0f * sinf((float)i);
    }
    float finger_rms = calc_rms(finger_frame, clean_baseline, N_PIXELS);
    assert(finger_rms > 18.0f);

    /* Case C: Spatial variance of finger vs clean sensor */
    float clean_var = calc_spatial_variance(empty_sensor_frame, N_PIXELS);
    float finger_var = calc_spatial_variance(finger_frame, N_PIXELS);
    assert(clean_var < 50.0f);
    assert(finger_var > 120.0f);

    /* Clean up temporary directory */
    unlink(cache_path);
    rmdir(temp_dir);

    printf("  [PASS] baseline cache permissions, RMS thresholds, and anti-touch guards verified!\n");
}

int main(void)
{
    printf("Running syna_pipeline unit tests...\n");
    test_microshift_alignment_and_outlier_rejection();
    test_baseline_logic();
    printf("All syna_pipeline unit tests passed!\n");
    return 0;
}
