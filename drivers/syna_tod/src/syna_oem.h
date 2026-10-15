#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNA_DEFAULT_DATA_DIR "/var/lib/fprint/syna_00dc"

typedef struct {
    char *host_key_pem;
    uint8_t *host_cert;
    size_t host_cert_len;
    uint8_t *server_ecdh_pub;
    size_t server_ecdh_pub_len;
    uint8_t *capture_init_blob;
    size_t capture_init_blob_len;
    uint8_t *capture_packet;
    size_t capture_packet_len;
} SynaOemData;

/**
 * Load OEM configuration and pairing credentials from directory.
 *
 * Looks in data_dir (or SYNA_DEFAULT_DATA_DIR if data_dir is NULL).
 * If files are not present in data_dir, also checks the local session directory
 * if available.
 *
 * @param data_dir Path to configuration directory, or NULL.
 * @param oem Output struct receiving allocated buffers.
 * @return 0 on success, negative error code if files are missing or unreadable.
 */
int syna_oem_load(const char *data_dir, SynaOemData *oem);

/**
 * Free memory in SynaOemData.
 */
void syna_oem_free(SynaOemData *oem);

#ifdef __cplusplus
}
#endif
