/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* nor.c -- the ESP32-C3FN4's in-package 4 MB flash, an XMC XM25QH32C (its
 * datasheet Rev. 2.0, 2023-04-25): what SPI0 and SPI1 (spimem.c) see of it.
 * The part is the one esptool reports on these chips ("Embedded Flash 4MB
 * (XMC)", RDID 20 40 16). Its bytes are the description's `flash` store
 * (esk_mach_store), which keeps them through every reset.
 *
 * One transaction is what the chip receives while its /CS is low -- the
 * instruction, then the address and data the controller sends -- and the
 * bytes it answers with after them. The instructions (tables 7.1.2 and
 * 7.1.3), in standard, dual and quad SPI:
 *   - Write Enable (06h), Volatile SR Write Enable (50h), Write Disable
 *     (04h);
 *   - Read Status Register-1, -2, -3 (05h, 35h, 15h), repeated while
 *     clocked; Write Status Register-1 (01h, and Status Register-2 after it
 *     when a second byte comes), -2 (31h), -3 (11h): tW (1 ms) busy after
 *     06h, none after 50h; SUS, BUSY and WEL read-only, LB3-1 one-time
 *     programmable;
 *   - Read Data (03h), Fast Read (0Bh), Dual Output (3Bh), Quad Output
 *     (6Bh), Dual I/O (BBh), Quad I/O (EBh), Word Read Quad I/O (E7h): from
 *     the address on, wrapping at the end of the array; the quad ones need
 *     QE (S9), and without it the chip does not answer (the lines read 1);
 *   - Page Program (02h) and Quad Input Page Program (32h, with QE): bits
 *     only go from 1 to 0, the address wraps inside its 256-byte page,
 *     tPP (0.5 ms) busy; Sector Erase (20h, 4 KB, tSE 50 ms), Block Erase
 *     (52h, 32 KB, tBE1 0.15 s; D8h, 64 KB, tBE2 0.3 s), Chip Erase
 *     (C7h/60h, tCE 20 s): each needs WEL, which it clears;
 *   - the three 256-byte Security Registers at 0x1000, 0x2000, 0x3000:
 *     Erase (44h), Program (42h), Read (48h), locked by LB1-3;
 *   - Erase/Program Suspend (75h) and Resume (7Ah): SUS set, BUSY clear,
 *     and the operation's remaining time kept for the resume;
 *   - Power-down (B9h): every instruction but ABh ignored; Ultra-Deep
 *     Power-down (79h, from power-down): ignored all until /CS is pulsed;
 *     Release Power-down / ID (ABh: 15h, repeated);
 *   - Manufacturer/Device ID (90h; dual 92h, quad 94h): 20h 15h, or 15h 20h
 *     with address bit 0 set; JEDEC ID (9Fh): 20h 40h 16h;
 *   - Read Unique ID (4Bh): 64 bits, 0x456d6253696d0003 ("EmbSim", 3: the
 *     part's own is unknown);
 *   - Enable Reset (66h) then Reset Device (99h): WEL, SUS and the
 *     suspended operation cleared, the volatile status bits from the
 *     non-volatile ones.
 * While BUSY only the status reads, Suspend and the reset pair are taken.
 * Times are the datasheet's typical ones, counted on the machine's clock.
 * Inside, the instruction's dummy clocks are not counted: the controller's
 * dummy phase is taken to sample the data where the chip puts it, as a
 * correct timing calibration does.
 * Not yet: QPI mode (38h), Burst Read with Wrap (77h, 0Ch), Set Read
 * Parameters (C0h), the continuous read mode (M7-M0), the SFDP table (5Ah
 * answers FFh), the block protection bits (BP2-0, TB, SEC, CMP: kept but
 * protecting nothing), /WP with SRP0/SRP1, WPS, and the output drive
 * strength (kept as written). */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define PAGE     256u
#define T_W_PS   1000000000ull     /* 1 ms */
#define T_PP_PS  500000000ull      /* 0.5 ms */
#define T_SE_PS  50000000000ull    /* 50 ms */
#define T_BE1_PS 150000000000ull   /* 0.15 s */
#define T_BE2_PS 300000000000ull   /* 0.3 s */
#define T_CE_PS  20000000000000ull /* 20 s */

#define SR1_BUSY     0x01u
#define SR1_WEL      0x02u
#define SR2_QE       0x02u
#define SR2_LB_SHIFT 3
#define SR2_SUS      0x80u

static const u8 unique_id[8] = { 0x45, 0x6d, 0x62, 0x53, 0x69, 0x6d, 0x00, 0x03 };

struct esk_c3_nor {
    struct esk_mach *m;
    struct esk_store *st;
    u8 sr[3];     /* the status registers as read */
    u8 sr_nv[3];  /* their non-volatile values */
    int vwe;      /* 50h: the next status write is volatile */
    int pd, udpd; /* power-down, ultra-deep power-down */
    int reset_en; /* 66h was the last instruction */
    u64 now_ps, last_cycles;
    u64 busy_end; /* the self-timed operation ends then (ps) */
    u64 sus_left; /* a suspended operation's remaining time */
    u8 sec[3][256];
};

static u64 now_ps(struct esk_c3_nor *f)
{
    u32 hz = esk_mach_core_hz(f->m);
    u64 dc = f->m->cycles - f->last_cycles;
    f->last_cycles = f->m->cycles;
    if (hz)
        f->now_ps += (u64)((double)dc * 1e12 / (double)hz);
    return f->now_ps;
}

static void sr_clear(struct esk_c3_nor *f, int k, u32 bits)
{
    f->sr[k] = (u8)(f->sr[k] & ~bits);
}
static void sr_set(struct esk_c3_nor *f, int k, u32 bits)
{
    f->sr[k] = (u8)(f->sr[k] | bits);
}

static int busy(struct esk_c3_nor *f)
{
    if ((f->sr[0] & SR1_BUSY) && now_ps(f) >= f->busy_end) {
        sr_clear(f, 0, SR1_BUSY | SR1_WEL);
    }
    return (f->sr[0] & SR1_BUSY) != 0;
}

static void start(struct esk_c3_nor *f, u64 ps)
{
    sr_set(f, 0, SR1_BUSY);
    f->busy_end = now_ps(f) + ps;
}

static u32 addr24(const u8 *tx, int n)
{
    u32 a = 0;
    int i;
    for (i = 1; i <= 3; i++)
        a = a << 8 | (i < n ? tx[i] : 0xffu);
    return a;
}

static void program(struct esk_c3_nor *f, u32 a, const u8 *d, int n)
{
    u32 page = a & ~(PAGE - 1u), off = a & (PAGE - 1u);
    int i;
    for (i = 0; i < n; i++) {
        u32 x = (page + ((off + (u32)i) & (PAGE - 1u))) % f->st->size;
        f->st->mem[x] = (u8)(f->st->mem[x] & d[i]);
    }
    esk_esp32c3_flash_written(f->m, page % f->st->size, PAGE);
}

static void erase(struct esk_c3_nor *f, u32 a, u32 size)
{
    u32 base = (a & ~(size - 1u)) % f->st->size;
    memset(f->st->mem + base, 0xff, size);
    esk_esp32c3_flash_written(f->m, base, size);
}

/* the security register an address names (0x1000, 0x2000, 0x3000), or -1 */
static int sec_reg(u32 a)
{
    u32 r = a >> 12;
    return (a >> 16) == 0 && (a & 0xf00u) == 0 && r >= 1 && r <= 3 ? (int)r - 1 : -1;
}

static void write_sr(struct esk_c3_nor *f, int k, u8 v)
{
    static const u8 writable[3] = { 0xfc, 0x7b, 0xe4 };
    u8 w = writable[k];
    if (k == 1) {
        /* LB3-1 are one-time programmable: once set they stay */
        v = (u8)(v | (f->sr[1] & (7u << SR2_LB_SHIFT)));
    }
    f->sr[k] = (u8)((f->sr[k] & ~w) | (v & w));
    if (!f->vwe)
        f->sr_nv[k] = (u8)((f->sr_nv[k] & ~w) | (v & w));
}

static void reset_device(struct esk_c3_nor *f)
{
    memcpy(f->sr, f->sr_nv, sizeof f->sr);
    sr_clear(f, 0, SR1_BUSY | SR1_WEL);
    sr_clear(f, 1, SR2_SUS);
    f->sus_left = 0;
    f->vwe = 0;
}

static int is_read_status(u8 op)
{
    return op == 0x05 || op == 0x35 || op == 0x15;
}

void esk_esp32c3_nor_xfer(struct esk_c3_nor *f, const u8 *tx, int ntx, u8 *rx, int nrx)
{
    u8 op;
    u32 a;
    int i, qe, sr;
    memset(rx, 0xff, (size_t)nrx);
    if (f->udpd) {
        f->udpd = 0; /* any /CS pulse brings it out (7.2.40) */
        return;
    }
    if (ntx < 1)
        return;
    op = tx[0];
    if (f->pd) {
        if (op == 0x79 && !busy(f)) {
            f->udpd = 1;
            return;
        }
        if (op != 0xab)
            return;
    }
    if (busy(f) && !is_read_status(op) && op != 0x75 && op != 0x66 && op != 0x99)
        return;
    if (op != 0x99)
        f->reset_en = op == 0x66;
    qe = (f->sr[1] & SR2_QE) != 0;
    a = addr24(tx, ntx);
    switch (op) {
    case 0x06: /* write enable */
        sr_set(f, 0, SR1_WEL);
        f->vwe = 0;
        break;
    case 0x50:
        f->vwe = 1;
        break;
    case 0x04:
        sr_clear(f, 0, SR1_WEL);
        f->vwe = 0;
        break;
    case 0x05:
    case 0x35:
    case 0x15:
        sr = op == 0x05 ? 0 : op == 0x35 ? 1 : 2;
        busy(f);
        for (i = 0; i < nrx; i++)
            rx[i] = f->sr[sr];
        break;
    case 0x01:
    case 0x31:
    case 0x11:
        if (!f->vwe && !(f->sr[0] & SR1_WEL))
            break;
        if (ntx < 2)
            break;
        sr = op == 0x01 ? 0 : op == 0x31 ? 1 : 2;
        write_sr(f, sr, tx[1]);
        if (op == 0x01 && ntx >= 3)
            write_sr(f, 1, tx[2]);
        if (f->vwe)
            f->vwe = 0;
        else
            start(f, T_W_PS); /* WEL clears as the cycle ends */
        break;
    case 0x6b:
    case 0xeb:
    case 0xe7:
        if (!qe)
            break;
        /* fall through */
    case 0x03:
    case 0x0b:
    case 0x3b:
    case 0xbb:
        for (i = 0; i < nrx; i++)
            rx[i] = f->st->mem[(a + (u32)i) % f->st->size];
        break;
    case 0x32:
        if (!qe)
            break;
        /* fall through */
    case 0x02:
        if (!(f->sr[0] & SR1_WEL) || ntx < 5)
            break;
        program(f, a, tx + 4, ntx - 4);
        start(f, T_PP_PS);
        break;
    case 0x20:
    case 0x52:
    case 0xd8:
        if (!(f->sr[0] & SR1_WEL) || ntx < 4)
            break;
        erase(f, a, op == 0x20 ? 4096u : op == 0x52 ? 32768u : 65536u);
        start(f, op == 0x20 ? T_SE_PS : op == 0x52 ? T_BE1_PS : T_BE2_PS);
        break;
    case 0xc7:
    case 0x60:
        if (!(f->sr[0] & SR1_WEL))
            break;
        erase(f, 0, f->st->size);
        start(f, T_CE_PS);
        break;
    case 0x44:
    case 0x42:
        sr = sec_reg(a);
        if (!(f->sr[0] & SR1_WEL) || ntx < 4 || sr < 0 || ((f->sr[1] >> (SR2_LB_SHIFT + sr)) & 1)) {
            sr_clear(f, 0, SR1_WEL);
            break;
        }
        if (op == 0x44) {
            memset(f->sec[sr], 0xff, 256);
            start(f, T_SE_PS);
        } else {
            for (i = 4; i < ntx; i++)
                f->sec[sr][(a + (u32)(i - 4)) & 0xffu] =
                    (u8)(f->sec[sr][(a + (u32)(i - 4)) & 0xffu] & tx[i]);
            start(f, T_PP_PS);
        }
        break;
    case 0x48:
        sr = sec_reg(a);
        for (i = 0; sr >= 0 && i < nrx; i++)
            rx[i] = (u8)((a & 0xffu) + (u32)i < 256u ? f->sec[sr][(a & 0xffu) + (u32)i] : 0xffu);
        break;
    case 0x75: /* suspend */
        if (busy(f)) {
            f->sus_left = f->busy_end - now_ps(f);
            sr_clear(f, 0, SR1_BUSY);
            sr_set(f, 1, SR2_SUS);
        }
        break;
    case 0x7a: /* resume */
        if (f->sr[1] & SR2_SUS) {
            sr_clear(f, 1, SR2_SUS);
            start(f, f->sus_left);
            f->sus_left = 0;
        }
        break;
    case 0xb9:
        f->pd = 1;
        break;
    case 0xab:
        /* the ID after three dummy bytes, repeated: sent as an address or
         * counted as dummy clocks, the bytes read are the ID */
        f->pd = 0;
        for (i = 0; i < nrx; i++)
            rx[i] = ESP32C3_NOR_DEVICE_ID;
        break;
    case 0x90:
    case 0x92:
    case 0x94:
        if (op == 0x94 && !qe)
            break;
        for (i = 0; i < nrx; i++)
            rx[i] = (u8)(((u32)i & 1u) == (a & 1u) ? 0x20u : ESP32C3_NOR_DEVICE_ID);
        break;
    case 0x9f:
        for (i = 0; i < nrx && i < 3; i++)
            rx[i] = (u8)(0x204016u >> (16 - 8 * i));
        break;
    case 0x4b:
        for (i = 0; i < nrx && i < 8; i++)
            rx[i] = unique_id[i];
        break;
    case 0x99:
        if (f->reset_en)
            reset_device(f);
        f->reset_en = 0;
        break;
    default:
        break;
    }
}

int esk_esp32c3_nor_busy(struct esk_c3_nor *f)
{
    return busy(f);
}

struct esk_c3_nor *esk_esp32c3_nor_create(struct esk_mach *m)
{
    struct esk_c3_nor *f;
    struct esk_store *st = esk_mach_store(m, "flash");
    if (!st || st->size == 0 || (st->size & (st->size - 1u)))
        return 0;
    f = (struct esk_c3_nor *)esk_mach_alloc(m, sizeof *f);
    if (!f)
        return 0;
    memset(f, 0, sizeof *f);
    f->m = m;
    f->st = st;
    /* as shipped: no protection, QE 0 (the "G" ordering code), driver
     * strength 25% (DRV1-0 = 11b), /HOLD (HOLD/RST 0) */
    f->sr_nv[2] = 0x60;
    memcpy(f->sr, f->sr_nv, sizeof f->sr);
    memset(f->sec, 0xff, sizeof f->sec);
    f->last_cycles = m->cycles;
    return f;
}

/* a power-on: the volatile state from the non-volatile bits (the array, the
 * status registers' non-volatile bits and the security registers stay) */
void esk_esp32c3_nor_power_on(struct esk_c3_nor *f)
{
    reset_device(f);
    f->pd = f->udpd = f->reset_en = 0;
    sr_clear(f, 0, SR1_BUSY | SR1_WEL);
    f->last_cycles = f->m->cycles;
}
