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

#define SYNA_CMD_VERSION    0x01
#define SYNA_CMD_CAPTURE    0x02
#define SYNA_CMD_STOP       0x04
#define SYNA_CMD_INIT       0x06
#define SYNA_CMD_CLEANUP    0x1a
#define SYNA_CMD_FLASH_META 0x3e
#define SYNA_CMD_OWNERSHIP  0x4f
#define SYNA_CMD_PUBLIC_MAT 0x50
#define SYNA_CMD_SENSOR_ID  0x75

#define SYNA_EXPECTED_ROM_MAJOR    6
#define SYNA_EXPECTED_ROM_MINOR    20
#define SYNA_EXPECTED_ROM_BUILD    154
#define SYNA_EXPECTED_PRODUCT_ID   0x33
#define SYNA_EXPECTED_SENSOR_MAJOR 0x0190
#define SYNA_EXPECTED_SENSOR_MINOR 0x00a2

#define SYNA_TLS_CHANGE_CIPHER_SPEC 0x14
#define SYNA_TLS_ALERT              0x15
#define SYNA_TLS_HANDSHAKE          0x16
#define SYNA_TLS_APPLICATION_DATA   0x17

#define SYNA_MATCH_THRESHOLD 0.50f

typedef struct {
    uint16_t status;
    uint32_t build_time;
    uint32_t build_number;
    uint8_t  version_major;
    uint8_t  version_minor;
    uint8_t  target;
    uint8_t  product_id;
    uint8_t  silicon_rev;
    uint8_t  formal_release;
} SynaVersionInfo;

typedef struct {
    uint16_t status;
    uint16_t major;
    uint16_t minor;
} SynaSensorIdInfo;

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

/**
 * Parse and validate the 38-byte response to plaintext command 0x01 (ROM Version).
 * Returns 0 if valid and matching expected 06cb:00dc ROM 6.20 build 154 (product 0x33).
 */
static inline int syna_parse_version_response(const uint8_t *resp, size_t len, SynaVersionInfo *out)
{
    if (!resp || len != 38) return -1;
    uint16_t status = (uint16_t)(resp[0] | ((uint16_t)resp[1] << 8));
    if (status != 0) return -2;

    SynaVersionInfo info;
    info.status = status;
    info.build_time = (uint32_t)resp[2] | ((uint32_t)resp[3] << 8) |
                      ((uint32_t)resp[4] << 16) | ((uint32_t)resp[5] << 24);
    info.build_number = (uint32_t)resp[6] | ((uint32_t)resp[7] << 8) |
                        ((uint32_t)resp[8] << 16) | ((uint32_t)resp[9] << 24);
    info.version_major = resp[10];
    info.version_minor = resp[11];
    info.target = resp[12];
    info.product_id = resp[13];
    info.silicon_rev = resp[14];
    info.formal_release = resp[15];

    if (out) *out = info;

    if (info.version_major != SYNA_EXPECTED_ROM_MAJOR ||
        info.version_minor != SYNA_EXPECTED_ROM_MINOR ||
        info.build_number != SYNA_EXPECTED_ROM_BUILD ||
        info.product_id != SYNA_EXPECTED_PRODUCT_ID) {
        return -3;
    }
    return 0;
}

/**
 * Parse and validate the 10-byte response to command 0x75 (Sensor Module Identity).
 * Returns 0 if valid and matching module 86C TM-P3568-001 (major 0x0190, minor 0x00a2 -> type 0x2449).
 */
static inline int syna_parse_sensor_id_response(const uint8_t *resp, size_t len, SynaSensorIdInfo *out)
{
    if (!resp || len != 10) return -1;
    uint16_t status = (uint16_t)(resp[0] | ((uint16_t)resp[1] << 8));
    if (status != 0) return -2;

    uint32_t reserved = (uint32_t)resp[2] | ((uint32_t)resp[3] << 8) |
                        ((uint32_t)resp[4] << 16) | ((uint32_t)resp[5] << 24);
    if (reserved != 0) return -3;

    SynaSensorIdInfo info;
    info.status = status;
    info.minor = (uint16_t)(resp[6] | ((uint16_t)resp[7] << 8));
    info.major = (uint16_t)(resp[8] | ((uint16_t)resp[9] << 8));

    if (out) *out = info;

    if (info.major != SYNA_EXPECTED_SENSOR_MAJOR ||
        info.minor != SYNA_EXPECTED_SENSOR_MINOR) {
        return -4;
    }
    return 0;
}

