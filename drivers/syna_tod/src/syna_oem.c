#include "syna_oem.h"
#include "syna_capture_tables.h"
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

    /* Load capture init blob (or use compiled-in ROM 6.20 table if file absent) */
    oem->capture_init_blob = read_file(found_dir, "capture_init.bin", &oem->capture_init_blob_len);
    if (!oem->capture_init_blob) {
        oem->capture_init_blob = malloc(sizeof(SYNA_BUILTIN_CAPTURE_INIT));
        if (!oem->capture_init_blob) {
            syna_oem_free(oem);
            return -6;
        }
        memcpy(oem->capture_init_blob, SYNA_BUILTIN_CAPTURE_INIT, sizeof(SYNA_BUILTIN_CAPTURE_INIT));
        oem->capture_init_blob_len = sizeof(SYNA_BUILTIN_CAPTURE_INIT);
    }

    /* Load capture packet (or use compiled-in Module 0x2449 table if file absent) */
    oem->capture_packet = read_file(found_dir, "capture_packet.bin", &oem->capture_packet_len);
    if (!oem->capture_packet) {
        oem->capture_packet = malloc(sizeof(SYNA_BUILTIN_CAPTURE_PACKET));
        if (!oem->capture_packet) {
            syna_oem_free(oem);
            return -7;
        }
        memcpy(oem->capture_packet, SYNA_BUILTIN_CAPTURE_PACKET, sizeof(SYNA_BUILTIN_CAPTURE_PACKET));
        oem->capture_packet_len = sizeof(SYNA_BUILTIN_CAPTURE_PACKET);
    }

    if (oem->host_cert_len != 184 || oem->server_ecdh_pub_len != 65 ||
        oem->server_ecdh_pub[0] != 4 || oem->capture_init_blob_len != 4676 ||
        oem->capture_packet_len != 1917 || oem->capture_init_blob[0] != 2 ||
        oem->capture_packet[0] != 2) {
        syna_oem_free(oem);
        return -8;
    }
    return 0;
}

static int write_private_file(const char *dir, const char *name, const uint8_t *buf, size_t len)
{
    char path[1024];
    char tmp_path[1040];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", dir, name);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return -1;
    chmod(tmp_path, 0600);
    if (fwrite(buf, 1, len, f) != len) {
        fclose(f);
        remove(tmp_path);
        return -2;
    }
    fclose(f);
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -3;
    }
    chmod(path, 0600);
    return 0;
}

int syna_oem_save_credentials(const char *data_dir,
                              const char *host_key_pem,
                              const uint8_t *host_cert, size_t host_cert_len,
                              const uint8_t *server_ecdh_pub, size_t server_ecdh_pub_len)
{
    if (!host_key_pem || !host_cert || host_cert_len != 184 ||
        !server_ecdh_pub || server_ecdh_pub_len != 65 || server_ecdh_pub[0] != 0x04) {
        return -1;
    }

    const char *target_dir = data_dir;
    if (!target_dir) target_dir = getenv("SYNA_TOD_DATA_DIR");
    if (!target_dir) target_dir = SYNA_DEFAULT_DATA_DIR;

    mkdir(target_dir, 0700);
    chmod(target_dir, 0700);

    if (write_private_file(target_dir, "host_key.pem", (const uint8_t *)host_key_pem, strlen(host_key_pem)) != 0)
        return -2;
    if (write_private_file(target_dir, "host_cert.bin", host_cert, host_cert_len) != 0)
        return -3;
    if (write_private_file(target_dir, "server_ecdh_pub.bin", server_ecdh_pub, server_ecdh_pub_len) != 0)
        return -4;

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

