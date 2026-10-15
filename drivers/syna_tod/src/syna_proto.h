#pragma once

#include <stdint.h>
#include <stddef.h>

#define SYNA_USB_VID 0x06cb
#define SYNA_USB_PID 0x00dc

#define SYNA_EP_CMD_OUT 0x01
#define SYNA_EP_CMD_IN  0x81
#define SYNA_EP_DATA_IN 0x82

#define SYNA_ROW_BYTES        152
#define SYNA_HEADER_BYTES     8
#define SYNA_SAMPLE_BYTES     144
#define SYNA_ROWS_PER_FRAME   112
#define SYNA_OUTPUT_ROWS      56
#define SYNA_OUTPUT_COLS      144
#define SYNA_FRAME_BYTES      (SYNA_ROW_BYTES * SYNA_ROWS_PER_FRAME)

#define SYNA_ROW_PREFIX_0 0x01
#define SYNA_ROW_PREFIX_1 0xfe
#define SYNA_ROW_SUFFIX_0 0xf0
#define SYNA_ROW_SUFFIX_1 0xf7
#define SYNA_ROW_SUFFIX_2 0x01
#define SYNA_ROW_SUFFIX_3 0x00

#define SYNA_CMD_VERSION  0x01
#define SYNA_CMD_CLEANUP  0x1a
#define SYNA_CMD_INIT     0x06
#define SYNA_CMD_CAPTURE  0x02
#define SYNA_CMD_STOP     0x04
#define SYNA_CMD_OWNERSHIP 0x4f

#define SYNA_TLS_CHANGE_CIPHER_SPEC 0x14
#define SYNA_TLS_ALERT              0x15
#define SYNA_TLS_HANDSHAKE          0x16
#define SYNA_TLS_APPLICATION_DATA   0x17

#define SYNA_MATCH_THRESHOLD 0.50f

/* Known status policy from successful USB experiments; not a universal device table. */
static inline int syna_response_ok(uint8_t command, const uint8_t *response,
                                  size_t length, uint16_t *status)
{
    if (status) *status = 0xffff;
    if (!response || length < 2) return 0;
    uint16_t value = response[0] | ((uint16_t)response[1] << 8);
    if (status) *status = value;
    return value == 0 || (command == SYNA_CMD_CLEANUP && value == 0x0491);
}
