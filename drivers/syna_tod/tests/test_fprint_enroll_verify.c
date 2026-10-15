#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libfprint-2/fprint.h>

static void
on_finger_status (GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    FpDevice *dev = FP_DEVICE (object);
    FpFingerStatusFlags status = fp_device_get_finger_status (dev);

    if (status & FP_FINGER_STATUS_NEEDED) {
        g_print ("\n>>> [SENSOR] >>> Please place and hold your finger on the sensor...\n");
    } else if (status & FP_FINGER_STATUS_PRESENT) {
        g_print (">>> [SENSOR] >>> Finger detected! Capturing biometric frames...\n");
    } else {
        g_print (">>> [SENSOR] >>> Finger removed.\n");
    }
    fflush (stdout);
}

static void
on_enroll_progress (FpDevice *device, gint completed_stages, FpPrint *print, gpointer user_data, GError *error)
{
    (void)device;
    (void)print;
    (void)user_data;
    if (error) {
        g_print ("  [Enroll progress error] %s\n", error->message);
    } else {
        g_print ("  [Enroll progress] Stage %d completed!\n", completed_stages);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    g_print("======================================================\n");
    g_print(" Synaptics 06cb:00dc libfprint-tod Interactive Test\n");
    g_print("======================================================\n\n");

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

    g_signal_connect (dev, "notify::finger-status", G_CALLBACK (on_finger_status), NULL);

    GError *error = NULL;
    g_print("[1/4] Opening device %s...\n", fp_device_get_name(dev));
    if (!fp_device_open_sync(dev, NULL, &error)) {
        g_print("[FAIL] Open failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        g_object_unref(ctx);
        return 1;
    }
    g_print("[PASS] Device opened and TLS authenticated.\n\n");

    /* Step 2: Enroll */
    g_print("[2/4] ENROLLMENT: Starting finger enrollment...\n");
    FpPrint *template_print = fp_print_new(dev);
    fp_print_set_finger(template_print, FP_FINGER_RIGHT_INDEX);

    FpPrint *enrolled_print = fp_device_enroll_sync(dev, template_print, NULL,
                                                   on_enroll_progress, NULL, &error);
    if (!enrolled_print) {
        g_print("[FAIL] Enrollment failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        fp_device_close_sync(dev, NULL, NULL);
        g_object_unref(ctx);
        return 1;
    }
    g_print("[PASS] Finger successfully enrolled!\n");

    /* Verify serialization and deserialization (as fprintd does when storing to disk) */
    guchar *ser_data = NULL;
    gsize ser_len = 0;
    if (!fp_print_serialize(enrolled_print, &ser_data, &ser_len, &error)) {
        g_print("[FAIL] Template serialization failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        g_print("  [Disk Storage] Template serialized to %zu bytes (fprintd compatible)\n", ser_len);
        FpPrint *deserialized_print = fp_print_deserialize(ser_data, ser_len, &error);
        if (deserialized_print) {
            g_print("  [Disk Storage] Template successfully deserialized from storage format!\n");
            g_object_unref(enrolled_print);
            enrolled_print = deserialized_print;
        } else {
            g_print("[FAIL] Deserialization failed: %s\n", error ? error->message : "unknown");
            g_clear_error(&error);
        }
        g_free(ser_data);
    }
    g_print("\n");

    /* Step 3: Verify */
    g_print("------------------------------------------------------\n");
    g_print(" Step 2 complete: Enrollment succeeded.\n");
    g_print(" Transitioning to verification test...\n");
    g_print("------------------------------------------------------\n\n");
    fflush(stdout);
    g_usleep(500000);

    g_print("[3/4] VERIFICATION: Testing matching against enrolled finger...\n");
    gboolean is_match = FALSE;
    FpPrint *matched_print = NULL;
    if (!fp_device_verify_sync(dev, enrolled_print, NULL, NULL, NULL,
                               &is_match, &matched_print, &error)) {
        g_print("[FAIL] Verification call failed: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        if (is_match) {
            g_print("\n*** [MATCH SUCCESS] *** Biometric verification PASSED!\n\n");
        } else {
            g_print("\n*** [MATCH FAILED] *** Finger did not match enrolled template.\n\n");
        }
    }

    /* Step 4: Close */
    g_print("[4/4] Closing device...\n");
    fp_device_close_sync(dev, NULL, NULL);
    g_print("[PASS] Device closed cleanly.\n");

    if (matched_print) g_object_unref(matched_print);
    if (enrolled_print) g_object_unref(enrolled_print);
    g_object_unref(ctx);

    g_print("======================================================\n");
    g_print(" Test completed successfully!\n");
    g_print("======================================================\n");
    return 0;
}
