# ESP32 physical flash identity and ROM/SDK boot verification

**Result on 2026-11-05: the actual board flash part is not identified. The pinned
ESP-IDF example boots in Espressif QEMU; native EmbSIM ROM boot fails at the first
eFuse-block read after seven retired instructions.** Passing instruction/flash
fixtures and CI must not be reported as successful board or SDK boot.

## Physical flash: what the evidence establishes

The owner's [TECNOIOT ASIN B07N97DD1B](https://www.amazon.fr/dp/B07N97DD1B) was
revisited. Its product title, feature bullets and overview do not identify a
flash manufacturer, JEDEC ID or order code. Related-product recommendations
are separate products and provide no evidence for this board. The existing
[advertised-board inventory](esp32-tecnoiot-b07n97dd1b.md) remains provisional.

The advertised photo shows an ESP-WROOM-32 shield. Espressif's
[ESP32-WROOM-32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf)
specifies 4 MB external SPI flash. This is a **conditional nominal capacity** for
the identified genuine module family, not proof of the shipped unit's identity
or capacity. PDF SHA-256 inspected here:
`fe7dced2f6077f983dcd7df0e72b6f6c8b7b77fd6de2ff1a0efceffe3fd1e1fa`.
It does not identify this unit's flash manufacturer or full ordering suffix.

There is no physical board/USB serial device in this cloud environment. The
owner has macOS/Linux; a read-only hardware result has been requested:

```sh
python3 -m esptool --chip esp32 --port PORT flash-id
```

Use the actual serial port, for example `/dev/cu.SLAB_USBtoUART` or a matching
`/dev/cu.usbserial-*` device on macOS, `/dev/ttyUSB0` on Linux. Esptool v4 uses the
older command spelling `flash_id`. This reads identification without programming
flash, but the normal connection resets the board. The manufacturer/device ID,
detected size and silicon revision lines can establish more of the hardware
profile. An ID shared by multiple part revisions does not establish the exact
ordering suffix; markings or a verified BOM may still be necessary.

The implemented W25Q32JV-IQ fixture therefore stays explicitly selected and
unverified against the owner's physical unit. Neither the SDK's configured
flash size nor QEMU's simulated part is hardware identification evidence.

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
`EFUSE_BLK0_RDATA0_REG`. The current bus has no eFuse controller/profile. This
is the **first observed blocker**, not a claim that adding one register would
finish boot. RTC/strapping/reset state, further eFuse behavior, dedicated flash
commands/cache modes, other MMIO and complete CPU/SDK execution remain separate
requirements. Dummy zero reads would hide this gap and cannot verify boot.

The probe uses the supported CPU reset profile and an explicit PRID=0 fixture,
not a silicon-qualified full PRID. It installs only the PRO ROM; APP-specific
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
