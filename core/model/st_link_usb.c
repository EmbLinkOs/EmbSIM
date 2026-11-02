/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* st_link_usb.c -- the ST-LINK/V2-1's own USB device, on the NUCLEO's CN1
 * (engine/usb.h, ADR-031: USB as packets), as a computer enumerates it:
 *
 *   - a full-speed composite device, 0483:374B, "STMicroelectronics" /
 *     "STM32 STLink" with the probe's serial number: interface 0 the
 *     debug interface (bulk 0x01 OUT for commands, 0x81 IN for their
 *     answers, 0x82 IN for the SWO trace: the endpoints OpenOCD's
 *     stlink_usb.c uses for a V2-1), interface 1 the mass-storage drive
 *     (SCSI transparent, bulk only), interfaces 2 and 3 the virtual COM
 *     port (CDC ACM, behind an interface association);
 *   - the debug interface carries the commands st_link.c answers: a
 *     16-byte command on 0x01, the data a memory write sends after it,
 *     the answer read on 0x81; the trace endpoint gives what the probe's
 *     SWO receiver holds;
 *   - the drive: a FAT16 volume labelled with the board's name
 *     (NODE_F446RE) holding MBED.HTM and DETAILS.TXT. A .bin file copied
 *     onto it is programmed into the target's flash from its start, as
 *     the firmware does: the core halted through DHCSR, the flash
 *     interface unlocked (KEYR), each sector the file covers erased (SER,
 *     STRT, BSY polled), the words programmed at PSIZE x32 (PG, BSY
 *     polled after each), the interface locked, the core let go and the
 *     part reset (AIRCR.SYSRESETREQ). The flash's own times set the pace
 *     (the F446's: 250 ms to 1 s a sector, 16 us a word). The probe then
 *     leaves the bus and comes back (the drive re-enumerated, the file
 *     gone); a copy it could not program leaves FAIL.TXT saying why.
 *
 * Where the inside differs: the drive's volume is made when read (sectors
 * written are kept: the FAT, the directory, the file); the SWD transfers
 * take no time, the flash's operations do.
 * Not modelled: the virtual COM port's data (its endpoints are there and
 * NAK: the board's console bridge stands for it, and its line coding is
 * kept), the volume's geometry and DETAILS.TXT's and MBED.HTM's exact
 * words (EmbSIM's; ST does not publish them), targets other than the
 * STM32F446 (their flash layout: such a copy fails), a .hex file (only
 * .bin is programmed), the drive's other commands (the firmware's
 * "MAINTENANCE" mode), USB suspend. */
#include <stdio.h>
#include <string.h>

#include "model/st_link_priv.h"

#define SECTOR 512u
#define VOL_SECTORS 8192u /* 4 MiB */
#define FAT_SECTORS 32u
#define FAT1 1u
#define FAT2 (FAT1 + FAT_SECTORS)
#define ROOT (FAT2 + FAT_SECTORS)
#define ROOT_SECTORS 32u /* 512 entries */
#define DATA (ROOT + ROOT_SECTORS)
#define MAX_IMAGE (512u * 1024u)
#define PS_US 1000000ull
#define PS_MS 1000000000ull

enum { TOK_JOB = 2, TOK_ATTACH = 3 };
enum { J_NONE, J_HALT, J_HALTED, J_UNLOCK, J_ERASE, J_ERASE_WAIT, J_PROG, J_PROG_WAIT, J_FINISH, J_DETACHED };
enum { B_CBW, B_IN, B_OUT, B_CSW };

#define FLASH_IF 0x40023C00u
#define FLASH_KEYR (FLASH_IF + 0x04u)
#define FLASH_SR (FLASH_IF + 0x0Cu)
#define FLASH_CR (FLASH_IF + 0x10u)

static const char mbed_htm[] =
    "<!-- mbed Microcontroller Website and Authentication Shortcut -->\r\n<html>\r\n<head>\r\n"
    "<meta http-equiv=\"refresh\" content=\"0; URL=https://os.mbed.com/platforms/ST-Nucleo-F446RE/\"/>\r\n"
    "<title>mbed Website Shortcut</title>\r\n</head>\r\n<body></body>\r\n</html>\r\n";
static const char details_txt[] = "Version: 0221\r\nBuild:   V2J37M26\r\n";

static const uint8_t dev_desc[18] = { 18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, 0x83, 0x04, 0x4B, 0x37,
                                      0x00, 0x01, 1, 2, 3, 1 };
static const uint8_t cfg_desc[] = {
    9, 2, 0, 0, 4, 1, 0, 0x80, 150, /* the total length is set below; 300 mA */
    /* 0: the debug interface */
    9, 4, 0, 0, 3, 0xFF, 0xFF, 0xFF, 4,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x01, 2, 64, 0, 0,
    7, 5, 0x82, 2, 64, 0, 0,
    /* 1: the drive */
    9, 4, 1, 0, 2, 8, 6, 0x50, 5,
    7, 5, 0x83, 2, 64, 0, 0,
    7, 5, 0x03, 2, 64, 0, 0,
    /* 2 and 3: the virtual COM port */
    8, 11, 2, 2, 2, 2, 1, 0,
    9, 4, 2, 0, 1, 2, 2, 1, 6,
    5, 0x24, 0x00, 0x10, 0x01,
    5, 0x24, 0x01, 0x00, 3,
    4, 0x24, 0x02, 0x02,
    5, 0x24, 0x06, 2, 3,
    7, 5, 0x84, 3, 2, 0, 255,
    9, 4, 3, 0, 2, 0x0A, 0, 0, 7,
    7, 5, 0x05, 2, 64, 0, 0,
    7, 5, 0x85, 2, 64, 0, 0,
};
static const char *const strings[] = { NULL, "STMicroelectronics", "STM32 STLink", NULL, "ST-Link Debug",
                                       "Mass Storage", "ST-Link VCP Ctrl", "ST-Link VCP Data" };

typedef struct st_sector {
    uint32_t lba;
    uint8_t d[SECTOR];
} st_sector;

struct sl_usb {
    sl_state *s;
    esk_usb_dev dev;
    int attached; /* on the bus (its D+ pull-up) */
    int linked;
    /* the bus */
    int addr, pending_addr, configured;
    uint8_t ctl[512];
    uint32_t ctl_len, ctl_off;
    int ctl_in, ctl_pid, ctl_status_in, ctl_out_req; /* the control transfer in progress */
    uint8_t setup[8];
    uint8_t tog_in[16], tog_out[16];
    uint8_t line_coding[7];
    /* the debug interface */
    uint8_t cmd[16];
    uint8_t wdata[6144];
    uint32_t wneed, wgot;
    int have_cmd;
    uint8_t ans[6144];
    uint32_t ans_len, ans_off;
    /* the drive's bulk-only transport */
    int bot;
    uint8_t cbw[31], csw[13];
    uint8_t *xfer;
    uint32_t xfer_len, xfer_off, xfer_cap;
    int xfer_in;
    uint32_t w_lba, w_count; /* a WRITE(10) in progress */
    uint8_t sense[3];
    /* the volume: the sectors written */
    st_sector *sec;
    size_t nsec, capsec;
    char fail[160];
    /* the programming job */
    int job;
    uint8_t *image;
    uint32_t image_len, at, sector, nsectors, sector_end;
    uint64_t programmed, copies;
    char state[48];
};

static esk_alloc *A(struct sl_usb *u) { return u->s->c->e->a; }

static void put16(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
}

static void put32b(uint8_t *b, uint32_t v)
{
    put16(b, v);
    put16(b + 2, v >> 16);
}

static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
static uint32_t get32(const uint8_t *b) { return get16(b) | get16(b + 2) << 16; }
static uint32_t be32(const uint8_t *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }

/* ---- the volume ---------------------------------------------------------------------- */

static st_sector *stored(struct sl_usb *u, uint32_t lba)
{
    size_t i;
    for (i = 0; i < u->nsec; i++)
        if (u->sec[i].lba == lba)
            return &u->sec[i];
    return NULL;
}

static void store(struct sl_usb *u, uint32_t lba, const uint8_t *d)
{
    st_sector *x = stored(u, lba);
    if (!x) {
        if (u->nsec == u->capsec) {
            size_t n = u->capsec ? 2 * u->capsec : 64;
            st_sector *v = (st_sector *)esk_realloc(A(u), u->sec, u->capsec * sizeof *v, n * sizeof *v);
            if (!v)
                return;
            u->sec = v;
            u->capsec = n;
        }
        x = &u->sec[u->nsec++];
        x->lba = lba;
    }
    memcpy(x->d, d, SECTOR);
}

static void forget(struct sl_usb *u)
{
    if (u->sec)
        esk_free(A(u), u->sec, u->capsec * sizeof *u->sec);
    u->sec = NULL;
    u->nsec = u->capsec = 0;
}

static void dirent(uint8_t *e, const char *name11, uint8_t attr, uint32_t cluster, uint32_t size)
{
    memcpy(e, name11, 11);
    e[11] = attr;
    put16(e + 22, 0x6000u); /* 12:00 */
    put16(e + 24, 0x5D21u); /* 2026-09-01 */
    put16(e + 18, 0x5D21u);
    put16(e + 16, 0x5D21u);
    put16(e + 26, cluster);
    put32b(e + 28, size);
}

static void label11(const struct sl_usb *u, char *out)
{
    const char *l = esk_comp_param_str(u->s->c, "drive", "NODE_F446RE");
    size_t i;
    for (i = 0; i < 11; i++)
        out[i] = *l ? *l++ : ' ';
}

/* a sector as the volume holds it: written, or as made */
static void sector_read(struct sl_usb *u, uint32_t lba, uint8_t *d)
{
    const st_sector *x = stored(u, lba);
    char label[11];
    memset(d, 0, SECTOR);
    if (x) {
        memcpy(d, x->d, SECTOR);
        return;
    }
    label11(u, label);
    if (lba == 0) { /* the boot sector */
        static const uint8_t jmp[3] = { 0xEB, 0x3C, 0x90 };
        memcpy(d, jmp, 3);
        memcpy(d + 3, "MSDOS5.0", 8);
        put16(d + 11, SECTOR);
        d[13] = 1;
        put16(d + 14, FAT1);
        d[16] = 2;
        put16(d + 17, ROOT_SECTORS * SECTOR / 32u);
        put16(d + 19, VOL_SECTORS);
        d[21] = 0xF8;
        put16(d + 22, FAT_SECTORS);
        put16(d + 24, 63);
        put16(d + 26, 255);
        d[36] = 0x80;
        d[38] = 0x29;
        put32b(d + 39, 0x4E554345u);
        memcpy(d + 43, label, 11);
        memcpy(d + 54, "FAT16   ", 8);
        d[510] = 0x55;
        d[511] = 0xAA;
    } else if (lba == FAT1 || lba == FAT2) { /* the media byte, the end marks of the two files */
        put16(d, 0xFFF8u);
        put16(d + 2, 0xFFFFu);
        put16(d + 4, 0xFFFFu);
        put16(d + 6, 0xFFFFu);
        if (u->fail[0])
            put16(d + 8, 0xFFFFu);
    } else if (lba == ROOT) {
        dirent(d, label, 0x08, 0, 0);
        dirent(d + 32, "MBED    HTM", 0x01, 2, (uint32_t)strlen(mbed_htm));
        dirent(d + 64, "DETAILS TXT", 0x01, 3, (uint32_t)strlen(details_txt));
        if (u->fail[0])
            dirent(d + 96, "FAIL    TXT", 0x01, 4, (uint32_t)strlen(u->fail));
    } else if (lba == DATA) {
        memcpy(d, mbed_htm, strlen(mbed_htm));
    } else if (lba == DATA + 1) {
        memcpy(d, details_txt, strlen(details_txt));
    } else if (lba == DATA + 2 && u->fail[0]) {
        memcpy(d, u->fail, strlen(u->fail));
    }
}

static uint32_t fat_next(struct sl_usb *u, uint32_t cl)
{
    uint8_t d[SECTOR];
    sector_read(u, FAT1 + cl * 2u / SECTOR, d);
    return get16(d + cl * 2u % SECTOR);
}

static void job_start(struct sl_usb *u);

/* a .bin file whose every sector has been written: its bytes, to program */
static void look_for_image(struct sl_usb *u)
{
    uint8_t d[SECTOR];
    uint32_t k, e;
    if (u->job != J_NONE)
        return;
    for (k = 0; k < ROOT_SECTORS; k++) {
        if (!stored(u, ROOT + k))
            continue;
        sector_read(u, ROOT + k, d);
        for (e = 0; e < SECTOR; e += 32) {
            const uint8_t *x = d + e;
            uint32_t size = get32(x + 28), cl = get16(x + 26), got = 0;
            if (x[0] == 0 || x[0] == 0xE5 || (x[11] & 0x18) || memcmp(x + 8, "BIN", 3) || !size || size > MAX_IMAGE)
                continue;
            /* every cluster of its chain written */
            while (cl >= 2 && cl < 0xFFF0u && got < size && stored(u, DATA + cl - 2u)) {
                got += SECTOR;
                cl = fat_next(u, cl);
            }
            if (got < size)
                continue;
            u->image = (uint8_t *)esk_malloc(A(u), (size + 3u) & ~3u);
            if (!u->image)
                return;
            memset(u->image, 0xFF, (size + 3u) & ~3u);
            for (cl = get16(x + 26), got = 0; got < size; got += SECTOR, cl = fat_next(u, cl)) {
                const st_sector *s = stored(u, DATA + cl - 2u);
                memcpy(u->image + got, s->d, size - got < SECTOR ? size - got : SECTOR);
            }
            u->image_len = (size + 3u) & ~3u;
            u->copies++;
            job_start(u);
            return;
        }
    }
}

/* ---- programming the target ---------------------------------------------------------- */

static const uint32_t f446_kib[8] = { 16, 16, 16, 16, 64, 128, 128, 128 };

static int rd(struct sl_usb *u, uint32_t a, uint32_t *v) { return sl_mem(u->s, a, 4, 0, v); }
static int wr(struct sl_usb *u, uint32_t a, uint32_t v) { return sl_mem(u->s, a, 4, 1, &v); }

static void wake(struct sl_usb *u, uint64_t ps) { (void)esk_comp_wake_at(u->s->c, esk_comp_now(u->s->c) + ps, TOK_JOB); }

static void leave_bus(struct sl_usb *u, uint64_t back_ps)
{
    u->attached = 0;
    if (u->linked)
        esk_usb_changed(u->s->c->e, &u->dev, esk_comp_now(u->s->c));
    (void)esk_comp_wake_at(u->s->c, esk_comp_now(u->s->c) + back_ps, TOK_ATTACH);
}

static void job_end(struct sl_usb *u, const char *fail)
{
    if (u->image)
        esk_free(A(u), u->image, u->image_len);
    u->image = NULL;
    forget(u); /* the drive comes back as it was, the file gone */
    snprintf(u->fail, sizeof u->fail, "%s", fail ? fail : "");
    snprintf(u->state, sizeof u->state, "%s", fail ? "failed" : "programmed");
    u->job = J_DETACHED;
    sl_say(u->s, fail ? "drive: %s" : "drive: the file programmed%s", fail ? fail : "");
    leave_bus(u, 500u * PS_MS);
}

static void job_start(struct sl_usb *u)
{
    sl_state *s = u->s;
    uint32_t id = 0, total = 0, k;
    if (!sl_target(s) || !sl_powered(s) || rd(u, 0xE0042000u, &id) != 0) {
        job_end(u, "The interface firmware FAILED to reset/halt the target MCU\r\n");
        return;
    }
    if ((id & 0xFFFu) != 0x421u) {
        job_end(u, "The target MCU is not supported by this drive\r\n");
        return;
    }
    for (k = 0; k < 8 && total < u->image_len; k++)
        total += f446_kib[k] * 1024u;
    u->nsectors = k;
    u->sector = 0;
    u->at = 0;
    u->job = J_HALT;
    {
        char n[16];
        snprintf(n, sizeof n, "%lu", (unsigned long)u->image_len);
        snprintf(u->state, sizeof u->state, "programming");
        sl_say(s, "drive: %s bytes to program", n);
    }
    wake(u, 0);
}

static void job_step(struct sl_usb *u)
{
    uint32_t v = 0, sr = 0;
    switch (u->job) {
    case J_HALT: /* C_DEBUGEN and C_HALT, then S_HALT a moment later */
        if (wr(u, 0xE000EDF0u, 0xA05F0003u))
            break;
        u->job = J_HALTED;
        wake(u, PS_US);
        return;
    case J_HALTED:
        if (rd(u, 0xE000EDF0u, &v) || !(v & (1u << 17)))
            break;
        u->job = J_UNLOCK;
        wake(u, 0);
        return;
    case J_UNLOCK:
        if (rd(u, FLASH_CR, &v))
            break;
        if (v & (1u << 31)) {
            (void)wr(u, FLASH_KEYR, 0x45670123u);
            (void)wr(u, FLASH_KEYR, 0xCDEF89ABu);
        }
        (void)wr(u, FLASH_SR, 0xF3u); /* the old errors */
        u->job = J_ERASE;
        wake(u, 0);
        return;
    case J_ERASE:
        (void)wr(u, FLASH_CR, 2u << 8 | u->sector << 3 | 2u); /* PSIZE x32, SNB, SER */
        (void)wr(u, FLASH_CR, 2u << 8 | u->sector << 3 | 2u | 1u << 16); /* STRT */
        u->job = J_ERASE_WAIT;
        wake(u, PS_MS);
        return;
    case J_ERASE_WAIT:
        if (rd(u, FLASH_SR, &sr))
            break;
        if (sr & (1u << 16)) { /* BSY */
            wake(u, PS_MS);
            return;
        }
        if (sr & 0xF0u) {
            job_end(u, "The flash erase failed\r\n");
            return;
        }
        if (++u->sector < u->nsectors) {
            u->job = J_ERASE;
        } else {
            (void)wr(u, FLASH_CR, 2u << 8 | 1u); /* PSIZE x32, PG */
            u->job = J_PROG;
        }
        wake(u, 0);
        return;
    case J_PROG:
        v = (uint32_t)u->image[u->at] | (uint32_t)u->image[u->at + 1] << 8 | (uint32_t)u->image[u->at + 2] << 16 |
            (uint32_t)u->image[u->at + 3] << 24;
        if (wr(u, 0x08000000u + u->at, v))
            break;
        u->at += 4;
        u->programmed += 4;
        u->job = J_PROG_WAIT;
        wake(u, 16u * PS_US);
        return;
    case J_PROG_WAIT:
        if (rd(u, FLASH_SR, &sr))
            break;
        if (sr & (1u << 16)) {
            wake(u, PS_US);
            return;
        }
        if (sr & 0xF0u) {
            job_end(u, "The flash programming failed\r\n");
            return;
        }
        u->job = u->at < u->image_len ? J_PROG : J_FINISH;
        wake(u, 0);
        return;
    case J_FINISH:
        (void)wr(u, FLASH_CR, 1u << 31);          /* LOCK */
        (void)wr(u, 0xE000EDF0u, 0xA05F0000u);    /* the core let go */
        (void)wr(u, 0xE000ED0Cu, 0x05FA0004u);    /* SYSRESETREQ */
        job_end(u, NULL);
        return;
    default:
        return;
    }
    job_end(u, "The interface firmware FAILED to reset/halt the target MCU\r\n");
}

/* ---- SCSI over the bulk-only transport ------------------------------------------------ */

static uint8_t *xbuf(struct sl_usb *u, uint32_t n)
{
    if (n > u->xfer_cap) {
        uint8_t *b = (uint8_t *)esk_realloc(A(u), u->xfer, u->xfer_cap, n);
        if (!b)
            return NULL;
        u->xfer = b;
        u->xfer_cap = n;
    }
    return u->xfer;
}

static void csw(struct sl_usb *u, uint32_t residue, uint8_t status)
{
    memcpy(u->csw, "USBS", 4);
    memcpy(u->csw + 4, u->cbw + 4, 4);
    put32b(u->csw + 8, residue);
    u->csw[12] = status;
    u->bot = B_CSW;
}

static void sense(struct sl_usb *u, uint8_t key, uint8_t asc, uint8_t ascq)
{
    u->sense[0] = key;
    u->sense[1] = asc;
    u->sense[2] = ascq;
}

/* the data the host reads, then the status */
static void reply(struct sl_usb *u, const uint8_t *d, uint32_t n)
{
    uint32_t want = get32(u->cbw + 8);
    if (n > want)
        n = want;
    if (!xbuf(u, n ? n : 1)) {
        csw(u, want, 1);
        return;
    }
    memcpy(u->xfer, d, n);
    u->xfer_len = n;
    u->xfer_off = 0;
    u->bot = n ? B_IN : B_CSW;
    if (!n)
        csw(u, want, 0);
}

static void scsi(struct sl_usb *u)
{
    const uint8_t *cb = u->cbw + 15;
    uint8_t r[36];
    uint32_t want = get32(u->cbw + 8), lba, n, k;
    memset(r, 0, sizeof r);
    switch (cb[0]) {
    case 0x00: /* TEST UNIT READY */
    case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
    case 0x1B: /* START STOP UNIT */
    case 0x2F: /* VERIFY(10) */
        sense(u, 0, 0, 0);
        csw(u, want, 0);
        return;
    case 0x12: /* INQUIRY */
        r[1] = 0x80; /* removable */
        r[2] = 0x02;
        r[3] = 0x02;
        r[4] = 31;
        memcpy(r + 8, "MBED    ", 8);
        memcpy(r + 16, "microcontroller ", 16);
        memcpy(r + 32, "1.0 ", 4);
        reply(u, r, 36);
        return;
    case 0x03: /* REQUEST SENSE */
        r[0] = 0x70;
        r[2] = u->sense[0];
        r[7] = 10;
        r[12] = u->sense[1];
        r[13] = u->sense[2];
        reply(u, r, 18);
        return;
    case 0x25: /* READ CAPACITY(10) */
        r[0] = (uint8_t)((VOL_SECTORS - 1u) >> 24);
        r[1] = (uint8_t)((VOL_SECTORS - 1u) >> 16);
        r[2] = (uint8_t)((VOL_SECTORS - 1u) >> 8);
        r[3] = (uint8_t)(VOL_SECTORS - 1u);
        r[6] = SECTOR >> 8;
        reply(u, r, 8);
        return;
    case 0x23: /* READ FORMAT CAPACITIES */
        r[3] = 8;
        r[4] = (uint8_t)(VOL_SECTORS >> 24);
        r[5] = (uint8_t)(VOL_SECTORS >> 16);
        r[6] = (uint8_t)(VOL_SECTORS >> 8);
        r[7] = (uint8_t)VOL_SECTORS;
        r[8] = 2;
        r[10] = SECTOR >> 8;
        reply(u, r, 12);
        return;
    case 0x1A: /* MODE SENSE(6): not write-protected */
        r[0] = 3;
        reply(u, r, 4);
        return;
    case 0x28: /* READ(10) */
        lba = be32(cb + 2);
        n = (uint32_t)cb[7] << 8 | cb[8];
        if (lba + n > VOL_SECTORS || !xbuf(u, n ? n * SECTOR : 1u)) {
            sense(u, 5, 0x21, 0);
            csw(u, want, 1);
            return;
        }
        for (k = 0; k < n; k++)
            sector_read(u, lba + k, u->xfer + k * SECTOR);
        u->xfer_len = n * SECTOR < want ? n * SECTOR : want;
        u->xfer_off = 0;
        u->bot = u->xfer_len ? B_IN : B_CSW;
        if (!u->xfer_len)
            csw(u, want, 0);
        return;
    case 0x2A: /* WRITE(10) */
        lba = be32(cb + 2);
        n = (uint32_t)cb[7] << 8 | cb[8];
        if (lba + n > VOL_SECTORS || u->job != J_NONE || !xbuf(u, n ? n * SECTOR : 1u)) {
            sense(u, u->job != J_NONE ? 2 : 5, u->job != J_NONE ? 0x04 : 0x21, 0);
            csw(u, want, 1);
            return;
        }
        u->w_lba = lba;
        u->w_count = n;
        u->xfer_len = n * SECTOR;
        u->xfer_off = 0;
        u->bot = u->xfer_len ? B_OUT : B_CSW;
        if (!u->xfer_len)
            csw(u, want, 0);
        return;
    }
    sense(u, 5, 0x20, 0); /* ILLEGAL REQUEST, INVALID COMMAND */
    if (want && (u->cbw[12] & 0x80)) {
        csw(u, want, 1);
        return;
    }
    csw(u, want, 1);
}

static void wrote(struct sl_usb *u)
{
    uint32_t k;
    for (k = 0; k < u->w_count; k++)
        store(u, u->w_lba + k, u->xfer + k * SECTOR);
    csw(u, 0, 0);
    look_for_image(u);
}

/* ---- the debug interface over USB ----------------------------------------------------- */

static uint32_t write_len(const uint8_t *c)
{
    if (c[0] != 0xF2)
        return 0;
    switch (c[1]) {
    case 0x08: case 0x0D: case 0x48: case 0x57: /* WRITEMEM_32BIT, _8BIT, _16BIT, _32BIT_NO_ADDR_INC */
        return get16(c + 6);
    }
    return 0;
}

static void run_command(struct sl_usb *u)
{
    u->ans_len = (uint32_t)esk_stlink_command(u->s->c, u->cmd, u->wdata, u->wgot, u->ans, sizeof u->ans);
    u->ans_off = 0;
    u->have_cmd = 0;
}

/* ---- the device's endpoints ---------------------------------------------------------- */

static int u_pullup(void *ctx)
{
    struct sl_usb *u = (struct sl_usb *)ctx;
    return u->attached ? ESK_USB_FULL_SPEED : ESK_USB_DETACHED;
}

static void bus_reset(struct sl_usb *u)
{
    u->addr = u->pending_addr = 0;
    u->configured = 0;
    memset(u->tog_in, 0, sizeof u->tog_in);
    memset(u->tog_out, 0, sizeof u->tog_out);
    u->ctl_len = u->ctl_off = 0;
    u->bot = B_CBW;
    u->have_cmd = 0;
    u->ans_len = u->ans_off = 0;
}

static void u_bus(void *ctx, int ev, uint32_t frame, es_time_t at)
{
    struct sl_usb *u = (struct sl_usb *)ctx;
    (void)frame;
    (void)at;
    if (ev == ESK_USB_RESET)
        bus_reset(u);
}

static uint32_t string_desc(uint8_t *d, uint32_t idx, const char *serial)
{
    const char *s = idx < sizeof strings / sizeof strings[0] ? strings[idx] : NULL;
    uint32_t n = 0;
    if (idx == 0) {
        d[0] = 4;
        d[1] = 3;
        put16(d + 2, 0x0409);
        return 4;
    }
    if (idx == 3)
        s = serial;
    if (!s)
        return 0;
    while (s[n] && n < 126)
        n++;
    d[0] = (uint8_t)(2 + 2 * n);
    d[1] = 3;
    for (idx = 0; idx < n; idx++)
        put16(d + 2 + 2 * idx, (uint8_t)s[idx]);
    return 2 + 2 * n;
}

static int u_setup(void *ctx, int addr, int ep, const uint8_t data[8], es_time_t at)
{
    struct sl_usb *u = (struct sl_usb *)ctx;
    uint32_t want = get16(data + 6), n = 0;
    uint8_t type = data[0], req = data[1];
    (void)at;
    if (!u->attached || addr != u->addr || ep != 0)
        return ESK_USB_NONE;
    memcpy(u->setup, data, 8);
    u->ctl_in = (type & 0x80) != 0;
    u->ctl_pid = 1;
    u->ctl_off = 0;
    u->ctl_out_req = 0;
    u->ctl_status_in = 0;
    if ((type & 0x60) == 0) { /* standard */
        switch (req) {
        case 6: { /* GET_DESCRIPTOR */
            uint32_t kind = data[3], idx = data[2];
            if (kind == 1) {
                memcpy(u->ctl, dev_desc, sizeof dev_desc);
                n = sizeof dev_desc;
            } else if (kind == 2) {
                memcpy(u->ctl, cfg_desc, sizeof cfg_desc);
                put16(u->ctl + 2, sizeof cfg_desc);
                n = sizeof cfg_desc;
            } else if (kind == 3) {
                n = string_desc(u->ctl, idx, esk_stlink_serial(u->s->c));
                if (!n)
                    return ESK_USB_STALL;
            } else {
                return ESK_USB_STALL;
            }
            break;
        }
        case 5: /* SET_ADDRESS: once its status stage is done */
            u->pending_addr = data[2] & 0x7f;
            break;
        case 9: /* SET_CONFIGURATION */
            u->configured = data[2] != 0;
            memset(u->tog_in + 1, 0, sizeof u->tog_in - 1);
            memset(u->tog_out + 1, 0, sizeof u->tog_out - 1);
            u->bot = B_CBW;
            break;
        case 8: /* GET_CONFIGURATION */
            u->ctl[0] = (uint8_t)u->configured;
            n = 1;
            break;
        case 0: /* GET_STATUS */
            n = 2;
            u->ctl[0] = u->ctl[1] = 0;
            break;
        case 1: /* CLEAR_FEATURE (an endpoint's halt): its toggle */
            if ((type & 0x1f) == 2) {
                uint32_t e = data[4] & 0x0f;
                if (data[4] & 0x80)
                    u->tog_in[e] = 0;
                else
                    u->tog_out[e] = 0;
            }
            break;
        case 3: /* SET_FEATURE */
        case 11: /* SET_INTERFACE */
            break;
        case 10: /* GET_INTERFACE */
            u->ctl[0] = 0;
            n = 1;
            break;
        default:
            return ESK_USB_STALL;
        }
    } else if ((type & 0x60) == 0x20) { /* class */
        switch (req) {
        case 0xFE: /* GET_MAX_LUN */
            u->ctl[0] = 0;
            n = 1;
            break;
        case 0xFF: /* Bulk-Only Mass Storage Reset */
            u->bot = B_CBW;
            break;
        case 0x20: /* SET_LINE_CODING: its 7 bytes in the data stage */
            u->ctl_out_req = 0x20;
            break;
        case 0x21: /* GET_LINE_CODING */
            memcpy(u->ctl, u->line_coding, 7);
            n = 7;
            break;
        case 0x22: /* SET_CONTROL_LINE_STATE */
            break;
        default:
            return ESK_USB_STALL;
        }
    } else {
        return ESK_USB_STALL;
    }
    u->ctl_len = n < want ? n : want;
    return ESK_USB_ACK;
}

static int u_out(void *ctx, int addr, int ep, int data1, const uint8_t *data, uint32_t len, es_time_t at)
{
    struct sl_usb *u = (struct sl_usb *)ctx;
    (void)at;
    if (!u->attached || addr != u->addr || ep < 0 || ep > 15)
        return ESK_USB_NONE;
    if (ep == 0) {
        if (u->ctl_in) { /* the status stage of a read */
            u->ctl_len = 0;
            return ESK_USB_ACK;
        }
        if (u->ctl_out_req == 0x20 && len) {
            memcpy(u->line_coding, data, len < 7 ? len : 7);
            u->ctl_status_in = 1;
        }
        return ESK_USB_ACK;
    }
    if (!u->configured)
        return ESK_USB_STALL;
    if (data1 != u->tog_out[ep])
        return ESK_USB_ACK; /* a repeat of a packet it has: taken again, dropped */
    if (ep == 1) { /* a command, or a write's data */
        if (u->have_cmd) {
            uint32_t n = len < sizeof u->wdata - u->wgot ? len : (uint32_t)sizeof u->wdata - u->wgot;
            memcpy(u->wdata + u->wgot, data, n);
            u->wgot += n;
            if (u->wgot >= u->wneed)
                run_command(u);
        } else if (len >= 16) {
            memcpy(u->cmd, data, 16);
            u->wneed = write_len(u->cmd);
            u->wgot = 0;
            u->have_cmd = 1;
            if (!u->wneed)
                run_command(u);
        }
        u->tog_out[ep] ^= 1;
        return ESK_USB_ACK;
    }
    if (ep == 3) { /* the drive */
        if (u->bot == B_CBW) {
            if (len != 31 || memcmp(data, "USBC", 4))
                return ESK_USB_STALL;
            memcpy(u->cbw, data, 31);
            u->tog_out[ep] ^= 1;
            scsi(u);
            return ESK_USB_ACK;
        }
        if (u->bot == B_OUT) {
            uint32_t n = len < u->xfer_len - u->xfer_off ? len : u->xfer_len - u->xfer_off;
            memcpy(u->xfer + u->xfer_off, data, n);
            u->xfer_off += n;
            u->tog_out[ep] ^= 1;
            if (u->xfer_off >= u->xfer_len)
                wrote(u);
            return ESK_USB_ACK;
        }
        return ESK_USB_NAK;
    }
    if (ep == 5) /* the virtual COM port's data: not carried (the console bridge stands for it) */
        return ESK_USB_NAK;
    return ESK_USB_STALL;
}

static int u_in(void *ctx, int addr, int ep, uint8_t *data, uint32_t cap, uint32_t *len, int *data1, es_time_t at)
{
    struct sl_usb *u = (struct sl_usb *)ctx;
    uint32_t n;
    (void)at;
    *len = 0;
    if (!u->attached || addr != u->addr || ep < 0 || ep > 15)
        return ESK_USB_NONE;
    if (ep == 0) {
        if (!u->ctl_in) { /* the status stage of a write */
            if (u->pending_addr && u->setup[1] == 5 && !(u->setup[0] & 0x60)) {
                u->addr = u->pending_addr;
                u->pending_addr = 0;
            }
            *data1 = 1;
            return ESK_USB_ACK;
        }
        n = u->ctl_len - u->ctl_off;
        if (n > cap)
            n = cap;
        if (n > 64)
            n = 64;
        memcpy(data, u->ctl + u->ctl_off, n);
        u->ctl_off += n;
        *len = n;
        *data1 = u->ctl_pid;
        u->ctl_pid ^= 1;
        return ESK_USB_ACK;
    }
    if (!u->configured)
        return ESK_USB_STALL;
    if (ep == 1) { /* the command's answer */
        if (u->ans_off >= u->ans_len)
            return ESK_USB_NAK;
        n = u->ans_len - u->ans_off;
        if (n > cap)
            n = cap;
        if (n > 64)
            n = 64;
        memcpy(data, u->ans + u->ans_off, n);
        u->ans_off += n;
    } else if (ep == 2) { /* the SWO trace */
        uint8_t t[64];
        n = (uint32_t)esk_stlink_trace(u->s->c, t, cap < 64 ? cap : 64);
        if (!n)
            return ESK_USB_NAK;
        memcpy(data, t, n);
    } else if (ep == 3) { /* the drive */
        if (u->bot == B_IN) {
            n = u->xfer_len - u->xfer_off;
            if (n > cap)
                n = cap;
            if (n > 64)
                n = 64;
            memcpy(data, u->xfer + u->xfer_off, n);
            u->xfer_off += n;
            if (u->xfer_off >= u->xfer_len)
                csw(u, get32(u->cbw + 8) - u->xfer_len, 0);
        } else if (u->bot == B_CSW) {
            n = 13;
            memcpy(data, u->csw, 13);
            u->bot = B_CBW;
        } else {
            return ESK_USB_NAK;
        }
    } else if (ep == 4 || ep == 5) { /* the virtual COM port: nothing to say */
        return ESK_USB_NAK;
    } else {
        return ESK_USB_STALL;
    }
    *len = n;
    *data1 = u->tog_in[ep];
    u->tog_in[ep] ^= 1;
    return ESK_USB_ACK;
}

/* ---- the part's side ----------------------------------------------------------------- */

es_status sl_usb_create(sl_state *s)
{
    struct sl_usb *u = (struct sl_usb *)esk_calloc(s->c->e->a, 1, sizeof *u);
    if (!u)
        return ES_ENOMEM;
    u->s = s;
    s->usb = u;
    return ES_OK;
}

void sl_usb_destroy(sl_state *s)
{
    struct sl_usb *u = s->usb;
    if (!u)
        return;
    if (u->linked)
        esk_usb_detach_dev(s->c->e, &u->dev);
    forget(u);
    if (u->xfer)
        esk_free(A(u), u->xfer, u->xfer_cap);
    if (u->image)
        esk_free(A(u), u->image, u->image_len);
    esk_free(s->c->e->a, u, sizeof *u);
    s->usb = NULL;
}

void sl_usb_reset(sl_state *s)
{
    struct sl_usb *u = s->usb;
    esk_pin *dp = esk_comp_pin(s->c, "USB_DP");
    static const uint8_t coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 }; /* 115200 8N1 */
    if (!u)
        return;
    forget(u);
    if (u->image)
        esk_free(A(u), u->image, u->image_len);
    u->image = NULL;
    u->job = J_NONE;
    u->fail[0] = 0;
    u->programmed = u->copies = 0;
    snprintf(u->state, sizeof u->state, "idle");
    memcpy(u->line_coding, coding, 7);
    bus_reset(u);
    u->attached = 1;
    if (!u->linked && dp && dp->net) {
        u->dev.ctx = u;
        u->dev.comp = s->c;
        u->dev.dp = dp;
        u->dev.pullup = u_pullup;
        u->dev.bus = u_bus;
        u->dev.setup = u_setup;
        u->dev.out = u_out;
        u->dev.in = u_in;
        if (esk_usb_attach_dev(s->c->e, &u->dev) == ES_OK)
            u->linked = 1;
    }
}

void sl_usb_on_time(sl_state *s, uint32_t token)
{
    struct sl_usb *u = s->usb;
    if (!u)
        return;
    if (token == TOK_JOB) {
        job_step(u);
    } else if (token == TOK_ATTACH && u->job == J_DETACHED) {
        u->job = J_NONE;
        bus_reset(u);
        u->attached = 1;
        if (u->linked)
            esk_usb_changed(s->c->e, &u->dev, esk_comp_now(s->c));
    }
}

void sl_usb_observe(sl_state *s, esk_json *o)
{
    struct sl_usb *u = s->usb;
    esk_alloc *a = s->c->e->a;
    if (!u)
        return;
    esk_json_set_bool(a, o, "usb_attached", u->attached && u->linked);
    esk_json_set_integer(a, o, "usb_address", u->addr);
    esk_json_set_bool(a, o, "usb_configured", u->configured);
    esk_json_set_string(a, o, "drive", u->state);
    esk_json_set_integer(a, o, "drive_copies", (int64_t)u->copies);
    esk_json_set_integer(a, o, "drive_programmed", (int64_t)u->programmed);
    esk_json_set_string(a, o, "drive_fail", u->fail);
}
