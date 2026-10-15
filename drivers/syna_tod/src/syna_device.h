#pragma once

#include <fprint.h>
#include <fpi-device.h>
#include <fpi-print.h>
#include <gusb.h>
#include "syna_tls.h"
#include "syna_oem.h"
#include "syna_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FPI_TYPE_DEVICE_SYNA_00DC (fpi_device_syna_00dc_get_type ())
G_DECLARE_FINAL_TYPE (FpiDeviceSyna00dc, fpi_device_syna_00dc, FPI, DEVICE_SYNA_00DC, FpDevice)

GType fpi_tod_shared_driver_get_type (void);

struct _FpiDeviceSyna00dc
{
    FpDevice parent;

    GUsbDevice *usb_device;
    GMainContext *main_context;
    SynaOemData oem;
    SynaTlsContext tls;

    gboolean is_open;
    gboolean is_cancelling;
    gboolean capture_pipeline_ready;

    /* Calibrated empty sensor baseline (56x144 floats) */
    float *baseline;
    gboolean has_baseline;
    gboolean initial_contact_present;

    /* Enrolled templates stored locally */
    GPtrArray *enrolled_templates;
};

#ifdef __cplusplus
}
#endif
