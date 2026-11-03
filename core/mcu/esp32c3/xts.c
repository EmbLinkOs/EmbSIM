/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* xts.c -- the ESP32-C3's external memory encryption and decryption (TRM
 * v1.4 chapter 23): XTS-AES-128 under the eFuse key block whose purpose is
 * XTS_AES_128_KEY (4), for the flash.
 *
 *   - The algorithm is Espressif's addressing of XTS-AES, as espsecure's
 *     _flash_encryption_operation_aes_xts computes it: each 128-byte data
 *     unit of the flash, at an address A aligned to 128, is reversed byte by
 *     byte, encrypted (or decrypted) by IEEE 1619 XTS-AES with the tweak
 *     LE32(A) followed by twelve zero bytes, and reversed again. The key is
 *     the eFuse block's 32 bytes in reverse order (espefuse burn_key
 *     reverses an XTS key's bytes into the block); its first half encrypts
 *     the data, its second the tweak. With no such key block the key is 0.
 *   - Manual encryption (0x600CC000, TRM 23.4.5 and 23.5), which ESP-IDF's
 *     esp_flash_write_encrypted and the boot ROM use: software writes the
 *     plaintext to PLAIN_0-7 (a line at its flash address modulo 32, table
 *     23.4-2), LINESIZE (0 16, 1 32 bytes), DESTINATION (0, the flash) and PHYSICAL_ADDRESS; TRIGGER
 *     encrypts the line (STATE 1 busy, then 2 done) while SYSTEM's
 *     EXTERNAL_DEVICE_ENCRYPT_DECRYPT_CONTROL has ENABLE_SPI_MANUAL_ENCRYPT;
 *     RELEASE gives it to SPI1 (STATE 3), whose page program then writes the
 *     ciphertext at the line's address, whatever its own address, length
 *     and data (spimem.c);
 *     DESTROY clears it (STATE 0).
 *   - Automatic decryption: while flash encryption is on, what the cache
 *     reads from the flash through the MMU is decrypted (cache.c); SPI1's
 *     own reads stay raw. On is an odd number of SPI_BOOT_CRYPT_CNT's three
 *     eFuse bits, as the boot ROM's ets_efuse_cache_encryption_enabled and
 *     ESP-IDF decide it (1 on, 3 off, 7 on); the TRM (23.4.6) words it as
 *     "the first bit or the third bit set", which would leave 3 on.
 *   - The tweak is the data unit's address within the 24-bit physical
 *     address space (TRM 23.4.3: base_addr & 0x00FFFF80); the key, with no
 *     key block of that purpose, is 256 zero bits (table 23.4-1).
 * Not yet: the time encryption takes (it completes at once), the
 * download-mode controls (ENABLE_DOWNLOAD_DB_ENCRYPT, G0CB_DECRYPT,
 * MANUAL_ENCRYPT), PSRAM (the C3 has none). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "util/wcrypto.h"

#define P_XTS_AES_128 4
#define LINE_MAX 32u
#define PLAIN_BYTES 32u /* XTS_AES_PLAIN_0-7 */
#define SYSTEM_CRYPT_CTRL 0x600C0044u

struct c3_xts {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *linesize, *dest, *phys, *state;
    u8 plain[PLAIN_BYTES];
    u8 enc[LINE_MAX];
    u32 enc_addr, enc_len;
};

static struct c3_xts *xts_of(struct esk_mach *m)
{
    return (struct c3_xts *)esk_rf_ctx_of(m->rf, &esk_esp32c3_xts_ops);
}

/* the hardware's key: the XTS_AES_128_KEY block's bytes reversed */
static void hw_key(struct esk_mach *m, u8 key[32])
{
    u8 raw[32];
    int id, purpose, i;
    memset(key, 0, 32);
    for (id = 0; id < 6; id++) {
        if (esk_esp32c3_efuse_key(m, id, raw, &purpose) == 0 && purpose == P_XTS_AES_128) {
            for (i = 0; i < 32; i++)
                key[i] = raw[31 - i];
            return;
        }
    }
}

/* x * alpha in GF(2^128), little-endian (IEEE 1619) */
static void times_alpha(u8 t[16])
{
    int i, carry = (t[15] >> 7) & 1;
    for (i = 15; i > 0; i--)
        t[i] = (u8)((t[i] << 1) | (t[i - 1] >> 7));
    t[0] = (u8)(t[0] << 1);
    if (carry)
        t[0] ^= 0x87u;
}

/* one 128-byte data unit at `unit` (aligned), in place */
static void unit_crypt(const esk_aes *k1, const esk_aes *k2, u32 unit, u8 data[128], int decrypt)
{
    u8 rev[128], t[16], tw[16], x[16];
    int i, b;
    for (i = 0; i < 128; i++)
        rev[i] = data[127 - i];
    memset(tw, 0, sizeof tw);
    tw[0] = (u8)unit;
    tw[1] = (u8)(unit >> 8);
    tw[2] = (u8)(unit >> 16);
    tw[3] = (u8)(unit >> 24);
    esk_aes_encrypt(k2, tw, t);
    for (b = 0; b < 8; b++) {
        for (i = 0; i < 16; i++)
            x[i] = (u8)(rev[16 * b + i] ^ t[i]);
        if (decrypt)
            esk_aes_decrypt(k1, x, x);
        else
            esk_aes_encrypt(k1, x, x);
        for (i = 0; i < 16; i++)
            rev[16 * b + i] = (u8)(x[i] ^ t[i]);
        times_alpha(t);
    }
    for (i = 0; i < 128; i++)
        data[i] = rev[127 - i];
}

/* `len` bytes at flash address `addr` (both multiples of 16), in place */
void esk_esp32c3_xts_crypt(struct esk_mach *m, u32 addr, u8 *buf, u32 len, int decrypt)
{
    u8 key[32], unitbuf[128];
    esk_aes k1, k2;
    u32 done = 0;
    addr &= 0x00ffffffu; /* a 24-bit physical address */
    hw_key(m, key);
    esk_aes_init(&k1, key, 16);
    esk_aes_init(&k2, key + 16, 16);
    while (done < len) {
        u32 a = addr + done, unit = a & 0x00ffff80u, off = a - unit, n = 128u - off;
        if (n > len - done)
            n = len - done;
        memset(unitbuf, 0, sizeof unitbuf);
        memcpy(unitbuf + off, buf + done, n);
        unit_crypt(&k1, &k2, unit, unitbuf, decrypt);
        memcpy(buf + done, unitbuf + off, n);
        done += n;
    }
    memset(key, 0, sizeof key);
}

/* SPI1's page program while a line is released: the line's address and
 * ciphertext replace the transaction's own (the ROM's
 * esp_rom_spiflash_write_encrypted starts it with ADDR untouched) */
int esk_esp32c3_xts_take(struct esk_mach *m, u32 *addr, u8 *data, int *len)
{
    struct c3_xts *k = xts_of(m);
    if (!k || (u32)k->state->value != 3u)
        return 0;
    *addr = k->enc_addr;
    memcpy(data, k->enc, k->enc_len);
    *len = (int)k->enc_len;
    return 1;
}

static u32 xts_read(void *ctx, u32 off, int n)
{
    struct c3_xts *k = (struct c3_xts *)ctx;
    if (off < PLAIN_BYTES) {
        u32 v = 0;
        int i;
        for (i = 0; i < n && off + (u32)i < PLAIN_BYTES; i++)
            v |= (u32)k->plain[off + (u32)i] << (8 * i);
        return v;
    }
    return esk_rf_read(k->p, off, n);
}

static void xts_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_xts *k = (struct c3_xts *)ctx;
    u32 ctrl = 0;
    int i;
    if (off < PLAIN_BYTES) {
        for (i = 0; i < n && off + (u32)i < PLAIN_BYTES; i++)
            k->plain[off + (u32)i] = (u8)(v >> (8 * i));
        return;
    }
    switch (off & ~3u) {
    case 0x4Cu: /* TRIGGER */
        (void)esk_bus_debug_read(&k->s->bus, SYSTEM_CRYPT_CTRL, 4, &ctrl);
        if (!(ctrl & 1u) || ((u32)k->dest->value & 1u))
            return; /* manual encryption off, or not the flash */
        k->enc_len = 16u << ((u32)k->linesize->value & 1u);
        k->enc_addr = (u32)k->phys->value & 0x00fffff0u;
        memcpy(k->enc, k->plain + (k->enc_addr % LINE_MAX), k->enc_len);
        esk_esp32c3_xts_crypt(k->s, k->enc_addr, k->enc, k->enc_len, 0);
        esk_rf_hw(k->state, 2u);
        return;
    case 0x50u: /* RELEASE */
        if ((u32)k->state->value == 2u)
            esk_rf_hw(k->state, 3u);
        return;
    case 0x54u: /* DESTROY */
        memset(k->enc, 0, sizeof k->enc);
        memset(k->plain, 0, sizeof k->plain);
        esk_rf_hw(k->state, 0u);
        return;
    case 0x58u: /* STATE */
        return;
    default:
        esk_rf_write(k->p, off, n, v);
        return;
    }
}

static void xts_reset(void *ctx)
{
    struct c3_xts *k = (struct c3_xts *)ctx;
    memset(k->plain, 0, sizeof k->plain);
    memset(k->enc, 0, sizeof k->enc);
    k->enc_len = 0;
    esk_rf_hw(k->state, 0u);
}

const struct esk_dev_ops esk_esp32c3_xts_ops = {
    "esp32c3-xts", xts_read, xts_write, xts_reset, 0, 0,
};

void *esk_esp32c3_xts_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_xts *k = (struct c3_xts *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->linesize = esk_rf_reg(p, "LINESIZE");
    k->dest = esk_rf_reg(p, "DESTINATION");
    k->phys = esk_rf_reg(p, "PHYSICAL_ADDRESS");
    k->state = esk_rf_reg(p, "STATE");
    if (!k->linesize || !k->dest || !k->phys || !k->state)
        return 0;
    return k;
}
