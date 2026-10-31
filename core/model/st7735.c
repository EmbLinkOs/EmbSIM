/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* st7735.c -- a Sitronix ST7735R TFT controller with its 1.8" 128 x 160
 * panel (the Adafruit 1.8" TFT's), on its 4-wire serial interface.
 *
 * As the controller does it (ST7735R datasheet, sections 9 and 10):
 *   - the 4-wire serial interface: CSX low selects it, each SDA bit is
 *     taken on SCL's rising edge, MSB first; D/CX, read with the eighth
 *     bit, says command (low) or data (high); CSX high resets the bit
 *     counter but not the command, so a RAMWR's data may come in several
 *     selections (as ST's BSP sends it);
 *   - the frame memory, 128 x 160 at 18 bits a pixel (GM 128 x 160), its
 *     content at power-up whatever the cells hold (noise here, the same
 *     each time);
 *   - CASET and RASET set the window, RAMWR writes it from its start,
 *     column by column then row by row as MADCTL's MV, MX and MY order
 *     them, wrapping at its end; COLMOD's 12, 16 and 18 bits a pixel;
 *     MADCTL's RGB bit swaps red and blue on the panel;
 *   - SLPIN, SLPOUT, DISPOFF, DISPON, INVOFF, INVON, IDMOFF, IDMON (eight
 *     colours: each component's top bit), NORON, PTLON with PTLAR (the
 *     rows outside the partial area blank), SWRESET and the RESX pin;
 *     after a reset (the pin, SWRESET, power-up) the controller takes no
 *     command for 5 ms (the datasheet's reset cancel time in sleep in
 *     mode), which is said once;
 *   - after a reset: sleep in, display off, normal mode, no inversion,
 *     COLMOD 18 bits, MADCTL 0, the window the whole memory;
 *   - the panel: blank (white, a normally-white panel) while sleeping or
 *     the display is off, dark without power or backlight (LED low).
 * The parameters of the panel commands (frame rate B1 to B3, inversion
 * B4, power C0 to C5, gamma E0, E1 and GAMSET) are taken and kept, with
 * no effect on the image.
 * Not modelled: the 3-wire (9-bit) interface and the parallel ones, the
 * read commands (RDDID, RDDST, RAMRD: SDA is never driven), vertical
 * scrolling (SCRLAR, VSCSAD), the tearing effect output, the frame rate
 * and the panel's response time, the gamma curves' effect on colours. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"

#define W 128
#define H 160
#define RESET_PS 5000000000ull /* 5 ms */

typedef struct st7735_state {
    esk_pin *vcc, *scl, *sda, *cs, *dc, *rst, *led;
    uint32_t mem[W * H]; /* RGB666: r << 12 | g << 6 | b */
    int bits;
    uint8_t shift;
    uint8_t cmd;
    int nparam;
    uint8_t param[16];
    int ramwr;
    uint8_t px[3];
    int npx;
    int xs, xe, ys, ye, x, y;
    int sleeping, display_on, inverted, idle, partial;
    int psr, per; /* the partial area's rows */
    uint8_t colmod, madctl;
    int last_scl, powered, in_reset;
    es_time_t busy_until;
    int warned_busy;
    uint64_t ram_writes, commands;
    int probe_x, probe_y;
} st7735_state;

static int lv(esk_pin *p, int dflt)
{
    esk_logic l;
    if (!p || !p->net)
        return dflt;
    l = esk_pin_read(p);
    return l == ESK_L1 ? 1 : l == ESK_L0 ? 0 : dflt;
}

static int powered(st7735_state *s)
{
    if (!s->vcc || !s->vcc->net)
        return 1;
    if (s->vcc->watch_on)
        return s->vcc->watch_state == ESK_L1;
    return esk_net_value(s->vcc->net) == ESK_L1;
}

/* what the cells hold at power-up: the same noise each time */
static void noise(st7735_state *s)
{
    uint32_t x = 0x2545F491u, i;
    for (i = 0; i < W * H; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s->mem[i] = x & 0x3FFFFu;
    }
}

static void soft_reset(st7735_state *s, esk_comp *c)
{
    s->bits = 0;
    s->cmd = 0;
    s->nparam = 0;
    s->ramwr = 0;
    s->npx = 0;
    s->xs = 0;
    s->xe = W - 1;
    s->ys = 0;
    s->ye = H - 1;
    s->x = s->y = 0;
    s->sleeping = 1;
    s->display_on = 0;
    s->inverted = s->idle = s->partial = 0;
    s->psr = 0;
    s->per = H - 1;
    s->colmod = 6;
    s->madctl = 0;
    s->busy_until = esk_comp_now(c) + RESET_PS;
}

/* the window's next address, as MADCTL orders it */
static void store(st7735_state *s, uint32_t rgb)
{
    int mv = (s->madctl >> 5) & 1, mx = (s->madctl >> 6) & 1, my = (s->madctl >> 7) & 1;
    int maxc = mv ? H - 1 : W - 1, maxr = mv ? W - 1 : H - 1;
    int c = mx ? maxc - s->x : s->x, r = my ? maxr - s->y : s->y;
    int px = mv ? r : c, py = mv ? c : r;
    if (px >= 0 && px < W && py >= 0 && py < H)
        s->mem[py * W + px] = rgb;
    s->ram_writes++;
    if (++s->x > s->xe) {
        s->x = s->xs;
        if (++s->y > s->ye)
            s->y = s->ys;
    }
}

static uint32_t c5to6(uint32_t v) { return v << 1 | v >> 4; }
static uint32_t c4to6(uint32_t v) { return v << 2 | v >> 2; }

static void pixel_byte(st7735_state *s, uint8_t b)
{
    s->px[s->npx++] = b;
    if (s->colmod == 5 && s->npx == 2) {
        uint32_t v = (uint32_t)s->px[0] << 8 | s->px[1];
        store(s, c5to6(v >> 11) << 12 | ((v >> 5) & 0x3Fu) << 6 | c5to6(v & 0x1Fu));
        s->npx = 0;
    } else if (s->colmod == 3 && s->npx == 3) {
        /* 12 bits a pixel: two pixels in three bytes */
        uint32_t a = (uint32_t)s->px[0] << 4 | s->px[1] >> 4, b2 = ((uint32_t)s->px[1] & 0xFu) << 8 | s->px[2];
        store(s, c4to6(a >> 8) << 12 | c4to6((a >> 4) & 0xFu) << 6 | c4to6(a & 0xFu));
        store(s, c4to6(b2 >> 8) << 12 | c4to6((b2 >> 4) & 0xFu) << 6 | c4to6(b2 & 0xFu));
        s->npx = 0;
    } else if (s->colmod != 5 && s->colmod != 3 && s->npx == 3) {
        store(s, (uint32_t)(s->px[0] >> 2) << 12 | (uint32_t)(s->px[1] >> 2) << 6 | (uint32_t)(s->px[2] >> 2));
        s->npx = 0;
    }
}

static void command(st7735_state *s, esk_comp *c, uint8_t b)
{
    s->cmd = b;
    s->nparam = 0;
    s->ramwr = 0;
    s->npx = 0;
    s->commands++;
    switch (b) {
    case 0x01: /* SWRESET */
        soft_reset(s, c);
        break;
    case 0x10: /* SLPIN */
        s->sleeping = 1;
        break;
    case 0x11: /* SLPOUT */
        s->sleeping = 0;
        break;
    case 0x12: /* PTLON */
        s->partial = 1;
        break;
    case 0x13: /* NORON */
        s->partial = 0;
        break;
    case 0x20: /* INVOFF */
        s->inverted = 0;
        break;
    case 0x21: /* INVON */
        s->inverted = 1;
        break;
    case 0x28: /* DISPOFF */
        s->display_on = 0;
        break;
    case 0x29: /* DISPON */
        s->display_on = 1;
        break;
    case 0x2C: /* RAMWR: from the window's start */
        s->ramwr = 1;
        s->x = s->xs;
        s->y = s->ys;
        break;
    case 0x38: /* IDMOFF */
        s->idle = 0;
        break;
    case 0x39: /* IDMON */
        s->idle = 1;
        break;
    default:
        break;
    }
}

static void data(st7735_state *s, uint8_t b)
{
    int n = s->nparam;
    if (s->ramwr) {
        pixel_byte(s, b);
        return;
    }
    if (n < (int)sizeof s->param)
        s->param[n] = b;
    s->nparam++;
    switch (s->cmd) {
    case 0x2A: /* CASET: XS, XE (16 bits each) */
    case 0x2B: /* RASET */
        if (n == 3) {
            int a0 = s->param[0] << 8 | s->param[1], a1 = s->param[2] << 8 | s->param[3];
            if (s->cmd == 0x2A) {
                s->xs = a0;
                s->xe = a1;
            } else {
                s->ys = a0;
                s->ye = a1;
            }
        }
        break;
    case 0x30: /* PTLAR: PSL, PEL */
        if (n == 3) {
            s->psr = s->param[0] << 8 | s->param[1];
            s->per = s->param[2] << 8 | s->param[3];
        }
        break;
    case 0x36: /* MADCTL */
        if (n == 0)
            s->madctl = b;
        break;
    case 0x3A: /* COLMOD: IFPF */
        if (n == 0)
            s->colmod = b & 7u;
        break;
    default:
        break; /* the panel's settings: kept, no effect on the image */
    }
}

static void byte_in(st7735_state *s, esk_comp *c, uint8_t b, int is_data)
{
    if (esk_comp_now(c) < s->busy_until) {
        if (!s->warned_busy) {
            s->warned_busy = 1;
            esk_comp_diag(c, ES_SEV_WARNING, "st7735.reset_wait",
                          "a byte came within 5 ms of the controller's reset (RESX, SWRESET or power-up) and was "
                          "ignored (ST7735R: the reset cancel time)");
        }
        return;
    }
    if (is_data)
        data(s, b);
    else
        command(s, c, b);
}

static void st_input(esk_comp *c, esk_pin *p)
{
    st7735_state *s = (st7735_state *)c->state;
    int scl, now_power = powered(s), rst;
    if (now_power != s->powered) {
        s->powered = now_power;
        if (now_power) {
            noise(s);
            soft_reset(s, c);
        }
    }
    if (!s->powered)
        return;
    rst = lv(s->rst, 1);
    if (!rst) {
        if (!s->in_reset)
            s->in_reset = 1;
        return;
    }
    if (s->in_reset) {
        s->in_reset = 0;
        soft_reset(s, c); /* the reset ends: 5 ms before the first command */
    }
    if (p == s->cs && lv(s->cs, 1)) {
        s->bits = 0; /* deselected: the serial interface starts again */
        return;
    }
    if (p != s->scl)
        return;
    scl = lv(s->scl, 0);
    if (scl && !s->last_scl && !lv(s->cs, 1)) {
        s->shift = (uint8_t)(s->shift << 1 | (lv(s->sda, 0) ? 1u : 0u));
        if (++s->bits == 8) {
            s->bits = 0;
            byte_in(s, c, s->shift, lv(s->dc, 1));
        }
    }
    s->last_scl = scl;
}

static es_status st_create(esk_comp *c)
{
    st7735_state *s = (st7735_state *)c->state;
    uint32_t i;
    s->vcc = esk_comp_pin(c, "VCC");
    s->scl = esk_comp_pin(c, "SCK");
    s->sda = esk_comp_pin(c, "SDA");
    s->cs = esk_comp_pin(c, "CS");
    s->dc = esk_comp_pin(c, "DC");
    s->rst = esk_comp_pin(c, "RST");
    s->led = esk_comp_pin(c, "LED");
    if (!s->scl || !s->sda)
        return ES_ESCHEMA;
    for (i = 0; i < c->npins; i++)
        if (c->pins[i].model.kind == ESK_PIN_DIGITAL_IN || c->pins[i].model.kind == ESK_PIN_DIGITAL_IO)
            esk_pin_watch(&c->pins[i], 1);
    return ES_OK;
}

static void st_reset(esk_comp *c)
{
    st7735_state *s = (st7735_state *)c->state;
    if (s->vcc && s->vcc->net) {
        (void)esk_pin_compare(s->vcc, 2.3); /* its supply's power-on reset */
        esk_pin_watch(s->vcc, 1);
    }
    s->powered = powered(s);
    noise(s);
    soft_reset(s, c);
    s->last_scl = lv(s->scl, 0);
    s->in_reset = !lv(s->rst, 1);
    s->warned_busy = 0;
    s->ram_writes = s->commands = 0;
    s->probe_x = s->probe_y = 0;
}

static es_status st_set(esk_comp *c, const char *input, const esk_json *value)
{
    st7735_state *s = (st7735_state *)c->state;
    int v;
    if (value->type != ESK_JSON_NUMBER)
        return ES_EINVAL;
    v = (int)value->u.number.value;
    if (!strcmp(input, "probe_x") && v >= 0 && v < W)
        s->probe_x = v;
    else if (!strcmp(input, "probe_y") && v >= 0 && v < H)
        s->probe_y = v;
    else
        return ES_EINVAL;
    return ES_OK;
}

/* the panel as it shows: RGB565 */
static uint16_t shown(const st7735_state *s, int px, int py)
{
    uint32_t v, r, g, b;
    if (!s->powered || !lv(s->led, 1))
        return 0x0000; /* no backlight: dark */
    if (s->sleeping || !s->display_on || (s->partial && (py < s->psr || py > s->per)))
        return 0xFFFF; /* blank: a normally-white panel */
    v = s->mem[py * W + px];
    r = v >> 12 & 0x3Fu;
    g = v >> 6 & 0x3Fu;
    b = v & 0x3Fu;
    if (s->madctl & 8u) { /* BGR */
        uint32_t t = r;
        r = b;
        b = t;
    }
    if (s->idle) {
        r = (r & 0x20u) ? 0x3Fu : 0;
        g = (g & 0x20u) ? 0x3Fu : 0;
        b = (b & 0x20u) ? 0x3Fu : 0;
    }
    if (s->inverted) {
        r ^= 0x3Fu;
        g ^= 0x3Fu;
        b ^= 0x3Fu;
    }
    return (uint16_t)((r >> 1) << 11 | g << 5 | (b >> 1));
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void st_observe(esk_comp *c, esk_json *out)
{
    st7735_state *s = (st7735_state *)c->state;
    esk_alloc *a = c->e->a;
    static uint8_t raw[W * H * 2];
    static char b64[(W * H * 2 + 2) / 3 * 4 + 1];
    uint32_t h = 2166136261u, i, o = 0, white = 0;
    char hex[12];
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint16_t v = shown(s, x, y);
            raw[2 * (y * W + x)] = (uint8_t)(v >> 8);
            raw[2 * (y * W + x) + 1] = (uint8_t)v;
            if (v == 0xFFFF)
                white++;
        }
    for (i = 0; i < sizeof raw; i++) {
        h ^= raw[i];
        h *= 16777619u;
    }
    for (i = 0; i + 2 < sizeof raw; i += 3) {
        uint32_t v = (uint32_t)raw[i] << 16 | (uint32_t)raw[i + 1] << 8 | raw[i + 2];
        b64[o++] = B64[v >> 18];
        b64[o++] = B64[(v >> 12) & 63u];
        b64[o++] = B64[(v >> 6) & 63u];
        b64[o++] = B64[v & 63u];
    }
    if (i < sizeof raw) { /* the last one or two bytes */
        uint32_t v = (uint32_t)raw[i] << 16 | (i + 1 < sizeof raw ? (uint32_t)raw[i + 1] << 8 : 0u);
        b64[o++] = B64[v >> 18];
        b64[o++] = B64[(v >> 12) & 63u];
        b64[o++] = i + 1 < sizeof raw ? B64[(v >> 6) & 63u] : '=';
        b64[o++] = '=';
    }
    b64[o] = 0;
    (void)snprintf(hex, sizeof hex, "%08lx", (unsigned long)h);
    esk_json_set_bool(a, out, "on", s->powered && !s->sleeping && s->display_on);
    esk_json_set_bool(a, out, "sleeping", s->sleeping);
    esk_json_set_bool(a, out, "inverted", s->inverted);
    esk_json_set_integer(a, out, "colmod", s->colmod);
    esk_json_set_integer(a, out, "madctl", s->madctl);
    esk_json_set_integer(a, out, "ram_writes", (int64_t)s->ram_writes);
    esk_json_set_integer(a, out, "commands", (int64_t)s->commands);
    esk_json_set_integer(a, out, "white_pixels", white);
    esk_json_set_integer(a, out, "probe", shown(s, s->probe_x, s->probe_y));
    esk_json_set_string(a, out, "checksum", hex);
    esk_json_set_string(a, out, "frame", b64);
}

const esk_model_ops esk_st7735_ops = { "st7735", sizeof(st7735_state), st_create, NULL, st_reset, st_input,
                                       NULL, st_set, st_observe, NULL, 0 };
