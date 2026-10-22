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
   loads it.
   *Done:* found by running ESP-IDF's `wifi/scan` with every access to the
   unclaimed blocks traced, each wait read in the code that waits (the
   closed libraries keep their symbols): the power detector
   (`rom_pkdet_vol_start`: +0x5C bit 19 starts, +0x50 bits 26:24 read 7),
   the DC estimator (`txdc_cal_v70`: +0x4C bit 24), the RF PLL's frequency
   calibration (`set_chan_freq_sw_start`: busy at +0x168 bit 31, the code
   given at +0x150 bits 26:20 found at +0x170 bits 23:17) and its end
   (`wait_rfpll_cal_end`: analog block 0x62, register 7 bit 1), the BBPLL
   (block 0x66 register 8: OR_LOCK, OR_CAL_END, OR_CAL_CAP), FE's IQ
   estimator (`rom_iq_est_enable`: +0x174 bit 16). The blocks
   `phy_init` programs keep what is written: FE2, FE, the receive gain
   table (0x60011000), the AGC and NRX (0x6001C000), the baseband
   (0x6001D000, now out of the register file: the SVD names one of its
   registers) and the MAC's registers (0x60033000-0x60035FFF, whose
   `hal_init` request at +0xD14 is acknowledged). `wifi/scan` runs as on a
   board on its first start: a full calibration saved to NVS, the
   station's MAC, a 2.5 s scan finding nothing (the MAC sends nothing
   yet). On the way: the temperature sensor's registers follow their own
   clock (TSENS_CLK_EN), not APB_SARADC's, as the PHY's reading of them
   shows.
3. **The Wi-Fi MAC.** Its registers, the transmit and receive DMA
   descriptor lists, its interrupts, the TSF timer, the power states. A
   frame the driver queues leaves as an 802.11 frame; a frame on the air
   reaches the driver as the hardware delivers it.
   *Done for a scan* (`wifi.c`), found the same way as step 2 and checked
   in libpp's routines: a queue's register (+0xD08 - 8q) takes its first
   lldesc and starts it; its PLCP word (+0x12F8 - 76q,
   `mac_tx_set_plcp1`) gives the length and the rate; at the end the
   queue's PMD word (+0x1320 - 76q) holds the outcome
   (`hal_mac_get_txq_pmd`: 0 sent, 5 no ACK), +0xCB0 its done bit
   (`hal_mac_get_txq_state`, cleared at +0xCAC) and the interrupt status
   (+0xC3C) bit 7. The receive list starts at +0x88; +0x8C is the
   hardware's next descriptor and +0x90 the last it filled; +0x84 bit 0
   reloads it (`wDev_Rxbuf_Init`'s and `wdevProcessRxSucDataAll`'s
   reading of them); a frame is taken only with word 0 bit 28 (the
   station's filter) or 29 set in ESP-IDF's rx_ctrl header; the addresses
   and masks at +0x40 to +0x6C. +0x2000 counts microseconds
   (`lmacSetTxFrame` stamps frames with it), and +0x200C latches the TSF
   at +0x2018 and +0x201C (`hal_get_tsf_time`). Not yet: retransmissions,
   contention, the TSF's adjustment and timers, CCMP, aggregation.
4. **The air and the access point.** The engine's medium (channels,
   airtime, RSSI from the world's distances, loss); an access point part
   (beacons, probe responses, authentication, association, WPA2 and WPA3
   personal handshakes, DHCP, ARP, DNS, ICMP, and TCP and UDP through to
   the host's loopback); a pcap capture instrument. `wifi/scan` lists the
   bench's access points; `station` joins and gets its address.
   *Under way:* the air (`core/engine/air.h`: frames with their channel,
   power, rate and airtime, free-space path loss over the bench distance,
   the receiver's sensitivity, ACKs) and `emblink/wifi_ap` (beacons,
   probe responses, open authentication and association, data frames,
   power-save buffering with the TIM and PS-Poll, a router answering ARP
   and pings and leasing addresses by DHCP). `wifi/scan` lists the two
   access points of its project with the RSSI their distances give;
   `station`, set for an open network, joins it and gets its address.
   And WPA2-Personal: the access point's 4-way handshake (the PSK from
   the passphrase, the PTK, the GTK wrapped under the KEK) and CCMP; the
   MAC's key table and CCMP (`hal_crypto_set_key_entry`: 40-byte entries
   from +0x1400, valid bits at +0x814; a frame queued with the PLCP
   word's bits 19:17 set is encrypted under its receiver's key, the PN the
   driver's CCMP header gives; a protected frame received is decrypted in
   place for net80211's `ccmp_decap`); the SHA accelerator's DMA mode and
   the AES accelerator, which ESP-IDF's wpa_supplicant uses for PBKDF2 and
   the key unwrap. `station` as it ships (WPA2-PSK) joins and gets its
   address. Next: the network beyond the router (DNS, TCP and UDP to the
   host) for `http_request`, then step 5.
5. **The rest of Wi-Fi.** The access point mode with a station part and
   with another board; `http_request`; ESP-NOW between two boards; power
   save and the radio's current; Arduino-ESP32's Wi-Fi sketches.
6. **Bluetooth LE.** The BT baseband, the RW BLE core and its exchange
   memory as ESP-IDF's controller drives them; the BLE air (advertising
   channels, connection events, the PHYs); BLE peer parts (a scanner, a
   central, a peripheral with a GATT database). NimBLE's `bleprph` and
   `blecent`, Bluedroid's `ble_ibeacon`, Arduino-ESP32's BLE examples;
   two boards connected.
   *Under way:* advertising (`ble.c`), found as in step 2 with the
   controller's ROM routines (`r_lld_*`, `r_sch_*`, `r_rwip_*`, `r_emi_*`)
   read where they wait. The core is RivieraWaves' BLE IP: VERSION (+0x04)
   reads 0x09001B00, which `r_lld_core_init` checks; the soft resets in
   +0x00 bits 31:29 are done at once. The native clock is sampled by +0x1C
   bit 31 (the half-slots, 312.5 us, in bits 27:0; +0x20 the
   half-microseconds left in the half-slot, counting down from 624). Three
   timers raise their interrupts when the clock reaches them: +0xE4 (in
   10 ms units, bit 9), +0xE8 (a half-slot, bit 10), +0xEC and +0xF0 (a
   half-slot and the count within it, bit 11). With +0x2D8 bit 31 set the
   interrupts go through a FIFO (`r_rwble_isr`: the oldest entry's bits in
   30:10, the count in 9:5, a write of bit 0 removes it), to interrupt
   source 8. The exchange memory is the controller's RAM, mapped by the
   registers from +0x204 (`r_emi_em_base_init`: bits 31:18 the EM offset
   in words, 17:0 the RAM's word address); its exchange table (16 entries
   of 16 bytes, `r_sch_prog_ble_push`) holds each event's state, start
   (half-slot and count) and control structure, and a write of bit 31 with
   the entry's index to +0x100 starts one. An advertising event's control
   structure (format 4 or 5) gives the address, the channel map and the
   transmit descriptor; its PDU goes out on each channel of the map at
   +9 dBm, one T_IFS and a SCAN_REQ's time apart, then the entry reads
   over and the end interrupt (bit 5) is raised. The radio's Wi-Fi and BLE
   share the chip's port to the air (`radio.c`). `emblink/ble_scanner`, a
   phone's scanner app, lists what it hears. NimBLE's `bleprph` as it
   ships advertises as "nimble-bleprph" and the scanner 2 m away lists it
   at -37 dBm. Next: receiving (scan requests and responses, CONNECT_IND),
   connection events and GATT with a central part, `blecent`,
   `ble_ibeacon`, Arduino-ESP32's BLE.
7. **The workbench.** The air as a phone and a scanner see it (the
   access points with their RSSI, BLE devices and their data), the
   capture, the radio's current on the power instruments.
