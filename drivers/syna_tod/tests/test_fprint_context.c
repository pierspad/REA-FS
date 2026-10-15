#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libfprint-2/fprint.h>

int main(int argc, char **argv)
{
    g_print("Initializing FpContext...\n");
    FpContext *ctx = fp_context_new();
    fp_context_enumerate(ctx);

    GPtrArray *devices = fp_context_get_devices(ctx);
    g_print("Found %u fingerprint device(s):\n", devices->len);

    gboolean syna_found = FALSE;
    for (guint i = 0; i < devices->len; i++) {
        FpDevice *dev = g_ptr_array_index(devices, i);
        const gchar *drv = fp_device_get_driver(dev);
        const gchar *dev_id = fp_device_get_device_id(dev);
        const gchar *name = fp_device_get_name(dev);
        g_print("  [%u] Driver: %s\n", i, drv ? drv : "(null)");
        g_print("      Device ID: %s\n", dev_id ? dev_id : "(null)");
        g_print("      Name: %s\n", name ? name : "(null)");
        g_print("      Scan type: %s\n", fp_device_get_scan_type(dev) == FP_SCAN_TYPE_PRESS ? "press" : "swipe");
        g_print("      Enroll stages: %d\n", fp_device_get_nr_enroll_stages(dev));

        if (g_strcmp0(drv, "syna_00dc_tod") == 0) {
            syna_found = TRUE;
        }
    }

    g_object_unref(ctx);

    if (syna_found) {
        g_print("[PASS] Successfully discovered and bound syna_00dc_tod driver!\n");
        return 0;
    } else {
        g_print("[INFO] syna_00dc_tod not matched among active devices (ensure FP_TOD_DRIVERS_DIR is set).\n");
        return 1;
    }
}
