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
    return 0;
}
