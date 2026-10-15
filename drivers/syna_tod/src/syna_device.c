#include "syna_device.h"
#include "syna_mode2.h"
#include "syna_matcher.h"
#include "syna_proto.h"
#include "syna_template.h"
#include "syna_timing.h"
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

G_DEFINE_TYPE (FpiDeviceSyna00dc, fpi_device_syna_00dc, FP_TYPE_DEVICE)

GType
fpi_tod_shared_driver_get_type (void)
{
    return FPI_TYPE_DEVICE_SYNA_00DC;
}

static const FpIdEntry syna_ids[] = {
    { { { .pid = SYNA_USB_PID, .vid = SYNA_USB_VID } }, 0 },
    { { { 0, 0 } }, 0 }
};

static void
fpi_device_syna_00dc_init (FpiDeviceSyna00dc *self)
{
    self->is_open = FALSE;
    self->main_context = g_main_context_ref_thread_default ();
    self->is_cancelling = FALSE;
    self->capture_pipeline_ready = FALSE;
    self->baseline = NULL;
    self->has_baseline = FALSE;
    self->initial_contact_present = FALSE;
    self->enrolled_templates = g_ptr_array_new_with_free_func (g_free);
}

static void
fpi_device_syna_00dc_finalize (GObject *object)
{
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (object);

    if (self->enrolled_templates) {
        g_ptr_array_unref (self->enrolled_templates);
        self->enrolled_templates = NULL;
    }
    if (self->baseline) {
        g_free (self->baseline);
        self->baseline = NULL;
    }
    syna_tls_free (&self->tls);
    syna_oem_free (&self->oem);
    g_main_context_unref (self->main_context);

    G_OBJECT_CLASS (fpi_device_syna_00dc_parent_class)->finalize (object);
}

typedef struct {
    FpiDeviceSyna00dc *self;
    FpFingerStatusFlags added, removed;
    guint stage;
    GError *retry_error;
} SynaEvent;

static gboolean
syna_report_event (gpointer data)
{
    SynaEvent *event = data;
    if (event->retry_error) {
        fpi_device_enroll_progress (FP_DEVICE (event->self), event->stage, NULL, event->retry_error);
        g_error_free (event->retry_error);
    } else if (event->stage) {
        fpi_device_enroll_progress (FP_DEVICE (event->self), event->stage, NULL, NULL);
    } else {
        fpi_device_report_finger_status_changes (FP_DEVICE (event->self), event->added, event->removed);
    }
    g_object_unref (event->self);
    g_free (event);
    return G_SOURCE_REMOVE;
}

static void
syna_report_status (FpiDeviceSyna00dc *self, FpFingerStatusFlags added, FpFingerStatusFlags removed)
{
    SynaEvent *event = g_new0 (SynaEvent, 1);
    event->self = g_object_ref (self);
    event->added = added;
    event->removed = removed;
    g_main_context_invoke_full (self->main_context, G_PRIORITY_DEFAULT, syna_report_event, event, NULL);
}

static void
syna_report_stage (FpiDeviceSyna00dc *self, guint stage)
{
    SynaEvent *event = g_new0 (SynaEvent, 1);
    event->self = g_object_ref (self);
    event->stage = stage;
    g_main_context_invoke_full (self->main_context, G_PRIORITY_DEFAULT, syna_report_event, event, NULL);
}

static void
syna_report_enroll_retry (FpiDeviceSyna00dc *self, guint stage, FpDeviceRetry retry_type)
{
    SynaEvent *event = g_new0 (SynaEvent, 1);
    event->self = g_object_ref (self);
    event->stage = stage;
    event->retry_error = fpi_device_retry_new (retry_type);
    g_main_context_invoke_full (self->main_context, G_PRIORITY_DEFAULT, syna_report_event, event, NULL);
}

/* Forward declarations */
static gboolean
syna_read_full_frame (GUsbDevice *usb_device, uint8_t *frame_buf, gint64 deadline, GCancellable *cancellable, GError **error);

/* Helper to execute a synchronous USB bulk exchange */
static gboolean
syna_usb_cmd (GUsbDevice *usb_dev,
              const uint8_t *cmd, size_t cmd_len,
              uint8_t *resp, size_t max_resp_len, size_t *actual_resp_len,
              GCancellable *cancellable, GError **error)
{
    gsize transferred = 0;
    if (!g_usb_device_bulk_transfer (usb_dev, SYNA_EP_CMD_OUT,
                                     (guint8 *)cmd, cmd_len,
                                     &transferred, 3000,
                                     cancellable, error)) {
        return FALSE;
    }

    if (!g_usb_device_bulk_transfer (usb_dev, SYNA_EP_CMD_IN,
                                     (guint8 *)resp, max_resp_len,
                                     &transferred, 5000,
                                     cancellable, error)) {
        return FALSE;
    }

    if (actual_resp_len)
        *actual_resp_len = transferred;

    return TRUE;
}

/* Helper to send encrypted application payload and decrypt reply */
static gboolean
syna_tls_app_exchange (FpiDeviceSyna00dc *self,
                       const uint8_t *req, size_t req_len,
                       uint8_t *resp, size_t max_resp_len, size_t *resp_len,
                       GCancellable *cancellable, GError **error)
{
    size_t enc_alloc = req_len + 512;
    uint8_t *enc = g_malloc (enc_alloc);
    size_t enc_len = 0;

    if (syna_tls_encrypt_app_data (&self->tls, req, req_len, enc, &enc_len) != 0) {
        g_free (enc);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "TLS encryption failed");
        return FALSE;
    }

    uint8_t wire_resp[4096];
    size_t wire_resp_len = 0;
    gboolean ok = syna_usb_cmd (self->usb_device, enc, enc_len, wire_resp, sizeof (wire_resp), &wire_resp_len, cancellable, error);
    g_free (enc);

    if (!ok) {
        return FALSE;
    }

    if (syna_tls_decrypt_app_data (&self->tls, wire_resp, wire_resp_len, resp, max_resp_len, resp_len) != 0) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "TLS response decryption/HMAC failed");
        return FALSE;
    }

    uint16_t status = 0xffff;
    if (!req_len || !syna_response_ok (req[0], resp, *resp_len, &status)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Sensor rejected command 0x%02x: status 0x%04x (reply %zu bytes)",
                     req_len ? req[0] : 0, status, *resp_len);
        return FALSE;
    }
    return TRUE;
}

/* Helper to initialize OEM capture pipeline (0x1a pre-cleanup + 0x06 capture init blob) */
static gboolean
syna_prepare_capture_pipeline (FpiDeviceSyna00dc *self, GCancellable *cancellable, GError **error)
{
    /* 1. Pre-config cleanup (0x1a) */
    uint8_t clean_cmd = SYNA_CMD_CLEANUP;
    uint8_t clean_resp[64];
    size_t clean_resp_len = 0;
    self->capture_pipeline_ready = FALSE;
    if (!syna_tls_app_exchange (self, &clean_cmd, 1, clean_resp, sizeof (clean_resp),
                                &clean_resp_len, cancellable, error))
        return FALSE;

    /* 2. Capture initializer (0x06 + blob) */
    size_t init_req_len = 1 + self->oem.capture_init_blob_len;
    uint8_t *init_req = g_malloc (init_req_len);
    init_req[0] = SYNA_CMD_INIT;
    memcpy (init_req + 1, self->oem.capture_init_blob, self->oem.capture_init_blob_len);

    uint8_t init_resp[64];
    size_t init_resp_len = 0;
    gboolean init_ok = syna_tls_app_exchange (self, init_req, init_req_len, init_resp, sizeof (init_resp), &init_resp_len, cancellable, error);
    g_free (init_req);

    if (init_ok) {
        self->capture_pipeline_ready = TRUE;
    }
    return init_ok;
}

static float s_cached_baseline[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
static gboolean s_has_cached_baseline = FALSE;
static GMutex s_baseline_mutex;

static char *
get_baseline_cache_path (const char *custom_dir)
{
    const char *dir = custom_dir;
    if (!dir) dir = getenv ("SYNA_TOD_DATA_DIR");
    if (!dir) dir = SYNA_DEFAULT_DATA_DIR;
    return g_build_filename (dir, "baseline_cache.bin", NULL);
}

static gboolean
syna_load_baseline_cache (const char *dir, float *out_baseline)
{
    char *path = get_baseline_cache_path (dir);
    FILE *f = fopen (path, "rb");
    g_free (path);
    if (!f) return FALSE;

    size_t expected = SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES;
    size_t read_count = fread (out_baseline, sizeof (float), expected, f);
    fclose (f);
    return (read_count == expected);
}

static void
syna_save_baseline_cache (const char *dir, const float *baseline)
{
    char *path = get_baseline_cache_path (dir);
    char *tmp_path = g_strdup_printf ("%s.tmp.%d", path, (int)getpid ());

    FILE *f = fopen (tmp_path, "wb");
    if (f) {
        int fd = fileno (f);
        if (fd >= 0) {
            fchmod (fd, 0600);
        }
        size_t expected = SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES;
        size_t written = fwrite (baseline, sizeof (float), expected, f);
        fclose (f);
        if (written == expected) {
            rename (tmp_path, path);
        } else {
            unlink (tmp_path);
        }
    }
    g_free (tmp_path);
    g_free (path);
}

static float
calc_rms (const float *frame, const float *baseline, size_t n)
{
    float sum_sq = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float diff = frame[i] - baseline[i];
        sum_sq += diff * diff;
    }
    return sqrtf (sum_sq / (float)n);
}

static float
calc_spatial_variance (const float *frame, size_t n)
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

/* Helper to calibrate empty sensor baseline upon device open */
static gboolean
syna_calibrate_baseline (FpiDeviceSyna00dc *self, GCancellable *cancellable, GError **error)
{
    /* Start capture stream to measure baseline */
    uint8_t cap_resp[4096];
    size_t cap_resp_len = 0;
    if (!syna_tls_app_exchange (self, self->oem.capture_packet, self->oem.capture_packet_len,
                                cap_resp, sizeof (cap_resp), &cap_resp_len, cancellable, error)) {
        return FALSE;
    }

    if (!self->baseline) {
        self->baseline = g_malloc0 (SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    }

    g_mutex_lock (&s_baseline_mutex);
    if (!s_has_cached_baseline) {
        s_has_cached_baseline = syna_load_baseline_cache (NULL, s_cached_baseline);
    }

    uint8_t frame_buf[SYNA_FRAME_BYTES];
    size_t total_pixels = SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES;
    int target_frames = s_has_cached_baseline ? 4 : 10;
    int base_count = 0;
    float *accumulated = g_malloc0 (total_pixels * sizeof (float));
    float rms_sum = 0.0f;
    float max_var = 0.0f;

    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;
    while (base_count < target_frames && g_get_monotonic_time () < deadline) {
        if (!syna_read_full_frame (self->usb_device, frame_buf, deadline, cancellable, error)) {
            break;
        }

        if (syna_validate_frame (frame_buf, NULL) == 0) {
            float dec[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
            syna_decode_frame_unclipped (frame_buf, dec);

            if (s_has_cached_baseline) {
                float r = calc_rms (dec, s_cached_baseline, total_pixels);
                rms_sum += r;
            } else {
                float v = calc_spatial_variance (dec, total_pixels);
                max_var = fmaxf (max_var, v);
            }

            for (size_t i = 0; i < total_pixels; i++) {
                accumulated[i] += dec[i];
            }
            base_count++;
        }
    }

    /* Send stop command 0x04 */
    uint8_t stop_cmd = SYNA_CMD_STOP;
    uint8_t stop_resp[64];
    size_t stop_resp_len = 0;
    GError *stop_error = NULL;
    if (!syna_tls_app_exchange (self, &stop_cmd, 1, stop_resp, sizeof (stop_resp),
                                &stop_resp_len, NULL, &stop_error)) {
        if (error && !*error) g_propagate_error (error, stop_error);
        else g_clear_error (&stop_error);
    }

    /* Drain bulk EP 130 */
    uint8_t drain_buf[16384];
    gsize drained = 0;
    for (int d = 0; d < 10; d++) {
        if (!g_usb_device_bulk_transfer (self->usb_device, SYNA_EP_DATA_IN,
                                         drain_buf, sizeof (drain_buf),
                                         &drained, 25, NULL, NULL) || drained == 0) {
            break;
        }
    }

    self->capture_pipeline_ready = FALSE;

    if (error && *error) {
        g_free (accumulated);
        g_mutex_unlock (&s_baseline_mutex);
        return FALSE;
    }

    if (base_count < (s_has_cached_baseline ? 2 : 5)) {
        g_free (accumulated);
        g_mutex_unlock (&s_baseline_mutex);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to read baseline frames from sensor");
        return FALSE;
    }

    self->initial_contact_present = FALSE;

    if (s_has_cached_baseline) {
        float avg_rms = rms_sum / (float)base_count;
        if (avg_rms > 18.0f) {
            /* User finger is already touching sensor! Retain clean cached baseline. */
            g_message ("Open: finger contact present at startup (RMS=%.1f > 18.0). Retaining clean cached baseline.", avg_rms);
            memcpy (self->baseline, s_cached_baseline, total_pixels * sizeof (float));
            self->has_baseline = TRUE;
            self->initial_contact_present = TRUE;
            g_free (accumulated);
            g_mutex_unlock (&s_baseline_mutex);
            return TRUE;
        }

        /* Clean empty sensor: update baseline */
        for (size_t i = 0; i < total_pixels; i++) {
            self->baseline[i] = accumulated[i] / (float)base_count;
        }
        memcpy (s_cached_baseline, self->baseline, total_pixels * sizeof (float));
        syna_save_baseline_cache (NULL, self->baseline);
        self->has_baseline = TRUE;
        g_free (accumulated);
        g_mutex_unlock (&s_baseline_mutex);
        return TRUE;
    }

    /* First-time calibration without cache */
    if (max_var > 120.0f) {
        g_free (accumulated);
        g_mutex_unlock (&s_baseline_mutex);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "Sensor not empty during initial calibration");
        return FALSE;
    }

    for (size_t i = 0; i < total_pixels; i++) {
        self->baseline[i] = accumulated[i] / (float)base_count;
    }
    memcpy (s_cached_baseline, self->baseline, total_pixels * sizeof (float));
    s_has_cached_baseline = TRUE;
    syna_save_baseline_cache (NULL, self->baseline);
    self->has_baseline = TRUE;
    g_free (accumulated);
    g_mutex_unlock (&s_baseline_mutex);
    return TRUE;
}

/* Background thread for OPEN */
static void
syna_open_thread (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)task_data;
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (source_object);
    GError *error = NULL;

    self->usb_device = fpi_device_get_usb_device (FP_DEVICE (self));
    if (!self->usb_device) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No USB device associated");
        return;
    }

    /* Reset USB interface to ensure clean state */
    g_usb_device_reset (self->usb_device, NULL);

    if (!g_usb_device_claim_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, &error)) {
        g_task_return_error (task, error);
        return;
    }

    /* Load OEM blobs and host credentials */
    if (syna_oem_load (NULL, &self->oem) != 0) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Failed to load Synaptics OEM pairing credentials");
        return;
    }

    /* Init TLS */
    if (syna_tls_init (&self->tls, self->oem.host_key_pem, self->oem.host_cert, self->oem.host_cert_len,
                       self->oem.server_ecdh_pub, self->oem.server_ecdh_pub_len) != 0) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to initialize TLS context");
        return;
    }

    /* 1. ClientHello */
    uint8_t hello[128];
    size_t hello_len = 0;
    syna_tls_build_client_hello (&self->tls, hello, &hello_len);

    uint8_t s_hello[512];
    size_t s_hello_len = 0;
    if (!syna_usb_cmd (self->usb_device, hello, hello_len, s_hello, sizeof (s_hello), &s_hello_len, cancellable, &error)) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_error (task, error);
        return;
    }

    if (syna_tls_handle_server_hello (&self->tls, s_hello, s_hello_len) != 0) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "ServerHello parsing failed");
        return;
    }

    /* 2. Flight 2 (KEX, Cert, Finished) */
    uint8_t flight2[1024];
    size_t flight2_len = 0;
    if (syna_tls_build_flight2 (&self->tls, flight2, &flight2_len) != 0) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to build TLS flight 2");
        return;
    }

    uint8_t s_fin[512];
    size_t s_fin_len = 0;
    if (!syna_usb_cmd (self->usb_device, flight2, flight2_len, s_fin, sizeof (s_fin), &s_fin_len, cancellable, &error)) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_error (task, error);
        return;
    }

    if (syna_tls_handle_flight2_reply (&self->tls, s_fin, s_fin_len) != 0) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "TLS Finished verification failed");
        return;
    }

    /* 3. Prepare capture pipeline (0x1a cleanup + 0x06 init) */
    if (!syna_prepare_capture_pipeline (self, cancellable, &error)) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_error (task, error);
        return;
    }

    self->has_baseline = FALSE;
    /* 4. Calibrate clean empty sensor baseline */
    if (!syna_calibrate_baseline (self, cancellable, &error)) {
        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        g_task_return_error (task, error);
        return;
    }

    self->is_open = TRUE;
    g_task_return_boolean (task, TRUE);
}

static void
on_open_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    FpDevice *device = FP_DEVICE (source_object);
    GError *error = NULL;
    g_task_propagate_boolean (G_TASK (res), &error);
    fpi_device_open_complete (device, error);
}

static void
syna_open (FpDevice *device)
{
    GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                              on_open_completed, NULL);
    g_task_run_in_thread (task, syna_open_thread);
    g_object_unref (task);
}

/* Background thread for CLOSE */
static void
syna_close_thread (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)task_data;
    (void)cancellable;
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (source_object);

    if (self->is_open && self->usb_device) {
        /* Send TLS close_notify */
        uint8_t close_buf[128];
        size_t close_len = 0;
        if (syna_tls_build_close_notify (&self->tls, close_buf, &close_len) == 0) {
            gsize transferred = 0;
            g_usb_device_bulk_transfer (self->usb_device, SYNA_EP_CMD_OUT, close_buf, close_len, &transferred, 1000, NULL, NULL);
        }

        g_usb_device_release_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, NULL);
        self->is_open = FALSE;
    }

    syna_tls_free (&self->tls);
    syna_oem_free (&self->oem);

    g_task_return_boolean (task, TRUE);
}

static void
on_close_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    FpDevice *device = FP_DEVICE (source_object);
    GError *error = NULL;
    g_task_propagate_boolean (G_TASK (res), &error);
    fpi_device_close_complete (device, error);
}

static void
syna_close (FpDevice *device)
{
    GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                              on_close_completed, NULL);
    g_task_run_in_thread (task, syna_close_thread);
    g_object_unref (task);
}

/* Helper to read a complete 17024-byte frame from bulk EP */
static gboolean
syna_read_full_frame (GUsbDevice *usb_device, uint8_t *frame_buf, gint64 deadline, GCancellable *cancellable, GError **error)
{
    size_t total = 0;
    while (total < SYNA_FRAME_BYTES) {
        if (g_cancellable_is_cancelled (cancellable)) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Capture cancelled");
            return FALSE;
        }
        guint timeout_ms = syna_remaining_timeout (deadline, g_get_monotonic_time ());
        if (!timeout_ms) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Frame deadline exceeded");
            return FALSE;
        }
        gsize chunk = 0;
        GError *local_err = NULL;
        if (!g_usb_device_bulk_transfer (usb_device, SYNA_EP_DATA_IN,
                                         frame_buf + total, SYNA_FRAME_BYTES - total,
                                         &chunk, timeout_ms, cancellable, &local_err)) {
            if (local_err) {
                g_propagate_error (error, local_err);
            }
            return FALSE;
        }
        if (chunk == 0) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Zero bytes read from bulk stream");
            return FALSE;
        }
        total += chunk;
    }
    return TRUE;
}

#define MAX_CONTACT_FRAMES 12
#define MIN_CONTACT_FRAMES 6

static float *
align_and_average_frames (float frames[MAX_CONTACT_FRAMES][SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES],
                          int num_frames,
                          float *out_quality)
{
    const int H = SYNA_OUTPUT_ROWS;
    const int W = SYNA_OUTPUT_COLS;
    const size_t N_PIXELS = (size_t)H * (size_t)W;

    if (num_frames <= 0) return NULL;

    /* 1. Calculate mean and contrast energy for each frame */
    float means[MAX_CONTACT_FRAMES];
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

    if (out_quality) {
        *out_quality = max_energy / (float)N_PIXELS;
    }

    /* 2. Anchor zero-mean image */
    float anchor_zero[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
    for (size_t p = 0; p < N_PIXELS; p++) {
        anchor_zero[p] = frames[anchor_idx][p] - means[anchor_idx];
    }

    /* 3. Micro-shift alignment: search dy, dx in [-2, +2] */
    int shift_y[MAX_CONTACT_FRAMES] = { 0 };
    int shift_x[MAX_CONTACT_FRAMES] = { 0 };
    gboolean accepted[MAX_CONTACT_FRAMES] = { FALSE };

    for (int i = 0; i < num_frames; i++) {
        if (i == anchor_idx) {
            shift_y[i] = 0;
            shift_x[i] = 0;
            accepted[i] = TRUE;
            continue;
        }

        float best_corr = -1.0f;
        int best_dy = 0;
        int best_dx = 0;

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
                float corr = dot / sqrtf (fmaxf (n_a * n_i, 1e-9f));
                if (corr > best_corr) {
                    best_corr = corr;
                    best_dy = dy;
                    best_dx = dx;
                }
            }
        }

        /* Accept frame if correlation with anchor >= 0.55 */
        if (best_corr >= 0.55f) {
            shift_y[i] = best_dy;
            shift_x[i] = best_dx;
            accepted[i] = TRUE;
        } else {
            accepted[i] = FALSE;
        }
    }

    /* 4. Average accepted frames */
    float *accum = g_malloc0 (N_PIXELS * sizeof (float));
    int *counts = g_malloc0 (N_PIXELS * sizeof (int));

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

    float *avg = g_malloc (N_PIXELS * sizeof (float));
    for (size_t p = 0; p < N_PIXELS; p++) {
        if (counts[p] > 0)
            avg[p] = accum[p] / (float)counts[p];
        else
            avg[p] = frames[anchor_idx][p];
    }

    g_free (accum);
    g_free (counts);
    return avg;
}

/* Helper to capture a single finger sample from USB bulk stream */
static float *
syna_capture_finger_sample (FpiDeviceSyna00dc *self, gboolean more_stages_needed, GCancellable *cancellable, GError **error)
{
    /* 1. Ensure clean baseline is available */
    if (!self->has_baseline) {
        if (!syna_calibrate_baseline (self, cancellable, error)) {
            return NULL;
        }
    }

    /* 2. Ensure capture pipeline is initialized with OEM initializer */
    if (!self->capture_pipeline_ready) {
        if (!syna_prepare_capture_pipeline (self, cancellable, error)) {
            return NULL;
        }
    }

    /* 3. Send capture command 0x02 + packet */
    uint8_t cap_resp[4096];
    size_t cap_resp_len = 0;
    if (!syna_tls_app_exchange (self, self->oem.capture_packet, self->oem.capture_packet_len,
                                cap_resp, sizeof (cap_resp), &cap_resp_len, cancellable, error)) {
        return NULL;
    }

    uint8_t frame_buf[SYNA_FRAME_BYTES];
    float collected_frames[MAX_CONTACT_FRAMES][SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
    int valid_contact_frames = 0;
    int transient_seen = 0;
    gboolean sensor_cleared = FALSE;
    gboolean finger_detected = FALSE;
    gboolean waiting_for_lift = FALSE;
    int empty_consecutive_frames = 0;

    if (self->initial_contact_present) {
        sensor_cleared = TRUE;
        finger_detected = TRUE;
        self->initial_contact_present = FALSE;
        syna_report_status (self, FP_FINGER_STATUS_PRESENT, FP_FINGER_STATUS_NONE);
    }

    gint64 deadline = g_get_monotonic_time () + 60 * G_TIME_SPAN_SECOND;
    float min_rms = INFINITY, max_rms = 0.0f;

    while (valid_contact_frames < MAX_CONTACT_FRAMES) {
        if (g_get_monotonic_time () >= deadline) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Capture deadline exceeded (60 seconds)");
            break;
        }
        if (g_cancellable_is_cancelled (cancellable)) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Capture cancelled");
            break;
        }

        if (!syna_read_full_frame (self->usb_device, frame_buf, deadline, cancellable, error)) {
            break;
        }

        if (syna_validate_frame (frame_buf, NULL) == 0) {
            float dec[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
            syna_decode_frame_unclipped (frame_buf, dec);

            float rms = calc_rms (dec, self->baseline, G_N_ELEMENTS (dec));
            min_rms = fminf (min_rms, rms);
            max_rms = fmaxf (max_rms, rms);

            /* Phase 1: Wait for clear sensor if not already cleared */
            if (!sensor_cleared) {
                if (rms > 28.0f) {
                    empty_consecutive_frames = 0;
                    if (!waiting_for_lift) {
                        waiting_for_lift = TRUE;
                        g_print ("\n>>> [SENSOR] >>> Finger still on sensor. Please lift your finger...\n");
                        fflush (stdout);
                    }
                    continue;
                } else {
                    empty_consecutive_frames++;
                    if (empty_consecutive_frames >= 2) {
                        sensor_cleared = TRUE;
                        if (waiting_for_lift) {
                            g_print (">>> [SENSOR] >>> Finger lifted! Ready.\n");
                            fflush (stdout);
                        }
                        syna_report_status (self, FP_FINGER_STATUS_NEEDED, FP_FINGER_STATUS_NONE);
                    }
                    continue;
                }
            }

            /* Phase 2: Touch detection */
            if (rms > 35.0f) {
                if (!finger_detected) {
                    finger_detected = TRUE;
                    syna_report_status (self, FP_FINGER_STATUS_PRESENT, FP_FINGER_STATUS_NEEDED);
                }

                transient_seen++;
                if (transient_seen <= 2) {
                    /* Discard first 2 transient contact frames */
                    continue;
                }

                /* Store contact frame minus baseline */
                for (size_t i = 0; i < G_N_ELEMENTS (dec); i++) {
                    collected_frames[valid_contact_frames][i] = dec[i] - self->baseline[i];
                }
                valid_contact_frames++;
            } else if (finger_detected && valid_contact_frames >= MIN_CONTACT_FRAMES) {
                /* Finger lifted after collecting at least MIN_CONTACT_FRAMES */
                break;
            }
        }
    }

    /* Phase 3: Wait for physical release if finger was touched */
    if (finger_detected) {
        int release_clear_count = 0;
        gint64 release_deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
        while (release_clear_count < 3 && g_get_monotonic_time () < release_deadline) {
            if (g_cancellable_is_cancelled (cancellable)) break;
            if (!syna_read_full_frame (self->usb_device, frame_buf, release_deadline, cancellable, NULL)) break;
            if (syna_validate_frame (frame_buf, NULL) == 0) {
                float dec[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
                syna_decode_frame_unclipped (frame_buf, dec);
                float rms = calc_rms (dec, self->baseline, G_N_ELEMENTS (dec));
                if (rms <= 28.0f) {
                    release_clear_count++;
                } else {
                    release_clear_count = 0;
                }
            }
        }

        /* Now update status after physical release */
        if (more_stages_needed) {
            syna_report_status (self, FP_FINGER_STATUS_NEEDED, FP_FINGER_STATUS_PRESENT);
        } else {
            syna_report_status (self, FP_FINGER_STATUS_NONE, FP_FINGER_STATUS_PRESENT);
        }
    } else if (sensor_cleared) {
        syna_report_status (self, FP_FINGER_STATUS_NONE, FP_FINGER_STATUS_NEEDED);
    }

    g_message ("Capture summary: clear=%d valid_frames=%d RMS=%.1f..%.1f",
               sensor_cleared, valid_contact_frames, min_rms, max_rms);

    /* Send stop command 0x04 */
    uint8_t stop_cmd = SYNA_CMD_STOP;
    uint8_t stop_resp[64];
    size_t stop_resp_len = 0;
    GError *stop_error = NULL;
    if (!syna_tls_app_exchange (self, &stop_cmd, 1, stop_resp, sizeof (stop_resp),
                                &stop_resp_len, NULL, &stop_error)) {
        if (error && !*error) g_propagate_error (error, stop_error);
        else g_clear_error (&stop_error);
    }

    /* Drain any leftover bulk packets on EP 130 */
    uint8_t drain_buf[16384];
    gsize drained = 0;
    for (int d = 0; d < 10; d++) {
        if (!g_usb_device_bulk_transfer (self->usb_device, SYNA_EP_DATA_IN,
                                         drain_buf, sizeof (drain_buf),
                                         &drained, 25, NULL, NULL) || drained == 0) {
            break;
        }
    }

    self->capture_pipeline_ready = FALSE;

    if ((error && *error) || valid_contact_frames < MIN_CONTACT_FRAMES) {
        if (error && !*error) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "No sustained finger contact detected");
        }
        return NULL;
    }

    /* Motion-blur-free alignment and averaging */
    float quality = 0.0f;
    float *contact_avg = align_and_average_frames (collected_frames, valid_contact_frames, &quality);
    if (!contact_avg) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Frame alignment failed");
        return NULL;
    }

    if (quality < 50.0f) {
        g_message ("Capture rejected: low contrast energy / flat ridge profile (%.1f)", quality);
        g_free (contact_avg);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Scan quality too low");
        return NULL;
    }

    float *filtered = g_malloc (SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    if (syna_bandpass_filter (contact_avg, filtered) != 0) {
        g_free (contact_avg);
        g_free (filtered);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Bandpass filtering failed");
        return NULL;
    }
    g_free (contact_avg);

    return filtered;
}

/* Background thread for ENROLL */
static void
syna_enroll_thread (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)task_data;
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (source_object);
    GError *error = NULL;

    GPtrArray *samples = g_ptr_array_new_with_free_func (g_free);
    FpPrint *print = FP_PRINT (task_data);

    guint stage = 0;
    while (stage < SYNA_ENROLL_STAGES) {
        if (g_cancellable_is_cancelled (cancellable)) {
            g_ptr_array_unref (samples);
            g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Enrollment cancelled");
            return;
        }

        gboolean more_stages = (stage + 1 < SYNA_ENROLL_STAGES);
        float *sample = syna_capture_finger_sample (self, more_stages, cancellable, &error);
        if (!sample) {
            if (error && (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
                          g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))) {
                g_ptr_array_unref (samples);
                g_task_return_error (task, error);
                return;
            }
            g_clear_error (&error);
            syna_report_enroll_retry (self, stage, FP_DEVICE_RETRY_REMOVE_FINGER);
            continue;
        }

        g_ptr_array_add (samples, sample);
        stage++;
        syna_report_stage (self, stage);
    }

    GVariant *encoded = g_variant_ref_sink (syna_template_encode (samples));
    g_ptr_array_unref (samples);
    (void)print;
    g_task_return_pointer (task, encoded, (GDestroyNotify)g_variant_unref);
}

static void
on_enroll_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    FpDevice *device = FP_DEVICE (source_object);
    GError *error = NULL;
    GVariant *encoded = g_task_propagate_pointer (G_TASK (res), &error);
    FpPrint *print = g_task_get_task_data (G_TASK (res));
    if (encoded) {
        fpi_print_set_type (print, FPI_PRINT_RAW);
        g_object_set (print, "fpi-data", encoded, NULL);
        g_variant_unref (encoded);
    }
    fpi_device_enroll_complete (device, encoded ? g_object_ref (print) : NULL, error);
}

static void
syna_enroll (FpDevice *device)
{
    GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                              on_enroll_completed, NULL);
    FpPrint *print = NULL;
    fpi_device_get_enroll_data (device, &print);
    g_task_set_task_data (task, g_object_ref (print), g_object_unref);
    g_task_run_in_thread (task, syna_enroll_thread);
    g_object_unref (task);
}

/* Background thread for VERIFY */
static void
syna_verify_thread (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)task_data;
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (source_object);
    GError *error = NULL;

    GPtrArray *samples = task_data;
    if (!samples) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Invalid or unsupported enrolled template; enroll again");
        return;
    }

    float *live_sample = syna_capture_finger_sample (self, FALSE, cancellable, &error);
    if (!live_sample) {
        g_task_return_error (task, error);
        return;
    }

    SynaMatchResult match_res = { .score = -1.0f };
    for (guint i = 0; i < samples->len; i++) {
        SynaMatchResult candidate;
        if (syna_match_images (samples->pdata[i], live_sample, 0.65f, &candidate) != 0) {
            g_free (live_sample);
            g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Matcher failed");
            return;
        }
        if (candidate.score > match_res.score) match_res = candidate;
        if (candidate.score >= 0.70f) {
            g_message ("Synaptics verify: early match exit on template %u (score: %.3f >= 0.70)",
                       i, candidate.score);
            break;
        }
    }
    g_free (live_sample);

    g_message ("Synaptics verify score: %.3f (threshold: %.2f, rotation: %.1f deg, overlap: %.1f%%)",
               match_res.score, (float)SYNA_MATCH_THRESHOLD, match_res.angle, match_res.overlap * 100.0f);

    SynaMatchResult *result = g_new (SynaMatchResult, 1);
    *result = match_res;
    g_task_return_pointer (task, result, g_free);
}

static void
on_verify_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    FpDevice *device = FP_DEVICE (source_object);
    GError *error = NULL;
    SynaMatchResult *result = g_task_propagate_pointer (G_TASK (res), &error);
    if (result) {
        FpPrint *print = NULL;
        fpi_device_get_verify_data (device, &print);
        gboolean matched = result->score >= SYNA_MATCH_THRESHOLD;
        fpi_device_verify_report (device, matched ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                                  matched ? print : NULL, NULL);
        g_free (result);
    }
    fpi_device_verify_complete (device, error);
}

static void
syna_verify (FpDevice *device)
{
    GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                              on_verify_completed, NULL);
    FpPrint *print = NULL;
    GVariant *data = NULL;
    fpi_device_get_verify_data (device, &print);
    g_object_get (print, "fpi-data", &data, NULL);
    GPtrArray *samples = syna_template_decode (data);
    if (data) g_variant_unref (data);
    g_task_set_task_data (task, samples, samples ? (GDestroyNotify)g_ptr_array_unref : NULL);
    g_task_run_in_thread (task, syna_verify_thread);
    g_object_unref (task);
}

static void
syna_cancel (FpDevice *device)
{
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (device);
    self->is_cancelling = TRUE;
}

static void
fpi_device_syna_00dc_class_init (FpiDeviceSyna00dcClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

    object_class->finalize = fpi_device_syna_00dc_finalize;

    dev_class->id = "syna_00dc_tod";
    dev_class->full_name = "Synaptics 06cb:00dc (Acer Aspire A514-54)";
    dev_class->type = FP_DEVICE_TYPE_USB;
    dev_class->scan_type = FP_SCAN_TYPE_PRESS;
    dev_class->nr_enroll_stages = SYNA_ENROLL_STAGES;
    dev_class->id_table = syna_ids;

    dev_class->open = syna_open;
    dev_class->close = syna_close;
    dev_class->enroll = syna_enroll;
    dev_class->verify = syna_verify;
    dev_class->cancel = syna_cancel;

    fpi_device_class_auto_initialize_features (dev_class);
}
