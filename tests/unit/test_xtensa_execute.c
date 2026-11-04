/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "mcu/xtensa/execute.h"

typedef struct memory { unsigned char data[512]; unsigned reads, writes, fail_read, fail_write; } memory;
static es_status read_mem(void *ctx, uint32_t addr, unsigned bytes, uint32_t *value)
{
    memory *m = (memory *)ctx; unsigned i;
    m->reads++;
    if (m->fail_read || addr > sizeof m->data || bytes > sizeof m->data - addr) return ES_EINVAL;
    *value = 0;
    for (i = 0; i < bytes; i++) *value |= (uint32_t)m->data[addr+i] << (8*i);
    return ES_OK;
}
static es_status write_mem(void *ctx, uint32_t addr, unsigned bytes, uint32_t value)
{
    memory *m = (memory *)ctx; unsigned i;
    m->writes++;
    if (m->fail_write || addr > sizeof m->data || bytes > sizeof m->data - addr) return ES_EINVAL;
    for (i = 0; i < bytes; i++) m->data[addr+i] = (unsigned char)((value >> (8*i)) & 255u);
    return ES_OK;
}
static es_status execute(esk_xtensa_state *s, esk_xtensa_memory *mem, uint32_t word, unsigned length, esk_xtensa_fault *f)
{
    unsigned char code[3]; size_t consumed = 99; es_status st;
    code[0] = (unsigned char)(word & 255u); code[1] = (unsigned char)((word >> 8) & 255u);
    code[2] = (unsigned char)((word >> 16) & 255u);
    st = esk_xtensa_execute(s, mem, code, length, &consumed, f);
    CHECK_EQ(consumed, st == ES_OK ? length : 0);
    return st;
}
static void program(void)
{
    /* GNU binutils-assembled mixed-width summing loop and CALL0/RET.N.
     * movi a2,256; movi.n a3,4; movi.n a4,0;
     * loop: l32i.n a5,a2,0; add.n a4,a4,a5; addi.n a2,a2,4;
     * addi.n a3,a3,-1; bnez a3,loop; call0 twice; j done;
     * twice: add.n a4,a4,a4; ret.n; done: nop.n */
    static const unsigned char code[] = {
        0x22,0xa1,0, 0x0c,0x43, 0x0c,0x04, 0x58,0x02, 0x5a,0x44,
        0x4b,0x22, 0x0b,0x33, 0x56,0x43,0xff, 0x45,0,0,
        0xc6,0,0, 0x4a,0x44, 0x0d,0xf0, 0x3d,0xf0
    };
    memory m = {{0},0,0,0,0}; esk_xtensa_memory bus = {&m,read_mem,write_mem,NULL};
    esk_xtensa_state s = {{0},0}; esk_xtensa_fault f; unsigned i, steps = 0;
    memcpy(m.data, code, sizeof code);
    for (i = 0; i < 4; i++) m.data[256+i*4] = (unsigned char)(i+1);
    while (s.pc != 28 && steps < 40) { CHECK_EQ(esk_xtensa_step(&s,&bus,&f), ES_OK); steps++; }
    CHECK_EQ(steps,27); CHECK_EQ(s.pc,28); CHECK_EQ(s.a[4],20); CHECK_EQ(s.a[2],272);
    CHECK_EQ(s.a[3],0); CHECK_EQ(s.a[0],21); CHECK_EQ(m.writes,0);
    CHECK_EQ(esk_xtensa_step(&s,&bus,&f),ES_OK); CHECK_EQ(s.pc,30);
}
static void memory_and_faults(void)
{
    memory m = {{0},0,0,0,0}; esk_xtensa_memory bus = {&m,read_mem,write_mem,NULL};
    esk_xtensa_state s = {{0},0}, before; esk_xtensa_fault f;
    s.a[2]=UINT32_C(0x89abcdef); s.a[3]=256;
    CHECK_EQ(execute(&s,&bus,0x006322,3,&f),ES_OK); /* s32i a2,a3,0 */
    CHECK_EQ(m.data[256],0xef); CHECK_EQ(m.data[259],0x89);
    s.a[2]=0; CHECK_EQ(execute(&s,&bus,0x002322,3,&f),ES_OK); CHECK_EQ(s.a[2],UINT32_C(0x89abcdef));
    CHECK_EQ(execute(&s,&bus,0x009322,3,&f),ES_OK); CHECK_EQ(s.a[2],UINT32_C(0xffffcdef));
    CHECK_EQ(execute(&s,&bus,0x001322,3,&f),ES_OK); CHECK_EQ(s.a[2],0xcdef);
    CHECK_EQ(execute(&s,&bus,0x000322,3,&f),ES_OK); CHECK_EQ(s.a[2],0xef);
    s.a[2]=UINT32_C(0x12345678); CHECK_EQ(execute(&s,&bus,0x004322,3,&f),ES_OK);
    CHECK_EQ(m.data[256],0x78); CHECK_EQ(m.data[257],0xcd);
    CHECK_EQ(execute(&s,&bus,0x005322,3,&f),ES_OK); CHECK_EQ(m.data[257],0x56); CHECK_EQ(m.data[258],0xab);
    /* A failed/unaligned store changes neither state nor memory. */
    s.a[3]=257; before=s; { unsigned writes=m.writes;
        CHECK_EQ(execute(&s,&bus,0x006322,3,&f),ES_EINVAL);
        CHECK_EQ(f.kind,ESK_XT_ALIGN); CHECK_EQ(f.address,257); CHECK_EQ(f.pc,before.pc);
        CHECK_EQ(memcmp(&s,&before,sizeof s),0); CHECK_EQ(m.writes,writes);
    }
    s.a[3]=256; before=s; m.fail_write=1;
    CHECK_EQ(execute(&s,&bus,0x006322,3,&f),ES_EINVAL); CHECK_EQ(f.kind,ESK_XT_STORE);
    CHECK_EQ(memcmp(&s,&before,sizeof s),0); CHECK_EQ(m.data[256],0x78);
    m.fail_read=1; CHECK_EQ(execute(&s,&bus,0x002322,3,&f),ES_EINVAL);
    CHECK_EQ(f.kind,ESK_XT_LOAD); CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    m.fail_read=0; m.fail_write=0;
    CHECK_EQ(execute(&s,&bus,0,3,&f),ES_EUNSUPPORTED); CHECK_EQ(f.kind,ESK_XT_UNSUPPORTED);
    CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    /* Fetch compact instruction at the memory edge, without a third read. */
    m.data[510]=0x3d; m.data[511]=0xf0; s.pc=510;
    CHECK_EQ(esk_xtensa_step(&s,&bus,&f),ES_OK); CHECK_EQ(s.pc,512);
    before=s; CHECK_EQ(esk_xtensa_step(&s,&bus,&f),ES_EINVAL);
    CHECK_EQ(f.kind,ESK_XT_FETCH); CHECK_EQ(f.address,512); CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    /* CALLX0 a0 reads the old link register before replacing it. */
    s.pc=7; s.a[0]=100; CHECK_EQ(execute(&s,&bus,0x0000c0,3,&f),ES_OK);
    CHECK_EQ(s.pc,100); CHECK_EQ(s.a[0],10);
    /* L32R uses aligned PC+3 and a one-extended 16-bit displacement. */
    s.pc=260; m.data[256]=0x91;
    CHECK_EQ(execute(&s,&bus,0xffff21,3,&f),ES_OK); CHECK_EQ(s.a[2],UINT32_C(0x89ab5691));
    s.pc=UINT32_C(0xfffffffe);
    CHECK_EQ(execute(&s,&bus,0x000006,3,&f),ES_OK); CHECK_EQ(s.pc,2);
}
static void branches_and_density(void)
{
    /* Assembled branches to 0x60; each pair specifies taken/not-taken inputs. */
    static const struct branch { uint32_t code, pc, yes_s, yes_t, no_s, no_t; unsigned bytes; } table[] = {
        {0x024216,0x38,0,0,1,0,3}, {0x021356,0x3b,1,0,0,0,3},
        {0x01e496,0x3e,0x80000000u,0,0,0,3}, {0x01b5d6,0x41,0,0,0xffffffffu,0,3},
        {0x181237,0x44,7,7,7,8,3}, {0x159237,0x47,7,8,7,7,3},
        {0x122237,0x4a,0x80000000u,0,0,0x80000000u,3},
        {0x0fa237,0x4d,0,0x80000000u,0x80000000u,0,3},
        {0x0c3237,0x50,0,0xffffffffu,0xffffffffu,0,3},
        {0x09b237,0x53,0xffffffffu,0,0,0xffffffffu,3},
        {0x628c,0x56,0,0,1,0,2}, {0x43cc,0x58,1,0,0,0,2}
    };
    size_t i; unsigned taken;
    for (i=0; i<sizeof table/sizeof table[0]; i++) {
        const struct branch *b=&table[i];
        for (taken=0; taken<2; taken++) {
            esk_xtensa_state s={{0},0}; esk_xtensa_fault f;
            unsigned reg=(b->code>>8)&15u;
            s.pc=b->pc; s.a[reg]=taken ? b->yes_s : b->no_s;
            if ((b->code&15u)==7) s.a[(b->code>>4)&15u]=taken ? b->yes_t : b->no_t;
            CHECK_EQ(execute(&s,NULL,b->code,b->bytes,&f),ES_OK);
            CHECK_EQ(s.pc,taken ? 0x60 : b->pc+b->bytes); CHECK_EQ(f.kind,ESK_XT_OK);
        }
    }
    /* The MOVI.N range is asymmetric: -32..95, not -64..63. */
    { int value;
        for (value=-32; value<=95; value++) {
            esk_xtensa_state s={{0},0}; esk_xtensa_fault f;
            uint32_t imm=(uint32_t)value&127u;
            uint32_t code=12u | (imm>>4)<<4 | 2u<<8 | (imm&15u)<<12;
            CHECK_EQ(execute(&s,NULL,code,2,&f),ES_OK); CHECK_EQ(s.a[2],(uint32_t)value);
        }
    }
    /* Calls using windows are not silently interpreted as CALL0. */
    { esk_xtensa_state s={{0},0}, before=s; esk_xtensa_fault f;
        CHECK_EQ(execute(&s,NULL,0x000015,3,&f),ES_EUNSUPPORTED);
        CHECK_EQ(memcmp(&s,&before,sizeof s),0);
        CHECK_EQ(execute(&s,NULL,0xf01d,2,&f),ES_EUNSUPPORTED); /* RETW.N */
        CHECK_EQ(memcmp(&s,&before,sizeof s),0);
    }
}
static void branch_immediates(void)
{
    static const struct { uint32_t code, yes, no; } cases[] = {
        {0x041226,1,0}, {0x040266,0,UINT32_MAX},
        {0x0402a6,0x80000000u,0}, {0x04f2e6,256,255},
        {0x0402b6,32767,32768}, {0x0412f6,65536,65535}
    };
    size_t i; unsigned taken;
    for (i=0;i<sizeof cases/sizeof cases[0];i++) for (taken=0;taken<2;taken++) {
        esk_xtensa_state s={{0},100}; esk_xtensa_fault f;
        s.a[2]=taken ? cases[i].yes : cases[i].no;
        CHECK_EQ(execute(&s,NULL,cases[i].code,3,&f),ES_OK);
        CHECK_EQ(s.pc,taken ? 108u : 103u);
    }
}
TEST_MAIN_BEGIN
    (void)t_str_eq;
    RUN(program); RUN(memory_and_faults); RUN(branches_and_density); RUN(branch_immediates);
TEST_MAIN_END
