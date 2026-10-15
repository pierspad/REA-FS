#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/syna_oem.h"

int main(void)
{
    printf("Running syna_oem loader tests...\n");
    SynaOemData oem;
    int rc = syna_oem_load(NULL, &oem);
    if (rc != 0) {
        printf("syna_oem_load returned %d\n", rc);
    }
    assert(rc == 0);

    assert(oem.host_key_pem != NULL);
    assert(strstr(oem.host_key_pem, "BEGIN") != NULL && strstr(oem.host_key_pem, "PRIVATE KEY") != NULL);

    assert(oem.host_cert != NULL);
    assert(oem.host_cert_len == 184);

    assert(oem.server_ecdh_pub != NULL);
    assert(oem.server_ecdh_pub_len == 65);
    assert(oem.server_ecdh_pub[0] == 0x04); /* Uncompressed EC point */

    assert(oem.capture_init_blob != NULL);
    assert(oem.capture_init_blob_len == 4676);

    assert(oem.capture_packet != NULL);
    assert(oem.capture_packet_len == 1917);

    syna_oem_free(&oem);

    /* Test loading from explicit credentials directory */
    rc = syna_oem_load("drivers/syna_tod/credentials", &oem);
    assert(rc == 0);
    assert(oem.host_key_pem != NULL);
    assert(oem.host_cert_len == 184);
    assert(oem.server_ecdh_pub_len == 65);
    assert(oem.capture_init_blob_len == 4676);
    assert(oem.capture_packet_len == 1917);
    syna_oem_free(&oem);

    printf("All syna_oem loader tests passed!\n");
    return 0;
}
