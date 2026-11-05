# ESP32 physical flash identity and ROM/SDK boot verification

**Result on 2026-11-05: the owner's board reports ESP32-D0WD-V3 revision 3.1,
JEDEC C4 60 16 and 4 MB flash. Giantec's GT25Q32A-U datasheet matches that ID;
exact package/ordering suffix remains unconfirmed. The pinned SDK example boots
in Espressif QEMU. Native EmbSIM stops at unsupported RSR.MEMCTL after 545 retired
instructions with the new explicit synthetic eFuse image; without that profile,
the baseline stops at an eFuse read after seven.** Neither result establishes
successful native boot.

## Physical readout and manufacturer match

The owner ran esptool v5.6.0 on macOS against the physical board. The
[recorded output](../../tests/vendor/xtensa_integer/evidence/hardware-identification-2026-11-05.txt)
and [structured evidence](../../tests/vendor/xtensa_integer/evidence/hardware-identification-2026-11-05.json)
establish these reported values:

| Item | Hardware/tool report | Scope |
|---|---|---|
| Silicon | ESP32-D0WD-V3, revision v3.1 | Identifies the responding chip/revision |
| Crystal | 40 MHz | Esptool detection, not an independent frequency measurement |
| Features | Wi-Fi, BT, dual core + LP core, 240 MHz, Vref calibration in eFuse, Coding Scheme None | Reported capabilities; 240 MHz is not a measured current CPU frequency |
| Flash manufacturer/device | C4 / 6016, JEDEC bytes C4 60 16 | Physical ID transaction through the running RAM stub |
| Flash capacity | 4 MB (4,194,304 bytes) | Detected capacity; not inferred from SDK configuration |
| Flash voltage | 3.3 V, set by strapping pin | Reported setting, not an electrical rail measurement |

The MAC is omitted from repository artifacts because it is unnecessary for
part qualification. This readout verifies the responding serial-download/RAM-stub
path and flash-ID transaction. It does not prove a physical SDK application boot
or any native EmbSIM boot behavior.

Giantec's [GT25Q32A-U datasheet V2.5, June 2026, section 9.2, page 36/88](https://en.giantec-semi.com/uploads/pdf/260605/GT25Q32A%20U%20DS_V2.5.pdf)
explicitly lists 9Fh response **C4h / 60 / 16**. PDF SHA-256:
`df021758e55bde131f39af59c42acac20f2e0b32e251f4ade673687d22b7cd55`.
This supports a Giantec GT25Q32-class identification and a match to the published
GT25Q32A-U ID. JEDEC alone does not prove an exact package, temperature/order
suffix, die revision or uniqueness across all related variants. Physical
markings or an authenticated BOM would resolve the remaining ordering details.

The observed ID differs from the W25Q32JV-IQ fixture's EF 40 16. That Winbond
fixture must not be selected as verified hardware for this board or relabeled
with C4 without qualifying Giantec-specific behavior. The subsequent
[named Giantec profile](esp32-giantec-profile.md) now qualifies a bounded standard
SPI array protocol and separately selected timing bounds. It does not establish
the exact fitted die or complete chip/board behavior.

## Listing and nominal module evidence

The [TECNOIOT ASIN B07N97DD1B](https://www.amazon.fr/dp/B07N97DD1B) was also
revisited. Its product-specific fields do not identify the fitted flash part;
related-product recommendations are separate products. The
[advertised-board inventory](esp32-tecnoiot-b07n97dd1b.md) is still provisional
for PCB, module marking, power, bridge and header continuity.

The advertised photo shows an ESP-WROOM-32 shield. Espressif's
[ESP32-WROOM-32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)
specifies nominal 4 MB external SPI flash, conditional on genuine/correct module
identity. PDF SHA-256:
`fe7dced2f6077f983dcd7df0e72b6f6c8b7b77fd6de2ff1a0efceffe3fd1e1fa`.
The owner's actual 4 MB report is separate, stronger evidence for the responding
unit's detected capacity. Neither source authenticates a full PCB BOM.

To repeat the read-only check on macOS/Linux, use the computer attached to the
board (no USB serial device is attached to this cloud environment):

```sh
python3 -m venv ~/.venvs/embsim-board-check
~/.venvs/embsim-board-check/bin/python -m pip install esptool
~/.venvs/embsim-board-check/bin/python -m serial.tools.list_ports
~/.venvs/embsim-board-check/bin/python -m esptool --chip esp32 --port PORT flash-id
```

Replace PORT with the actual enumerated device. Esptool v4 uses `flash_id`.
The command does not program flash, but normally resets the board and executes
a RAM stub. The SDK's configured flash size and QEMU's simulated part remain
configuration choices, rather than hardware identification evidence.

## Positive reference: real SDK example in Espressif QEMU

Built ESP-IDF **v4.4.7**, commit
`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`, with Espressif's actual ESP32 GCC
8.4.0 esp-2021r2-patch5. Source in `examples/get-started/hello_world` and its
bootloader was unchanged. Configuration used the normal ESP32 target and
`COMPONENTS=main`; CMake 4.4.4 needed `CMAKE_POLICY_VERSION_MINIMUM=3.5` for this
older SDK. Build timestamps mean rebuilt binary hashes can differ; the exact
inputs tested are recorded in the evidence artifact.

Copied the bootloader at 0x1000, partition table at 0x8000 and application at
0x10000 according to generated `flasher_args.json`, into an explicitly supplied
4 MiB erased backing. No application or bootloader bytes were patched.

Espressif QEMU release `esp-develop-9.2.2-20260417` observed:

```text
ESP-IDF v4.4.7 2nd stage bootloader
Pro cpu up.
App cpu up.
Starting scheduler on PRO CPU.
Starting scheduler on APP CPU.
Hello world!
```

The [captured UART log](../../tests/vendor/xtensa_integer/evidence/esp-idf-v4.4.7-qemu-hello.log)
contains the bootloader/app loading and application countdown. The host stopped
QEMU after 15 seconds; this was an intentional capture limit. It establishes
this example's ROM-to-bootloader-to-two-core-scheduler/application path in QEMU,
not every SDK function, physical timing or any native EmbSIM boot behavior.

QEMU selects `gd25q32` for a 4 MiB backing; this is its configured model, not a
measurement of the owner's board. The SDK's default image header specifies
2 MiB, and its log explicitly warns that the simulated chip reports 4 MiB.
The application's printed 2 MiB follows the header limit. This illustrates why
configuration or printed SDK capacity must not substitute for hardware identity.

## Native EmbSIM result: blocked

**Latest follow-up:** [Region protection](esp32-region-protection.md) now completes
both instruction/data setup loops and barriers. The explicit-input probe reaches
545 retirements and unsupported RSR.MEMCTL at PC 0x400004f2. Full SDK boot remains
unverified.

**Prior atomic-control result:** [ATOMCTL](esp32-atomic-control.md) now passes the former
CPU-register blocker. The same explicit-input probe reaches 451 retirements and
unsupported WITLB at PC 0x400004c9. Native SDK boot remains blocked.

**Prior eFuse implementation result:** the new [eFuse read profile](esp32-efuse-read-profile.md)
accepts an explicitly supplied image. The synthetic unprogrammed fixture advances
the same ROM to 440 retired instructions and an unsupported WSR.ATOMCTL at
PC 0x40000488, with explicitly supplied controller-completion events. That is not the owner's factory image and still does not
boot the application. The baseline no-eFuse-profile result below remains valid.


The same merged image and Espressif QEMU-distributed PRO ROM were supplied to
the native machine. No firmware PC was set past reset and no missing MMIO
values were supplied to force boot onward. Result:

```text
reset PC             0x40000400
retired instructions 7
failure PC           0x4000fdd8
failure opcode       0x00a8
access address       0x3ff5a000
fault                load failure (kind 5), status -1
```

Pinned SDK `soc/esp32/include/soc/efuse_reg.h` identifies that address as
`EFUSE_BLK0_RDATA0_REG`. This baseline deliberately has no eFuse profile
attached. With the new explicit image attached, the first observed blocker
moved first to WSR.ATOMCTL, then WITLB, and now RSR.MEMCTL, as recorded above. The earlier synchronous profile took
the ROM failure path and requested a software reset through RTC options;
it did not pass the eFuse handshake. RTC/strapping/reset state,
further eFuse behavior, dedicated flash
commands/cache modes, other MMIO and complete CPU/SDK execution remain separate
requirements. Dummy zero reads would hide this gap and cannot verify boot.

The probe uses the supported CPU reset profile. Its default PRO PRID=0xcdcd
comes from pinned ESP-IDF `xtensa_context.h`, not a readout of the owner chip.
Use `--prid 0` to reproduce the historical explicit invalid-ID fixture. It installs only the PRO ROM; APP-specific
ROM behavior is not tested. Missing DROM tail bytes are zero-filled to match the
QEMU region setup, not asserted as physical ROM contents. The blob is an
Espressif QEMU distribution input, not an authenticated dump of the owner's chip.
No vendor ROM/QEMU implementation is embedded in the application or repository.

## Reproduction and artifacts

[Machine-readable evidence](../../tests/vendor/xtensa_integer/evidence/boot-verification-2026-11-05.json)
records source revisions, tested binary hashes, reference milestones and native
failure details. QEMU archive SHA-256:
`0eecb2a34a5586c0e59110f77b9343b7b336e82fdb0e1a30e1dc1bab8a547e35`.
The archive is available from [Espressif's release](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417).
PRO ROM SHA-256:
`9115bbcd03da9f54d3baca5ad94d588a43da54ed332347137ba3dc9010df258b`.

With the SDK/toolchain/Python environment installed externally:

```sh
idf.py -C /path/to/esp-idf/examples/get-started/hello_world -B /tmp/idf-hello \
  -D COMPONENTS=main -D CMAKE_POLICY_VERSION_MINIMUM=3.5 set-target esp32 build
```

Construct a 4 MiB backing from the build's actual files, without modifying them:

```python
import json
from pathlib import Path
build = Path('/tmp/idf-hello')
image = bytearray(b'\xff' * 0x400000)
for offset, name in json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))['flash_files'].items():
    data = (build / name).read_bytes()
    start = int(offset, 0)
    image[start:start + len(data)] = data
Path('/tmp/idf-hello-flash.bin').write_bytes(image)
```

Run the reference with the release's ROM files in its default share directory:

```sh
qemu-system-xtensa -M esp32 -nographic -nic none \
  -drive file=/tmp/idf-hello-flash.bin,if=mtd,format=raw
```

Run the native diagnostic from the repository root:

```sh
python3 tests/vendor/xtensa_integer/probe_rom_boot.py \
  --rom /path/to/qemu/share/qemu/esp32-v3-rom.bin \
  --flash /tmp/idf-hello-flash.bin --report /tmp/native-boot.json
```

The diagnostic deliberately returns **1** for blocked/inconclusive boot; it is
not registered as a passing CTest/vendor qualification. A step budget without
an observed application-success validator cannot establish boot success.
