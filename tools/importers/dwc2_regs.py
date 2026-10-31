#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The register block of an STM32's USB OTG FS core (Synopsys DWC2), for a
register description (SPEC-006 §5).

ST's SVDs for the F446 carry the F40x's older OTG_FS layout (four endpoints,
no GRXSTSP, GCCFG without VBDEN) split into four peripherals that the RCC's
OTGFSEN and OTGFSRST cannot name. This writes the block as RM0390 (rev 6,
30.15 "OTG_FS registers") lays it out: one peripheral OTGFS at 0x50000000,
its six IN and six OUT endpoints, its twelve host channels, and the FIFO
windows (0x1000 per endpoint from 0x1000, the RAM's debug view from
0x20000) as a range the model serves. Field names are those of ST's CMSIS
device header (stm32f446xx.h, USB_OTG_*), which the HAL uses.

  dwc2_regs.py --splice registers.json   replace its OTG_FS_* peripherals
"""
import argparse
import json
import sys

R, W, C1, RS = "r", "w", "rc_w1", "rs"


def reg(name, off, fields, reset=None):
    d = {"name": name, "offset": "0x%03X" % off}
    if reset:
        d["reset"] = "0x%08X" % reset
    d["fields"] = [list(f) for f in fields]
    return d


def block(in_eps=6, out_eps=6, channels=12, fifo_words=320):
    rs = []
    rs.append(reg("GOTGCTL", 0x000, [
        ("SRQSCS", 0, 1, R), ("SRQ", 1, 1), ("VBVALOEN", 2, 1), ("VBVALOVAL", 3, 1),
        ("AVALOEN", 4, 1), ("AVALOVAL", 5, 1), ("BVALOEN", 6, 1), ("BVALOVAL", 7, 1),
        ("HNGSCS", 8, 1, R), ("HNPRQ", 9, 1), ("HSHNPEN", 10, 1), ("DHNPEN", 11, 1),
        ("EHEN", 12, 1), ("CIDSTS", 16, 1, R), ("DBCT", 17, 1, R), ("ASVLD", 18, 1, R),
        ("BSVLD", 19, 1, R), ("OTGVER", 20, 1)], 0x00010000))
    rs.append(reg("GOTGINT", 0x004, [
        ("SEDET", 2, 1, C1), ("SRSSCHG", 8, 1, C1), ("HNSSCHG", 9, 1, C1), ("HNGDET", 17, 1, C1),
        ("ADTOCHG", 18, 1, C1), ("DBCDNE", 19, 1, C1), ("IDCHNG", 20, 1, C1)]))
    rs.append(reg("GAHBCFG", 0x008, [("GINT", 0, 1), ("TXFELVL", 7, 1), ("PTXFELVL", 8, 1)]))
    rs.append(reg("GUSBCFG", 0x00C, [
        ("TOCAL", 0, 3), ("PHYSEL", 6, 1, R), ("SRPCAP", 8, 1), ("HNPCAP", 9, 1), ("TRDT", 10, 4),
        ("PHYLPCS", 15, 1), ("TSDPS", 22, 1), ("FHMOD", 29, 1), ("FDMOD", 30, 1)], 0x00001440))
    rs.append(reg("GRSTCTL", 0x010, [
        ("CSRST", 0, 1), ("HSRST", 1, 1), ("FCRST", 2, 1), ("RXFFLSH", 4, 1), ("TXFFLSH", 5, 1),
        ("TXFNUM", 6, 5), ("AHBIDL", 31, 1, R)], 0x80000000))
    rs.append(reg("GINTSTS", 0x014, [
        ("CMOD", 0, 1, R), ("MMIS", 1, 1, C1), ("OTGINT", 2, 1, R), ("SOF", 3, 1, C1),
        ("RXFLVL", 4, 1, R), ("NPTXFE", 5, 1, R), ("GINAKEFF", 6, 1, R), ("BOUTNAKEFF", 7, 1, R),
        ("ESUSP", 10, 1, C1), ("USBSUSP", 11, 1, C1), ("USBRST", 12, 1, C1), ("ENUMDNE", 13, 1, C1),
        ("ISOODRP", 14, 1, C1), ("EOPF", 15, 1, C1), ("IEPINT", 18, 1, R), ("OEPINT", 19, 1, R),
        ("IISOIXFR", 20, 1, C1), ("PXFR_INCOMPISOOUT", 21, 1, C1), ("RSTDET", 23, 1, C1),
        ("HPRTINT", 24, 1, R), ("HCINT", 25, 1, R), ("PTXFE", 26, 1, R), ("LPMINT", 27, 1, C1),
        ("CIDSCHG", 28, 1, C1), ("DISCINT", 29, 1, C1), ("SRQINT", 30, 1, C1), ("WKUINT", 31, 1, C1)],
        0x04000020))
    rs.append(reg("GINTMSK", 0x018, [
        ("MMISM", 1, 1), ("OTGINT", 2, 1), ("SOFM", 3, 1), ("RXFLVLM", 4, 1), ("NPTXFEM", 5, 1),
        ("GINAKEFFM", 6, 1), ("GONAKEFFM", 7, 1), ("ESUSPM", 10, 1), ("USBSUSPM", 11, 1),
        ("USBRST", 12, 1), ("ENUMDNEM", 13, 1), ("ISOODRPM", 14, 1), ("EOPFM", 15, 1),
        ("EPMISM", 17, 1), ("IEPINT", 18, 1), ("OEPINT", 19, 1), ("IISOIXFRM", 20, 1),
        ("PXFRM_IISOOXFRM", 21, 1), ("RSTDETM", 23, 1), ("PRTIM", 24, 1), ("HCIM", 25, 1),
        ("PTXFEM", 26, 1), ("LPMINTM", 27, 1), ("CIDSCHGM", 28, 1), ("DISCINT", 29, 1),
        ("SRQIM", 30, 1), ("WUIM", 31, 1)]))
    rx = [("EPNUM", 0, 4, R), ("BCNT", 4, 11, R), ("DPID", 15, 2, R), ("PKTSTS", 17, 4, R),
          ("FRMNUM", 21, 4, R)]
    rs.append(reg("GRXSTSR", 0x01C, rx))
    rs.append(reg("GRXSTSP", 0x020, rx))
    rs.append(reg("GRXFSIZ", 0x024, [("RXFD", 0, 16)], 0x00000200))
    rs.append(reg("DIEPTXF0_HNPTXFSIZ", 0x028, [("TX0FSA", 0, 16), ("TX0FD", 16, 16)], 0x02000200))
    rs.append(reg("HNPTXSTS", 0x02C, [("NPTXFSAV", 0, 16, R), ("NPTQXSAV", 16, 8, R),
                                      ("NPTXQTOP", 24, 7, R)], 0x00080200))
    rs.append(reg("GCCFG", 0x038, [
        ("DCDET", 0, 1, R), ("PDET", 1, 1, R), ("SDET", 2, 1, R), ("PS2DET", 3, 1, R),
        ("PWRDWN", 16, 1), ("BCDEN", 17, 1), ("DCDEN", 18, 1), ("PDEN", 19, 1), ("SDEN", 20, 1),
        ("VBDEN", 21, 1)]))
    rs.append(reg("CID", 0x03C, [("PRODUCT_ID", 0, 32)], 0x00002000))
    rs.append(reg("GSNPSID", 0x040, [("GSNPSID", 0, 32, R)], 0x4F54320A))
    rs.append(reg("GLPMCFG", 0x054, [
        ("LPMEN", 0, 1), ("LPMACK", 1, 1), ("BESL", 2, 4, R), ("REMWAKE", 6, 1, R), ("L1SSEN", 7, 1),
        ("BESLTHRS", 8, 4), ("L1DSEN", 12, 1), ("LPMRSP", 13, 2, R), ("SLPSTS", 15, 1, R),
        ("L1RSMOK", 16, 1, R), ("LPMCHIDX", 17, 4), ("LPMRCNT", 21, 3), ("SNDLPM", 24, 1),
        ("LPMRCNTSTS", 25, 3, R), ("ENBESL", 28, 1)]))
    rs.append(reg("HPTXFSIZ", 0x100, [("PTXSA", 0, 16), ("PTXFD", 16, 16)], 0x02000600))
    for n in range(1, in_eps):
        rs.append(reg("DIEPTXF%d" % n, 0x100 + 4 * n, [("INEPTXSA", 0, 16), ("INEPTXFD", 16, 16)],
                      0x02000400))
    # host mode
    rs.append(reg("HCFG", 0x400, [("FSLSPCS", 0, 2), ("FSLSS", 2, 1, R)]))
    rs.append(reg("HFIR", 0x404, [("FRIVL", 0, 16)], 0x0000EA60))
    rs.append(reg("HFNUM", 0x408, [("FRNUM", 0, 16, R), ("FTREM", 16, 16, R)], 0x00003FFF))
    rs.append(reg("HPTXSTS", 0x410, [("PTXFSAVL", 0, 16, R), ("PTXQSAV", 16, 8, R), ("PTXQTOP", 24, 8, R)],
                  0x00080100))
    rs.append(reg("HAINT", 0x414, [("HAINT", 0, 16, R)]))
    rs.append(reg("HAINTMSK", 0x418, [("HAINTM", 0, 16)]))
    rs.append(reg("HPRT", 0x440, [
        ("PCSTS", 0, 1, R), ("PCDET", 1, 1, C1), ("PENA", 2, 1, C1), ("PENCHNG", 3, 1, C1),
        ("POCA", 4, 1, R), ("POCCHNG", 5, 1, C1), ("PRES", 6, 1), ("PSUSP", 7, 1, RS), ("PRST", 8, 1),
        ("PLSTS", 10, 2, R), ("PPWR", 12, 1), ("PTCTL", 13, 4), ("PSPD", 17, 2, R)]))
    for n in range(channels):
        b = 0x500 + 0x20 * n
        rs.append(reg("HCCHAR%d" % n, b, [
            ("MPSIZ", 0, 11), ("EPNUM", 11, 4), ("EPDIR", 15, 1), ("LSDEV", 17, 1), ("EPTYP", 18, 2),
            ("MC", 20, 2), ("DAD", 22, 7), ("ODDFRM", 29, 1), ("CHDIS", 30, 1), ("CHENA", 31, 1)]))
        rs.append(reg("HCINT%d" % n, b + 0x08, [
            ("XFRC", 0, 1, C1), ("CHH", 1, 1, C1), ("STALL", 3, 1, C1), ("NAK", 4, 1, C1),
            ("ACK", 5, 1, C1), ("TXERR", 7, 1, C1), ("BBERR", 8, 1, C1), ("FRMOR", 9, 1, C1),
            ("DTERR", 10, 1, C1)]))
        rs.append(reg("HCINTMSK%d" % n, b + 0x0C, [
            ("XFRCM", 0, 1), ("CHHM", 1, 1), ("STALLM", 3, 1), ("NAKM", 4, 1), ("ACKM", 5, 1),
            ("NYET", 6, 1), ("TXERRM", 7, 1), ("BBERRM", 8, 1), ("FRMORM", 9, 1), ("DTERRM", 10, 1)]))
        rs.append(reg("HCTSIZ%d" % n, b + 0x10, [("XFRSIZ", 0, 19), ("PKTCNT", 19, 10), ("DPID", 29, 2)]))
    # device mode
    rs.append(reg("DCFG", 0x800, [
        ("DSPD", 0, 2), ("NZLSOHSK", 2, 1), ("DAD", 4, 7), ("PFIVL", 11, 2), ("XCVRDLY", 14, 1),
        ("ERRATIM", 15, 1), ("PERSCHIVL", 24, 2)], 0x02200000))
    rs.append(reg("DCTL", 0x804, [
        ("RWUSIG", 0, 1), ("SDIS", 1, 1), ("GINSTS", 2, 1, R), ("GONSTS", 3, 1, R), ("TCTL", 4, 3),
        ("SGINAK", 7, 1, W), ("CGINAK", 8, 1, W), ("SGONAK", 9, 1, W), ("CGONAK", 10, 1, W),
        ("POPRGDNE", 11, 1), ("DSBESLRJCT", 18, 1)], 0x00000002))
    rs.append(reg("DSTS", 0x808, [
        ("SUSPSTS", 0, 1, R), ("ENUMSPD", 1, 2, R), ("EERR", 3, 1, R), ("FNSOF", 8, 14, R),
        ("DEVLNSTS", 22, 2, R)], 0x00000010))
    rs.append(reg("DIEPMSK", 0x810, [
        ("XFRCM", 0, 1), ("EPDM", 1, 1), ("TOM", 3, 1), ("ITTXFEMSK", 4, 1), ("INEPNMM", 5, 1),
        ("INEPNEM", 6, 1), ("TXFURM", 8, 1), ("NAKM", 13, 1)]))
    rs.append(reg("DOEPMSK", 0x814, [
        ("XFRCM", 0, 1), ("EPDM", 1, 1), ("STUPM", 3, 1), ("OTEPDM", 4, 1), ("OTEPSPRM", 5, 1),
        ("B2BSTUP", 6, 1), ("OPEM", 8, 1), ("BOIM", 9, 1), ("NAKM", 13, 1), ("NYETM", 14, 1)]))
    rs.append(reg("DAINT", 0x818, [("IEPINT", 0, 16, R), ("OEPINT", 16, 16, R)]))
    rs.append(reg("DAINTMSK", 0x81C, [("IEPM", 0, 16), ("OEPM", 16, 16)]))
    rs.append(reg("DVBUSDIS", 0x828, [("VBUSDT", 0, 16)], 0x000017D7))
    rs.append(reg("DVBUSPULSE", 0x82C, [("DVBUSP", 0, 12)], 0x000005B8))
    rs.append(reg("DIEPEMPMSK", 0x834, [("INEPTXFEM", 0, 16)]))
    for n in range(in_eps):
        b = 0x900 + 0x20 * n
        if n == 0:
            ctl = [("MPSIZ", 0, 2), ("USBAEP", 15, 1, R), ("NAKSTS", 17, 1, R), ("EPTYP", 18, 2, R),
                   ("STALL", 21, 1, RS), ("TXFNUM", 22, 4), ("CNAK", 26, 1, W), ("SNAK", 27, 1, W),
                   ("EPDIS", 30, 1, RS), ("EPENA", 31, 1, RS)]
            tsz = [("XFRSIZ", 0, 7), ("PKTCNT", 19, 2)]
        else:
            ctl = [("MPSIZ", 0, 11), ("USBAEP", 15, 1), ("EONUM_DPID", 16, 1, R), ("NAKSTS", 17, 1, R),
                   ("EPTYP", 18, 2), ("STALL", 21, 1), ("TXFNUM", 22, 4), ("CNAK", 26, 1, W),
                   ("SNAK", 27, 1, W), ("SD0PID_SEVNFRM", 28, 1, W), ("SODDFRM", 29, 1, W),
                   ("EPDIS", 30, 1, RS), ("EPENA", 31, 1, RS)]
            tsz = [("XFRSIZ", 0, 19), ("PKTCNT", 19, 10), ("MULCNT", 29, 2)]
        rs.append(reg("DIEPCTL%d" % n, b, ctl, 0x00008000 if n == 0 else None))
        rs.append(reg("DIEPINT%d" % n, b + 0x08, [
            ("XFRC", 0, 1, C1), ("EPDISD", 1, 1, C1), ("TOC", 3, 1, C1), ("ITTXFE", 4, 1, C1),
            ("INEPNM", 5, 1, C1), ("INEPNE", 6, 1, R), ("TXFE", 7, 1, R), ("TXFIFOUDRN", 8, 1, C1),
            ("BNA", 9, 1, C1), ("PKTDRPSTS", 11, 1, C1), ("NAK", 13, 1, C1)], 0x00000080))
        rs.append(reg("DIEPTSIZ%d" % n, b + 0x10, tsz))
        rs.append(reg("DTXFSTS%d" % n, b + 0x18, [("INEPTFSAV", 0, 16, R)], 0x00000200 if n == 0 else 0x00000400))
    for n in range(out_eps):
        b = 0xB00 + 0x20 * n
        if n == 0:
            ctl = [("MPSIZ", 0, 2, R), ("USBAEP", 15, 1, R), ("NAKSTS", 17, 1, R), ("EPTYP", 18, 2, R),
                   ("SNPM", 20, 1), ("STALL", 21, 1, RS), ("CNAK", 26, 1, W), ("SNAK", 27, 1, W),
                   ("EPDIS", 30, 1, R), ("EPENA", 31, 1, RS)]
            tsz = [("XFRSIZ", 0, 7), ("PKTCNT", 19, 1), ("STUPCNT", 29, 2)]
        else:
            ctl = [("MPSIZ", 0, 11), ("USBAEP", 15, 1), ("EONUM_DPID", 16, 1, R), ("NAKSTS", 17, 1, R),
                   ("EPTYP", 18, 2), ("SNPM", 20, 1), ("STALL", 21, 1), ("CNAK", 26, 1, W),
                   ("SNAK", 27, 1, W), ("SD0PID_SEVNFRM", 28, 1, W), ("SODDFRM", 29, 1, W),
                   ("EPDIS", 30, 1, RS), ("EPENA", 31, 1, RS)]
            tsz = [("XFRSIZ", 0, 19), ("PKTCNT", 19, 10), ("RXDPID_STUPCNT", 29, 2)]
        rs.append(reg("DOEPCTL%d" % n, b, ctl, 0x00008000 if n == 0 else None))
        rs.append(reg("DOEPINT%d" % n, b + 0x08, [
            ("XFRC", 0, 1, C1), ("EPDISD", 1, 1, C1), ("STUP", 3, 1, C1), ("OTEPDIS", 4, 1, C1),
            ("OTEPSPR", 5, 1, C1), ("B2BSTUP", 6, 1, C1), ("NAK", 13, 1, C1), ("STPKTRX", 15, 1, C1)],
            0x00000080))
        rs.append(reg("DOEPTSIZ%d" % n, b + 0x10, tsz))
    rs.append(reg("PCGCCTL", 0xE00, [
        ("STPPCLK", 0, 1), ("GATEHCLK", 1, 1), ("PHYSUSP", 4, 1, R), ("ENL1GTG", 5, 1),
        ("PHYSLEEP", 6, 1, R), ("SUSP", 7, 1, R)]))
    return {
        "name": "OTGFS",
        "base": "0x50000000",
        "interrupts": ["OTG_FS", "OTG_FS_WKUP"],
        "registers": rs,
        # every address of the block without a register: the endpoints' FIFO
        # windows and the RAM's debug view, which the model serves, and the
        # reserved words between the registers (they read 0)
        "reserved": [["0x0", "0x40000"]],
    }


NOTE = ("; the USB OTG FS block (OTG_FS_GLOBAL, OTG_FS_HOST, OTG_FS_DEVICE, OTG_FS_PWRCLK: the F40x's "
        "layout) replaced by OTGFS as RM0390 30.15 lays it out, written by tools/importers/dwc2_regs.py "
        "(six endpoints each way, GRXSTSP, GCCFG's VBDEN, GLPMCFG; GSNPSID 0x4F54320A as TinyUSB's dwc2_info "
        "table measures on the F412's and F76x's OTG_FS, the same core generation: no ST document gives it)")


def splice(path):
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    ps = d["peripherals"]
    at = [i for i, p in enumerate(ps) if p["name"].startswith("OTG_FS") or p["name"] == "OTGFS"]
    if not at:
        sys.exit("%s: no OTG_FS peripheral" % path)
    new = [p for i, p in enumerate(ps) if i not in at]
    new.insert(at[0], block())
    d["peripherals"] = new
    src = d.get("source", "")
    if "; the USB OTG FS block" in src:  # an earlier run's note, replaced
        src = src[:src.index("; the USB OTG FS block")]
    d["source"] = src + NOTE
    with open(path, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=1, ensure_ascii=False)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--splice", nargs="+", metavar="registers.json")
    a = ap.parse_args()
    if a.splice:
        for p in a.splice:
            splice(p)
    else:
        json.dump(block(), sys.stdout, indent=2)
        sys.stdout.write("\n")


if __name__ == "__main__":
    main()
