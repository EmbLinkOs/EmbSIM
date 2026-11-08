#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Assemble test_lx6.c's programs with Espressif's Xtensa assembler and
write lx6_programs.h, so the test needs no toolchain.

    gen_lx6_programs.py --as xtensa-esp32-elf-as --objcopy xtensa-esp32-elf-objcopy

Each program is linked at 0x40080000 (IRAM), its data at 0x3FFB0000.
The sources are the PROGRAMS below; their expected results are in
test_lx6.c, computed by hand from the Xtensa ISA reference."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

PROGRAMS = {
    # integer arithmetic and logic: results left in a2-a15
    "alu": """
    movi a2, 7
    movi a3, -3
    add a4, a2, a3          # 4
    addx4 a5, a2, a3        # 25
    subx8 a6, a2, a3        # 59
    neg a7, a3              # 3
    abs a8, a3              # 3
    min a9, a2, a3          # -3
    maxu a10, a2, a3        # 0xfffffffd
    sext a11, a3, 7         # -3
    movi a12, 300
    clamps a12, a12, 7      # 127
    nsau a13, a2            # 29
    nsa a14, a3             # 29
    extui a15, a3, 4, 8     # 0xff
    j .
""",
    "shifts": """
    movi a2, 0x123
    slli a3, a2, 20         # 0x12300000
    srli a4, a3, 8          # 0x00123000
    movi a5, -256
    srai a6, a5, 4          # -16
    ssl a2                  # shift left by a2 & 31 = 3
    sll a7, a2              # 0x918
    movi a8, 12
    ssr a8
    srl a9, a3              # 0x00012300
    sra a10, a5             # -1
    movi a11, 0x87654321
    ssai 8
    src a12, a11, a2        # (0x87654321:0x123) >> 8 = 0x21000001
    ssa8l a8                # sar = (12 & 3) * 8 = 0
    rsr.sar a13             # 0
    ssa8b a2                # sar = 32 - (0x123 & 3) * 8 = 8
    rsr.sar a14             # 8
    j .
""",
    "muldiv": """
    movi a2, -7
    movi a3, 3
    mull a4, a2, a3         # -21
    muluh a5, a2, a3        # 2
    mulsh a6, a2, a3        # -1
    quos a7, a2, a3         # -2
    rems a8, a2, a3         # -1
    quou a9, a2, a3         # 0x55555553
    remu a10, a2, a3        # 0
    mul16u a11, a2, a3      # 0xfff9 * 3 = 0x2ffeb
    mul16s a12, a2, a3      # -21
    movi a13, 0
    j .
""",
    # a loop exited by a branch taken to LEND (the ROM's strcmp does it)
    "loops": """
    movi a2, 0
    movi a3, 5
    loop a3, 1f
    addi a2, a2, 1
1:
    movi a4, 0
    movi a5, 0
    loopnez a5, 2f
    addi a4, a4, 1
2:
    movi a6, 0
    movi a7, 10
    movi a8, 0
    loop a8, 3f             # LCOUNT 0xffffffff: only the branch leaves
    addi a6, a6, 1
    addi a7, a7, -1
    beqz a7, 3f
3:
    movi a9, 0
    movi a10, -2
    loopgtz a10, 4f
    addi a9, a9, 1
4:
    j .
""",
    "branches": """
    movi a2, 0
    movi a3, 0x80
    bbci a3, 7, 1f
    addi a2, a2, 1          # taken path skips nothing: bit 7 is set
1:  bbsi a3, 7, 2f
    addi a2, a2, 16         # not reached
2:  movi a4, 65536
    bltui a4, 32768, 3f
    addi a2, a2, 2          # reached: 65536 >= 32768
3:  bgeui a4, 65536, 4f
    addi a2, a2, 32         # not reached
4:  movi a5, -1
    beqi a5, -1, 5f
    addi a2, a2, 64         # not reached
5:  movi a6, 0xf0
    movi a7, 0x30
    ball a6, a7, 6f
    addi a2, a2, 128        # not reached
6:  bnone a6, a7, 7f
    addi a2, a2, 4          # reached
7:  movi a8, 5
    bbs a3, a8, 8f          # bit 5 of 0x80: clear
    addi a2, a2, 8          # reached
8:  j .
""",
    "memory": """
    j 1f
    .align 4
lit: .word 0xcafef00d
1:
    movi a2, 0x3ffb0000
    movi a3, 0x8081
    s16i a3, a2, 0
    l16si a4, a2, 0         # 0xffff8081
    l16ui a5, a2, 0         # 0x8081
    l8ui a6, a2, 1          # 0x80
    movi a7, 0x11223344
    s32i a7, a2, 4
    addi a15, a2, 3
    l32i a8, a15, 0         # unaligned: bytes 3..6, 0x22334400
    movi a9, 0x55
    wsr.scompare1 a9
    movi a10, 0x55
    s32i a10, a2, 8
    movi a11, 0x66
    s32c1i a11, a2, 8       # memory 0x55 == SCOMPARE1: stored; a11 = 0x55
    l32i a12, a2, 8         # 0x66
    movi a13, 0x77
    s32c1i a13, a2, 8       # 0x66 != 0x55: not stored; a13 = 0x66
    l32r a14, lit
    j .
""",
    # windowed calls deep enough to spill and fill through the ROM's vectors
    "windows": """
    movi a1, 0x3ffb8000
    movi a3, 0x3ffb8100     # the outermost frame's caller's stack pointer
    addi a4, a1, -16
    s32i a3, a4, 4          # at sp - 12, for WindowOverflow8 to find its save area
    movi a10, 12
    call8 fib
    mov a15, a10
    j .
    .align 4
fib:
    entry a1, 32
    bltui a2, 2, 1f
    addi a10, a2, -1
    call8 fib
    mov a3, a10
    addi a10, a2, -2
    call8 fib
    add a2, a3, a10
1:  retw
""",
    # an exception and its return, through a vector table of the test's
    "syscall": """
    movi a2, vectors
    wsr.vecbase a2
    movi a3, 0
    syscall
    addi a3, a3, 1
    j .
    .align 1024
vectors:
    .org vectors + 0x300
kernel:
    rsr.exccause a4
    rsr.epc1 a5
    addi a5, a5, 3
    wsr.epc1 a5
    rfe
""",
    # CCOMPARE0 against CCOUNT: the timer interrupt (6) at level 1
    "timer": """
    movi a2, vectors
    wsr.vecbase a2
    movi a6, 0
    rsr.ccount a3
    addi a3, a3, 200
    wsr.ccompare0 a3
    movi a4, 1 << 6
    wsr.intenable a4
    rsil a5, 0
1:  beqz a6, 1b
    j .
    .align 1024
vectors:
    .org vectors + 0x300
kernel:
    rsr.exccause a7
    rsr.interrupt a8
    movi a6, 1
    movi a9, 0
    wsr.intenable a9
    rfe
""",
    "float": """
    movi a2, 1
    wsr.cpenable a2
    rsync
    movi a3, 3
    float.s f0, a3, 0        # 3.0
    movi a4, 5
    float.s f1, a4, 1        # 2.5
    add.s f2, f0, f1         # 5.5
    mul.s f3, f0, f1         # 7.5
    madd.s f2, f0, f1        # 5.5 + 7.5 = 13.0
    trunc.s a5, f2, 0        # 13
    round.s a6, f1, 0        # 2 (ties to even)
    olt.s b0, f1, f0
    movi a7, 0
    movt a7, a4, b0          # 5: 2.5 < 3.0
    rfr a8, f3               # 0x40f00000
    j .
""",
    "mac16": """
    movi a2, 0x00030004
    movi a3, 0xfffe0005
    mul.aa.ll a2, a3         # 4 * 5 = 20
    mula.aa.hh a2, a3        # + 3 * -2 = 14
    rsr.acclo a4
    rsr.acchi a5
    umul.aa.hl a2, a3        # 3 * 5 = 15
    rsr.acclo a6
    j .
""",
}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--as", dest="asm", default="xtensa-esp32-elf-as")
    ap.add_argument("--ld", default="xtensa-esp32-elf-ld")
    ap.add_argument("--objcopy", default="xtensa-esp32-elf-objcopy")
    ap.add_argument("--nm", default="xtensa-esp32-elf-nm")
    ap.add_argument("-o", "--output", type=Path, default=HERE / "lx6_programs.h")
    args = ap.parse_args(argv)
    out = ["/* SPDX-License-Identifier: Apache-2.0 */",
           "/* Copyright 2026 Junior Deogracias */",
           "/* lx6_programs.h -- test_lx6.c's programs, assembled by Espressif's",
           " * xtensa-esp32-elf-as and linked at 0x40080000 by gen_lx6_programs.py",
           " * (generated: do not edit; the sources are in the script). */",
           "#ifndef LX6_PROGRAMS_H", "#define LX6_PROGRAMS_H", ""]
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        for name, src in PROGRAMS.items():
            s = t / f"{name}.S"
            s.write_text(".text\n.literal_position\n.global _start\n_start:\n" + src, encoding="utf-8")
            subprocess.run([args.asm, "--text-section-literals", "-o", str(t / f"{name}.o"), str(s)], check=True)
            subprocess.run([args.ld, "-Ttext=0x40080000", "-o", str(t / f"{name}.elf"), str(t / f"{name}.o")],
                           check=True)
            subprocess.run([args.objcopy, "-O", "binary", "-j", ".text", str(t / f"{name}.elf"),
                            str(t / f"{name}.bin")], check=True)
            data = (t / f"{name}.bin").read_bytes()
            syms = subprocess.run([args.nm, str(t / f"{name}.elf")], check=True, capture_output=True,
                                  text=True).stdout.split("\n")
            entry = next(int(x.split()[0], 16) for x in syms if x.endswith(" _start"))
            out.append(f"#define PROG_{name.upper()}_ENTRY 0x{entry:08X}u")
            out.append(f"static const unsigned char prog_{name}[{len(data)}] = {{")
            for i in range(0, len(data), 16):
                out.append("    " + " ".join(f"0x{b:02x}," for b in data[i:i + 16]))
            out.append("};")
            out.append("")
    out += ["#endif", ""]
    args.output.write_text("\n".join(out), encoding="utf-8")
    print(f"{args.output}: {len(PROGRAMS)} programs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
