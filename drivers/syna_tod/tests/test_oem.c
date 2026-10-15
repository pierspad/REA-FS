#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syna_oem.h"

static void fixture(const char *dir, const char *name, size_t size, int first)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    for (size_t i = 0; i < size; i++) fputc(i == 0 ? first : 0, f);
    assert(fclose(f) == 0);
}

int main(void)
{
    SynaOemData oem;
    /* Explicit directories must not silently use this computer's real keys. */
    assert(syna_oem_load("/nonexistent/rea-fs-test-credentials", &oem) != 0);
    char dir[] = "/tmp/rea-fs-oem-XXXXXX";
    assert(mkdtemp(dir));
    fixture(dir, "host_key.pem", 32, 'x'); /* Synthetic loader input, not a usable key. */
    fixture(dir, "host_cert.bin", 184, 0);
    fixture(dir, "server_ecdh_pub.bin", 65, 4);
    fixture(dir, "capture_init.bin", 4676, 2);
    fixture(dir, "capture_packet.bin", 1917, 2);
    assert(syna_oem_load(dir, &oem) == 0);
    assert(oem.host_cert_len == 184 && oem.server_ecdh_pub_len == 65);
    assert(oem.capture_init_blob_len == 4676 && oem.capture_packet_len == 1917);
    syna_oem_free(&oem);
    fixture(dir, "host_cert.bin", 2, 0);
    assert(syna_oem_load(dir, &oem) != 0);
    const char *files[] = {"host_key.pem", "host_cert.bin", "server_ecdh_pub.bin", "capture_init.bin", "capture_packet.bin"};
    for (size_t i = 0; i < 5; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
        assert(unlink(path) == 0);
    }
    assert(rmdir(dir) == 0);
    return 0;
}
