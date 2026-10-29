# Real parts' captures

Logic captures of real parts on their buses, replayed into the parts' models
by `tools/verify/replay_captures.py` (the `real_captures` test). This is the
evidence 01 §8 asks for beyond the datasheet: what the simulated part puts on
the wire is compared with what the real one did.

For each case in a directory's `cases.json`, the host's half of the capture
(for a single-wire sensor, the host's start pulses) is played into the model
by a pattern generator at the same times; the model's answers are recorded;
both sides are decoded by the protocol's rules and compared:

- for a single-wire sensor (AM2302): whether each read was answered, from
  the real part's first answer on (its history before the capture is
  unknown, and it decides the first answers); each timed phase, as the mean
  over the answered reads, within the case's tolerance (2 us by default; the
  captures are sampled at 1 MHz); that the simulated frames' checksums hold;
- for a 1-Wire bus (DS18B20): every bit the parts sent, one for one (ROM
  search, scratchpads, CRCs), past the transactions that show what the parts
  held before the capture (a scratchpad read before a conversion has had
  time to finish); the presence pulse's and a read 0's timing.

| Directory | Part | Captures | Source |
|---|---|---|---|
| `am2302/` | Aosong AM2302 (`emblink/dht22`) | 15, about 230 reads: start pulses from 100 us to 20 ms, reads from 71 ms to 2.3 s apart | sigrok-dumps `am230x/am2302` at 0ad13477, public domain; an AM2302 on an ATmega8, a USBee AX Pro clone at 1 MHz |
| `ds18b20/` | Maxim DS18B20 (`emblink/ds18b20`) | 1, two parts on one bus: four ROM searches, scratchpad reads, Convert T | sigrok-dumps `onewire/ds18b20` at 0ad13477, public domain; an STM32F103C8 master, a Saleae Logic clone at 1 MHz |

What these captures changed in the model is in its header and fidelity: the
real part's times differ from the datasheet's typical ones (54 us bit lows,
64 us before each byte, 73 us ones, a 47 us closing low), it answers starts
the datasheet does not ask for (from 400 us, and shorter ones after a long
idle), and it ignores a read within 0.4 s of the last.

## Adding captures

A capture belongs here when it is of a real part, its licence allows it
(ADR-029; record it in `cases.json`'s `provenance`), and the protocol has a
decoder in `replay_captures.py` (today the AM230x single wire and 1-Wire).

To capture a part on your own bench with sigrok and any fx2-based analyzer:

    sigrok-cli -d fx2lafw:samplerate=1mhz --channels D0=SDA --time 10s -o part.sr

with the analyzer's ground on the circuit's and D0 on the data line, and the
host doing what the case should show (a library's example sketch, reads at
an interval). Note the board, the firmware and its timing in the case's
`doc`.
