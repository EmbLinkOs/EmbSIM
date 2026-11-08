/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* misc.c -- what the ESP32 answers outside its register description:
 *
 *   - the random number generator's RNG_DATA (TRM v5 chapter 26) at
 *     0x3FF75144: every read a new 32-bit number. The chip's numbers come
 *     from the noise of its RF and SAR ADC circuits; the model's from a
 *     generator (xorshift64* seeded by the chip's MAC), the same sequence
 *     every run so a run replays, another for each chip;
 *   - the peripherals' AHB view (TRM 3.1.4): 0x60000000-0x6003FFFF reaches
 *     the same registers as 0x3FF40000-0x3FF7FFFF (ESP-IDF reads UART FIFOs
 *     there). Inside, the access is passed to the bus at the DPORT address. */
#include "mcu/esp32/soc/esp32.h"

#define RNG_DATA 0x3FF75144u
#define AHB_BASE 0x60000000u
#define AHB_SIZE 0x40000u
#define DPORT_VIEW 0x3FF40000u

struct e32_rng {
    struct esk_mach *s;
    u64 state;
    int seeded;
};

static u32 rng_next(struct e32_rng *k)
{
    if (!k->seeded) {
        int i;
        k->seeded = 1;
        if (k->s->has_factory_mac)
            for (i = 0; i < 6; i++)
                k->state = (k->state ^ k->s->factory_mac[i]) * 0x100000001B3ull;
        if (!k->state)
            k->state = 1;
    }
    k->state ^= k->state >> 12;
    k->state ^= k->state << 25;
    k->state ^= k->state >> 27;
    return (u32)((k->state * 0x2545F4914F6CDD1Dull) >> 32);
}

static u32 rng_read(void *ctx, u32 off, int n)
{
    ES_UNUSED(n);
    return rng_next((struct e32_rng *)ctx) >> (8 * (off & 3u));
}

static void rng_write(void *ctx, u32 off, int n, u32 v)
{
    ES_UNUSED(ctx);
    ES_UNUSED(off);
    ES_UNUSED(n);
    ES_UNUSED(v);
}

const struct esk_dev_ops esk_esp32_rng_ops = {
    "esp32-rng", rng_read, rng_write, 0, 0, 0,
};

static u32 ahb_read(void *ctx, u32 off, int n)
{
    struct esk_mach *s = (struct esk_mach *)ctx;
    u32 v = 0;
    if (esk_bus_read(&s->bus, DPORT_VIEW + off, n, &v))
        esk_bus_fault(&s->bus);
    return v;
}

static void ahb_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_mach *s = (struct esk_mach *)ctx;
    if (esk_bus_write(&s->bus, DPORT_VIEW + off, n, v))
        esk_bus_fault(&s->bus);
}

static const struct esk_dev_ops ahb_ops = {
    "esp32-ahb", ahb_read, ahb_write, 0, 0, 0,
};

/* the RNG and the AHB view: 0, or -1 */
int esk_esp32_misc_add(struct esk_mach *s)
{
    struct e32_rng *k = (struct e32_rng *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return -1;
    k->s = s;
    k->state = 0x456D6253696D0032ull;
    if (esk_mach_add_dev(s, RNG_DATA, 4, &esk_esp32_rng_ops, k) != ES_OK ||
        esk_mach_add_dev(s, AHB_BASE, AHB_SIZE, &ahb_ops, s) != ES_OK)
        return -1;
    return 0;
}
