# The ESP32-C3's radios: Wi-Fi and Bluetooth LE

**Status:** the owner's direction of 2026-10-22 ("we really need Wi-Fi and
Bluetooth"), after the RTOS boards plan's part 2 left the radios declared
(its step 5). Q11 accepted them as stubs first, "a scripted access point,
loopback sockets", until modelled. This plan models them.

## How it works in real life

The ESP32-C3 has one 2.4 GHz radio shared by an 802.11 b/g/n station and
access point (1T1R, 20 and 40 MHz channels) and a Bluetooth 5 LE
controller (1M, 2M and Coded PHYs, advertising extensions), with
coexistence between them. Firmware reaches them through ESP-IDF:

| Layer | Wi-Fi | Bluetooth LE | Source |
|---|---|---|---|
| Application API | `esp_wifi`, `esp_netif`, lwIP sockets | NimBLE or Bluedroid host | open (Apache-2.0, lwIP BSD) |
| Security | wpa_supplicant (WPA2/WPA3 personal, enterprise) | the host's SMP | open |
| Upper MAC | `libnet80211.a` (scan, join, AP) | the controller, `libbtdm_app.a` | closed |
| Lower MAC, timing | `libpp.a`, `libcore.a` | the RivieraWaves BLE controller, mostly in the mask ROM | closed |
| PHY, RF calibration | `libphy.a` | `libbtbb.a` | closed |
| Hardware | the Wi-Fi MAC (0x6002E000-0x60039FFF, interrupt sources 0-3), the baseband (BB, 0x6001D000), FE, FE2, NRX, the analog blocks through the internal I2C (0x6000E000) | the BT baseband and the RW BLE core (sources 4-10) | undocumented |

The closed libraries keep their symbol names (libpp alone has 115 `hal_*`
functions such as `hal_set_sta_tsf_wakeup`), and the ROM ELF names its
routines (`rom_pkdet_vol_start`, `rom_iq_est_enable`). Espressif documents
none of the registers; they are learnt from what this code does with them.

Engineers use it through ESP-IDF's examples and menuconfig, the console
log (`wifi:connected with ...`, `got ip:`), a phone's Wi-Fi list or a BLE
scanner (nRF Connect), Wireshark on a monitor-mode adapter, and a power
meter (the radio's current dominates a board's draw). Their usual
mistakes: a wrong password or auth mode threshold, no NVS init, a 5 GHz
access point the chip cannot see, the event loop missing, BLE advertising
data over 31 bytes, a connection interval the central refuses.

## What can be observed from outside

- **The air:** 802.11 management, control and data frames and BLE
  advertising and connection PDUs, on their channels, at their rates and
  their times (beacon intervals, DTIM, connection events), with the
  transmit power set; what each receiver measures (RSSI falling with
  distance); a frame lost to range or collision.
- **The console:** ESP-IDF's own log lines, phy_init's calibration, the
  driver's events, unchanged.
- **The network:** DHCP, ARP, DNS, ICMP, TCP and UDP to whatever the
  network holds.
- **Power:** the current by radio state (TX, RX, modem sleep, light
  sleep) from the datasheet's tables.
- **Timing:** how long a scan, a join or a connection takes.

## Where the inside may differ

- **The PHY's analog calibration answers as an ideal radio.** `phy_init`
  runs the PLL, DC offset, IQ and power-detector calibrations through the
  analog I2C block, FE and BB: each measurement completes when the
  hardware's would and reads as a calibrated front end. Nothing outside
  the chip can see these values; the frames on air are what count.
- **The air carries frames, not waveforms** (as ADR-031 carries USB as
  packets): a frame has its channel, rate, power, start and duration, and
  arrives at each receiver with an RSSI from the world's distances
  (ADR-026). Modulation, fading and interference are not modelled.
- **The other end is the bench's.** Access points, stations and BLE peers
  are parts of a project, scripted (an access point with an SSID,
  security, a DHCP server and a network behind it) or other boards.

## The full implementation

Every mode ESP-IDF offers: station, access point and both; scan (active,
passive, all channels); WPA2/WPA3 personal; power save; 40 MHz; ESP-NOW;
BLE advertising, scanning, connections as central and peripheral, GATT,
pairing, the 2M and Coded PHYs, extended advertising. Anything not done
yet is declared in the part's fidelity.

## How it is proven

- ESP-IDF v5.4's own examples, unmodified, under scenarios: `wifi/scan`,
  `wifi/getting_started/station` and `softAP`, `protocols/http_request`,
  `wifi/espnow`, `bluetooth/nimble/bleprph` and `blecent`,
  `bluetooth/bluedroid/ble/ble_ibeacon`; Arduino-ESP32's `WiFiScan`,
  `WiFiClient` and BLE examples.
- Two boards on one air: a station joining another board's access point,
  ESP-NOW, a BLE central connecting to a peripheral.
- A capture of the air in pcap form that Wireshark decodes as it decodes
  a real monitor-mode capture.
- Espressif's QEMU (GPL-2.0) may referee where it models the same, run as
  a separate program (ADR-029). No GPL emulator's source is read for the
  registers.

## Steps

Each step lands usable and green, as in the RTOS boards plan.

1. **The ROM's radio data.** `esprom.py --alias` places
   `.rodata.interface` (the Wi-Fi and Bluetooth code's pointers and
   constant tables, linked at 0x3FF1EE3C and loaded by no segment).
   *Done:* `esp_wifi_init` no longer stores through a null
   `our_instances_ptr`; the driver allocates its buffers and starts
   `phy_init`.
2. **The PHY's start.** The analog I2C block's measurement registers
   (0x6000E04C-0x6000E170: the power detector, the DC estimator, the PLL's
   calibration), FE's IQ estimator and the baseband answer as an ideal
   radio, at the hardware's times. `phy_init` completes, reports its
   calibration, and stores it in NVS as a board does, so the next start
   loads it. Research so far: `rom_pkdet_vol_start` waits for 0x6000E050
   bits 26:24 to read 7, `txdc_cal_v70` for bit 24 of 0x6000E04C,
   `rom_iq_est_enable` for bit 16 of 0x60006174.
3. **The Wi-Fi MAC.** Its registers, the transmit and receive DMA
   descriptor lists, its interrupts, the TSF timer, the power states. A
   frame the driver queues leaves as an 802.11 frame; a frame on the air
   reaches the driver as the hardware delivers it.
4. **The air and the access point.** The engine's medium (channels,
   airtime, RSSI from the world's distances, loss); an access point part
   (beacons, probe responses, authentication, association, WPA2 and WPA3
   personal handshakes, DHCP, ARP, DNS, ICMP, and TCP and UDP through to
   the host's loopback); a pcap capture instrument. `wifi/scan` lists the
   bench's access points; `station` joins and gets its address.
5. **The rest of Wi-Fi.** The access point mode with a station part and
   with another board; `http_request`; ESP-NOW between two boards; power
   save and the radio's current; Arduino-ESP32's Wi-Fi sketches.
6. **Bluetooth LE.** The BT baseband, the RW BLE core and its exchange
   memory as ESP-IDF's controller drives them; the BLE air (advertising
   channels, connection events, the PHYs); BLE peer parts (a scanner, a
   central, a peripheral with a GATT database). NimBLE's `bleprph` and
   `blecent`, Bluedroid's `ble_ibeacon`, Arduino-ESP32's BLE examples;
   two boards connected.
7. **The workbench.** The air as a phone and a scanner see it (the
   access points with their RSSI, BLE devices and their data), the
   capture, the radio's current on the power instruments.
