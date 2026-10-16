#include <assert.h>
#include "syna_proto.h"
int main(void)
{
    const uint8_t ok[] = {0, 0, 42};
    const uint8_t already[] = {0x91, 4};
    const uint8_t rejected[] = {0x50, 4};
    uint16_t status = 0xffff;
    assert(syna_response_ok(SYNA_CMD_CAPTURE, ok, sizeof(ok), &status) && status == 0);
    assert(syna_response_ok(SYNA_CMD_CLEANUP, already, 2, &status) && status == 0x0491);
    assert(!syna_response_ok(SYNA_CMD_INIT, already, 2, &status));
    assert(!syna_response_ok(SYNA_CMD_CAPTURE, rejected, 2, &status) && status == 0x0450);
    assert(!syna_response_ok(SYNA_CMD_STOP, ok, 1, &status));
    assert(!syna_response_ok(SYNA_CMD_STOP, NULL, 0, &status));

    /* Test 0x01 ROM Version response parser (38 bytes) */
    uint8_t ver_ok[38] = {
        0x00, 0x00, 0x10, 0xb6, 0x04, 0x54, 0x9a, 0x00,
        0x00, 0x00, 0x06, 0x14, 0x01, 0x33, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x06
    };
    SynaVersionInfo vinfo;
    assert(syna_parse_version_response(ver_ok, sizeof(ver_ok), &vinfo) == 0);
    assert(vinfo.version_major == 6 && vinfo.version_minor == 20 && vinfo.build_number == 154 && vinfo.product_id == 0x33);
    ver_ok[10] = 5; /* Wrong major version */
    assert(syna_parse_version_response(ver_ok, sizeof(ver_ok), &vinfo) != 0);

    /* Test 0x75 Sensor Module Identity parser (10 bytes: status=0, reserved=0, minor=0x00a2, major=0x0190) */
    uint8_t id_ok[10] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa2, 0x00, 0x90, 0x01 };
    SynaSensorIdInfo idinfo;
    assert(syna_parse_sensor_id_response(id_ok, sizeof(id_ok), &idinfo) == 0);
    assert(idinfo.major == 0x0190 && idinfo.minor == 0x00a2);
    id_ok[8] = 0x91; /* Wrong module major */
    assert(syna_parse_sensor_id_response(id_ok, sizeof(id_ok), &idinfo) != 0);
    return 0;
}

