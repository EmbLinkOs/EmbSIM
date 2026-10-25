/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* mpu.c -- the Cortex-M memory protection unit: PMSAv7 on ARMv7-M (the
 * M3, M4 and M7 when the part has one), PMSAv6 on ARMv6-M (the M0+),
 * written from the ARMv7-M and ARMv6-M Architecture Reference Manuals
 * (B3.5).
 *
 * Regions: a base (RBAR, aligned to the region's size), a size of 2^(SIZE+1)
 * bytes from 32 bytes to 4 GB, eight subregions each disabled by a bit of
 * SRD on a region of 256 bytes or more, access permissions (AP), and
 * execute-never (XN). The highest-numbered enabled region that covers an
 * address decides it; an address no region covers is refused, unless the
 * access is privileged and PRIVDEFENA puts the default memory map behind
 * the regions. The MPU does not apply:
 * - to the private peripheral bus, which always uses the default map;
 * - to vector table reads, which always use the default map;
 * - while the core runs at a priority below 0 (HardFault, NMI, FAULTMASK
 *   set) unless HFNMIENA says it should.
 *
 * Whatever the MPU says, the default map's System region (0xE0000000 up)
 * never executes, and with the MPU off neither do its Peripheral and
 * Device regions.
 *
 * A refused data access is a MemManage fault: DACCVIOL with MMFAR for an
 * instruction's access, MSTKERR while stacking an exception frame,
 * MUNSTKERR while unstacking one, MLSPERR while preserving the
 * floating-point state lazily. A refused fetch is IACCVIOL. On ARMv6-M,
 * which has no MemManage, each is a HardFault.
 *
 * TEX, S, C and B (the memory type and cacheability) are kept and read
 * back; the core has no cache, so they change nothing it can observe. */
#include "mcu/cortexm/cortexm.h"

#define MPU_TYPE 0xd90
#define MPU_CTRL 0xd94
#define MPU_RNR  0xd98
#define MPU_RBAR 0xd9c
#define MPU_RASR 0xda0

int esk_cm_default_xn(u32 a)
{
    return (a >= 0x40000000u && a < 0x60000000u) || a >= 0xA0000000u;
}

int esk_cm_access_privileged(struct esk_cm_state *cs)
{
    return !cs->force_unpriv && esk_cm_privileged(cs);
}

/* the region that decides a, or -1 */
static int region_of(const struct esk_cm_state *cs, u32 a)
{
    int i;
    for (i = cs->mpu_regions - 1; i >= 0; i--) {
        u32 rasr = cs->mpu_rasr[i], base, off;
        int bits;
        if (!(rasr & 1))
            continue;
        bits = (int)(rasr >> 1 & 31) + 1;
        if (bits < 5)
            bits = 5; /* SIZE below 4 is reserved: the 32-byte minimum */
        base = bits >= 32 ? 0 : cs->mpu_rbar[i] & ~((1u << bits) - 1);
        off = a - base;
        if (bits < 32 && off >> bits)
            continue;
        if (bits >= 8 && (rasr >> (8 + (off >> (bits - 3) & 7)) & 1))
            continue; /* its subregion is disabled */
        return i;
    }
    return -1;
}

/* whether the MPU decides this access at all */
static int mpu_applies(struct esk_cm_state *cs)
{
    if (!(cs->mpu_ctrl & 1) || cs->access == ACC_VECTOR)
        return 0;
    return esk_cm_exec_prio(cs) >= 0 || (cs->mpu_ctrl & 2);
}

/* one byte's access through the regions: 1 when refused */
static int refused(struct esk_cm_state *cs, u32 a, int write, int exec, int priv)
{
    int r;
    u32 rasr, ap;
    if (a >= 0xE0000000u && a < 0xE0100000u)
        return exec; /* the private peripheral bus: the default map */
    r = region_of(cs, a);
    if (r < 0)
        return !(priv && (cs->mpu_ctrl & 4)) || (exec && esk_cm_default_xn(a));
    rasr = cs->mpu_rasr[r];
    if (exec && (rasr >> 28 & 1))
        return 1;
    ap = rasr >> 24 & 7;
    switch (ap) {
    case 1: return !priv;
    case 2: return !priv && write;
    case 3: return 0;
    case 5: return !priv || write;
    case 6: case 7: return write;
    default: return 1; /* 0: no access; 4: reserved */
    }
}

int esk_cm_mpu_denied(struct esk_cm_state *cs, u32 a, int n, int write, int exec)
{
    int priv;
    u32 last = a + (u32)n - 1;
    if (exec && a >= 0xE0000000u)
        return 1; /* the System region never executes */
    if (!mpu_applies(cs))
        return exec && esk_cm_default_xn(a);
    priv = esk_cm_access_privileged(cs);
    if (!refused(cs, a, write, exec, priv) && (last == a || !refused(cs, last, write, exec, priv)))
        return 0;
    if (exec)
        return 1; /* the fetch's caller takes IACCVIOL */
    switch (cs->access) {
    case ACC_STACK: esk_cm_fault_raise(cs, EXC_MEM, MMFSR_MSTKERR); break;
    case ACC_UNSTACK: esk_cm_fault_raise(cs, EXC_MEM, MMFSR_MUNSTKERR); break;
    case ACC_LAZYFP: esk_cm_fault_raise(cs, EXC_MEM, MMFSR_MLSPERR); break;
    default:
        if (!cs->fault_exc) {
            esk_cm_fault_raise(cs, EXC_MEM, MMFSR_DACCVIOL | MMFSR_MMARVALID);
            cs->fault_addr = a;
            cs->fault_addr_valid = 1;
        }
        break;
    }
    return 1;
}

int esk_cm_mpu_read(struct esk_cm_state *cs, u32 off, u32 *v)
{
    u32 r = cs->mpu_rnr;
    if (off < MPU_TYPE || off > 0xdb8)
        return 0;
    if (!cs->mpu_regions) {
        *v = 0; /* no MPU: TYPE reads 0 and the rest is reserved */
        return 1;
    }
    if (off > MPU_RASR && cs->model->arch != ARCH_V7M) {
        *v = 0; /* ARMv6-M has no alias registers */
        return 1;
    }
    switch (off) {
    case MPU_TYPE: *v = (u32)cs->mpu_regions << 8; return 1;
    case MPU_CTRL: *v = cs->mpu_ctrl; return 1;
    case MPU_RNR: *v = r; return 1;
    default:
        /* RBAR, RASR and the aliases RBAR_A1..A3 and RASR_A1..A3, all of
         * the region RNR selects */
        if (r >= (u32)cs->mpu_regions) {
            *v = 0;
            return 1;
        }
        *v = (off - MPU_RBAR) & 4 ? cs->mpu_rasr[r] : (cs->mpu_rbar[r] | (r & 15));
        return 1;
    }
}

int esk_cm_mpu_write(struct esk_cm_state *cs, u32 off, u32 v)
{
    u32 r;
    if (off < MPU_TYPE || off > 0xdb8)
        return 0;
    if (!cs->mpu_regions || (off > MPU_RASR && cs->model->arch != ARCH_V7M))
        return 1;
    switch (off) {
    case MPU_TYPE: return 1;
    case MPU_CTRL:
        cs->mpu_ctrl = v & 7;
        cs->mpu_on = (int)(v & 1);
        return 1;
    case MPU_RNR:
        cs->mpu_rnr = v & 0xff;
        return 1;
    default:
        if ((off - MPU_RBAR) & 4) {
            r = cs->mpu_rnr;
            if (r < (u32)cs->mpu_regions)
                cs->mpu_rasr[r] = v & (cs->model->arch == ARCH_V7M ? 0x173fff3fu : 0x1707ff3fu);
            return 1;
        }
        /* RBAR: VALID selects the region named in its REGION field */
        if (v & 0x10)
            cs->mpu_rnr = v & 15;
        r = cs->mpu_rnr;
        if (r < (u32)cs->mpu_regions)
            cs->mpu_rbar[r] = v & ~0x1fu;
        return 1;
    }
}
