/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usbboot.c -- the RP2040's USB boot: the drive a computer sees when the
 * Pico is plugged in with BOOTSEL held, and the UF2 files copied onto it
 * (the RP2040 datasheet, 2.8.4 to 2.8.5; ADR-031, USB as packets).
 *
 * The bootrom enters its USB boot when BOOTSEL holds the flash's chip
 * select low at reset, when flash has no valid boot2, or when a program
 * calls reset_to_usb_boot. EmbSIM's bootrom stand-in (boot.c) does the
 * same, and this is what the bootrom's USB device then does, done by the
 * host as its other functions are (rom.c):
 *
 *   - it attaches at full speed (D+ pulled up) as the bootrom's device:
 *     VID 0x2E8A, PID 0x0003, "Raspberry Pi" / "RP2 Boot", with a mass
 *     storage interface (SCSI transparent, bulk only: 0x81 IN, 0x02 OUT)
 *     and the PICOBOOT vendor interface (0x03 OUT, 0x84 IN);
 *   - the drive is a 128 MiB FAT16 volume labelled RPI-RP2 holding
 *     INFO_UF2.TXT and INDEX.HTM (2.8.4.1); it answers INQUIRY, TEST UNIT
 *     READY, REQUEST SENSE, READ CAPACITY, READ FORMAT CAPACITIES, MODE
 *     SENSE, PREVENT/ALLOW MEDIUM REMOVAL, START STOP UNIT, VERIFY, READ(10)
 *     and WRITE(10), and anything else with CHECK CONDITION, ILLEGAL
 *     REQUEST;
 *   - what is written is not kept as files: each 512-byte sector written
 *     that is a UF2 block (2.8.4.2: both start magics and the end magic,
 *     the family ID flag with RP2040's family 0xe48bff56, not "not main
 *     flash", a 256-byte payload at a 256-byte aligned address in flash) is
 *     programmed into flash, its 4 KiB sector erased first, at the
 *     W25Q16JV's typical times (45 ms an erase, 0.4 ms a page, the bulk
 *     endpoint NAKing meanwhile); a block of a file with another
 *     numBlocks starts the count again;
 *   - once every block of the file has been written the part reboots
 *     (the watchdog's reset) and boots what it now holds.
 *
 * Inside: the bootrom's code does not run (as boot.c says); the drive's
 * sectors are made when read. Omissions: the PICOBOOT interface's
 * commands (picotool) are not answered (its endpoints NAK); UF2 files for
 * RAM (2.8.4.2's RAM target) are ignored; the serial number string and
 * INDEX.HTM's version argument (the bootrom's build) are not the
 * bootrom's; the GPIO activity LED (reset_to_usb_boot's argument). */
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define FLASH_BASE 0x10000000u
#define SECTOR 512u
#define VOL_SECTORS (128u * 1024u * 1024u / SECTOR)
#define SPC 8u          /* sectors per cluster: 4 KiB */
#define FAT_SECTORS 128u /* 32768 clusters, two bytes each */
#define ROOT_ENTRIES 512u
#define FAT1 1u
#define FAT2 (FAT1 + FAT_SECTORS)
#define ROOT (FAT2 + FAT_SECTORS)
#define DATA (ROOT + ROOT_ENTRIES * 32u / SECTOR)
#define UF2_FAMILY_RP2040 0xe48bff56u

enum { B_CBW, B_IN, B_OUT, B_CSW };

static const char info_uf2[] = "UF2 Bootloader v3.0\r\nModel: Raspberry Pi RP2\r\nBoard-ID: RPI-RP2\r\n";
static const char index_htm[] =
    "<html><head><meta http-equiv=\"refresh\" content=\"0;URL='https://raspberrypi.com/device/RP2'\"/></head>"
    "<body>Redirecting to <a href='https://raspberrypi.com/device/RP2'>raspberrypi.com</a></body></html>\n";

static const u8 dev_desc[18] = { 18, 1, 0x10, 0x01, 0, 0, 0, 64, 0x8a, 0x2e, 0x03, 0x00, 0x00, 0x01, 1, 2, 3, 1 };
static const u8 cfg_desc[55] = {
    9, 2, 55, 0, 2, 1, 0, 0x80, 250,
    /* the drive: mass storage, SCSI transparent, bulk only */
    9, 4, 0, 0, 2, 8, 6, 0x50, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0,
    /* PICOBOOT */
    9, 4, 1, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x03, 2, 64, 0, 0,
    7, 5, 0x84, 2, 64, 0, 0,
};

struct dpram {
    int kind; /* K_DPRAM */
    struct esk_rf_periph *p;
    u8 mem[0x1000]; /* the buffers (0x100 up); the control words are the register file's */
};

enum { K_USB = 1, K_DPRAM = 2 };

struct usbboot {
    int kind; /* K_USB */
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int active, attached;
    int addr, new_addr, config;
    /* endpoint 0's data stage */
    u8 ep0[256];
    u32 ep0_len, ep0_off;
    int ep0_in, ep0_toggle;
    /* bulk only transport */
    int bot, in_toggle, out_toggle;
    u32 tag, want, done;
    u8 status;
    u8 sense, asc;
    u32 lba, count, sec_off;
    u8 sec[SECTOR];
    u8 reply[64];
    u32 reply_len;
    int writing; /* the data out stage is WRITE(10)'s */
    /* the UF2 file being written */
    u32 nblocks, seen;
    u8 got[8192];   /* a bit per block number */
    u8 erased[512]; /* a bit per 4 KiB flash sector */
    u64 busy_until; /* the core's cycle when the flash is ready again */
    int reboot;
    /* the controller as a program uses it (USBCTRL_REGS and the DPRAM) */
    struct dpram *dp;
    int irq, irq_was, dev_sof, pull_was;
    u8 sel[32]; /* each endpoint's next buffer (0 or 1) when double buffered */
};

static u32 le32(const u8 *b) { return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24; }
static u32 be32(const u8 *b) { return (u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | (u32)b[3]; }
static void put16(u8 *b, u32 v) { b[0] = (u8)(v & 0xffu); b[1] = (u8)(v >> 8 & 0xffu); }
static void put32(u8 *b, u32 v) { put16(b, v & 0xffffu); put16(b + 2, v >> 16); }
static void putbe32(u8 *b, u32 v)
{
    b[0] = (u8)(v >> 24);
    b[1] = (u8)(v >> 16 & 0xffu);
    b[2] = (u8)(v >> 8 & 0xffu);
    b[3] = (u8)(v & 0xffu);
}

static struct usbboot *of(struct esk_mach *s)
{
    struct esk_rf_periph *p = s->rf ? esk_rf_periph(s->rf, "USB") : 0;
    return p && p->ops == &esk_rp2040_usb_ops ? (struct usbboot *)p->ctx : 0;
}

static u64 cycles_in(struct esk_mach *s, double seconds)
{
    u32 hz = s->core_hz ? s->core_hz(s) : s->reset_hz;
    return (u64)(seconds * (hz ? hz : 6500000u));
}

/* a directory entry: an 8.3 name, its attributes, first cluster and size */
static void dirent(u8 *d, const char *name11, u8 attr, u32 cluster, u32 size)
{
    memcpy(d, name11, 11);
    d[11] = attr;
    put16(d + 26, cluster);
    put32(d + 28, size);
}

/* the drive's sector `lba`, made as the bootrom's virtual disk shows it */
static void read_sector(u32 lba, u8 *b)
{
    memset(b, 0, SECTOR);
    if (lba == 0) {
        static const u8 jump[3] = { 0xeb, 0x3c, 0x90 };
        memcpy(b, jump, 3);
        memcpy(b + 3, "UF2 UF2 ", 8);
        put16(b + 11, SECTOR);
        b[13] = (u8)SPC;
        put16(b + 14, 1);
        b[16] = 2;
        put16(b + 17, ROOT_ENTRIES);
        put16(b + 19, 0); /* the 32-bit count below */
        b[21] = 0xf8;
        put16(b + 22, FAT_SECTORS);
        put16(b + 24, 1);
        put16(b + 26, 1);
        put32(b + 32, VOL_SECTORS);
        b[36] = 0x80;
        b[38] = 0x29;
        put32(b + 39, 0x00420042u);
        memcpy(b + 43, "RPI-RP2    ", 11);
        memcpy(b + 54, "FAT16   ", 8);
        b[510] = 0x55;
        b[511] = 0xaa;
    } else if (lba == FAT1 || lba == FAT2) {
        /* clusters 0 and 1 reserved, 2 and 3 the two files, one cluster each */
        put16(b, 0xfff8u);
        put16(b + 2, 0xffffu);
        put16(b + 4, 0xffffu);
        put16(b + 6, 0xffffu);
    } else if (lba == ROOT) {
        dirent(b, "RPI-RP2    ", 0x28, 0, 0);
        dirent(b + 32, "INFO_UF2TXT", 0x01, 2, (u32)sizeof info_uf2 - 1u);
        dirent(b + 64, "INDEX   HTM", 0x01, 3, (u32)sizeof index_htm - 1u);
    } else if (lba == DATA) {
        memcpy(b, info_uf2, sizeof info_uf2 - 1u);
    } else if (lba == DATA + SPC) {
        memcpy(b, index_htm, sizeof index_htm - 1u);
    }
}

/* a sector written: a UF2 block for flash is programmed, anything else is
 * ignored as the bootrom ignores it */
static void write_sector(struct usbboot *u, const u8 *b)
{
    struct esk_mach *s = u->s;
    struct esk_region *fl = esk_bus_region(&s->bus, FLASH_BASE, 1);
    u32 flags = le32(b + 8), addr = le32(b + 12), size = le32(b + 16), no = le32(b + 20), n = le32(b + 24);
    u32 off, sec, i;
    double t = 0.0;
    u8 *mem;
    if (le32(b) != 0x0a324655u || le32(b + 4) != 0x9e5d5157u || le32(b + 508) != 0x0ab16f30u)
        return;
    if ((flags & 1u) || !(flags & 0x2000u) || le32(b + 28) != UF2_FAMILY_RP2040 || size != 256u || (addr & 255u))
        return;
    if (!fl || addr < FLASH_BASE || addr - FLASH_BASE + 256u > fl->size || n == 0 || n > 8u * sizeof u->got ||
        no >= n)
        return;
    if (n != u->nblocks) { /* another file: its count starts again */
        u->nblocks = n;
        u->seen = 0;
        memset(u->got, 0, sizeof u->got);
    }
    off = addr - FLASH_BASE;
    mem = fl->mem + (FLASH_BASE - fl->base);
    sec = off >> 12;
    if (sec < 8u * sizeof u->erased && !(u->erased[sec >> 3] & (1u << (sec & 7u)))) {
        memset(mem + (sec << 12), 0xff, fl->size - (sec << 12) < 4096u ? fl->size - (sec << 12) : 4096u);
        u->erased[sec >> 3] |= (u8)(1u << (sec & 7u));
        t += 0.045;
    }
    for (i = 0; i < 256u; i++)
        mem[off + i] &= b[32 + i];
    t += 0.0004;
    fl->gen++;
    esk_bus_code_changed(&s->bus);
    s->flash_written = 1;
    u->busy_until = s->cycles + cycles_in(s, t);
    if (!(u->got[no >> 3] & (1u << (no & 7u)))) {
        u->got[no >> 3] |= (u8)(1u << (no & 7u));
        if (++u->seen == u->nblocks) {
            esk_rf_diag(s, ES_SEV_INFO, "mcu.uf2_written",
                        "the RP2040's USB boot wrote all %lu blocks of a UF2 file to flash: it reboots",
                        (unsigned long)n);
            u->reboot = 1;
        }
    }
}

static void sense(struct usbboot *u, u8 key, u8 asc)
{
    u->sense = key;
    u->asc = asc;
}

/* a command block wrapper: what follows it, and its reply */
static void command(struct usbboot *u, const u8 *cbw)
{
    const u8 *cb = cbw + 15;
    u8 *r = u->reply;
    u->tag = le32(cbw + 4);
    u->want = le32(cbw + 8);
    u->done = 0;
    u->status = 0;
    u->reply_len = 0;
    u->count = 0;
    u->writing = 0;
    memset(r, 0, sizeof u->reply);
    switch (cb[0]) {
    case 0x00: /* TEST UNIT READY */
    case 0x1e: /* PREVENT ALLOW MEDIUM REMOVAL */
    case 0x1b: /* START STOP UNIT */
    case 0x2f: /* VERIFY(10) */
        break;
    case 0x12: /* INQUIRY */
        r[0] = 0x00;
        r[1] = 0x80; /* removable */
        r[2] = 0x02;
        r[3] = 0x02;
        r[4] = 31;
        memcpy(r + 8, "RPI     ", 8);
        memcpy(r + 16, "RP2             ", 16);
        memcpy(r + 32, "3   ", 4);
        u->reply_len = 36;
        break;
    case 0x03: /* REQUEST SENSE */
        r[0] = 0x70;
        r[2] = u->sense;
        r[7] = 10;
        r[12] = u->asc;
        u->reply_len = 18;
        sense(u, 0, 0);
        break;
    case 0x25: /* READ CAPACITY(10) */
        putbe32(r, VOL_SECTORS - 1u);
        putbe32(r + 4, SECTOR);
        u->reply_len = 8;
        break;
    case 0x23: /* READ FORMAT CAPACITIES */
        r[3] = 8;
        putbe32(r + 4, VOL_SECTORS);
        r[8] = 0x02; /* formatted media */
        r[10] = (u8)(SECTOR >> 8);
        u->reply_len = 12;
        break;
    case 0x1a: /* MODE SENSE(6) */
        r[0] = 3;
        u->reply_len = 4;
        break;
    case 0x28: /* READ(10) */
    case 0x2a: /* WRITE(10) */
        u->lba = be32(cb + 2);
        u->count = (u32)cb[7] << 8 | cb[8];
        u->sec_off = 0;
        u->writing = cb[0] == 0x2a;
        if (u->lba + u->count > VOL_SECTORS) {
            u->status = 1;
            sense(u, 0x05, 0x21); /* LBA out of range */
            u->count = 0;
        } else if (!u->writing && u->count) {
            read_sector(u->lba, u->sec);
        }
        break;
    default:
        u->status = 1;
        sense(u, 0x05, 0x20); /* invalid command operation code */
        break;
    }
    if (u->want == 0)
        u->bot = B_CSW;
    else
        u->bot = (cbw[12] & 0x80u) ? B_IN : B_OUT;
}

static int boot_pullup(void *ctx)
{
    struct usbboot *u = (struct usbboot *)ctx;
    return u->active ? ESK_USB_FULL_SPEED : ESK_USB_DETACHED; /* until the reboot's reset */
}

static void boot_bus(void *ctx, int ev, u32 frame)
{
    struct usbboot *u = (struct usbboot *)ctx;
    (void)frame;
    if (ev == ESK_USB_RESET) {
        u->addr = u->new_addr = 0;
        u->config = 0;
        u->bot = B_CBW;
        u->in_toggle = u->out_toggle = 0;
        u->ep0_len = u->ep0_off = 0;
    }
}

static void string_desc(struct usbboot *u, int idx)
{
    static const char *const strs[] = { 0, "Raspberry Pi", "RP2 Boot", "000000000000" };
    u32 i, n;
    if (idx == 0) {
        u->ep0[0] = 4;
        u->ep0[1] = 3;
        put16(u->ep0 + 2, 0x0409u);
        u->ep0_len = 4;
        return;
    }
    n = (u32)strlen(strs[idx]);
    u->ep0[0] = (u8)(2u + 2u * n);
    u->ep0[1] = 3;
    for (i = 0; i < n; i++) {
        u->ep0[2 + 2 * i] = (u8)strs[idx][i];
        u->ep0[3 + 2 * i] = 0;
    }
    u->ep0_len = 2u + 2u * n;
}

static int boot_setup(void *ctx, int addr, int ep, const u8 d[8])
{
    struct usbboot *u = (struct usbboot *)ctx;
    u32 wlen = (u32)d[6] | (u32)d[7] << 8, value = (u32)d[2] | (u32)d[3] << 8;
    if (addr != u->addr || ep != 0)
        return ESK_USB_NONE;
    u->ep0_len = u->ep0_off = 0;
    u->ep0_in = (d[0] & 0x80u) != 0;
    u->ep0_toggle = 1;
    if (d[0] == 0x80 && d[1] == 6) { /* GET_DESCRIPTOR */
        int type = (int)(value >> 8), idx = (int)(value & 0xffu);
        if (type == 1) {
            memcpy(u->ep0, dev_desc, sizeof dev_desc);
            u->ep0_len = sizeof dev_desc;
        } else if (type == 2) {
            memcpy(u->ep0, cfg_desc, sizeof cfg_desc);
            u->ep0_len = sizeof cfg_desc;
        } else if (type == 3 && idx <= 3) {
            string_desc(u, idx);
        } else {
            return ESK_USB_STALL;
        }
    } else if (d[0] == 0x00 && d[1] == 5) { /* SET_ADDRESS: after its status stage */
        u->new_addr = (int)(value & 0x7fu);
    } else if (d[0] == 0x00 && d[1] == 9) { /* SET_CONFIGURATION */
        u->config = (int)(value & 0xffu);
        u->bot = B_CBW;
        u->in_toggle = u->out_toggle = 0;
    } else if (d[0] == 0x80 && d[1] == 8) { /* GET_CONFIGURATION */
        u->ep0[0] = (u8)u->config;
        u->ep0_len = 1;
    } else if (d[0] == 0x80 && d[1] == 0) { /* GET_STATUS */
        u->ep0[0] = 0;
        u->ep0[1] = 0;
        u->ep0_len = 2;
    } else if (d[0] == 0xa1 && d[1] == 0xfe) { /* GET MAX LUN */
        u->ep0[0] = 0;
        u->ep0_len = 1;
    } else if (d[0] == 0x21 && d[1] == 0xff) { /* bulk-only mass storage reset */
        u->bot = B_CBW;
    } else if ((d[0] & 0x60u) == 0 && (d[1] == 1 || d[1] == 3 || d[1] == 11)) {
        /* CLEAR_FEATURE (a halt), SET_FEATURE, SET_INTERFACE */
        if (d[1] == 1 && (d[0] & 3u) == 2) {
            if ((d[4] & 0x7fu) == 1)
                u->in_toggle = 0;
            if ((d[4] & 0x7fu) == 2)
                u->out_toggle = 0;
        }
    } else {
        return ESK_USB_STALL;
    }
    if (u->ep0_len > wlen)
        u->ep0_len = wlen;
    return ESK_USB_ACK;
}

static int boot_out(void *ctx, int addr, int ep, int data1, const u8 *data, u32 len)
{
    struct usbboot *u = (struct usbboot *)ctx;
    struct esk_mach *s = u->s;
    u32 take;
    if (addr != u->addr)
        return ESK_USB_NONE;
    if (ep == 0) /* a status stage after IN data, or OUT data no request here takes */
        return ESK_USB_ACK;
    if (ep == 3)
        return ESK_USB_NAK; /* PICOBOOT: not answered */
    if (ep != 2 || !u->config)
        return ESK_USB_STALL;
    if (s->cycles < u->busy_until)
        return ESK_USB_NAK; /* the flash is busy */
    if (data1 != u->out_toggle)
        return ESK_USB_ACK; /* a repeat of a packet already taken */
    u->out_toggle ^= 1;
    if (u->bot == B_CBW) {
        if (len == 31 && le32(data) == 0x43425355u)
            command(u, data);
        return ESK_USB_ACK;
    }
    if (u->bot != B_OUT)
        return ESK_USB_ACK;
    u->done += len;
    if (u->writing) {
        while (len && u->count) {
            take = SECTOR - u->sec_off < len ? SECTOR - u->sec_off : len;
            memcpy(u->sec + u->sec_off, data, take);
            u->sec_off += take;
            data += take;
            len -= take;
            if (u->sec_off == SECTOR) {
                write_sector(u, u->sec);
                u->sec_off = 0;
                u->lba++;
                u->count--;
            }
        }
    }
    if (u->done >= u->want)
        u->bot = B_CSW;
    return ESK_USB_ACK;
}

static int boot_in(void *ctx, int addr, int ep, u8 *data, u32 cap, u32 *len, int *data1)
{
    struct usbboot *u = (struct usbboot *)ctx;
    u32 n = 0;
    if (addr != u->addr)
        return ESK_USB_NONE;
    if (ep == 0) {
        if (!u->ep0_in) { /* the status stage of an OUT or no-data request */
            *len = 0;
            *data1 = 1;
            if (u->new_addr) {
                u->addr = u->new_addr;
                u->new_addr = 0;
            }
            return ESK_USB_ACK;
        }
        n = u->ep0_len - u->ep0_off;
        if (n > 64u)
            n = 64u;
        if (n > cap)
            n = cap;
        memcpy(data, u->ep0 + u->ep0_off, n);
        u->ep0_off += n;
        *len = n;
        *data1 = u->ep0_toggle;
        u->ep0_toggle ^= 1;
        return ESK_USB_ACK;
    }
    if (ep == 4)
        return ESK_USB_NAK;
    if (ep != 1 || !u->config)
        return ESK_USB_STALL;
    if (u->bot == B_IN) {
        if (u->reply_len) {
            n = u->reply_len - u->done < 64u ? u->reply_len - u->done : 64u;
            if (n > u->want - u->done)
                n = u->want - u->done;
            memcpy(data, u->reply + u->done, n);
        } else if (u->count) {
            n = 64u;
            memcpy(data, u->sec + u->sec_off, n);
            u->sec_off += n;
            if (u->sec_off == SECTOR) {
                u->sec_off = 0;
                u->lba++;
                if (--u->count)
                    read_sector(u->lba, u->sec);
            }
        }
        u->done += n;
        if (n < 64u || u->done >= u->want || (!u->count && !u->reply_len) ||
            (u->reply_len && u->done >= u->reply_len))
            u->bot = B_CSW;
        if (n == 0)
            u->bot = B_CSW;
        if (n || u->bot != B_CSW) {
            *len = n;
            *data1 = u->in_toggle;
            u->in_toggle ^= 1;
            return ESK_USB_ACK;
        }
    }
    if (u->bot == B_CSW) {
        if (u->s->cycles < u->busy_until)
            return ESK_USB_NAK;
        put32(data, 0x53425355u);
        put32(data + 4, u->tag);
        put32(data + 8, u->want > u->done ? u->want - u->done : 0);
        data[12] = u->status;
        *len = 13;
        *data1 = u->in_toggle;
        u->in_toggle ^= 1;
        u->bot = B_CBW;
        if (u->reboot && !u->s->reset_req) {
            /* the bootrom's reboot through the watchdog, once the host has the status */
            u->s->reset_req = 1;
            u->s->yield = 1;
        }
        return ESK_USB_ACK;
    }
    return ESK_USB_NAK;
}


/* ---- the controller as a program uses it (the datasheet's 4.1) ------------
 *
 * USBCTRL_REGS and the DPRAM in device mode, as TinyUSB (the Pico SDK's
 * stdio_usb, MicroPython) drives them: the pull-up with MAIN_CTRL's
 * CONTROLLER_EN and SIE_CTRL's PULLUP_EN (not HOST_NDEVICE); a bus reset
 * sets SIE_STATUS.BUS_RESET and CONNECTED; each SOF its frame in SOF_RD and
 * DEV_SOF until SOF_RD is read; a SETUP at the device's address is put at
 * the DPRAM's start, SETUP_REC set and EP_STALL_ARM cleared. A transaction
 * on an endpoint uses its buffer control word (EP0's at 0x80 and 0x84, its
 * buffer at 0x100; the others enabled by their endpoint control word, which
 * gives the buffer's place): an IN sends a buffer the program made FULL and
 * AVAILABLE (LENGTH bytes, its PID) and clears both; an OUT fills an
 * AVAILABLE buffer, setting LENGTH and FULL and clearing AVAILABLE; neither
 * is a NAK; STALL (EP0's armed by EP_STALL_ARM) a STALL. Double buffered,
 * the buffers alternate from buffer 0 (the RESET bit starts again from
 * it); BUFF_STATUS is set per buffer, or per pair with
 * INTERRUPT_PER_DOUBLE_BUFF, or at a short packet; EP0 per buffer with
 * SIE_CTRL's EP0_INT_1BUF. EP_ABORT is done at once (EP_ABORT_DONE). INTR
 * is made from these, INTS from INTR, INTF and INTE, and USBCTRL_IRQ
 * follows INTS. Omissions: host mode, isochronous endpoints, suspend
 * detection (no SOF for 3 ms) and remote wakeup, NAK and stall interrupts,
 * data sequence errors (a packet with the other PID is taken as it is). */

#define R_ADDR 0x00u
#define R_MAIN 0x40u
#define R_SOF_RD 0x48u
#define R_SIE_CTRL 0x4cu
#define R_SIE_STATUS 0x50u
#define R_BUFF_STATUS 0x58u
#define R_EP_ABORT 0x60u
#define R_EP_ABORT_DONE 0x64u
#define R_EP_STALL_ARM 0x68u
#define R_INTR 0x8cu
#define R_INTE 0x90u
#define R_INTF 0x94u
#define R_INTS 0x98u

#define ST_BUS_RESET (1u << 19)
#define ST_SETUP_REC (1u << 17)
#define ST_CONNECTED (1u << 16)
#define ST_RESUME (1u << 11)
#define ST_SUSPENDED (1u << 4)

#define BC_FULL 0x8000u
#define BC_LAST 0x4000u
#define BC_PID 0x2000u
#define BC_RESET 0x1000u
#define BC_STALL 0x0800u
#define BC_AVAIL 0x0400u
#define BC_LEN 0x03ffu

#define EC_ENABLE 0x80000000u
#define EC_DOUBLE 0x40000000u
#define EC_PER_BUFF 0x20000000u
#define EC_PER_DOUBLE 0x10000000u

/* the register at `off` in a peripheral's block */
static struct esk_rf_reg *reg_at_off(struct esk_rf_periph *p, u32 off)
{
    int i;
    for (i = 0; i < p->nreg; i++)
        if (p->reg[i]->addr == p->base + off)
            return p->reg[i];
    return 0;
}

static u32 rd(struct usbboot *u, u32 off) { return esk_rf_read(u->p, off, 4); }
static void hw(struct usbboot *u, u32 off, u32 v)
{
    struct esk_rf_reg *r = reg_at_off(u->p, off);
    if (r)
        esk_rf_hw(r, v);
}

static struct dpram *dpram_of(struct usbboot *u)
{
    if (!u->dp) {
        struct esk_rf_periph *p = u->s->rf ? esk_rf_periph(u->s->rf, "USB_DPRAM") : 0;
        if (p && p->ops == &esk_rp2040_usb_ops && p->ctx && ((struct dpram *)p->ctx)->kind == K_DPRAM)
            u->dp = (struct dpram *)p->ctx;
    }
    return u->dp;
}

static u32 dp_word(struct usbboot *u, u32 off)
{
    struct dpram *d = dpram_of(u);
    return d ? esk_rf_read(d->p, off, 4) : 0;
}

static void dp_set(struct usbboot *u, u32 off, u32 v)
{
    struct dpram *d = dpram_of(u);
    struct esk_rf_reg *r = d ? reg_at_off(d->p, off) : 0;
    if (r)
        esk_rf_hw(r, v);
}

static int ctrl_pullup(struct usbboot *u)
{
    u32 m = rd(u, R_MAIN);
    return (m & 1u) && !(m & 2u) && (rd(u, R_SIE_CTRL) & (1u << 16));
}

/* INTR from the controller's state, INTS from it, and the interrupt line */
static void ctrl_update(struct usbboot *u)
{
    u32 st = rd(u, R_SIE_STATUS), intr = 0, ints;
    int level;
    if (rd(u, R_BUFF_STATUS))
        intr |= 1u << 4;
    if (st & ST_BUS_RESET)
        intr |= 1u << 12;
    if (st & ST_CONNECTED)
        intr |= 1u << 13;
    if (st & ST_SUSPENDED)
        intr |= 1u << 14;
    if (st & ST_RESUME)
        intr |= 1u << 15;
    if (st & ST_SETUP_REC)
        intr |= 1u << 16;
    if (u->dev_sof)
        intr |= 1u << 17;
    if (rd(u, R_EP_ABORT_DONE))
        intr |= 1u << 18;
    hw(u, R_INTR, intr);
    ints = (intr | rd(u, R_INTF)) & rd(u, R_INTE);
    hw(u, R_INTS, ints);
    level = ints != 0;
    esk_rf_irq_level(u->p, u->irq, level, &u->irq_was);
}

static void ctrl_bus(struct usbboot *u, int ev, u32 frame)
{
    u32 st = rd(u, R_SIE_STATUS);
    if (ev == ESK_USB_RESET) {
        st |= ST_BUS_RESET | ST_CONNECTED;
        st &= ~ST_SUSPENDED;
        memset(u->sel, 0, sizeof u->sel);
    } else if (ev == ESK_USB_SOF) {
        hw(u, R_SOF_RD, frame & 0x7ffu);
        u->dev_sof = 1;
        st &= ~ST_SUSPENDED;
    } else if (ev == ESK_USB_RESUME) {
        st |= ST_RESUME;
    }
    hw(u, R_SIE_STATUS, st);
    ctrl_update(u);
}

static int ctrl_setup(struct usbboot *u, int addr, int ep, const u8 d[8])
{
    if (!ctrl_pullup(u) || addr != (int)(rd(u, R_ADDR) & 0x7fu) || ep != 0)
        return ESK_USB_NONE;
    dp_set(u, 0, le32(d));
    dp_set(u, 4, le32(d + 4));
    hw(u, R_SIE_STATUS, rd(u, R_SIE_STATUS) | ST_SETUP_REC);
    hw(u, R_EP_STALL_ARM, 0);
    u->sel[0] = u->sel[1] = 0;
    ctrl_update(u);
    return ESK_USB_ACK;
}

/* an endpoint's buffer control word, its buffer's place and its control
 * word (EP0's: none, EP0_INT_1BUF instead); 0 when it is not enabled */
static int ep_words(struct usbboot *u, int ep, int out, u32 *bc_off, u32 *buf, u32 *ec)
{
    *bc_off = 0x80u + (u32)ep * 8u + (out ? 4u : 0u);
    if (ep == 0) {
        *buf = 0x100u;
        *ec = EC_ENABLE | ((rd(u, R_SIE_CTRL) & (1u << 29)) ? EC_PER_BUFF : 0);
        return 1;
    }
    *ec = dp_word(u, 0x08u + (u32)(ep - 1) * 8u + (out ? 4u : 0u));
    *buf = *ec & 0xffc0u;
    return (*ec & EC_ENABLE) != 0;
}

/* a buffer done: which half, its endpoint's interrupt rules */
static void buffer_done(struct usbboot *u, int id, u32 ec, int half, int short_pkt)
{
    int raise = 1;
    if (ec & EC_DOUBLE) {
        u->sel[id] ^= 1;
        if ((ec & EC_PER_DOUBLE) && !(ec & EC_PER_BUFF))
            raise = half == 1 || short_pkt;
    } else if (!(ec & (EC_PER_BUFF | EC_PER_DOUBLE))) {
        raise = 0;
    }
    if (raise)
        hw(u, R_BUFF_STATUS, rd(u, R_BUFF_STATUS) | 1u << id);
}

/* the half of the buffer control word this transaction uses */
static int half_of(struct usbboot *u, int id, u32 bc, u32 ec)
{
    if (bc & BC_RESET) /* the program starts again from buffer 0 */
        u->sel[id] = 0;
    return (ec & EC_DOUBLE) ? u->sel[id] : 0;
}

static int ctrl_out(struct usbboot *u, int addr, int ep, int data1, const u8 *data, u32 len)
{
    struct dpram *d = dpram_of(u);
    u32 bc_off, buf, ec, bc, hv;
    int id = ep * 2 + 1, half;
    (void)data1;
    if (!ctrl_pullup(u) || addr != (int)(rd(u, R_ADDR) & 0x7fu) || !d)
        return ESK_USB_NONE;
    if (ep < 0 || ep > 15 || !ep_words(u, ep, 1, &bc_off, &buf, &ec))
        return ESK_USB_NONE;
    bc = dp_word(u, bc_off);
    if ((bc & BC_STALL) && (ep != 0 || (rd(u, R_EP_STALL_ARM) & 2u)))
        return ESK_USB_STALL;
    half = half_of(u, id, bc, ec);
    hv = bc >> (16 * half) & 0xffffu;
    if (!(hv & BC_AVAIL) || (hv & BC_FULL))
        return ESK_USB_NAK;
    if (len > 64u)
        len = 64u;
    if (buf + (u32)half * 64u + len <= sizeof d->mem)
        memcpy(d->mem + buf + (u32)half * 64u, data, len);
    hv = (hv & ~(BC_AVAIL | BC_LEN | BC_RESET)) | BC_FULL | len;
    bc = (bc & ~(0xffffu << (16 * half))) | hv << (16 * half);
    dp_set(u, bc_off, bc & ~(u32)BC_RESET);
    buffer_done(u, id, ec, half, len < 64u);
    ctrl_update(u);
    return ESK_USB_ACK;
}

static int ctrl_in(struct usbboot *u, int addr, int ep, u8 *data, u32 cap, u32 *len, int *data1)
{
    struct dpram *d = dpram_of(u);
    u32 bc_off, buf, ec, bc, hv, n;
    int id = ep * 2, half;
    if (!ctrl_pullup(u) || addr != (int)(rd(u, R_ADDR) & 0x7fu) || !d)
        return ESK_USB_NONE;
    if (ep < 0 || ep > 15 || !ep_words(u, ep, 0, &bc_off, &buf, &ec))
        return ESK_USB_NONE;
    bc = dp_word(u, bc_off);
    if ((bc & BC_STALL) && (ep != 0 || (rd(u, R_EP_STALL_ARM) & 1u)))
        return ESK_USB_STALL;
    half = half_of(u, id, bc, ec);
    hv = bc >> (16 * half) & 0xffffu;
    if (!(hv & BC_AVAIL) || !(hv & BC_FULL))
        return ESK_USB_NAK;
    n = hv & BC_LEN;
    if (n > cap)
        n = cap;
    if (buf + (u32)half * 64u + n <= sizeof d->mem)
        memcpy(data, d->mem + buf + (u32)half * 64u, n);
    *len = n;
    *data1 = (hv & BC_PID) != 0;
    hv &= ~(BC_AVAIL | BC_FULL | BC_RESET);
    bc = (bc & ~(0xffffu << (16 * half))) | hv << (16 * half);
    dp_set(u, bc_off, bc & ~(u32)BC_RESET);
    buffer_done(u, id, ec, half, n < 64u);
    ctrl_update(u);
    return ESK_USB_ACK;
}

/* ---- the cable's end: the bootrom's device while in its USB boot, else the program's */

static int pullup(void *ctx)
{
    struct usbboot *u = (struct usbboot *)ctx;
    if (u->active)
        return boot_pullup(ctx);
    return ctrl_pullup(u) ? ESK_USB_FULL_SPEED : ESK_USB_DETACHED;
}

static void bus(void *ctx, int ev, u32 frame)
{
    struct usbboot *u = (struct usbboot *)ctx;
    if (u->active)
        boot_bus(ctx, ev, frame);
    else
        ctrl_bus(u, ev, frame);
}

static int setup(void *ctx, int addr, int ep, const u8 d[8])
{
    struct usbboot *u = (struct usbboot *)ctx;
    return u->active ? boot_setup(ctx, addr, ep, d) : ctrl_setup(u, addr, ep, d);
}

static int out(void *ctx, int addr, int ep, int data1, const u8 *data, u32 len)
{
    struct usbboot *u = (struct usbboot *)ctx;
    return u->active ? boot_out(ctx, addr, ep, data1, data, len) : ctrl_out(u, addr, ep, data1, data, len);
}

static int in(void *ctx, int addr, int ep, u8 *data, u32 cap, u32 *len, int *data1)
{
    struct usbboot *u = (struct usbboot *)ctx;
    return u->active ? boot_in(ctx, addr, ep, data, cap, len, data1) : ctrl_in(u, addr, ep, data, cap, len, data1);
}

void esk_rp2040_usb_boot(struct esk_mach *s)
{
    struct usbboot *u = of(s);
    if (!u)
        return;
    u->active = 1;
    u->reboot = 0;
    u->nblocks = u->seen = 0;
    memset(u->got, 0, sizeof u->got);
    memset(u->erased, 0, sizeof u->erased);
    if (s->host.usb_changed)
        s->host.usb_changed(s->host.ctx);
}

int esk_rp2040_in_usb_boot(struct esk_mach *s)
{
    struct usbboot *u = of(s);
    return u && u->active;
}

static u32 usb_read(void *ctx, u32 off, int n)
{
    u32 v;
    if (*(int *)ctx == K_DPRAM) {
        struct dpram *d = (struct dpram *)ctx;
        if (off < 0x100u)
            return esk_rf_read(d->p, off, n);
        v = 0;
        if (off + (u32)n <= sizeof d->mem)
            memcpy(&v, d->mem + off, (size_t)n); /* little-endian, as the bus */
        return v;
    } else {
        struct usbboot *u = (struct usbboot *)ctx;
        v = esk_rf_read(u->p, off, n);
        if ((off & ~3u) == R_SOF_RD && u->dev_sof) { /* reading the frame clears DEV_SOF */
            u->dev_sof = 0;
            ctrl_update(u);
        }
        return v;
    }
}

static void usb_write(void *ctx, u32 off, int n, u32 v)
{
    if (*(int *)ctx == K_DPRAM) {
        struct dpram *d = (struct dpram *)ctx;
        if (off < 0x100u)
            esk_rf_write(d->p, off, n, v);
        else if (off + (u32)n <= sizeof d->mem)
            memcpy(d->mem + off, &v, (size_t)n);
    } else {
        struct usbboot *u = (struct usbboot *)ctx;
        int pull;
        esk_rf_write(u->p, off, n, v);
        if ((off & ~3u) == R_EP_ABORT) /* nothing is under way between transactions: done at once */
            hw(u, R_EP_ABORT_DONE, rd(u, R_EP_ABORT_DONE) | rd(u, R_EP_ABORT));
        ctrl_update(u);
        pull = ctrl_pullup(u);
        if (pull != u->pull_was && !u->active) {
            u->pull_was = pull;
            if (u->s->host.usb_changed)
                u->s->host.usb_changed(u->s->host.ctx);
        }
    }
}

/* a chip reset: the bootrom decides again whether it boots USB (boot.c
 * calls esk_rp2040_usb_leave, then esk_rp2040_usb_boot when it does) */
static void usb_reset(void *ctx)
{
    struct usbboot *u = (struct usbboot *)ctx;
    if (u->kind == K_DPRAM) {
        memset(((struct dpram *)ctx)->mem, 0, sizeof ((struct dpram *)ctx)->mem);
        return;
    }
    u->dev_sof = 0;
    memset(u->sel, 0, sizeof u->sel);
    if (u->pull_was) {
        u->pull_was = 0;
        if (u->s->host.usb_changed)
            u->s->host.usb_changed(u->s->host.ctx);
    }
    u->addr = u->new_addr = u->config = 0;
    u->bot = B_CBW;
    u->busy_until = 0;
}

void esk_rp2040_usb_leave(struct esk_mach *s)
{
    struct usbboot *u = of(s);
    if (!u || !u->active)
        return;
    u->active = 0;
    u->reboot = 0;
    if (s->host.usb_changed)
        s->host.usb_changed(s->host.ctx);
}

const struct esk_dev_ops esk_rp2040_usb_ops = { "rp2040-usb", usb_read, usb_write, usb_reset, 0, 0 };

void *esk_rp2040_usb_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct usbboot *u;
    if (!strcmp(p->name, "USB_DPRAM")) {
        struct dpram *d = (struct dpram *)esk_mach_alloc(s, sizeof *d);
        if (!d)
            return 0;
        d->kind = K_DPRAM;
        d->p = p;
        p->model_rsv = 1; /* the buffers past the control words */
        return d;
    }
    if (strcmp(p->name, "USB") != 0)
        return 0;
    u = (struct usbboot *)esk_mach_alloc(s, sizeof *u);
    if (!u)
        return 0;
    u->kind = K_USB;
    u->irq = esk_rf_irq(p, "USBCTRL");
    u->s = s;
    u->p = p;
    s->usb.ctx = u;
    s->usb.dp_at = -1;
    s->usb.dp_pin = "USB_DP";
    s->usb.pullup = pullup;
    s->usb.bus = bus;
    s->usb.setup = setup;
    s->usb.out = out;
    s->usb.in = in;
    s->strap_pin = "QSPI_SS";
    s->strap_low = esk_rp2040_enter_usb_boot;
    return u;
}
