/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* st_link.c -- ST's ST-LINK/V2-1, the debugger on a NUCLEO-64 (UM1724
 * 6.2: an STM32F103CBT6 running ST's firmware), as a debugger on the
 * computer drives it.
 *
 * The probe's command interface is what its USB bulk endpoints carry: a
 * 16-byte command, then the data a write sends or a read answers. ST
 * publishes no specification of it; the commands, their layouts and their
 * answers are those of firmware V2J37M26 as OpenOCD 0.12's
 * src/jtag/drivers/stlink_usb.c sends and reads them (read, not copied:
 * OpenOCD is GPL-2.0):
 *
 *   - GET_VERSION (V2, JTAG/SWD 37, mass storage 26; 0483:374B),
 *     GET_CURRENT_MODE, DFU_EXIT, GET_TARGET_VOLTAGE (its T_VCC pin's
 *     voltage, as the probe's ADC reads it against its 1.2 V reference);
 *   - DEBUG: APIV2_ENTER with SWD (JTAG answers an error: the NUCLEO's
 *     probe is wired for SWD), EXIT, READ_IDCODES (the target's SW-DP
 *     IDCODE), SWD_SET_FREQ, DRIVE_NRST (low, high, a pulse), RESETSYS
 *     (AIRCR.SYSRESETREQ), GETSTATUS, FORCEDEBUG, RUNCORE, STEPCORE,
 *     READREG and WRITEREG (through DCRSR and DCRDR), READALLREGS,
 *     READDEBUGREG and WRITEDEBUGREG, memory reads and writes of 8, 16 and
 *     32 bits (and 32 bits without address increment), GETLASTRWSTATUS
 *     and GETLASTRWSTATUS2, READ_DAP_REG and WRITE_DAP_REG (the DP's
 *     DPIDR, CTRL/STAT, SELECT and RDBUFF; the MEM-AP's CSW, TAR, DRW,
 *     BASE and IDR), INIT_AP and CLOSE_AP_DBG; the trace commands answer
 *     an empty trace;
 *   - the status of each: 0x80 OK, 0x81 a fault, 0x09 no target answered
 *     (unpowered, or no debug port on SWDIO), an AP fault (0x11) for a
 *     transfer nothing answered.
 *
 * Its target is the MCU with a pin on the probe's SWDIO net; its accesses
 * reach the target through that MCU's MEM-AP (esk_mcu_dap_mem).
 *
 * Where the inside differs: the probe's USB transport is not simulated --
 * the session's stlink-server (core/session/stlinksrv.c) hands it each
 * command, so a host tool reaches it through stlink-server's TCP protocol,
 * not through a USB device on the host; SWD is transactions, not SWDIO and
 * SWCLK edges, and a command takes no simulated time. Not yet: its virtual
 * COM port and mass-storage drive on the same USB device (the board's
 * console bridge stands for the port), SWO trace, the firmware's own
 * upgrade (DFU), JTAG, the SWIM interface. */
#include <stdio.h>
#include <string.h>

#include "engine/engine.h"
#include "mcu/mcu.h"
#include "model/registry.h"

#define LOG_CAP 4096
#define V_MAJOR 2u
#define V_JTAG 37u
#define V_MSD 26u

enum { MODE_DFU = 0, MODE_MASS = 1, MODE_DEBUG = 2 };
enum { ST_OK = 0x80, ST_FAULT = 0x81, ST_NO_TARGET = 0x09, ST_AP_FAULT = 0x11 };
enum { P_SWDIO, P_SWCLK, P_NRST, P_TVCC, P_GND };
enum { TOK_NRST = 1 };

typedef struct sl_state {
    esk_comp *c;
    int mode;
    int swd;          /* SWD entered: the debug port powered up */
    int nrst_low;     /* the probe pulls NRST low */
    uint8_t rw_status;
    uint32_t rw_addr; /* the address of the last transfer's fault */
    uint32_t dp_select, ap_csw, ap_tar;
    unsigned divisor; /* SWD_SET_FREQ's */
    uint64_t commands;
    char log[LOG_CAP];
    size_t log_len;
} sl_state;

static void say(sl_state *s, const char *fmt, const char *a)
{
    char line[160];
    int n = snprintf(line, sizeof line, fmt, a ? a : "");
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof line)
        n = (int)sizeof line - 1;
    if (s->log_len + (size_t)n + 1 >= LOG_CAP)
        s->log_len = 0; /* the log keeps its last lines */
    memcpy(s->log + s->log_len, line, (size_t)n);
    s->log_len += (size_t)n;
    s->log[s->log_len++] = '\n';
    s->log[s->log_len] = 0;
}

static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

static esk_pin *pin(sl_state *s, int i) { return i < (int)s->c->npins ? &s->c->pins[i] : NULL; }

/* the MCU whose pin is on SWDIO's net */
static esk_comp *target(sl_state *s)
{
    esk_pin *p = pin(s, P_SWDIO);
    esk_engine *e = s->c->e;
    size_t i;
    uint32_t k;
    if (!p || !p->net)
        return NULL;
    for (i = 0; i < e->comps.len; i++) {
        esk_comp *c = e->comps.data[i];
        if (c == s->c || !esk_mcu_is(c))
            continue;
        for (k = 0; k < c->npins; k++)
            if (c->pins[k].net == p->net)
                return c;
    }
    return NULL;
}

/* T_VCC as the probe's divider and ADC see it */
static double target_volts(sl_state *s)
{
    esk_pin *p = pin(s, P_TVCC);
    if (!p || !p->net)
        return 0.0;
    if (p->net->island)
        return p->net->voltage;
    if (p->net->is_supply)
        return p->net->supply_v;
    return esk_pin_level(p) == ESK_L1 ? p->model.nominal_v : 0.0;
}

static int powered(sl_state *s) { return target_volts(s) >= 1.65; }

/* a MEM-AP transfer, its status kept for GETLASTRWSTATUS */
static int mem(sl_state *s, uint32_t addr, int bytes, int write, uint32_t *v)
{
    esk_comp *t = target(s);
    int r = t && powered(s) ? esk_mcu_dap_mem(t, addr, bytes, write, v) : ESK_DAP_NO_TARGET;
    if (r != ESK_DAP_OK) {
        if (s->rw_status == ST_OK) {
            s->rw_status = r == ESK_DAP_NO_TARGET ? ST_NO_TARGET : ST_AP_FAULT;
            s->rw_addr = addr;
        }
        if (!write)
            *v = 0;
        return 0;
    }
    return 1;
}

static void nrst(sl_state *s, int low)
{
    esk_pin *p = pin(s, P_NRST);
    s->nrst_low = low;
    if (!p)
        return;
    if (low)
        esk_pin_drive(p, ESK_L0, ESK_STR_STRONG, 0);
    else
        esk_pin_release(p);
}

/* the MEM-AP's registers as READ_DAP_REG and WRITE_DAP_REG reach them */
static uint8_t dap_reg(sl_state *s, uint16_t port, uint16_t addr, int write, uint32_t *v)
{
    esk_comp *t = target(s);
    if (!t || !powered(s) || !s->swd)
        return ST_NO_TARGET;
    if (port == 0xffffu) { /* the DP */
        switch (addr & 0xcu) {
        case 0x0: if (!write) *v = esk_mcu_dap_idcode(t); return ST_OK;
        case 0x4: if (!write) *v = 0xF0000040u; return ST_OK; /* the power-up requests acknowledged */
        case 0x8: if (write) s->dp_select = *v; else *v = 0; return ST_OK;
        default: if (!write) *v = 0; return ST_OK;
        }
    }
    if (port != 0)
        return ST_FAULT; /* one AP, the MEM-AP 0 */
    switch (addr & 0xfcu) {
    case 0x00: if (write) s->ap_csw = *v; else *v = s->ap_csw | 0x03000040u; return ST_OK;
    case 0x04: if (write) s->ap_tar = *v; else *v = s->ap_tar; return ST_OK;
    case 0x0c: {
        int size = (int)(s->ap_csw & 7u), bytes = size == 0 ? 1 : size == 1 ? 2 : 4;
        uint32_t lane = s->ap_tar & 3u, w = write ? *v >> (8u * lane) : 0;
        if (!mem(s, s->ap_tar, bytes, write, &w))
            return ST_AP_FAULT;
        if (!write)
            *v = w << (8u * lane);
        if ((s->ap_csw >> 4 & 3u) == 1u) /* AddrInc single */
            s->ap_tar += (uint32_t)bytes;
        return ST_OK;
    }
    case 0xf8: if (!write) *v = 0xE00FF003u; return ST_OK; /* BASE: the ROM table, present */
    case 0xfc: if (!write) *v = esk_mcu_dap_idcode(t) == 0x0BC11477u ? 0x04770031u : 0x24770011u; return ST_OK;
    }
    if (!write)
        *v = 0;
    return ST_OK;
}

/* DCRSR's regsel through the core's debug registers */
static int core_reg(sl_state *s, unsigned regsel, int write, uint32_t *v)
{
    uint32_t sel = regsel & 0x7fu, dhcsr = 0;
    int i;
    if (write) {
        if (!mem(s, 0xE000EDF8u, 4, 1, v))
            return 0;
        sel |= 1u << 16;
    }
    if (!mem(s, 0xE000EDF4u, 4, 1, &sel))
        return 0;
    for (i = 0; i < 4 && !(dhcsr & (1u << 16)); i++) /* S_REGRDY */
        if (!mem(s, 0xE000EDF0u, 4, 0, &dhcsr))
            return 0;
    return write ? 1 : mem(s, 0xE000EDF8u, 4, 0, v);
}

size_t esk_stlink_command(esk_comp *c, const uint8_t cmd[16], const uint8_t *out, size_t nout, uint8_t *in, size_t cap)
{
    sl_state *s = (sl_state *)c->state;
    uint8_t r[96];
    size_t n = 0, i;
    uint32_t v = 0;
    memset(r, 0, sizeof r);
    s->commands++;
    if (cmd[0] == 0xF1) { /* GET_VERSION */
        uint16_t ver = (uint16_t)(V_MAJOR << 12 | V_JTAG << 6 | V_MSD);
        r[0] = (uint8_t)(ver >> 8);
        r[1] = (uint8_t)ver;
        r[2] = 0x83;
        r[3] = 0x04;
        r[4] = 0x4B;
        r[5] = 0x37;
        n = 6;
    } else if (cmd[0] == 0xF5) { /* GET_CURRENT_MODE */
        r[0] = (uint8_t)s->mode;
        n = 2;
    } else if (cmd[0] == 0xF3) { /* DFU */
        if (cmd[1] == 0x07 && s->mode == MODE_DFU)
            s->mode = MODE_MASS;
    } else if (cmd[0] == 0xF7) { /* GET_TARGET_VOLTAGE: two ADC counts, 1.2 V and T_VCC / 2 */
        double tv = target_volts(s);
        put32(r, 1489u);
        put32(r + 4, (uint32_t)(tv / 2.0 / 1.2 * 1489.0 + 0.5));
        n = 8;
    } else if (cmd[0] == 0xF2) {
        uint8_t st = ST_OK;
        switch (cmd[1]) {
        case 0x21: /* EXIT */
            if (s->mode == MODE_DEBUG)
                say(s, "debug mode left%s", "");
            s->mode = MODE_MASS;
            s->swd = 0;
            return 0;
        case 0x30: /* APIV2_ENTER */
            if (cmd[2] != 0xA3) {
                st = ST_FAULT; /* JTAG: not on this probe's connector */
            } else if (!target(s) || !powered(s)) {
                st = ST_NO_TARGET;
                say(s, "SWD: no target answers%s", "");
            } else {
                s->mode = MODE_DEBUG;
                s->swd = 1;
                s->ap_csw = 0x23000002u;
                say(s, "SWD entered: %s", target(s)->id);
            }
            r[0] = st;
            n = 2;
            break;
        case 0x31: /* READ_IDCODES */
            r[0] = s->swd && target(s) ? ST_OK : ST_NO_TARGET;
            put32(r + 4, s->swd && target(s) ? esk_mcu_dap_idcode(target(s)) : 0);
            n = 12;
            break;
        case 0x22: /* READCOREID (API v1) */
            put32(r, s->swd && target(s) ? esk_mcu_dap_idcode(target(s)) : 0);
            n = 4;
            break;
        case 0x43: /* SWD_SET_FREQ */
        case 0x44: /* JTAG_SET_FREQ */
            s->divisor = cmd[2];
            r[0] = ST_OK;
            n = 2;
            break;
        case 0x3C: /* DRIVE_NRST */
            if (cmd[2] == 0)
                nrst(s, 1);
            else if (cmd[2] == 1)
                nrst(s, 0);
            else {
                nrst(s, 1);
                (void)esk_comp_wake_at(c, esk_comp_now(c) + 2u * ES_PS_PER_MS, TOK_NRST);
            }
            say(s, cmd[2] == 0 ? "NRST low%s" : cmd[2] == 1 ? "NRST released%s" : "NRST pulsed%s", "");
            r[0] = ST_OK;
            n = 2;
            break;
        case 0x32: /* RESETSYS: AIRCR.SYSRESETREQ */
        case 0x03:
            v = 0x05FA0004u;
            s->rw_status = ST_OK;
            r[0] = mem(s, 0xE000ED0Cu, 4, 1, &v) ? ST_OK : ST_FAULT;
            n = 2;
            break;
        case 0x01: /* GETSTATUS */
            s->rw_status = ST_OK;
            r[0] = mem(s, 0xE000EDF0u, 4, 0, &v) && (v & (1u << 17)) ? 0x81 : 0x80;
            n = 2;
            break;
        case 0x02: /* FORCEDEBUG */
        case 0x09: /* RUNCORE */
        case 0x0A: /* STEPCORE */
            v = cmd[1] == 0x02 ? 0xA05F0003u : cmd[1] == 0x09 ? 0xA05F0001u : 0xA05F000Du;
            s->rw_status = ST_OK;
            r[0] = mem(s, 0xE000EDF0u, 4, 1, &v) ? ST_OK : ST_FAULT;
            n = 2;
            break;
        case 0x33: /* READREG */
            s->rw_status = ST_OK;
            r[0] = core_reg(s, cmd[2], 0, &v) ? ST_OK : ST_FAULT;
            put32(r + 4, v);
            n = 8;
            break;
        case 0x34: /* WRITEREG */
            v = le32(cmd + 3);
            s->rw_status = ST_OK;
            r[0] = core_reg(s, cmd[2], 1, &v) ? ST_OK : ST_FAULT;
            n = 2;
            break;
        case 0x3A: { /* READALLREGS: R0-R15, xPSR, MSP, PSP, the special registers' word */
            static const unsigned sel[20] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20 };
            int ok = 1;
            s->rw_status = ST_OK;
            for (i = 0; i < 20; i++) {
                v = 0;
                ok = core_reg(s, sel[i], 0, &v) && ok;
                put32(r + 4 + 4 * i, v);
            }
            r[0] = ok ? ST_OK : ST_FAULT;
            n = 88;
            break;
        }
        case 0x35: /* WRITEDEBUGREG */
        case 0x0F:
            v = le32(cmd + 6);
            s->rw_status = ST_OK;
            r[0] = mem(s, le32(cmd + 2), 4, 1, &v) ? ST_OK : ST_FAULT;
            n = 2;
            break;
        case 0x36: /* READDEBUGREG */
            s->rw_status = ST_OK;
            r[0] = mem(s, le32(cmd + 2), 4, 0, &v) ? ST_OK : ST_FAULT;
            put32(r + 4, v);
            n = 8;
            break;
        case 0x3B: /* GETLASTRWSTATUS */
            r[0] = s->rw_status;
            n = 2;
            break;
        case 0x3E: /* GETLASTRWSTATUS2 */
            r[0] = s->rw_status;
            put32(r + 4, s->rw_addr);
            n = 12;
            break;
        case 0x45: /* READ_DAP_REG */
            r[0] = dap_reg(s, le16(cmd + 2), le16(cmd + 4), 0, &v);
            put32(r + 4, v);
            n = 8;
            break;
        case 0x46: /* WRITE_DAP_REG */
            v = le32(cmd + 6);
            r[0] = dap_reg(s, le16(cmd + 2), le16(cmd + 4), 1, &v);
            n = 2;
            break;
        case 0x4B: /* INIT_AP */
        case 0x4C: /* CLOSE_AP_DBG */
            r[0] = cmd[2] == 0 ? ST_OK : ST_FAULT;
            n = 2;
            break;
        case 0x40: /* START_TRACE_RX */
        case 0x41: /* STOP_TRACE_RX */
            r[0] = ST_OK;
            n = 2;
            break;
        case 0x42: /* GET_TRACE_NB: nothing traced */
            n = 2;
            break;
        case 0x07: /* READMEM_32BIT */
        case 0x54: /* READMEM_32BIT_NO_ADDR_INC */
        case 0x47: /* READMEM_16BIT */
        case 0x0C: { /* READMEM_8BIT */
            uint32_t a = le32(cmd + 2), len = le16(cmd + 6);
            int bytes = cmd[1] == 0x0C ? 1 : cmd[1] == 0x47 ? 2 : 4;
            s->rw_status = ST_OK;
            if (len > cap)
                len = (uint32_t)cap;
            for (i = 0; i + (size_t)bytes <= len; i += (size_t)bytes) {
                uint32_t at = cmd[1] == 0x54 ? a : a + (uint32_t)i;
                (void)mem(s, at, bytes, 0, &v);
                in[i] = (uint8_t)v;
                if (bytes >= 2)
                    in[i + 1] = (uint8_t)(v >> 8);
                if (bytes == 4) {
                    in[i + 2] = (uint8_t)(v >> 16);
                    in[i + 3] = (uint8_t)(v >> 24);
                }
            }
            /* one byte read answers two */
            if (bytes == 1 && len == 1 && cap >= 2) {
                in[1] = 0;
                return 2;
            }
            return len;
        }
        case 0x08: /* WRITEMEM_32BIT */
        case 0x50: /* WRITEMEM_32BIT_NO_ADDR_INC */
        case 0x48: /* WRITEMEM_16BIT */
        case 0x0D: { /* WRITEMEM_8BIT */
            uint32_t a = le32(cmd + 2), len = le16(cmd + 6);
            int bytes = cmd[1] == 0x0D ? 1 : cmd[1] == 0x48 ? 2 : 4;
            s->rw_status = ST_OK;
            if (len > nout)
                len = (uint32_t)nout;
            for (i = 0; i + (size_t)bytes <= len; i += (size_t)bytes) {
                uint32_t at = cmd[1] == 0x50 ? a : a + (uint32_t)i;
                v = out[i];
                if (bytes >= 2)
                    v |= (uint32_t)out[i + 1] << 8;
                if (bytes == 4)
                    v |= (uint32_t)out[i + 2] << 16 | (uint32_t)out[i + 3] << 24;
                (void)mem(s, at, bytes, 1, &v);
            }
            return 0;
        }
        default:
            r[0] = ST_FAULT;
            n = 2;
            break;
        }
    } else {
        n = 0; /* not a command this firmware answers */
    }
    if (n > cap)
        n = cap;
    memcpy(in, r, n);
    return n;
}

int esk_stlink_is(const esk_comp *c) { return c && c->ops == &esk_st_link_ops; }

const char *esk_stlink_serial(esk_comp *c) { return esk_comp_param_str(c, "serial", "066DFF495051717867152438"); }

static es_status sl_create(esk_comp *c)
{
    sl_state *s = (sl_state *)c->state;
    s->c = c;
    return ES_OK;
}

static void sl_reset(esk_comp *c)
{
    sl_state *s = (sl_state *)c->state;
    s->mode = MODE_MASS;
    s->swd = 0;
    s->nrst_low = 0;
    s->rw_status = ST_OK;
    s->rw_addr = 0;
    s->dp_select = s->ap_csw = s->ap_tar = 0;
    s->divisor = 0;
    s->commands = 0;
    s->log_len = 0;
    s->log[0] = 0;
    if (pin(s, P_NRST))
        esk_pin_release(pin(s, P_NRST));
}

static void sl_on_time(esk_comp *c, uint32_t token)
{
    sl_state *s = (sl_state *)c->state;
    if (token == TOK_NRST)
        nrst(s, 0); /* the pulse ends */
}

static void sl_observe(esk_comp *c, esk_json *o)
{
    sl_state *s = (sl_state *)c->state;
    esk_alloc *a = c->e->a;
    esk_comp *t = target(s);
    esk_json_set_string(a, o, "mode", s->mode == MODE_DEBUG ? "debug" : s->mode == MODE_DFU ? "dfu" : "mass");
    esk_json_set_bool(a, o, "swd", s->swd);
    esk_json_set_string(a, o, "target", t ? t->id : "");
    esk_json_set_number(a, o, "target_voltage", target_volts(s));
    esk_json_set_bool(a, o, "nrst_low", s->nrst_low);
    esk_json_set_integer(a, o, "commands", (int64_t)s->commands);
    esk_json_set_string(a, o, "log", s->log);
}

const esk_model_ops esk_st_link_ops = {
    "st_link", sizeof(sl_state), sl_create, NULL, sl_reset, NULL, sl_on_time, NULL, sl_observe, NULL, 0,
};
