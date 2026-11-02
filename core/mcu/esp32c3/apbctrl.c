/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* apbctrl.c -- the ESP32-C3's APB_CTRL registers, for its random number
 * generator (TRM v1.4 chapter 25): every read of RND_DATA (APB_CTRL +
 * 0xB0, the TRM's RNG_DATA_REG) is a new 32-bit number. The other
 * registers keep what is written.
 *
 * Where it differs inside: the chip's numbers come from the noise of its
 * ADCs and RC_FAST_CLK's jitter; the model's from a generator (xorshift64*
 * with a fixed seed, which a board's own chip, its settings.mac, changes),
 * the same sequence every run so that a run replays, and another for each
 * chip, as two chips never draw the same numbers.
 * Its numbers do not depend on which noise sources are on (the TRM's
 * pseudo-random numbers with none), nor on the rate they are read at. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define RND_DATA 0xB0u

struct c3_apbctrl {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u64 state;
    int seeded;
};

static u32 next(struct c3_apbctrl *k)
{
    if (!k->seeded) { /* this chip's own sequence */
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

static u32 ap_read(void *ctx, u32 off, int n)
{
    struct c3_apbctrl *k = (struct c3_apbctrl *)ctx;
    if ((off & ~3u) == RND_DATA)
        return next(k) >> (8u * (off & 3u));
    return esk_rf_read(k->p, off, n);
}

static void ap_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_apbctrl *k = (struct c3_apbctrl *)ctx;
    if ((off & ~3u) != RND_DATA)
        esk_rf_write(k->p, off, n, v);
}

const struct esk_dev_ops esk_esp32c3_apbctrl_ops = {
    "esp32c3-apbctrl", ap_read, ap_write, 0, 0, 0,
};

void *esk_esp32c3_apbctrl_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_apbctrl *k = (struct c3_apbctrl *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->state = 0x456D6253696D0003ull; /* any value but 0 */
    return k;
}
