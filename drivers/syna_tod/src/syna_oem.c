#include "syna_oem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint8_t *read_file(const char *dir, const char *name, size_t *out_len)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[sz] = 0;
    fclose(f);

    *out_len = (size_t)sz;
    return buf;
}

int syna_oem_load(const char *data_dir, SynaOemData *oem)
{
    if (!oem) return -1;
    memset(oem, 0, sizeof(*oem));

    const char *found_dir = data_dir;
    if (!found_dir) found_dir = getenv("SYNA_TOD_DATA_DIR");
    if (!found_dir) found_dir = SYNA_DEFAULT_DATA_DIR;

    /* Load host key */
    oem->host_key_pem = (char *)read_file(found_dir, "host_key.pem", &oem->host_cert_len);
    if (!oem->host_key_pem) {
        syna_oem_free(oem);
        return -3;
    }

    /* Load host cert */
    oem->host_cert = read_file(found_dir, "host_cert.bin", &oem->host_cert_len);
    if (!oem->host_cert) {
        syna_oem_free(oem);
        return -4;
    }

    /* Load server ECDH pubkey (65 bytes uncompressed: 0x04 || X_be || Y_be) */
    oem->server_ecdh_pub = read_file(found_dir, "server_ecdh_pub.bin", &oem->server_ecdh_pub_len);
    if (!oem->server_ecdh_pub) {
        syna_oem_free(oem);
        return -5;
    }

    /* Load capture init blob */
    oem->capture_init_blob = read_file(found_dir, "capture_init.bin", &oem->capture_init_blob_len);
    if (!oem->capture_init_blob) {
        syna_oem_free(oem);
        return -6;
    }

    /* Load capture packet */
    oem->capture_packet = read_file(found_dir, "capture_packet.bin", &oem->capture_packet_len);
    if (!oem->capture_packet) {
        syna_oem_free(oem);
        return -7;
    }

    if (oem->host_cert_len != 184 || oem->server_ecdh_pub_len != 65 ||
        oem->server_ecdh_pub[0] != 4 || oem->capture_init_blob_len != 4676 ||
        oem->capture_packet_len != 1917) {
        syna_oem_free(oem);
        return -8;
    }
    return 0;
}

void syna_oem_free(SynaOemData *oem)
{
    if (!oem) return;
    free(oem->host_key_pem);
    free(oem->host_cert);
    free(oem->server_ecdh_pub);
    free(oem->capture_init_blob);
    free(oem->capture_packet);
    memset(oem, 0, sizeof(*oem));
}
