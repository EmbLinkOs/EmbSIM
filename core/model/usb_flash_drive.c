/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb_flash_drive.c -- a USB flash drive: a full-speed USB Mass Storage
 * device (USB MSC Bulk-Only Transport 1.0 with the SCSI transparent command
 * set) on a cable of the bench (usb_dev.h), its medium formatted as a stick
 * comes from the shop: an MBR with one FAT16 partition from sector 2048,
 * 512-byte sectors, the volume label EMBSIM.
 *
 *   - its descriptors: a device of class 0, one configuration (bus powered,
 *     200 mA), one interface of class 8 (mass storage), subclass 6 (SCSI
 *     transparent), protocol 0x50 (Bulk-Only), a bulk IN endpoint 0x81 and
 *     a bulk OUT endpoint 0x02 of 64 bytes; a serial number, as BOT asks;
 *   - its class requests: Get Max LUN (one LUN: 0) and Bulk-Only Mass
 *     Storage Reset;
 *   - Bulk-Only Transport: a CBW of 31 bytes with its signature on the OUT
 *     endpoint, its data phase, then the CSW of 13 bytes on the IN endpoint
 *     with the CBW's tag, the residue and its status; a CBW that is not
 *     valid (its length, its signature) halts both endpoints until the
 *     host's reset recovery (the class reset, then CLEAR_FEATURE on each);
 *     a data phase shorter than the host asked ends with a short packet,
 *     or with a STALL of the IN endpoint when the last packet was full; a
 *     command that fails answers CHECK CONDITION (status 1) and keeps its
 *     sense for REQUEST SENSE;
 *   - SCSI: TEST UNIT READY, REQUEST SENSE, INQUIRY (removable, 'EmbSIM'
 *     'USB Flash Drive' '1.00'), MODE SENSE(6) and (10) (the write
 *     protection), START STOP UNIT, PREVENT ALLOW MEDIUM REMOVAL, READ
 *     FORMAT CAPACITIES, READ CAPACITY(10), READ(10), WRITE(10), VERIFY(10),
 *     SYNCHRONIZE CACHE(10); anything else ILLEGAL REQUEST, INVALID COMMAND
 *     OPERATION CODE; a block past the end LOGICAL BLOCK ADDRESS OUT OF
 *     RANGE; a write while write_protect is set DATA PROTECT, WRITE
 *     PROTECTED;
 *   - its outputs: what a computer shows of it: the files in the root
 *     directory with their sizes, and what it was asked to do.
 * Inside: the medium is in memory, as large as its capacity parameter, and
 * starts formatted at each power-up of the simulation (no image kept
 * between runs); a command's data moves at once, with no NAND page times
 * (a real stick NAKs while it programs). Not modelled: high speed (the
 * drive is a full-speed device, as on a full-speed host anyway), UAS,
 * more than one LUN, the drive's activity LED. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "model/registry.h"
#include "model/usb_dev.h"

#define SECTOR 512u
#define PART_START 2048u
#define MPS 64u
#define CBW_SIG 0x43425355u
#define CSW_SIG 0x53425355u

static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,  /* USB 2.0, class in the interface, EP0 64 bytes */
    0x09, 0x12, 0x01, 0x00,          /* 1209:0001, pid.codes' test PID: no vendor's own */
    0x00, 0x01, 1, 2, 3, 1,          /* bcdDevice 1.00, strings 1, 2 and the serial, one configuration */
};

static const uint8_t cfg_desc[32] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 100,          /* bus powered, 200 mA */
    9, 4, 0, 0, 2, 8, 6, 0x50, 0,             /* mass storage, SCSI, Bulk-Only */
    7, 5, 0x81, 2, MPS, 0, 0,                 /* bulk IN */
    7, 5, 0x02, 2, MPS, 0, 0,                 /* bulk OUT */
};

static const char *const strings[3] = { "EmbSIM", "USB Flash Drive", "EMBSIM0000000001" };

enum { IDLE, DATA_IN, DATA_OUT, STATUS };

typedef struct drive_state {
    usbd u;
    uint8_t *disk;
    uint32_t sectors;
    int write_protect;
    /* the command under way */
    int phase;
    uint32_t tag, want, done; /* dCBWDataTransferLength, bytes moved */
    uint8_t status;
    uint8_t buf[4096];
    uint32_t buf_len;         /* an IN command's answer (not READ) */
    uint32_t lba, blocks;     /* READ(10), WRITE(10) */
    int reading, writing;
    int stall_after;          /* the data phase ends with the IN endpoint's STALL */
    uint8_t sense_key, asc, ascq;
    /* outputs */
    uint64_t commands, sectors_read, sectors_written;
    char last[24];
    char files[1024];
} drive_state;

static void put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put16le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32le(uint8_t *p, uint32_t v)
{
    put16le(p, v & 0xFFFFu);
    put16le(p + 2, v >> 16);
}

static uint32_t get32be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t get16le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

static uint32_t get32le(const uint8_t *p) { return get16le(p) | get16le(p + 2) << 16; }

/* the FAT16 geometry of the partition: one sector a cluster up to 16 MiB,
 * more beyond, as FORMAT would choose */
static void geometry(uint32_t part, uint32_t *spc, uint32_t *fat_sz)
{
    uint32_t c = 1, f;
    while (part / c > 65000u && c < 64u)
        c *= 2u;
    f = ((part / c + 2u) * 2u + SECTOR - 1u) / SECTOR;
    *spc = c;
    *fat_sz = f;
}

/* an empty stick: the MBR, the partition's boot sector, its two FATs and
 * the root directory with the volume label */
static void format(drive_state *d)
{
    uint8_t *m = d->disk, *b = d->disk + (size_t)PART_START * SECTOR;
    uint32_t part = d->sectors - PART_START, spc, fsz, k;
    memset(d->disk, 0, (size_t)d->sectors * SECTOR);
    geometry(part, &spc, &fsz);
    /* the MBR: one partition, FAT16 (0x06 from 32 MiB, else 0x04), LBA */
    m[446] = 0x00;
    m[446 + 4] = part >= 65536u ? 0x06 : 0x04;
    put32le(m + 446 + 8, PART_START);
    put32le(m + 446 + 12, part);
    m[446 + 1] = 0xFE; /* CHS fields out of range: the LBA fields count */
    m[446 + 2] = 0xFF;
    m[446 + 3] = 0xFF;
    m[446 + 5] = 0xFE;
    m[446 + 6] = 0xFF;
    m[446 + 7] = 0xFF;
    put32le(m + 440, 0x454D4253u); /* the disk signature */
    m[510] = 0x55;
    m[511] = 0xAA;
    /* the boot sector's BPB */
    b[0] = 0xEB;
    b[1] = 0x3C;
    b[2] = 0x90;
    memcpy(b + 3, "MSDOS5.0", 8);
    put16le(b + 11, SECTOR);
    b[13] = (uint8_t)spc;
    put16le(b + 14, 1);   /* reserved sectors */
    b[16] = 2;            /* FATs */
    put16le(b + 17, 512); /* root directory entries */
    if (part < 65536u)
        put16le(b + 19, part);
    else
        put32le(b + 32, part);
    b[21] = 0xF8;
    put16le(b + 22, fsz);
    put16le(b + 24, 63);
    put16le(b + 26, 255);
    put32le(b + 28, PART_START);
    b[36] = 0x80;
    b[38] = 0x29;
    put32le(b + 39, 0x20261031u);
    memcpy(b + 43, "EMBSIM     ", 11);
    memcpy(b + 54, "FAT16   ", 8);
    b[510] = 0x55;
    b[511] = 0xAA;
    /* the FATs' first two entries: the media byte, end of chain */
    for (k = 0; k < 2u; k++) {
        uint8_t *f = b + (size_t)(1u + k * fsz) * SECTOR;
        put16le(f, 0xFFF8u);
        put16le(f + 2, 0xFFFFu);
    }
    /* the root directory: the volume label */
    memcpy(b + (size_t)(1u + 2u * fsz) * SECTOR, "EMBSIM     ", 11);
    b[(size_t)(1u + 2u * fsz) * SECTOR + 11] = 0x08;
}

/* the root directory's files as a computer lists them: NAME.EXT (size) */
static void list_files(drive_state *d)
{
    const uint8_t *m = d->disk, *b, *e;
    uint32_t start, rsv, nfat, fsz, roots, i;
    size_t o = 0;
    d->files[0] = 0;
    start = get32le(m + 446 + 8);
    if (m[510] != 0x55 || m[511] != 0xAA || !start || start >= d->sectors)
        start = 0; /* no partition table: a superfloppy */
    b = m + (size_t)start * SECTOR;
    if (get16le(b + 11) != SECTOR)
        return;
    rsv = get16le(b + 14);
    nfat = b[16];
    roots = get16le(b + 17);
    fsz = get16le(b + 22);
    if (!roots || (size_t)(start + rsv + nfat * fsz) * SECTOR + (size_t)roots * 32u > (size_t)d->sectors * SECTOR)
        return;
    e = b + (size_t)(rsv + nfat * fsz) * SECTOR;
    for (i = 0; i < roots; i++, e += 32) {
        char name[13];
        int n = 0, k;
        if (e[0] == 0)
            break;
        if (e[0] == 0xE5 || (e[11] & 0x0Fu) == 0x0Fu || (e[11] & 0x08u))
            continue; /* deleted, a long name's part, the volume label */
        for (k = 0; k < 8 && e[k] != ' '; k++)
            name[n++] = (char)e[k];
        if (e[8] != ' ') {
            name[n++] = '.';
            for (k = 8; k < 11 && e[k] != ' '; k++)
                name[n++] = (char)e[k];
        }
        name[n] = 0;
        o += (size_t)snprintf(d->files + o, sizeof d->files - o, "%s%s%s (%lu)", o ? ", " : "", name,
                              (e[11] & 0x10u) ? "/" : "", (unsigned long)get32le(e + 28));
        if (o >= sizeof d->files - 1)
            break;
    }
}

static void sense(drive_state *d, uint8_t key, uint8_t asc, uint8_t ascq)
{
    d->sense_key = key;
    d->asc = asc;
    d->ascq = ascq;
}

/* a CBW's command: its answer or its blocks; the status if it fails */
static void command(drive_state *d, const uint8_t *cb, int dir_in)
{
    uint8_t op = cb[0];
    uint8_t *r = d->buf;
    d->buf_len = 0;
    d->reading = d->writing = 0;
    d->status = 0;
    d->commands++;
    switch (op) {
    case 0x00: /* TEST UNIT READY */
        snprintf(d->last, sizeof d->last, "TEST UNIT READY");
        break;
    case 0x03: /* REQUEST SENSE: fixed format */
        snprintf(d->last, sizeof d->last, "REQUEST SENSE");
        memset(r, 0, 18);
        r[0] = 0x70;
        r[2] = d->sense_key;
        r[7] = 10;
        r[12] = d->asc;
        r[13] = d->ascq;
        d->buf_len = cb[4] < 18 ? cb[4] : 18;
        sense(d, 0, 0, 0);
        break;
    case 0x12: /* INQUIRY: standard data */
        snprintf(d->last, sizeof d->last, "INQUIRY");
        memset(r, 0, 36);
        r[1] = 0x80; /* removable */
        r[2] = 0x04; /* SPC-2 */
        r[3] = 0x02;
        r[4] = 31;
        memcpy(r + 8, "EmbSIM  ", 8);
        memcpy(r + 16, "USB Flash Drive ", 16);
        memcpy(r + 32, "1.00", 4);
        d->buf_len = cb[4] < 36 ? cb[4] : 36;
        break;
    case 0x1A: /* MODE SENSE(6): the header, the write protection */
    case 0x5A: /* MODE SENSE(10) */
        snprintf(d->last, sizeof d->last, "MODE SENSE");
        if (op == 0x1A) {
            r[0] = 3;
            r[1] = 0;
            r[2] = d->write_protect ? 0x80 : 0;
            r[3] = 0;
            d->buf_len = cb[4] < 4 ? cb[4] : 4;
        } else {
            memset(r, 0, 8);
            r[1] = 6;
            r[3] = d->write_protect ? 0x80 : 0;
            d->buf_len = 8;
        }
        break;
    case 0x1B: /* START STOP UNIT */
        snprintf(d->last, sizeof d->last, "START STOP UNIT");
        break;
    case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
        snprintf(d->last, sizeof d->last, "PREVENT ALLOW REMOVAL");
        break;
    case 0x23: /* READ FORMAT CAPACITIES: the formatted capacity */
        snprintf(d->last, sizeof d->last, "READ FORMAT CAPACITIES");
        memset(r, 0, 12);
        r[3] = 8;
        put32be(r + 4, d->sectors);
        r[8] = 0x02;
        r[10] = SECTOR >> 8;
        d->buf_len = 12;
        break;
    case 0x25: /* READ CAPACITY(10): the last block, its length */
        snprintf(d->last, sizeof d->last, "READ CAPACITY(10)");
        put32be(r, d->sectors - 1u);
        put32be(r + 4, SECTOR);
        d->buf_len = 8;
        break;
    case 0x28: /* READ(10) */
    case 0x2A: /* WRITE(10) */
    case 0x2F: /* VERIFY(10) */
        d->lba = get32be(cb + 2);
        d->blocks = (uint32_t)cb[7] << 8 | cb[8];
        snprintf(d->last, sizeof d->last, op == 0x28 ? "READ(10)" : op == 0x2A ? "WRITE(10)" : "VERIFY(10)");
        if (d->lba > d->sectors || d->blocks > d->sectors - d->lba) {
            sense(d, 0x05, 0x21, 0x00); /* LOGICAL BLOCK ADDRESS OUT OF RANGE */
            d->status = 1;
            break;
        }
        if (op == 0x2A && d->write_protect) {
            sense(d, 0x07, 0x27, 0x00); /* DATA PROTECT, WRITE PROTECTED */
            d->status = 1;
            break;
        }
        d->reading = op == 0x28 && dir_in;
        d->writing = op == 0x2A && !dir_in;
        if ((op == 0x28 && !dir_in && d->want) || (op == 0x2A && dir_in && d->want))
            d->status = 2; /* the direction the host gave is not the command's: a phase error */
        break;
    case 0x35: /* SYNCHRONIZE CACHE(10) */
        snprintf(d->last, sizeof d->last, "SYNCHRONIZE CACHE");
        break;
    default:
        snprintf(d->last, sizeof d->last, "0x%02X", op);
        sense(d, 0x05, 0x20, 0x00); /* ILLEGAL REQUEST, INVALID COMMAND OPERATION CODE */
        d->status = 1;
        break;
    }
}

static void start_status(drive_state *d)
{
    d->phase = STATUS;
}

static int msc_out(usbd *u, int ep, const uint8_t *data, uint32_t len)
{
    drive_state *d = (drive_state *)u->cls_ctx;
    if (ep != 2)
        return ESK_USB_STALL;
    if (d->phase == IDLE) {
        int dir_in;
        if (len != 31 || get32le(data) != CBW_SIG || (data[13] & 0x0Fu) != 0 || data[14] < 1 || data[14] > 16) {
            /* not a valid CBW: both endpoints halt until reset recovery
             * (BOT 6.6.1) */
            u->halted_in |= 1u << 1;
            u->halted_out |= 1u << 2;
            return ESK_USB_STALL;
        }
        d->tag = get32le(data + 4);
        d->want = get32le(data + 8);
        d->done = 0;
        d->stall_after = 0;
        dir_in = (data[12] & 0x80u) != 0;
        command(d, data + 15, dir_in);
        if (!d->want || d->status)
            start_status(d);
        else
            d->phase = dir_in ? DATA_IN : DATA_OUT;
        if (d->status && d->want && dir_in) {
            /* nothing to send: the IN endpoint stalls, the host clears it
             * and takes the CSW (BOT's case 4) */
            u->halted_in |= 1u << 1;
        } else if (d->status && d->want && !dir_in) {
            u->halted_out |= 1u << 2; /* case 9 */
        }
        return ESK_USB_ACK;
    }
    if (d->phase == DATA_OUT) {
        if (d->writing) {
            uint32_t at = d->lba * SECTOR + d->done, end = (d->lba + d->blocks) * SECTOR;
            uint32_t n = len;
            if (at + n > end)
                n = at < end ? end - at : 0;
            memcpy(d->disk + at, data, n);
            if ((at % SECTOR) + len >= SECTOR)
                d->sectors_written += ((at % SECTOR) + len) / SECTOR;
        }
        d->done += len;
        if (d->done >= d->want || len < MPS) {
            if (d->writing)
                list_files(d);
            start_status(d);
        }
        return ESK_USB_ACK;
    }
    return ESK_USB_NAK; /* the CSW is still to be taken */
}

static int msc_in(usbd *u, int ep, uint8_t *data, uint32_t cap, uint32_t *len)
{
    drive_state *d = (drive_state *)u->cls_ctx;
    uint32_t n;
    if (ep != 1)
        return ESK_USB_STALL;
    if (d->phase == DATA_IN) {
        uint32_t have = d->reading ? d->blocks * SECTOR : d->buf_len;
        if (d->done >= have) {
            /* less than the host asked, the last packet full: STALL ends
             * the data phase (BOT 6.7.2) */
            u->halted_in |= 1u << 1;
            start_status(d);
            return ESK_USB_STALL;
        }
        n = have - d->done;
        if (n > d->want - d->done)
            n = d->want - d->done;
        if (n > MPS)
            n = MPS;
        if (n > cap)
            n = cap;
        if (d->reading) {
            uint32_t at = d->lba * SECTOR + d->done;
            memcpy(data, d->disk + at, n);
            if ((at % SECTOR) + n >= SECTOR)
                d->sectors_read++;
        } else {
            memcpy(data, d->buf + d->done, n);
        }
        d->done += n;
        *len = n;
        if (d->done >= d->want || (d->done >= have && n < MPS))
            start_status(d); /* all of it, or a short packet's end */
        else if (d->done >= have)
            d->stall_after = 1;
        return ESK_USB_ACK;
    }
    if (d->phase == STATUS) {
        uint32_t residue = d->want > d->done ? d->want - d->done : 0;
        put32le(data, CSW_SIG);
        put32le(data + 4, d->tag);
        put32le(data + 8, residue);
        data[12] = d->status;
        *len = 13;
        d->phase = IDLE;
        return ESK_USB_ACK;
    }
    return ESK_USB_NAK;
}

static int msc_setup(usbd *u, const uint8_t s[8], const uint8_t *data, uint8_t *out, uint32_t cap, uint32_t *len)
{
    drive_state *d = (drive_state *)u->cls_ctx;
    (void)data;
    (void)cap;
    if ((s[0] & 0x60u) != 0x20u)
        return ESK_USB_STALL;
    if (s[1] == 0xFE) { /* Get Max LUN */
        out[0] = 0;
        *len = 1;
        return ESK_USB_ACK;
    }
    if (s[1] == 0xFF) { /* Bulk-Only Mass Storage Reset: ready for a CBW */
        d->phase = IDLE;
        d->done = d->want = 0;
        return ESK_USB_ACK;
    }
    return ESK_USB_STALL;
}

static void msc_configured(usbd *u, int cfg)
{
    drive_state *d = (drive_state *)u->cls_ctx;
    (void)cfg;
    d->phase = IDLE;
}

static const usbd_class msc_class = { msc_setup, msc_in, msc_out, msc_configured };

static es_status f_create(esk_comp *c)
{
    drive_state *d = (drive_state *)c->state;
    double mb = 8.0;
    (void)esk_comp_param_double(c, "capacity_mb", &mb);
    if (mb < 2.0)
        mb = 2.0;
    if (mb > 64.0)
        mb = 64.0;
    d->sectors = (uint32_t)(mb * 2048.0);
    d->disk = (uint8_t *)esk_calloc(c->e->a, d->sectors, SECTOR);
    if (!d->disk)
        return ES_ENOMEM;
    return usbd_init(&d->u, c, 0, dev_desc, cfg_desc, sizeof cfg_desc, strings, 3, &msc_class, d);
}

static void f_destroy(esk_comp *c)
{
    drive_state *d = (drive_state *)c->state;
    if (!d)
        return;
    usbd_destroy(&d->u);
    if (d->disk)
        esk_free(c->e->a, d->disk, (size_t)d->sectors * SECTOR);
    d->disk = NULL;
}

static void f_reset(esk_comp *c)
{
    drive_state *d = (drive_state *)c->state;
    int wp = 0;
    (void)esk_comp_param_bool(c, "write_protect", &wp);
    d->write_protect = wp;
    format(d);
    list_files(d);
    d->phase = IDLE;
    d->want = d->done = 0;
    sense(d, 0, 0, 0);
    d->commands = d->sectors_read = d->sectors_written = 0;
    d->last[0] = 0;
    usbd_reset(&d->u);
}

static void f_input(esk_comp *c, esk_pin *pin)
{
    drive_state *d = (drive_state *)c->state;
    (void)pin;
    usbd_power(&d->u);
}

static es_status f_set(esk_comp *c, const char *input, const esk_json *value)
{
    drive_state *d = (drive_state *)c->state;
    if (!strcmp(input, "plugged") && value->type == ESK_JSON_BOOL) {
        d->u.plugged = value->u.boolean != 0;
        usbd_power(&d->u);
        return ES_OK;
    }
    if (!strcmp(input, "write_protect") && value->type == ESK_JSON_BOOL) {
        d->write_protect = value->u.boolean != 0;
        return ES_OK;
    }
    return ES_EINVAL;
}

static void f_observe(esk_comp *c, esk_json *out)
{
    drive_state *d = (drive_state *)c->state;
    esk_alloc *a = c->e->a;
    esk_json_set_string(a, out, "state", usbd_state(&d->u));
    esk_json_set_integer(a, out, "address", d->u.addr);
    esk_json_set_string(a, out, "files", d->files);
    esk_json_set_integer(a, out, "commands", (int64_t)d->commands);
    esk_json_set_string(a, out, "last_command", d->last);
    esk_json_set_integer(a, out, "sectors_read", (int64_t)d->sectors_read);
    esk_json_set_integer(a, out, "sectors_written", (int64_t)d->sectors_written);
    esk_json_set_integer(a, out, "capacity_sectors", d->sectors);
}

const esk_model_ops esk_usb_flash_drive_ops = { "usb_flash_drive", sizeof(drive_state), f_create, f_destroy, f_reset,
                                                f_input, NULL, f_set, f_observe, NULL, 0 };
