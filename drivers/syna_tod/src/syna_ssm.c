/*
 * Synaptics 06cb:00dc WBDI Async FpiSsm + FpiUsbTransfer Driver Engine
 *
 * Non-blocking sequential state machines (FpiSsm) for upstream libfprint:
 *   1. OPEN_SSM:
 *      - Reset & Claim interface 0
 *      - 0x01 ROM Version check (ROM 6.20 build 154, product 0x33)
 *      - 0x75 Sensor Module ID check (0x0190:0x00a2 -> 86C TM-P3568-001)
 *      - Optional first-boot host pairing (0x4f Ownership + 0x50 Public Material)
 *      - TLS 1.2 ClientHello -> ServerHello -> Flight 2 -> Server Finished
 *      - Encrypted 0x1a Pre-config Cleanup + 0x06 Capture Initializer
 *      - Persistent baseline load (instant 0-frame startup)
 *   2. CAPTURE_SSM:
 *      - Encrypted 0x02 Capture Command TX/RX
 *      - Asynchronous 17024-byte raw Mode-2 bulk stream reads on EP 0x82
 *      - Encrypted 0x04 Capture Stop TX/RX
 */

#include "syna_ssm.h"
#include "syna_proto.h"
#include <string.h>

typedef enum {
    SYNA_OPEN_RESET_AND_CLAIM = 0,
    SYNA_OPEN_GET_VERSION_TX,
    SYNA_OPEN_GET_VERSION_RX,
    SYNA_OPEN_GET_SENSOR_ID_TX,
    SYNA_OPEN_GET_SENSOR_ID_RX,
    SYNA_OPEN_LOAD_OR_PAIR_TX_4F,
    SYNA_OPEN_PAIR_RX_4F,
    SYNA_OPEN_PAIR_TX_50,
    SYNA_OPEN_PAIR_RX_50,
    SYNA_OPEN_TLS_CLIENT_HELLO_TX,
    SYNA_OPEN_TLS_SERVER_HELLO_RX,
    SYNA_OPEN_TLS_FLIGHT2_TX,
    SYNA_OPEN_TLS_FLIGHT2_RX,
    SYNA_OPEN_CLEANUP_1A_TX,
    SYNA_OPEN_CLEANUP_1A_RX,
    SYNA_OPEN_INIT_06_TX,
    SYNA_OPEN_INIT_06_RX,
    SYNA_OPEN_NUM_STATES
} SynaOpenState;

typedef struct {
    FpiDeviceSyna00dc *self;
    char *pending_key_pem;
    uint8_t ownership_reply[512];
    size_t ownership_reply_len;
    uint8_t rx_buf[4096];
    size_t rx_len;
} SynaOpenSsmData;

static void
syna_open_ssm_data_free (gpointer data)
{
    SynaOpenSsmData *ctx = data;
    if (!ctx) return;
    free (ctx->pending_key_pem);
    g_free (ctx);
}

static void
syna_ssm_bulk_tx (FpiSsm *ssm, FpDevice *dev, const uint8_t *buf, size_t len, guint timeout_ms)
{
    FpiUsbTransfer *t = fpi_usb_transfer_new (dev);
    t->ssm = ssm;
    t->short_is_error = TRUE;
    fpi_usb_transfer_fill_bulk (t, SYNA_EP_CMD_OUT, len);
    memcpy (t->buffer, buf, len);
    fpi_usb_transfer_submit (t, timeout_ms, fpi_device_get_cancellable (dev),
                             fpi_ssm_usb_transfer_cb, NULL);
}

static void
syna_ssm_bulk_rx_cb (FpiUsbTransfer *transfer, FpDevice *device, gpointer user_data, GError *error)
{
    (void)device;
    SynaOpenSsmData *ctx = user_data;
    if (error) {
        fpi_ssm_mark_failed (transfer->ssm, error);
        return;
    }
    if (transfer->actual_length < 0 || (size_t)transfer->actual_length > sizeof (ctx->rx_buf)) {
        fpi_ssm_mark_failed (transfer->ssm,
                             fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "Invalid USB bulk response length"));
        return;
    }
    memcpy (ctx->rx_buf, transfer->buffer, (size_t)transfer->actual_length);
    ctx->rx_len = (size_t)transfer->actual_length;
    fpi_ssm_next_state (transfer->ssm);
}

static void
syna_ssm_bulk_rx (FpiSsm *ssm, FpDevice *dev, SynaOpenSsmData *ctx, size_t max_len, guint timeout_ms)
{
    FpiUsbTransfer *t = fpi_usb_transfer_new (dev);
    t->ssm = ssm;
    fpi_usb_transfer_fill_bulk (t, SYNA_EP_CMD_IN, max_len);
    fpi_usb_transfer_submit (t, timeout_ms, fpi_device_get_cancellable (dev),
                             syna_ssm_bulk_rx_cb, ctx);
}

static void
syna_open_ssm_handler (FpiSsm *ssm, FpDevice *dev)
{
    FpiDeviceSyna00dc *self = FPI_DEVICE_SYNA_00DC (dev);
    SynaOpenSsmData *ctx = fpi_ssm_get_data (ssm);
    GError *error = NULL;

    switch ((SynaOpenState)fpi_ssm_get_cur_state (ssm)) {
    case SYNA_OPEN_RESET_AND_CLAIM:
        self->usb_device = fpi_device_get_usb_device (dev);
        if (!self->usb_device) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "No GUsbDevice attached"));
            return;
        }
        g_usb_device_reset (self->usb_device, NULL);
        if (!g_usb_device_claim_interface (self->usb_device, 0, G_USB_DEVICE_CLAIM_INTERFACE_NONE, &error)) {
            fpi_ssm_mark_failed (ssm, error);
            return;
        }
        fpi_ssm_next_state (ssm);
        break;

    case SYNA_OPEN_GET_VERSION_TX: {
        uint8_t cmd = SYNA_CMD_VERSION;
        syna_ssm_bulk_tx (ssm, dev, &cmd, 1, 3000);
        break;
    }

    case SYNA_OPEN_GET_VERSION_RX:
        syna_ssm_bulk_rx (ssm, dev, ctx, 64, 5000);
        break;

    case SYNA_OPEN_GET_SENSOR_ID_TX: {
        SynaVersionInfo ver_info;
        if (syna_parse_version_response (ctx->rx_buf, ctx->rx_len, &ver_info) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
                                                                "Unsupported Synaptics ROM version"));
            return;
        }
        uint8_t cmd = SYNA_CMD_SENSOR_ID;
        syna_ssm_bulk_tx (ssm, dev, &cmd, 1, 3000);
        break;
    }

    case SYNA_OPEN_GET_SENSOR_ID_RX:
        syna_ssm_bulk_rx (ssm, dev, ctx, 32, 5000);
        break;

    case SYNA_OPEN_LOAD_OR_PAIR_TX_4F: {
        SynaSensorIdInfo id_info;
        if (syna_parse_sensor_id_response (ctx->rx_buf, ctx->rx_len, &id_info) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
                                                                "Unsupported Synaptics sensor module ID"));
            return;
        }
        if (syna_oem_load (NULL, &self->oem) == 0) {
            /* Credentials already present on disk -> jump directly to TLS ClientHello */
            fpi_ssm_jump_to_state (ssm, SYNA_OPEN_TLS_CLIENT_HELLO_TX);
            return;
        }
        const char *allow_pair = getenv ("SYNA_ALLOW_AUTO_PAIR");
        if (!allow_pair || strcmp (allow_pair, "1") != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_NOT_FOUND,
                                                                "Missing OEM pairing credentials"));
            return;
        }
        uint8_t own_req[453];
        size_t own_req_len = 0;
        if (syna_pairing_generate_request (&ctx->pending_key_pem, own_req, &own_req_len) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                                "Failed to generate 0x4f pairing request"));
            return;
        }
        syna_ssm_bulk_tx (ssm, dev, own_req, own_req_len, 3000);
        break;
    }

    case SYNA_OPEN_PAIR_RX_4F:
        syna_ssm_bulk_rx (ssm, dev, ctx, 512, 5000);
        break;

    case SYNA_OPEN_PAIR_TX_50: {
        if (ctx->rx_len > sizeof (ctx->ownership_reply)) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "0x4f reply overflow"));
            return;
        }
        memcpy (ctx->ownership_reply, ctx->rx_buf, ctx->rx_len);
        ctx->ownership_reply_len = ctx->rx_len;
        uint8_t pub_cmd[13] = { SYNA_CMD_PUBLIC_MAT, 0 };
        syna_ssm_bulk_tx (ssm, dev, pub_cmd, sizeof (pub_cmd), 3000);
        break;
    }

    case SYNA_OPEN_PAIR_RX_50:
        syna_ssm_bulk_rx (ssm, dev, ctx, 1024, 5000);
        break;

    case SYNA_OPEN_TLS_CLIENT_HELLO_TX: {
        if (ctx->pending_key_pem) {
            uint8_t host_cert[184];
            size_t host_cert_len = 0;
            uint8_t server_pub[65];
            size_t server_pub_len = 0;
            if (syna_pairing_extract_credentials (ctx->ownership_reply, ctx->ownership_reply_len,
                                                  ctx->rx_buf, ctx->rx_len,
                                                  host_cert, &host_cert_len,
                                                  server_pub, &server_pub_len) != 0 ||
                syna_oem_save_credentials (NULL, ctx->pending_key_pem,
                                           host_cert, host_cert_len,
                                           server_pub, server_pub_len) != 0 ||
                syna_oem_load (NULL, &self->oem) != 0) {
                fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                                    "Failed to extract/save paired credentials"));
                return;
            }
            free (ctx->pending_key_pem);
            ctx->pending_key_pem = NULL;
        }
        if (syna_tls_init (&self->tls, self->oem.host_key_pem,
                           self->oem.host_cert, self->oem.host_cert_len,
                           self->oem.server_ecdh_pub, self->oem.server_ecdh_pub_len) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "TLS init failed"));
            return;
        }
        uint8_t hello[128];
        size_t hello_len = 0;
        syna_tls_build_client_hello (&self->tls, hello, &hello_len);
        syna_ssm_bulk_tx (ssm, dev, hello, hello_len, 3000);
        break;
    }

    case SYNA_OPEN_TLS_SERVER_HELLO_RX:
        syna_ssm_bulk_rx (ssm, dev, ctx, 512, 5000);
        break;

    case SYNA_OPEN_TLS_FLIGHT2_TX: {
        if (syna_tls_handle_server_hello (&self->tls, ctx->rx_buf, ctx->rx_len) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "ServerHello parse failed"));
            return;
        }
        uint8_t flight2[1024];
        size_t flight2_len = 0;
        if (syna_tls_build_flight2 (&self->tls, flight2, &flight2_len) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "TLS Flight 2 build failed"));
            return;
        }
        syna_ssm_bulk_tx (ssm, dev, flight2, flight2_len, 3000);
        break;
    }

    case SYNA_OPEN_TLS_FLIGHT2_RX:
        syna_ssm_bulk_rx (ssm, dev, ctx, 512, 5000);
        break;

    case SYNA_OPEN_CLEANUP_1A_TX: {
        if (syna_tls_handle_flight2_reply (&self->tls, ctx->rx_buf, ctx->rx_len) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "TLS Finished verify failed"));
            return;
        }
        uint8_t clean_cmd = SYNA_CMD_CLEANUP;
        uint8_t enc[128];
        size_t enc_len = 0;
        if (syna_tls_encrypt_app_data (&self->tls, &clean_cmd, 1, enc, &enc_len) != 0) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "TLS encrypt 0x1a failed"));
            return;
        }
        syna_ssm_bulk_tx (ssm, dev, enc, enc_len, 3000);
        break;
    }

    case SYNA_OPEN_CLEANUP_1A_RX:
        syna_ssm_bulk_rx (ssm, dev, ctx, 256, 5000);
        break;

    case SYNA_OPEN_INIT_06_TX: {
        uint8_t plain[64];
        size_t plain_len = 0;
        uint16_t status = 0xffff;
        if (syna_tls_decrypt_app_data (&self->tls, ctx->rx_buf, ctx->rx_len, plain, sizeof (plain), &plain_len) != 0 ||
            !syna_response_ok (SYNA_CMD_CLEANUP, plain, plain_len, &status)) {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO, "0x1a cleanup rejected"));
            return;
        }
        size_t req_len = 1 + self->oem.capture_init_blob_len;
        uint8_t *req = g_malloc (req_len);
        req[0] = SYNA_CMD_INIT;
        memcpy (req + 1, self->oem.capture_init_blob, self->oem.capture_init_blob_len);

        uint8_t *enc = g_malloc (req_len + 512);
        size_t enc_len = 0;
        int rc = syna_tls_encrypt_app_data (&self->tls, req, req_len, enc, &enc_len);
        g_free (req);
        if (rc != 0) {
            g_free (enc);
            fpi_ssm_mark_failed (ssm, fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "TLS encrypt 0x06 failed"));
            return;
        }
        syna_ssm_bulk_tx (ssm, dev, enc, enc_len, 3000);
        g_free (enc);
        break;
    }

    case SYNA_OPEN_INIT_06_RX:
        syna_ssm_bulk_rx (ssm, dev, ctx, 256, 5000);
        break;

    default:
        fpi_ssm_mark_completed (ssm);
        break;
    }
}

FpiSsm *
syna_ssm_open_new (FpiDeviceSyna00dc *self)
{
    FpDevice *dev = FP_DEVICE (self);
    FpiSsm *ssm = fpi_ssm_new (dev, syna_open_ssm_handler, SYNA_OPEN_NUM_STATES);
    SynaOpenSsmData *ctx = g_new0 (SynaOpenSsmData, 1);
    ctx->self = self;
    fpi_ssm_set_data (ssm, ctx, syna_open_ssm_data_free);
    return ssm;
}
