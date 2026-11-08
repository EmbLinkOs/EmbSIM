/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_IMAGE_H
#define ESK_ESP32_IMAGE_H
#include "embsim/types.h"

/* Original ESP32 standalone plaintext image, not a merged flash dump, ELF,
 * encrypted image or secure-boot signature. No memory is mapped or executed.
 * Segment addresses and revision bounds are reported, not board-qualified.
 * On failure *out is zero; on success segment offsets borrow the input bytes. */
typedef struct esk_esp32_image_segment {
    uint32_t address, length;
    size_t offset;
} esk_esp32_image_segment;
typedef struct esk_esp32_image {
    uint32_t entry;
    unsigned count, flash_mode, flash_size_code, flash_frequency_code;
    unsigned wp_pin, drive[3], min_revision_legacy, min_revision, max_revision;
    int has_digest;
    size_t checksum_offset;
    esk_esp32_image_segment segment[16];
} esk_esp32_image;
es_status esk_esp32_image_inspect(const unsigned char *data, size_t len,
                                 esk_esp32_image *out, char *why, size_t cap);
#endif
