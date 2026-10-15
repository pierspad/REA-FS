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
    if (sz <= 0) {
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

    const char *dirs[] = {
        data_dir,
        SYNA_DEFAULT_DATA_DIR,
        "sessions/2026-10-15-synaptics-06cb-00dc/artifacts",
        "/home/ribben/Desktop/pierspad/github/REA-FS/sessions/2026-10-15-synaptics-06cb-00dc/artifacts"
    };

    const char *found_dir = NULL;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (!dirs[i]) continue;
        size_t test_len = 0;
        uint8_t *test = read_file(dirs[i], "capture_init.bin", &test_len);
        if (!test) {
            test = read_file(dirs[i], "acer-capture-init-rom6-20-group2.bin", &test_len);
        }
        if (test) {
            free(test);
            found_dir = dirs[i];
            break;
        }
    }

    if (!found_dir)
        return -2;

    /* Load host key */
    oem->host_key_pem = (char *)read_file(found_dir, "host_key.pem", &oem->host_cert_len);
    if (!oem->host_key_pem) {
        /* Check parent pairing-candidate */
        char pdir[1024];
        snprintf(pdir, sizeof(pdir), "%s/../pairing-candidate", found_dir);
        size_t klen = 0;
        oem->host_key_pem = (char *)read_file(pdir, "host-key.pem", &klen);
    }
    if (!oem->host_key_pem) {
        syna_oem_free(oem);
        return -3;
    }

    /* Load host cert */
    oem->host_cert = read_file(found_dir, "host_cert.bin", &oem->host_cert_len);
    if (!oem->host_cert) {
        char pdir[1024];
        snprintf(pdir, sizeof(pdir), "%s/../pairing-candidate", found_dir);
        size_t reply_len = 0;
        uint8_t *reply = read_file(pdir, "ownership-atomic-oem-reply.bin", &reply_len);
        if (!reply) {
            reply = read_file(pdir, "ownership-reply.bin", &reply_len);
        }
        if (reply && reply_len >= 190) {
            oem->host_cert = malloc(184);
            memcpy(oem->host_cert, reply + 6, 184);
            oem->host_cert_len = 184;
            free(reply);
        }
    }
    if (!oem->host_cert) {
        syna_oem_free(oem);
        return -4;
    }

    /* Load server ECDH pubkey (65 bytes uncompressed: 0x04 || X_be || Y_be) */
    oem->server_ecdh_pub = read_file(found_dir, "server_ecdh_pub.bin", &oem->server_ecdh_pub_len);
    if (!oem->server_ecdh_pub) {
        char pdir[1024];
        snprintf(pdir, sizeof(pdir), "%s/../pairing-candidate", found_dir);
        size_t mat_len = 0;
        uint8_t *mat = read_file(pdir, "public-material-after-ownership.bin", &mat_len);
        if (mat && mat_len == 926) {
            const uint8_t *ecdh = mat + 526;
            const uint8_t *x_le = ecdh + 8;
            const uint8_t *y_le = ecdh + 76;

            oem->server_ecdh_pub = malloc(65);
            oem->server_ecdh_pub[0] = 0x04;
            for (int i = 0; i < 32; i++) {
                oem->server_ecdh_pub[1 + i]  = x_le[31 - i];
                oem->server_ecdh_pub[33 + i] = y_le[31 - i];
            }
            oem->server_ecdh_pub_len = 65;
            free(mat);
        }
    }
    if (!oem->server_ecdh_pub) {
        syna_oem_free(oem);
        return -5;
    }

    /* Load capture init blob */
    oem->capture_init_blob = read_file(found_dir, "capture_init.bin", &oem->capture_init_blob_len);
    if (!oem->capture_init_blob) {
        oem->capture_init_blob = read_file(found_dir, "acer-capture-init-rom6-20-group2.bin", &oem->capture_init_blob_len);
    }
    if (!oem->capture_init_blob) {
        syna_oem_free(oem);
        return -6;
    }

    /* Load capture packet */
    oem->capture_packet = read_file(found_dir, "capture_packet.bin", &oem->capture_packet_len);
    if (!oem->capture_packet) {
        oem->capture_packet = read_file(found_dir, "type2449-raw-capture-request.bin", &oem->capture_packet_len);
    }
    if (!oem->capture_packet) {
        syna_oem_free(oem);
        return -7;
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
