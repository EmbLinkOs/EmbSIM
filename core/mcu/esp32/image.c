/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Original implementation from Espressif's documented firmware image format:
 * docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html
 * No ROM or third-party loader code is embedded. See image.h for scope. */
#include "mcu/esp32/image.h"
#include "util/sha256.h"
#include <stdio.h>
#include <string.h>

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static es_status bad(char *why, size_t cap, const char *message)
{
    if (why && cap) snprintf(why, cap, "%s", message);
    return ES_EINVAL;
}
es_status esk_esp32_image_inspect(const unsigned char *data, size_t len,
                                 esk_esp32_image *out, char *why, size_t cap)
{
    esk_esp32_image v;
    size_t pos = 24, i, k, footer;
    unsigned checksum = 0xef;
    if (why && cap) why[0] = 0;
    if (!out) return bad(why, cap, "An output descriptor is required.");
    memset(out, 0, sizeof *out); memset(&v, 0, sizeof v);
    if (!data || len < 24 || len > 67108864) return bad(why, cap, "Expected a standalone ESP32 image of 24 bytes to 64 MiB.");
    if (data[0] != 0xe9) return bad(why, cap, "Expected ESP image magic E9; ELF, encrypted and merged flash images are not accepted.");
    if (!data[1] || data[1] > 16) return bad(why, cap, "ESP image segment count must be 1 to 16.");
    if (le16(data + 12) != 0) return bad(why, cap, "Image is not for the original ESP32 (chip ID 0).");
    if (data[23] > 1) return bad(why, cap, "Invalid appended-digest flag.");
    if (data[2] > 3 || (data[3] >> 4) > 4 || ((data[3] & 15) > 2 && (data[3] & 15) != 15))
        return bad(why, cap, "Unsupported SPI flash mode, size or frequency encoding.");
    v.count = data[1]; v.entry = le32(data + 4); v.flash_mode = data[2];
    v.flash_size_code = (unsigned)data[3] >> 4; v.flash_frequency_code = data[3] & 15u;
    v.wp_pin = data[8]; for (i = 0; i < 3; i++) v.drive[i] = data[9 + i];
    v.min_revision_legacy = data[14]; v.min_revision = le16(data + 15); v.max_revision = le16(data + 17);
    v.has_digest = data[23];
    for (i = 0; i < v.count; i++) {
        uint32_t address, size;
        if (len - pos < 8) return bad(why, cap, "Truncated segment header.");
        address = le32(data + pos); size = le32(data + pos + 4); pos += 8;
        if ((uint64_t)address + size > UINT64_C(0x100000000)) return bad(why, cap, "Segment address wraps the 32-bit address space.");
        if ((size_t)size > len - pos) return bad(why, cap, "Truncated segment data.");
        v.segment[i].address = address; v.segment[i].length = size; v.segment[i].offset = pos;
        for (k = 0; k < size; k++) checksum ^= data[pos + k];
        pos += size;
    }
    /* The checksum is the last byte of the next 16-byte block, including
     * when segment data itself already ends on a 16-byte boundary. */
    footer = pos + (15 - (pos & 15u));
    if (footer >= len) return bad(why, cap, "Truncated checksum footer.");
    for (i = pos; i < footer; i++) if (data[i]) return bad(why, cap, "Nonzero image footer padding.");
    if (data[footer] != checksum) return bad(why, cap, "ESP image segment XOR checksum mismatch.");
    v.checksum_offset = footer;
    pos = footer + 1;
    if (v.has_digest) {
        esk_sha256 hash; unsigned char digest[32];
        if (len - pos < 32) return bad(why, cap, "Truncated appended SHA-256 digest.");
        esk_sha256_init(&hash); esk_sha256_update(&hash, data, pos); esk_sha256_final(&hash, digest);
        if (memcmp(digest, data + pos, 32)) return bad(why, cap, "ESP image appended SHA-256 mismatch.");
        pos += 32;
    }
    if (pos != len) return bad(why, cap, "Trailing bytes: merged dumps, signed images and appended data are not supported by this inspector.");
    *out = v; return ES_OK;
}
