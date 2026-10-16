#pragma once

#include "syna_device.h"
#include <fpi-ssm.h>
#include <fpi-usb-transfer.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a non-blocking libfprint sequential state machine (FpiSsm) for device open,
 * hardware identity verification (0x01/0x75), optional first-boot pairing (0x4f/0x50),
 * TLS 1.2 session establishment, and capture pipeline initialization (0x1a/0x06).
 */
FpiSsm *syna_ssm_open_new (FpiDeviceSyna00dc *self);

#ifdef __cplusplus
}
#endif
