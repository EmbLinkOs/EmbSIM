# Explicit Giantec GT25Q32A-U storage profile

**Implemented: bounded unprotected standard-SPI programming/read/erase for the
Giantec family matching the owner's reported C4 60 16 ID. Not a complete chip,
exact ordering/die revision match, physical timing or native ROM/SDK boot.**

## Part-specific behavior

Giantec GT25Q32A-U V2.5 (June 2026) section 9.2 identifies C4 60 16, matching the
[owner's esptool report](esp32-boot-verification.md). The model has a separate
named adapter, not a renamed Winbond device. Only the shared, verified standard
array protocol is factored into `nor32.c`; identities, durations and WEL policies
are independently selected by each named adapter. Winbond's existing fixture and
behavior remain available and separately tested.

The qualified Giantec subset is 03h read, 05h status1, 06h/04h WREN/WRDI, 9Fh
ID, 02h page program and 20h/52h/D8h/C7h/60h erase. Storage is an explicitly
supplied 4 MiB image; the model does not invent missing/erased bytes. PP accepts
1..256 bytes, wraps within its 256-byte page and clears bits. Writes without WEL
and supported no-receive commands while BUSY are ignored. Data/ID reads during
BUSY fail because the ignored receive-line output is unqualified. SR1 upper bits
are the explicit unprotected fixture configuration, not a measured board state.

The AC tables give these maximum-duration fixture bounds:

| Operation | Giantec selected bound |
|---|---|
| One-byte PP (tBP) | 150 us |
| Longer page program (tPP) | 1,500 us |
| 4 KiB, 32 KiB, 64 KiB erase | 8,000 us |
| Chip erase | 16,000 us |

These differ substantially from the Winbond profile. They are conservative
published bounds, not measured times or proof that the owner's flash die uses
this exact revision. Program/erase BUSY clears at the supplied deadline; array
changes commit then. External microseconds advance independently of CPU turns
or APP gating. Exact physical timing, intermediate cells and power failure are
unqualified.

Giantec specifies that WEL resets at an **unspecified time before completion**.
The model chooses early clear on accepted program/erase as a deterministic
fixture policy inside that unspecified interval; no measured WEL transition time
is claimed. Winbond's separately specified completion-time clear is preserved.
Status-polling firmware must use BUSY for operation completion.

## Attachment and omissions

`esk_esp32_fabric_gt25q32_configure` explicitly attaches the named part. It
requires a 4 MiB backing, both caches disabled and no pending operation. Named
parts are mutually exclusive: attaching/detaching the wrong active part fails,
rather than silently changing identity. Attach/detach resets chip fixture state,
not storage/controller state; this is host setup, not a silicon power action.
Backing replacement while attached fails. CPU-only reset preserves the external
chip operation. SPI1 transactions use the existing validated byte/buffer phases;
each transfer is limited to 64 bytes, or 32 in a selected high buffer. Firmware
must split at page and controller-buffer boundaries.

The existing conservative cache contract also applies: both caches disabled
throughout SPI1 transfers, no re-enable while BUSY, and a fresh flush after
completion before mapped reads. It is not live cache-tag/coherence or SPI0/1
arbitration qualification. Transaction completion/source 29 is distinct from
self-timed chip completion.

Unimplemented features fail explicitly: 1 KiB mini-sector erase 82h (the inspected
AC tables do not specify its tMSE maximum), fast/dual/quad/QPI, array-end read
wrap/address aliases, long PP frames, status2/3/protection writes, SFDP, OTP,
suspend/resume, reset/power-down, pin/voltage/drive behavior and endurance/disturb.
No unsupported protection write is accepted while protecting nothing. This
subset must not be presented as every GT25Q32A-U feature or exact board fidelity.

## Evidence

- `test_esp32_gt25q32`: 1,172 native assertions covering identity, byte/page bounds,
  WEL policy, all qualified erase sizes/aliases, buffer packing, busy detach and
  mutual exclusion. The shared engine's all-page-offset/bit-clearing tests also
  pass through the unchanged Winbond adapter.
- `run_w25q32.py --part gt25q32` independently parses the pinned Giantec PDF's ID,
  tBP/tPP/erase maxima and checks a Python array/time oracle: 2,631 comparisons,
  219 completed operations and 37 ignored writes. The separate Winbond PDF
  referee continues to pass.
- Actual ESP32 C plus seven unchanged SDK helper bodies performs WREN, page-safe
  PP, status polling, readback and execution of supplied code copied to RAM.
  With Giantec selected and a deliberately slower external-time fixture:
  **128 reads, 32 programmed workers, 32 exact replays, 139,520 retired
  instructions and 432 firmware-observed BUSY reads** pass. Native status/lock
  memory is observed directly; there are no Python memory callbacks or host
  worker-PC shortcuts. The fixture injects 1 ms every 257 scheduled turns,
  rather than claiming an instruction-to-time conversion. It explicitly checks
  that firmware consumes BUSY=1 before completion.

## References and reproduction

Primary reference: [Giantec GT25Q32A-U V2.5](https://en.giantec-semi.com/uploads/pdf/260605/GT25Q32A%20U%20DS_V2.5.pdf),
sections 8/9 and AC characteristics, inspected 2026-11-05. SHA-256:
`df021758e55bde131f39af59c42acac20f2e0b32e251f4ade673687d22b7cd55`.
The datasheet and SDK inputs remain external, not vendored implementation.

```sh
python3 tests/vendor/xtensa_integer/run_w25q32.py --part gt25q32 \
  --datasheet /path/to/GT25Q32A-U-V2.5.pdf
python3 tests/vendor/xtensa_integer/run_spi_c.py --program-workers \
  --flash-part gt25q32 --flash-tick-period 257 \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld --nm /path/to/xtensa-esp32-elf-nm \
  --ll-header /path/to/spi_flash_ll.h --struct-header /path/to/spi_struct.h
```

Toolchain and SDK hashes are in the [SPI1 contract](esp32-spi1-reads.md).
