#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libfprint-2/fprint.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    g_print("Creating FpContext...\n");
    FpContext *ctx = fp_context_new();
    fp_context_enumerate(ctx);

    GPtrArray *devices = fp_context_get_devices(ctx);
    if (!devices || devices->len == 0) {
        g_print("[FAIL] No fingerprint devices found.\n");
        g_object_unref(ctx);
        return 1;
    }

    FpDevice *dev = NULL;
    for (guint i = 0; i < devices->len; i++) {
        FpDevice *candidate = g_ptr_array_index(devices, i);
        if (g_strcmp0(fp_device_get_driver(candidate), "syna_00dc_tod") == 0) {
            dev = candidate;
            break;
        }
    }

    if (!dev) {
        g_print("[FAIL] syna_00dc_tod device not found.\n");
        g_object_unref(ctx);
        return 1;
    }

    g_print("Testing fp_device_open_sync on %s (%s)...\n",
            fp_device_get_name(dev), fp_device_get_driver(dev));

    GError *error = NULL;
    if (!fp_device_open_sync(dev, NULL, &error)) {
        g_print("[FAIL] fp_device_open_sync failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        g_object_unref(ctx);
        return 1;
    }
    g_print("[PASS] Device opened successfully and TLS handshake + OEM init completed!\n");

    g_print("Testing fp_device_close_sync...\n");
    if (!fp_device_close_sync(dev, NULL, &error)) {
        g_print("[FAIL] fp_device_close_sync failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        g_object_unref(ctx);
        return 1;
    }
    g_print("[PASS] Device closed cleanly!\n");

    g_object_unref(ctx);
    return 0;
}
