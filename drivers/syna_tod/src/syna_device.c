#include "syna_device.h"
#include "syna_mode2.h"
#include "syna_matcher.h"
#include "syna_proto.h"
#include <math.h>

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
    self->is_cancelling = FALSE;
    self->capture_pipeline_ready = FALSE;
    self->baseline = NULL;
    self->has_baseline = FALSE;
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

    G_OBJECT_CLASS (fpi_device_syna_00dc_parent_class)->finalize (object);
}

/* Forward declarations */
static gboolean
syna_read_full_frame (GUsbDevice *usb_device, uint8_t *frame_buf, GCancellable *cancellable, GError **error);

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
    syna_tls_app_exchange (self, &clean_cmd, 1, clean_resp, sizeof (clean_resp), &clean_resp_len, cancellable, NULL);

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

/* Helper to calibrate empty sensor baseline upon device open */
static gboolean
syna_calibrate_baseline (FpiDeviceSyna00dc *self, GCancellable *cancellable, GError **error)
{
    /* Start capture stream to measure clean baseline */
    uint8_t cap_resp[4096];
    size_t cap_resp_len = 0;
    if (!syna_tls_app_exchange (self, self->oem.capture_packet, self->oem.capture_packet_len,
                                cap_resp, sizeof (cap_resp), &cap_resp_len, cancellable, error)) {
        return FALSE;
    }

    if (!self->baseline) {
        self->baseline = g_malloc0 (SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    } else {
        memset (self->baseline, 0, SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    }

    uint8_t frame_buf[SYNA_FRAME_BYTES];
    int base_count = 0;
    while (base_count < 20) {
        if (!syna_read_full_frame (self->usb_device, frame_buf, cancellable, error)) {
            break;
        }

        if (syna_validate_frame (frame_buf, NULL) == 0) {
            uint8_t dec[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
            syna_decode_frame_mode2 (frame_buf, NULL, dec);
            for (size_t i = 0; i < sizeof (dec); i++) {
                self->baseline[i] += (float)dec[i];
            }
            base_count++;
        }
    }

    /* Send stop command 0x04 */
    uint8_t stop_cmd = SYNA_CMD_STOP;
    uint8_t stop_resp[64];
    size_t stop_resp_len = 0;
    syna_tls_app_exchange (self, &stop_cmd, 1, stop_resp, sizeof (stop_resp), &stop_resp_len, NULL, NULL);

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

    if (base_count < 10) {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to read baseline frames from sensor");
        return FALSE;
    }

    for (size_t i = 0; i < SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES; i++) {
        self->baseline[i] /= (float)base_count;
    }
    self->has_baseline = TRUE;

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

/* Helper to read a complete 16128-byte frame from bulk EP */
static gboolean
syna_read_full_frame (GUsbDevice *usb_device, uint8_t *frame_buf, GCancellable *cancellable, GError **error)
{
    size_t total = 0;
    while (total < SYNA_FRAME_BYTES) {
        if (g_cancellable_is_cancelled (cancellable)) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Capture cancelled");
            return FALSE;
        }
        gsize chunk = 0;
        GError *local_err = NULL;
        if (!g_usb_device_bulk_transfer (usb_device, SYNA_EP_DATA_IN,
                                         frame_buf + total, SYNA_FRAME_BYTES - total,
                                         &chunk, 1000, cancellable, &local_err)) {
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

/* Helper to capture a single finger sample from USB bulk stream */
static float *
syna_capture_finger_sample (FpiDeviceSyna00dc *self, GCancellable *cancellable, GError **error)
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

    /* 4. Report waiting for finger IMMEDIATELY before reading */
    fpi_device_report_finger_status_changes (FP_DEVICE (self), FP_FINGER_STATUS_NEEDED, FP_FINGER_STATUS_NONE);

    /* 5. Stream frames and wait for finger touch */
    uint8_t frame_buf[SYNA_FRAME_BYTES];
    float *accumulated_contact = g_malloc0 (SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    gboolean finger_detected = FALSE;
    int contact_frames = 0;
    int idle_frames = 0;

    while (contact_frames < 25) { /* collect 25 biometric frames (~0.4s) */
        if (g_cancellable_is_cancelled (cancellable)) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Capture cancelled");
            break;
        }

        if (!syna_read_full_frame (self->usb_device, frame_buf, cancellable, error)) {
            break;
        }

        if (syna_validate_frame (frame_buf, NULL) == 0) {
            uint8_t dec[SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES];
            syna_decode_frame_mode2 (frame_buf, NULL, dec);

            /* Calculate RMS difference against calibrated empty baseline */
            float sum_sq = 0.0f;
            for (size_t i = 0; i < sizeof (dec); i++) {
                float diff = (float)dec[i] - self->baseline[i];
                sum_sq += diff * diff;
            }
            float rms = sqrtf (sum_sq / (float)(sizeof (dec)));

            if (rms > 15.0f) {
                if (!finger_detected) {
                    finger_detected = TRUE;
                    fpi_device_report_finger_status_changes (FP_DEVICE (self), FP_FINGER_STATUS_PRESENT, FP_FINGER_STATUS_NEEDED);
                }
                for (size_t i = 0; i < sizeof (dec); i++) {
                    accumulated_contact[i] += (float)dec[i] - self->baseline[i];
                }
                contact_frames++;
            } else if (finger_detected && contact_frames >= 10) {
                /* Finger removed */
                fpi_device_report_finger_status_changes (FP_DEVICE (self), FP_FINGER_STATUS_NONE, FP_FINGER_STATUS_PRESENT);
                break;
            } else {
                idle_frames++;
                if (!finger_detected && idle_frames > 600) { /* ~15s timeout */
                    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Finger touch timed out");
                    break;
                }
            }
        }
    }

    if (finger_detected && contact_frames >= 25) {
        fpi_device_report_finger_status_changes (FP_DEVICE (self), FP_FINGER_STATUS_NONE, FP_FINGER_STATUS_PRESENT);
    } else if (!finger_detected) {
        fpi_device_report_finger_status_changes (FP_DEVICE (self), FP_FINGER_STATUS_NONE, FP_FINGER_STATUS_NEEDED);
    }

    /* 6. Send stop command 0x04 */
    uint8_t stop_cmd = SYNA_CMD_STOP;
    uint8_t stop_resp[64];
    size_t stop_resp_len = 0;
    syna_tls_app_exchange (self, &stop_cmd, 1, stop_resp, sizeof (stop_resp), &stop_resp_len, NULL, NULL);

    /* Drain any leftover bulk packets on EP 130 to keep endpoint cleanly halted */
    uint8_t drain_buf[16384];
    gsize drained = 0;
    for (int d = 0; d < 10; d++) {
        if (!g_usb_device_bulk_transfer (self->usb_device, SYNA_EP_DATA_IN,
                                         drain_buf, sizeof (drain_buf),
                                         &drained, 25, NULL, NULL) || drained == 0) {
            break;
        }
    }

    /* Next capture cycle will require re-initializing the capture pipeline */
    self->capture_pipeline_ready = FALSE;

    if (contact_frames < 10) {
        g_free (accumulated_contact);
        if (error && !*error) {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "No sustained finger contact detected");
        }
        return NULL;
    }

    /* Average contact frames and apply bandpass filter */
    float *contact_avg = g_malloc (SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    for (size_t i = 0; i < SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES; i++) {
        contact_avg[i] = accumulated_contact[i] / (float)contact_frames;
    }
    g_free (accumulated_contact);

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

    float *template_sample = syna_capture_finger_sample (self, cancellable, &error);
    if (!template_sample) {
        g_task_return_error (task, error);
        return;
    }

    /* Store enrolled template */
    g_ptr_array_add (self->enrolled_templates, template_sample);

    FpPrint *print = NULL;
    fpi_device_get_enroll_data (FP_DEVICE (self), &print);
    fpi_print_set_type (print, FPI_PRINT_RAW);

    /* Attach raw template data to FpPrint object */
    GBytes *bytes = g_bytes_new (template_sample, SYNA_OUTPUT_ROWS * SYNA_SAMPLE_BYTES * sizeof (float));
    g_object_set_data_full (G_OBJECT (print), "syna_template", bytes, (GDestroyNotify)g_bytes_unref);

    fpi_device_enroll_progress (FP_DEVICE (self), 1, print, NULL);
    g_task_return_pointer (task, g_object_ref (print), g_object_unref);
}

static void
on_enroll_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    FpDevice *device = FP_DEVICE (source_object);
    GError *error = NULL;
    FpPrint *print = g_task_propagate_pointer (G_TASK (res), &error);
    fpi_device_enroll_complete (device, print, error);
}

static void
syna_enroll (FpDevice *device)
{
    GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                              on_enroll_completed, NULL);
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

    FpPrint *enrolled_print = NULL;
    fpi_device_get_verify_data (FP_DEVICE (self), &enrolled_print);

    GBytes *enrolled_bytes = g_object_get_data (G_OBJECT (enrolled_print), "syna_template");
    const float *enrolled_sample = NULL;
    if (enrolled_bytes) {
        enrolled_sample = (const float *)g_bytes_get_data (enrolled_bytes, NULL);
    } else if (self->enrolled_templates->len > 0) {
        enrolled_sample = g_ptr_array_index (self->enrolled_templates, 0);
    }

    if (!enrolled_sample) {
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No enrolled template to verify against");
        return;
    }

    float *live_sample = syna_capture_finger_sample (self, cancellable, &error);
    if (!live_sample) {
        g_task_return_error (task, error);
        return;
    }

    SynaMatchResult match_res;
    syna_match_images (enrolled_sample, live_sample, 0.65f, &match_res);
    g_free (live_sample);

    g_message ("Synaptics verify score: %.3f (threshold: %.2f, rotation: %.1f deg, overlap: %.1f%%)",
               match_res.score, (float)SYNA_MATCH_THRESHOLD, match_res.angle, match_res.overlap * 100.0f);

    FpiMatchResult res = (match_res.score >= SYNA_MATCH_THRESHOLD) ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL;
    fpi_device_verify_report (FP_DEVICE (self), res, enrolled_print, NULL);
    g_task_return_boolean (task, TRUE);
}

static void
on_verify_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)user_data;
    FpDevice *device = FP_DEVICE (source_object);
    GError *error = NULL;
    g_task_propagate_boolean (G_TASK (res), &error);
    fpi_device_verify_complete (device, error);
}

static void
syna_verify (FpDevice *device)
{
    GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                              on_verify_completed, NULL);
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
    dev_class->nr_enroll_stages = 1;
    dev_class->id_table = syna_ids;

    dev_class->open = syna_open;
    dev_class->close = syna_close;
    dev_class->enroll = syna_enroll;
    dev_class->verify = syna_verify;
    dev_class->cancel = syna_cancel;

    fpi_device_class_auto_initialize_features (dev_class);
}
