# Changelog

All notable changes are recorded here. The format follows Keep a Changelog;
versions follow semantic versioning. There is no release yet.

## Unreleased

### Native parameter sweeps

- Run bounded independent reset-and-sample electrical sweeps through native CLI,
  RPC and schematic controls without replacing the live circuit.
- Plot measured loaded responses, retain failed/unavailable points explicitly,
  and export samples with exact native source/model identities.
- Validate sweep artifacts offline and check loaded dividers and RC responses
  against analytic equations. Converged DC and AC/noise/distortion remain unsupported.

### Native assembly variants

- Author independent per-instance population/value/catalog alternatives, review
  isolated native circuits, export resolved BOMs and activate with stale-base protection.
- Model DNP as real open footprints without drivers, pulls, controls, internal ties
  or compiled composite channels; retain schematic terminals and live voltage evidence.
- Keep derived circuits immutable, retain hash-verified exact base recovery and
  require an explicit save path after activation. Include optional zero-quantity DNP rows.


### Declared version contracts

- Enforce part, board, MCU, shared/composite member and installed library dependency
  requests against actual selected versions with bounded SemVer rules.
- Edit schematic version requests through native staged transactions; report
  shared scope, rollback, restart and native satisfaction evidence explicitly.
- Show native diagnostic reasons instead of generic validation errors, and verify
  version evidence offline with shared C/Python and independent reference cases.

### Pinned library inputs

- Pin non-hidden library file bytes and effective catalog selection with portable
  SHA-256 baselines; reject drift in the native loader before instantiation.
- Review named file changes through CLI/RPC and graphical engineering reports;
  explicitly pin/remove/undo metadata without resetting the simulation clock.
- Refuse stale descriptions, ambiguous roots, links and special files; retain
  explicit engine/firmware/hidden/external-input and dependency-solver limits.

### Native engineering reports

- Decode native CLI artifacts explicitly as UTF-8 on Windows; wait for browser
  editing/projection readiness and compare connectivity independently of live states.

- Capture atomic project/package/model inventories with native resolved parameters
  and exact SHA-256 document identities through RPC and the manifest CLI command.
- Group hierarchy-aware assembly quantities, retain compiled model dependencies
  separately, export BOM/source artifacts and locate exact schematic instances.
- Verify exports offline, reject stale GUI snapshots and distinguish generic parts,
  simulation fixtures, source declarations and unqualified model behavior.

### Physically loaded instrument controls

- Configure meter/scope/shunt/generator impedances and actual lead nets in Run,
  including representative scope loading presets and explicit restart behavior.
- Add signed voltage/current observations with selectable sampling and expose
  qualified instrument readouts inside reusable project blocks.
- Protect parameter bindings and stale drafts; retain time for unchanged settings.
- Verify native differential loading, burden, source impedance, triggered physical
  output capture, frozen CSV and independent nested measurement stages.

### LM358P nominal dynamic behavior

- Correct the named LM358P wrapper to forward the legacy device's nominal
  0.7 MHz bandwidth and 0.3 V/µs slew to both native channels; expose experimental
  overrides. Existing saved circuits can change transient response.
- Add both-channel rising/falling, bandwidth, forwarding and isolation fixtures,
  a routed two-channel bench with actual scope loading and native CSV acceptance.
- Pin the reviewed source and distinguish typical dynamic anchors from complete
  manufacturer qualification and the model's excluded physical effects.

### Project-wide schematic reliability

- Author block parameter declarations and rename their bindings/parent overrides.
- Copy circuits between sheets with isolated nets and dependency-aware conflict
  checking; protect connected ports and referenced parameters against removal.
- Edit ordered sheet buses, trace members and find references/nets/ports across
  concrete instances and shared definitions.
- Review/export project-wide advisory electrical and drawing checks alongside
  native observed contention/floating/unknown states, with navigable locations.

### Editable hierarchy and reusable circuit blocks

- Add project-owned block definitions, explicit port aliases, nested instances and
  native cross-sheet tracing with instance-local net scope.
- Author/edit shared sheets, extract selections, expose/remap/rename ports,
  import/export block projects and operate/probe qualified instances in the GUI.
- Stage coherent circuit edits atomically; preserve clock/state for child drawing
  edits and preflight electrical changes before a reported simulation restart.
- Add loaded/isolated divider acceptance, native browser workflow and hierarchy
  validation/undo fixtures. Nested board firmware and 3D rendering remain deferred.

### Triggered schematic acquisition

- Add sampled rising/falling/either Single and Normal captures with hysteresis,
  pre/post windows, holdoff, trigger markers and immutable exported records.
- Move ideal waveform probes directly on the schematic or through keyboard net
  selectors while retaining native identity and subscriptions. Capture epochs
  prevent signals from old targets entering a new recording.
- Break plots/readings at lost data, preserve null readings, bound exports and
  frequency estimates to observed data, and include component labels when fitting.
- Add a loaded RC acceptance bench with native protocol/browser verification.

### Live schematic simulation surface

- Center Run on an interactive schematic without loading the optional 3D renderer.
- Bind native LED/RGB, bargraph, segment, LCD/OLED and serial-display outputs to
  schematic symbols; operate contacts, pots and temperature stimulus on the sheet.
- Add accepted-time electrical LED/RGB emission integrals and selectable interval
  exposure for PWM; retain instantaneous light and raw circuit observations.
- Add two-lead bicolor and 8×8 row-anode matrix topologies using independent LED
  junctions, with polarity, selected-pixel and floating-return fixtures.
- Add signed A/B voltage comparison with net picks and movable observation leads;
  explicitly distinguish ideal observations from physically loaded instruments.
- Treat attached repeated labels as complete drawing connections, honor label
  rotation, and keep functional displays dark without a known common return.
- Document complete-schematic release gates and enlarged display-symbol drawing
  compatibility; advanced analysis/hierarchy are not claimed complete.

### Wired instrument loading and electrical feedback

- Add configurable voltmeter input resistance, ammeter insertion burden,
  oscilloscope input R/C and a function generator with 50 Ω output impedance.
- Keep detached instrument/voltage-source leads floating during incremental
  wiring; do not compress terminal order or manufacture a ground return.
- Show wired instruments, connected leads and native readings with trace and
  schematic actions; observation-only probes retain zero loading.
- Highlight selected nets through pin rings, junctions and labels; reveal readable
  pin names and observed logic states on selected/focused terminals.
- Report active receiver identities and current driver conflicts in native net
  observations, retaining solved mixed-signal voltages; distinguish unknown
  inputs from confirmed opposing known drives.
- Fit captured waveforms to the available interval, stop plots/cursor readings at
  the producer frontier and center instrument lead navigation in the actual pane.
- Add unconnected-input and live electrical warnings, an analytic loading bench,
  twelve component fixtures and RC refinement/driver-release checks.

### Firmware-driven indicators and waveform acceptance

- Boot a native ESP32-C3 RGB acceptance circuit from a flash image; physical
  button inputs change real LEDC duty while firmware runs.
- Apply scalar MCU driver resistance/slew settings to base GPIO models.
- Add time-weighted mean/RMS/digital duty scenario assertions, with strict
  coverage and unknown-state handling, and expose them in the scenario editor.
- Show scope RMS, time-weighted mean, logic-analyzer duty and capture coverage;
  preserve same-time waveform timestamps instead of shifting edges by 1 ps.
- Carry declared probe units and sampling rates through the native protocol;
  switch scope trace groups by unit instead of mixing voltage and current axes.
- Deliver every retained waveform sample in bounded chunks, report real history
  gaps and producer watermarks, and replace captures explicitly on restart.
- Make F7 and the command palette use the toolbar's selected advance interval.

### Named indicator evidence

- Add Kingbright WP7113ID and WP154A4SUREQBFZGC with exact terminal mappings,
  source body dimensions and bounded 25 °C, 2–20 mA nominal DC curve evidence.
- Fit RGB die slopes/resistances independently; preserve parameter inheritance
  and allocate only active internal rows for zero/mixed series resistance.
- Keep open LED/RGB leads floating without reordering terminals or inventing a
  ground return; add source holdouts, driver load lines and an interactive example.
- Show checked envelopes and source snapshots in part details, with explicit limits.

### Circuit construction and live RGB indication

- Edit/select/trace bench jumpers by keyboard or pointer; change color, rise and
  route points without resetting simulation, reconnect atomically, and undo wiring
  together with appearance. Trace connected strip contacts across shared views.
- Reject invalid net endpoints, duplicate/reversed jumpers, reused direct holes
  and malformed routes in native load/commit and schema/semantic validation.
- Model discrete RGB LEDs as three current-carrying junctions in electrical mode;
  show engine-driven mixed light in 3D and keep floating functional LEDs off.
  Add an interactive resistor-fed RGB acceptance circuit with all color mixes.

### Responsive workspaces

- Keep project, perspective and utility navigation in a clear header row, with
  simulation controls below; constrain long project names and keep popups usable.
- Fit open dock panels to compact windows without replacing saved proportions or
  folded choices; resize from the visible pane edge and restore sizes on expansion.
- Make status details keyboard-scrollable and reduce schematic ribbon height;
  show schematic tools in their own view while the bench uses its physical controls.

### Workbench startup

- Load the 3D bench when opened, keeping its geometry/Three.js code out of the
  initial schematic entry. Keep part selection available during loading and
  recover to 2D with an explicit reload action if its files cannot be fetched.

### The ESP32-C3's radios (their plan, steps 1 to 3; step 4's WPA2; step 6's advertising and connections)

- **Arduino-ESP32's Wi-Fi and BLE sketches**, unmodified
  (`tests/vendor/arduino-esp32`): `WiFiScan` lists the bench's two access
  points with their RSSI, channel and security; `WiFiClientConnect` joins
  the WPA2 network it names and prints its DHCP address; the BLE library's
  `Server` (Bluedroid, as Arduino-ESP32 3.2.0 builds it) is connected to by
  a tablet's GATT browser, which reads "Hello World says Neil"; `iBeacon`
  advertises a beacon a phone's scanner decodes. So both ESP-IDF's BLE
  hosts, NimBLE and Bluedroid, run on the controller.
- **Wi-Fi's power save on the ESP32-C3** (the radios plan, step 5): the
  MAC's station TSF (loaded from the access point's beacons), the TBTT's
  interrupt ahead of each target beacon time, four TSF timers and the
  power interrupt (source 2), learnt from libpp's `hal_tsf.o`; the radio
  is deaf while ESP-IDF has the Wi-Fi/BT common clock off between
  beacons. A station in power save wakes for each DTIM beacon and keeps
  its access point; `http_request` reads 21.9 mA on the USB power meter
  in power save, 102 mA while it scans.
- **Parts draw the current their datasheets give** (ADR-036, new): a
  part's model sets the current its supply pins draw; under the
  electrical policy each power pin a supply of the circuit feeds sinks
  its share to the part's ground, less as its supply falls away. An MCU
  description's `supply` (new in the schema) gives its datasheet's
  currents: the core running and waiting at its clock, held in reset, its
  low-power modes, its radio listening and sending by modulation.
  - The ESP32-C3's, from its datasheet v2.4 (5.6): 13 to 23 mA by clock
    and activity, 1 uA held off, receiving 84 mA, sending 335 mA at
    802.11b's rates, 285 mA at 11g's, 276 mA at 11n's, frame by frame.
  - `emblink/usb_power_meter` (new), an inline USB tester between a port
    and a board's cable: VBUS's voltage, the current and the power
    averaged over its refresh (twice a second) at its resolution, the
    mAh and mWh since plugged in; its 50 mOhm in VBUS; its own draw from
    the host's side, not shown; dark below 3.5 V. Four fixtures.
  - ESP-IDF's `hello_world` and `http_request` run through the meter:
    16.7 mA idle at 160 MHz, 102 mA while the radio scans.
- **The air as phones and sniffers see it** (the ESP32-C3 radios plan,
  step 7):
  - `emblink/wifi_scanner` (new), a phone's Wi-Fi analyzer: it scans
    channels 1 to 13 as a phone does (40 ms each, a probe request for any
    SSID on each, every 10 s; or passively) and lists the networks it
    hears, the strongest first: SSID (hidden ones' empty), BSSID, channel,
    security from the Privacy bit, the RSN element's AKM suites and WPA's
    vendor element, RSSI.
  - `emblink/air_sniffer` (new), a sniffer for Wireshark: a Wi-Fi adapter
    in monitor mode on one channel (every frame with its FCS) or a BLE
    sniffer on one advertising channel (the access address, the PDU and
    its CRC-24), the snapshot length as tcpdump's -s, the last 64 frames
    in its `capture` output.
  - The workbench's Air panel (Run, the Air tab): the phones' networks and
    devices with signal bars and an iBeacon's fields; the sniffer's frames
    gathered as the session reports them and named as Wireshark's packet
    list names them (management frames by SSID, EAPOL's messages 1 to 4,
    ARP, IPv4 with its ports, BLE PDUs with their addresses and names); a
    frame's bytes; Save .pcap (radiotap, or
    LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR, nanosecond timestamps). A phone or
    a sniffer can be added from the panel.
- **The network beyond the router** (`core/engine/inet.h`, new): the
  internet a router's WAN port reaches. IPv4 packets go between the
  routers' WAN sides and the hosts out there after each link's latency;
  a name resolves to the host that has it.
  - `emblink/wifi_ap`'s router has a NAT (TCP and UDP ports and ICMP echo
    identifiers mapped to its own from 49152 on its WAN address, `wan_ip`,
    `wan_latency`) and a DNS forwarder at its address, the DNS server its
    DHCP gives (A queries answered after a round trip to the ISP's
    resolver, or NXDOMAIN). Its log has the names resolved and the
    addresses reached.
  - `emblink/web_server` (new): a web server on the internet, by its DNS
    name at its address. Its TCP answers SYN with SYN-ACK and its MSS,
    follows the client's MSS and window, acknowledges in order, sends the
    response then FIN, retransmits after 1 s (doubling, five times),
    keeps TIME-WAIT and answers a stray segment with RST. Its HTTP/1.x
    serves GET and HEAD of its path (and its index.html), 404 and 501
    otherwise, and it answers pings.
  - ESP-IDF's `protocols/http_request` (`http_request`, 15 assertions)
    and Arduino-ESP32's `HTTPClient/BasicHttpClient` (7 assertions)
    resolve example.com, connect and print its page, unmodified.
- **The softAP serves a station on another board** (`wifi_softap`, 15
  assertions, both examples unmodified): WPA3's SAE, the 4-way handshake
  and DHCP, a 40 MHz BSS on channel 1.
  - **The RSA accelerator** (`rsa.c`, new): modular multiplication and
    exponentiation through Montgomery products with the r and M' the
    driver writes, multiplication, operands to 3072 bits, SEARCH_POS, the
    interrupt. ESP-IDF's mbedtls uses it for SAE's elliptic curves; the
    access point's start waited on it for ever.
  - **Fixed: the AES and SHA accelerators raised the wrong interrupt
    sources** (51 and 52, the CPUs' FROM_CPU_INTR1 and 2, which FreeRTOS
    uses); they are 48 and 49, and RSA's 47, as the interrupt matrix's
    map registers have them.
  - **A 40 MHz BSS** (`wifi.c`): the RF PLL sits at the channel pair's
    centre and the MAC's +0x400 says where the primary channel is; frames
    go out and are heard there.
  - **The key entry a frame names** (`wifi.c`): the PLCP word's bits
    21:17 pick the key the MAC encrypts under, as the access point's
    frames to its stations need (only bits 19:17 were read before, so
    entry 8 read as none and its frames left in clear, as ESP-NOW's
    encrypted unicast did with entry 24). Protected management frames,
    ESP-NOW's action frames, are decrypted on receipt when the interface's
    management crypto (+0x800 bit 17) is on.
- **ESP-NOW between two boards** (`espnow`, 12 assertions, unmodified):
  ESP-IDF's `wifi/espnow` broadcasts, each board adds the other as a
  peer, and one sends the other encrypted unicast data. The Wi-Fi MAC's
  +0x207C (`hal_random`) is now a new random number at each read:
  net80211 waits there for a nonce that differs from the last, so
  `esp_now_send` had never returned. Each chip (its `settings.mac`) draws
  its own random numbers, from RND_DATA too.
- **Bluedroid's `ble_ibeacon` on two boards** (`ble_ibeacon`, 14
  assertions, unmodified): the sender advertises its iBeacon, and the
  same example built as the receiver (`IBEACON_RECEIVER`, `run.py`'s
  variants) on a second board scans actively and logs the beacon's UUID,
  major, minor and the RSSI its controller reports (-21 dBm 30 cm away).
  The core runs active scan events (format 9): T_IFS after a scannable PDU
  the SCAN_REQ of its transmit descriptor, with the specification's
  backoff, and the SCAN_RSP taken.
- **Scan requests and responses** (`ble.c`): after ADV_IND or ADV_SCAN_IND
  the core takes a SCAN_REQ for its address and sends T_IFS later the
  scan response the controller keeps at the advertising descriptor's next
  pointer (`r_lld_adv_scan_rsp_data_set`). `emblink/ble_scanner` now scans
  actively, as phones do, with the specification's backoff, so a phone
  lists the names advertisers put in their scan response (the iBeacon
  sketch's "ESP32").
- **BLE packets collide on the air**: two packets on one channel that
  overlap are both lost unless a receiver hears one 21 dB above the other
  (BLE's co-channel rejection). With bleprph's phone and tablet both
  answering the same ADV_IND, the phone's SCAN_REQ and the tablet's
  CONNECT_IND collide until the phone's backoff leaves the tablet a turn;
  `emblink/ble_central` takes a connection not heard from in its first six
  intervals as failed to be established (0x3E) and connects again, as the
  specification has it.
- **`emblink/ble_scanner` decodes iBeacons**: Apple's manufacturer data
  (company 0x004C, type 0x02) gives the proximity UUID, major, minor and
  measured power, as a beacon app shows them.
- **Bluetooth LE advertising** (`ble.c`, step 6). The ESP32-C3's BLE core,
  RivieraWaves' IP as the controller in the ROM drives it: its version,
  soft resets, the native clock (half-slots and the half-microsecond count
  down), the 10 ms, half-slot and half-microsecond timers, the interrupt
  FIFO on source 8, the exchange memory's mapping into RAM, the exchange
  table's events started by +0x100, and an advertising event sending its
  PDU on each channel of its map at +9 dBm. The chip's Wi-Fi and BLE share
  its port to the air (`radio.c`). NimBLE's `bluetooth/nimble/bleprph`,
  unmodified, advertises as "nimble-bleprph" (`bleprph`, 6 assertions).
- **Bluetooth LE as the central, two boards on one air** (`ble.c`): scan
  events (format 8) take every advertising PDU on their channel for their
  window; initiating events (format 14) answer their peer's connectable
  PDU with the controller's CONNECT_IND, its window offset aimed at the
  first anchor the controller planned; connection events as the master
  (format 2); channel selection algorithm #2. ESP-IDF's `blecent` on one
  board finds `bleprph` on another, connects as the master and runs its
  six GATT operations to the notification (`blecent`, 17 assertions, the
  two examples unmodified; `run.py` builds a project's other boards'
  examples too).
- **A board's own chip**: a project's board `settings.mac` gives its chip's
  factory MAC (an ESP32-C3's eFuse BLOCK1), so two boards are two chips.
- **Bluetooth LE connections** (`ble.c`): the core listens after a
  connectable advertising PDU and takes a CONNECT_IND into its receive
  descriptors; connection events as the slave on channel selection
  algorithm #1, its receive window, the answer from its transmit
  descriptor or an empty packet, the sequence numbers' acknowledgement and
  resending, the receive, transmit and end interrupts; the exchange
  memory's blocks mapped by the hardware's fixed table. NimBLE's `bleprph`,
  unmodified, is connected to and logs it; a tablet's GATT browser
  discovers its services and reads its characteristic (`bleprph`, 14
  assertions).
- **`emblink/ble_central`**, a phone or tablet with a GATT browser (nRF
  Connect): it scans, connects to its target with a CONNECT_IND, holds the
  connection as the master (channel selection #1, acknowledgement,
  L2CAP's fragments, the slave's link layer procedures answered, the
  supervision timeout), discovers the GATT database (MTU, primary
  services, characteristics) and reads the readable values.
- **The air synchronises on BLE packets**: a BLE packet reaches the radios
  tuned to it when its access address ends, and they take it whole, so a
  receiver's window need only cover its start.
- **The ESP32-C3's ROM image** now holds the Bluetooth controller's initial
  data, which ESP-IDF's `btdm_controller_rom_data_init` copies from
  0x4005966C: `esprom.py` takes more than one copy table (`--data-table`
  again for the entry at 0x400591F4). Without it the controller's MISC task
  had no handler and each advertising buffer freed after a connection
  leaked.
- **`emblink/ble_scanner`**, a phone's BLE scanner app: passive scanning
  on channels 37, 38 and 39 in turn with a window and interval, the
  advertisers heard with their address, PDU type, name and RSSI. It lists
  `bleprph` 2 m from the board at -37 dBm.
- **WPA2-Personal end to end.** ESP-IDF's `wifi/getting_started/station`
  as it ships (WPA2-PSK, `myssid`/`mypassword`) joins the bench's access
  point and leases its address (`wifi_station`, 13 assertions): the
  station's wpa_supplicant derives the PSK by PBKDF2 on the SHA
  accelerator, the access point runs the 4-way handshake (IEEE
  802.11-2020 12.7.6: the PTK, MICs, the GTK wrapped under the KEK, EAPOL
  retransmission), the station unwraps the GTK on the AES accelerator and
  installs both keys in the MAC, whose CCMP encrypts and decrypts its data
  frames.
- **The ESP32-C3's MAC: CCMP** (`wifi.c`): its key table (+0x1400, 40-byte
  entries, the valid bits at +0x814) and crypto enables, learnt from
  libpp's hal_crypto_* routines; a frame queued with the PLCP word's
  encryption leaves under the receiver's key, its PN the driver's; a
  protected frame received is decrypted in place for net80211's
  ccmp_decap.
- **The ESP32-C3's SHA accelerator: DMA-SHA** (DMA_START, DMA_CONTINUE,
  DMA_BLOCK_NUM, the interrupt), its blocks from the GDMA, as ESP-IDF's
  mbedtls port uses it for every SHA-1 and SHA-2 hash. **The AES
  accelerator** (`aes.c`, new): Typical AES and DMA-AES with ECB, CBC,
  OFB, CTR, CFB8 and CFB128, AES-128 and AES-256, the IV kept, the
  interrupt. Both were missing: ESP-IDF's PBKDF2 aborted on an all-zero
  digest, and its AES waited for ever.
- **`util/wcrypto`**: SHA-1, HMAC, PBKDF2, 802.11's PRF, AES-128/256, the
  key wrap and CCMP, checked against FIPS 180-4, RFC 2202, IEEE 802.11
  Annex J, FIPS 197, RFC 3394 and SP 800-38A's vectors.

- **The air** (`core/engine/air.h`): the 2.4 GHz medium a session's radios
  share. A frame is carried whole with its channel, power, rate and
  airtime (802.11's DSSS and OFDM preambles and symbols, BLE's packet);
  it reaches every radio tuned to its channel when it ends, at the power
  the free-space path loss leaves over their distance on the bench, above
  a receiver's sensitivity; a unicast frame taken is answered with an ACK
  the sender hears a SIFS and an ACK's airtime later.
- **The ESP32-C3's Wi-Fi MAC** (`wifi.c`, step 3), learnt from what
  ESP-IDF's libpp does with its registers: five transmit queues on
  descriptor lists sending one frame at a time at the PLCP word's rate on
  the RF PLL's channel, their completions (sent, or no ACK) and interrupt;
  the receive list filled with ESP-IDF's rx_ctrl header for frames the
  station's or the access point's address filter takes, its reload; the
  interrupt status on source 0; the microsecond counter and the TSF latch.
- **`emblink/wifi_ap`**, a Wi-Fi access point across the room with a
  router behind it: beacons (SSID, rates, channel, TIM, ERP, WPA2-Personal's
  RSN element), probe responses, open authentication, association; data
  frames to and from its stations, kept for dozing ones and announced in
  the TIM until a PS-Poll or a wake; the router answering ARP and pings and
  leasing addresses by DHCP; its stations and their leases in its outputs.
  ESP-IDF's unmodified `wifi/scan` lists two of them on the bench as a
  board does: HomeNet (WPA2-PSK, CCMP, channel 6) at -30 dBm from 3 m and
  CoffeeShop (open, channel 1) at -42 dBm from 12 m. Its
  `wifi/getting_started/station`, set in menuconfig for CoffeeShop,
  authenticates, associates, starts modem sleep and gets 192.168.1.100 by
  DHCP (`wifi_station_open`, 13 assertions).
- A board's own parts (its MCU, its sensors) are where the board is on the
  bench, for the world's rays and the air's distances. A component may have
  no pins (one that reaches the circuit only through the air).

- **The plan** (`docs/engineering/c3-radios.md`): Wi-Fi and Bluetooth LE
  modelled at the registers ESP-IDF's closed radio libraries drive, so
  that ESP-IDF's and Arduino-ESP32's examples run unmodified, with an air
  that carries frames between boards and the bench's access points and
  BLE peers.
- **Fixed:** the ESP32-C3's ROM image lacked `.rodata.interface`, which
  Espressif's ROM ELF links at 0x3FF1EE3C (ROM1 on the data bus) but loads
  with no segment: the Wi-Fi and Bluetooth code's pointers into the ROM's
  RAM and their constant tables read zeros, and ESP-IDF's `esp_wifi_init`
  panicked storing through `our_instances_ptr`. `esprom.py --alias
  VADDR=PADDR:SIZE` places a section linked in an alias of the ROM; the
  image changes only in those 0x11C4 bytes.
- **The PHY's start (step 2).** The radio's register blocks as ESP-IDF's
  closed PHY drives them: FE2, FE, the receive gain table, the AGC and
  NRX, the baseband (taken out of the register file, where the SVD named
  one of its registers) and the Wi-Fi MAC's registers keep what is
  written; the calibrations answer as an ideal radio (the BBPLL and RF PLL
  calibrated and locked, the power detector's, DC and IQ measurements
  done). ESP-IDF v5.4's `wifi/scan` runs unmodified, as on a board's first
  start: a full RF calibration saved to NVS, the station with the eFuse
  MAC and a scan of every channel. It joins the ESP-IDF suite.
- **Fixed:** the ESP32-C3's temperature sensor registers (APB_TSENS_CTRL,
  TSENS_CTRL2) were gated by APB_SARADC's clock; they follow TSENS_CLK_EN,
  the one ESP-IDF turns on to use them. A register may now have a clock
  of its own in the register file.

### EmbLinkRTOS on the ESP32-C3-DevKitM-1 (the RTOS boards plan, part 2, step 8)

- **EmbLinkRTOS's `esp32c3_devkitm1` board** runs its conformance suite on
  EmbSIM's ESP32-C3-DevKitM-1 (`emblink/esp32c3_devkitm1`): the twelve
  test images of its checked base profile, each a flash image Espressif's
  boot ROM checks and loads, pass. Its RISC-V port takes the tick from
  SYSTIMER through the interrupt matrix and its console is UART0, the
  board's USB-serial bridge. EmbLinkRTOS's `rv32-embsim` CI job builds
  EmbSIM and runs them there, as `cm4-embsim` does on the NUCLEO-F446RE.
- The ESP-IDF gptimer scenario pins each event by a window, not by the
  log's millisecond timestamps, which move with the image's size.
- **The ESP32-C3-DevKitM-1 is qualified against its reference**
  (`reference.json`): Espressif's V1.0 dimension drawing, read from the
  PDF's vector paths, gives the outline, the 30 header pads and the
  micro-USB's body, and places every other part. The artwork's pads had
  been 0.39 mm high, the RGB LED 1.1 mm and the micro-USB 0.64 mm out; the
  header labels are now the board's narrow vertical silkscreen. The
  reference check reads oblong (rect) pads as well as round ones.

### ESP-IDF's examples on the ESP32-C3-DevKitM-1 (the RTOS boards plan, part 2, step 7)

- **`tests/vendor/esp-idf/`** builds ESP-IDF v5.4's own examples
  unmodified, as a user does (`idf.py set-target esp32c3`, `idf.py build`),
  puts the bootloader, partition table and application into a 4 MB flash
  image at flasher_args.json's offsets, and runs each on the board under a
  scenario: hello_world (to its restart), blink (the SK6812 on GPIO8
  through RMT and led_strip), ledc_basic (4 kHz on GPIO5), temp_sensor (the
  room's 25 degC, uncalibrated as BLOCK2 is blank), adc_oneshot_read (1.1 V
  and 2.2 V on IO2 and IO3), i2c_eeprom (48 bytes written to a 24LC256
  and read back), gptimer (TIMG0's alarms, stop, reload and moved alarm,
  each count as the ISR reads it), uart_echo (UART1 on IO4 and IO5 to a
  terminal and back) and real_time_stats (FreeRTOS's run-time statistics
  of six spinning tasks). CI's `esp-idf` job fetches ESP-IDF v5.4 and its
  RISC-V toolchain and runs them.
- **`tests/vendor/arduino-esp32/`** builds sketches with Espressif's
  Arduino-ESP32 3.2.0 (on ESP-IDF v5.4) through arduino-cli as the IDE
  builds them for "ESP32C3 Dev Module", and runs the merged flash image:
  Blink (LED_BUILTIN is the RGB LED, white at 64 through RMT),
  DigitalReadSerial, Fade (LEDC at 1 kHz on pin 9), GetChipID (the eFuse
  MAC's last bytes) and RepeatTimer (a timer's ISR each second until the
  button on IO0 ends it). CI's `arduino-esp32` job installs the platform as
  the board manager does.
- **The 24LC256** (`emblink/eeprom_24lc256`), Microchip's 256-kbit I2C
  EEPROM: two address bytes, 64-byte page writes with the page wrap, the
  self-timed write cycle without ACK (ACK polling), the address from
  A0-A2, write protect; five fixtures. The KiCad importer maps it.
- **Scenarios** take `not_contains`: text a serial, value-text or memory
  probe must not show (hello_world's restart not before its countdown).
- **Fixed:** a sleeping core's devices got no quantum for an event an input
  gave them (the ESP32-C3's I2C master seeing SCL high while ESP-IDF waited
  in WFI): they caught up at the next wake-up and the edges in between ran
  together on the pins. The core's next quantum now comes at that event.
- **Fixed:** `embsim run --target` under the functional policy refused a
  board with a USB port (the DevKitM-1, the NUCLEO-F446RE): the port's 5 V
  source is an electrical primitive. The board now runs unplugged there,
  its supplies ideal, as EmbLinkRTOS's EmbSIM runner asks.
- **Fixed:** a pin the ESP32-C3's GPIO matrix gave U0RXD or U1RXD was not
  the UART's to the runtime (only GPIO20 through the IO MUX was): ESP-IDF
  connects TXD first, so UART1 never received what came in on its pin.

### The ESP32-C3's peripherals (the RTOS boards plan, part 2, step 5)

- **The GPIO matrix** carries any modelled peripheral's output signal and
  enable to the pins and gives its inputs back through IN_SEL (or a pin's
  IO MUX function), with the TRM table's defaults; a model can follow the
  pins' inputs.
- **RMT** (`esp32c3-rmt`): two transmitters and two receivers over the
  192-word RAM, CONF_UPDATE, normal, wrap and continuous modes with loop
  counts, the carrier, simultaneous starts and TX_STOP; the receivers' idle
  threshold, filter, demodulation and wrap; the edges at the clock that
  makes them, so the board's SK6812 sees exact widths.
- **LEDC** (`esp32c3-ledc`): four timers with the fractional divider and six
  generators with HPOINT, duty, dithering, fading (ESP-IDF's `ledc_set_duty`
  is a one-step fade) and the overflow counter.
- **I2C** (`esp32c3-i2c`): the master's command list (RSTART, WRITE, READ,
  STOP, END) at its timing registers' bus timing, a slave's stretching,
  arbitration, the timeout and the bus-clear pulses; the slave with 7- and
  10-bit addresses, the general call, double addressing, ACK control and
  stretching; the FIFOs with their interrupts.
- **GP-SPI2** (`esp32c3-spi2`) as a master: CS setup and hold, CMD, ADDR,
  DUMMY and data on 1, 2 or 4 lines or QPI, full and half duplex, the four
  clock modes, the bit orders, its buffer or the GDMA.
- **GDMA** (`esp32c3-gdma`): three transmit and three receive channels on
  descriptor lists with the owner check, RESTART, write-back, the EOF
  interrupts and memory-to-memory transfers.
- **The SAR ADCs and the temperature sensor** (`esp32c3-adc`): one-time
  sampling with the attenuations, the timer's pattern-table scanning into
  the GDMA, the filters and threshold monitors, the self-calibration's
  grounded input; the sensor reads the world's temperature through
  ESP-IDF's offset table.
- `svd2regs.py --memory` adds the memories an SVD leaves out (the RMT's RAM,
  the I2C controller's FIFOs) as word registers, recorded in the source.
- TWAI, I2S, UHCI, the crypto accelerators but SHA, GP-SPI2's slave mode and
  the radios are declared.

### The ESP32-C3-DevKitM-1 (the RTOS boards plan, part 2, step 6)

- **The board** (`emblink/esp32c3_devkitm1`), from Espressif's V1.0
  schematic and dimension drawing: the ESP32-C3-MINI-1 module (a new
  package of the ESP32-C3, `mini1`), the micro-USB's VBUS through a
  1N5819HW to 5 V, the SGM2212-3.3 regulator (a new preset from SG Micro's
  datasheet), the 5 V LED, R5 and C6 delaying CHIP_PU at power-up, the RST
  and BOOT buttons, R20 holding GPIO8 high, the RGB LED on GPIO8, the two
  15-pin headers, an original artwork and its place on the 3D bench. The
  CP2102N is the console's bridge on UART0 and a host serial port. Pressing
  RST restarts the chip with `rst:0x1 (POWERON)`; holding BOOT through it,
  the ROM waits for a download. Six fixtures check the power path, the
  reset delay and the buttons.
- **The SK6812MINI-HS** (`emblink/sk6812mini_hs`), the board's RGB LED,
  with OPSCO's timing (0 and 1 told apart at 0.49 us, a reset of more than
  80 us); the WS2812B's Lua model now takes a part's figures as parameters.
  The 3D bench shows an addressable LED in the colour it reports.
- **An ESP32-C3's CHIP_EN** is its reset pin: low, the chip is off and in
  reset, without the pull-up an NRST has.
- **`embsim run --target`** plugs the board into a USB port (5 V on its
  VBUS), so a board whose MCU waits for its supply starts as on the bench.
- ESP-IDF v5.4's hello_world runs on the board from its flash image.
- **Fixed:** a peripheral reset by its MCU (STM32's RCC, RP2040's RESETS,
  the ESP32-C3's SYSTEM) no longer registers the peripheral's clock a second
  time: a timer reset that way counted twice as fast (ESP-IDF's FreeRTOS
  tick on SYSTIMER ran at 2 kHz instead of 1 kHz), and a stopped one stayed
  listed. A unit test resets SYSTIMER and TIMG0 and checks their rates.

### The ESP32-C3's pins and ESP-IDF's hello_world (the RTOS boards plan, part 2, step 4)

- **The GPIO matrix and IO MUX** (`esp32c3-gpio`, `esp32c3-iomux`): each
  pad's reset configuration from the TRM's table 5.12-1 (U0TXD and U0RXD
  on GPIO21 and GPIO20, the pull-ups on GPIO9, MTCK and USB D+), outputs
  through the matrix's 128 signals or the pad's IO MUX function, open
  drain, inputs through FUNCn_IN_SEL, the five interrupt types on the CPU
  and NMI sources (16, 17), and the UARTs' TX and RX on whichever pins the
  matrix gives them.
- **USB Serial/JTAG** (`esp32c3-usbjtag`), as on a DevKitM-1 with no host
  on its D- and D+: 64 bytes of send buffer, flushed by the 64th byte or
  WR_DONE and then never read, the interrupts on source 26. The ROM and
  ESP-IDF time out on it as the TRM asks (30.4).
- **What ESP-IDF's start needs**: the internal I2C master to the analog
  blocks (`regi2c.c`, as the ROM's regi2c routines drive it, with the
  o-code calibration's done flags); RND_DATA's random numbers
  (`esp32c3-apbctrl`, a seeded generator, the same every run); the radio's
  register blocks FE, FE2 and NRX (`radio.c`); the ROM's last word, the
  `ets_rom_layout_p` pointer (`esprom.py --word`); the performance counter
  reached through 0x800-0x802, where the ROM starts it; the timer group's
  periodic RTC_SLOW_CLK calibration from reset and its timeout; a CPU
  reset clearing the permission control's locks (TRM 14.8).
- **ESP-IDF v5.4's hello_world runs unmodified**: built by `idf.py`, its
  bootloader, partition table and app merged into the flash as `idf.py
  flash` writes them, it boots through the ROM and the second-stage
  bootloader, prints its banner, chip revision v0.3 and `Hello world!`,
  counts down on FreeRTOS's tick and restarts with `rst:0xc
  (RTC_SW_CPU_RST)`.

### The ESP32-C3's clocks and timers (the RTOS boards plan, part 2, step 3)

- **SYSTEM** (`esp32c3-system`): CPU_CLK as the TRM's tables 6.2-1 and
  6.2-2 have it: XTAL_CLK / (PRE_DIV_CNT + 1), 20 MHz out of reset, the
  PLL's 80 or 160 MHz, or RC_FAST_CLK; APB_CLK 80 MHz from the PLL. The
  peripherals' clock enables gate their registers, with a diagnostic, and
  their reset bits reset them (the ROM itself turns on the SHA
  accelerator's and the Debug Assistant's clocks). CPU_INTR_FROM_CPU_0-3
  are the software interrupts, matrix sources 50-53.
- **The system timer** (`esp32c3-systimer`): two 52-bit counters at
  16 MHz with update, value-valid and load; three comparators, in target
  mode (an alarm at once for a target already passed) or period mode,
  their level interrupts on sources 37-39.
- **TIMG0 and TIMG1** (`esp32c3-timg`): T0's prescaler, direction, latch,
  load, alarm and auto-reload; the MWDT's four stages and their actions,
  its write key, its configuration taking effect at WDT_CONF_UPDATE_EN and
  the flash-boot protection: a blank chip now resets as a real one does,
  `rst:0x7 (TG0WDT_SYS_RST)` after 1.3 s; the RTC_SLOW_CLK calibration
  ESP-IDF runs at start-up, timing out without a 32 kHz crystal.
- **The eFuse controller** (`esp32c3-efuse`): read and program commands
  over eleven blocks. The chip's factory bits are a revision v0.3 in the
  QFN32 package with 4 MB of XMC flash and a MAC in Espressif's OUI, as
  esptool decodes them.
- The ESP32-C3's devices run on lazy clocks, as the STM32's do: the ROM's
  polling loops run at about 0.7 times real time instead of 0.3.

### The ESP32-C3 boots its own ROM from its flash (the RTOS boards plan, part 2, step 2)

- **Espressif's boot ROM** runs on EmbSIM's ESP32-C3, unmodified: its image
  is made from Espressif's Apache-2.0 ROM ELF by `tools/importers/esprom.py`
  (the loadable segments, and the RAM variables' initial values its own
  copy table names) and checked against the description's SHA-256. It
  prints its banner, the strapping (`boot:0x8 (SPI_FAST_FLASH_BOOT)`) and
  the reset cause, then boots the flash:
  - a blank flash: `invalid header: 0xffffffff`, again and again, until the
    RTC watchdog's flash-boot protection resets the part
    (`rst:0x10 (RTCWDT_RTC_RST)`, then `Saved PC:0x...`);
  - an ESP image: its segments loaded through SPI1, the checksum and the
    SHA-256 checked, `entry 0x...`; a wrong digest is reported and the
    image booted anyway, as with secure boot off.
- **The in-package flash**, an XMC XM25QH32C as its datasheet has it: the
  standard, dual and quad reads, page program and the erases with their
  typical busy times (0.5 ms to 20 s), the three status registers, the
  security registers, suspend and resume, power-down and ultra-deep
  power-down, the IDs (`20 40 16`), the reset pair.
- **SPI0 and SPI1** (`esp32c3-spimem`): the user command's phases and the
  fixed commands as the ROM and ESP-IDF drive them.
- **The cache and MMU** (`esp32c3-cache`): 128 entries mapping the flash's
  64 KB pages on the instruction bus (0x42000000) and the data bus
  (0x3C000000); invalidate, preload, lock and freeze complete at once. A
  page shows the flash as it was mapped until an invalidate covers it.
- **The SHA accelerator** (`esp32c3-sha`): SHA-1, SHA-224 and SHA-256 in
  Typical SHA mode, interleaving included.
- **The Debug Assistant's PC logging** (`esp32c3-assist`): the PC and SP at
  a CPU reset, which the ROM prints as `Saved PC`.
- **A project's `bin` firmware** is a flash image written at offset 0, as
  `esptool.py write_flash 0x0` writes `merge_bin`'s output; it is written at
  power-on only, so the flash keeps the program's writes through the
  part's other resets. An ESP32-C3 without firmware runs its ROM on an
  erased flash. Memory descriptions gain `external` (a store no address
  reaches) and `window` (bytes a device provides) kinds, and `file` and
  `sha256` for a region's factory contents (SPEC-006).
- **UART fix (ESP32-C3):** TX_IDLE_NUM is the idle time between transfers,
  after the FIFO empties, not after every frame. With its reset value, 256
  bit times, every character took 266 bit times instead of 10, and the ROM
  printed about 27 times slower than its 115200 baud.
- The bus gives an access to the smallest device whose range holds it (the
  MMU table inside the register file's span), and the register file finds
  a register with one search instead of five.
- The RISC-V core's ESP-RV variant has the user CSRs and the user-mode
  counter the ROM writes (`ustatus`, `utvec`, `uscratch`, ...).

### The ESP32-C3's core and interrupt controller (the RTOS boards plan, part 2, step 1)

- **The ESP-RV core** (`esp-rv32imc`), as the ESP32-C3 TRM's chapter 1 has it:
  - RV32IMC with user mode; the AMOs are illegal, as are mie, mip and the
    standard counters, which the part does not have;
  - mstatus's MIE, MPIE, MPP and TW; mtvec vectored only, on a 256-byte
    base; five-bit mcause codes; MRET clears MPIE;
  - the performance counter (mpcer, mpcmr, mpccr), counting cycles outside
    WFI, instructions, idle cycles, loads, stores, jumps, branches, taken
    branches or compressed instructions, saturating or wrapping;
  - the trigger module: eight triggers matching an address exactly or a
    NAPOT region, on execute, load or store, raising a breakpoint
    exception; tcontrol's MTE and MPTE;
  - PMP without static priority (any matching entry may grant), NAPOT up to
    1 GB; the cpu_gpio CSRs.
- **The interrupt matrix and controller** (`esp32c3-intmtx`): 62 sources
  routed onto CPU interrupts 1-31, level and edge types, priorities 1-15
  and the threshold, the 4-cycle latency, the clear toggle for claimed edge
  interrupts.
- **`espressif/esp32c3`**: the register file converted from Espressif's
  Apache-2.0 esp32c3.svd, the QFN32 pinout, the memory map with SRAM1 and
  ROM1 on both buses.
- `svd2regs.py`: field arrays step by `dimIncrement` (Espressif's RMT had
  its channels' bits on top of each other), and `--drop` leaves out a
  peripheral that is another's registers (the C3's RNG). The register file
  takes up to 1024 registers per peripheral (the DS block's memories).
- The RISC-V core's model `rv32imc` no longer executes the A extension.

### EmbLinkRTOS on the NUCLEO-F446RE (the NUCLEO-F446RE plan, step 11)

- **EmbLinkRTOS's `nucleo_f446re` board** runs its conformance suite on
  EmbSIM's NUCLEO-F446RE (`emblink/nucleo_f446re`): the ten test images of
  its checked base profile pass in 4 s. The board brings the part from HSI
  to 180 MHz as RM0390 orders it: HSE bypass from the ST-LINK's MCO, the
  PLL, over-drive, five wait states. Its console is USART2, the virtual COM
  port. EmbLinkRTOS's `cm4-embsim` CI job builds EmbSIM and runs them
  there, as `avr-embsim` does on the Uno.

### Two boards over I2C, and the F4's flash (the NUCLEO-F446RE plan, step 10, part 2)

- **CubeF4's I2C_TwoBoards examples** run on two NUCLEO-F446RE boards wired
  as their readmes draw them: ComPolling, ComIT, ComDMA and AdvComIT. The
  slave's image is the example with `#define MASTER_BOARD` commented out,
  as each readme asks. Every buffer arrives whole, and SCL's edges count
  exactly the bytes sent.
- **EEPROM_Emulation** runs its thousands of writes of three variables
  through the F4's flash interface (`stm32f4-flash`):
  - KEYR's unlock;
  - programming at PSIZE's width, bits going only from 1 to 0;
  - PGPERR, PGAERR, PGSERR and WRPERR;
  - sector and mass erase;
  - EOP, OPERR and the FLASH interrupt;
  - OPTKEYR and OPTSTRT writing the option bytes, which each reset loads.
  - **Not modelled:** the program and erase times. BSY never reads 1.
- **Two cores on one circuit.** A core no longer stops at another core's
  next quantum. It runs to the next event that is not a core's, and no
  further than one quantum past the core furthest behind (SPEC-002 §5).
  Before, two busy cores stepped each other almost an instruction at a
  time: 50 ms of two NUCLEOs took 194 s, and now takes 2.3 s.
- **I2C fixes, as RM0390 has them:**
  - A slave transmitter's STOP after the master's NACK no longer sets
    STOPF. A stale STOPF used to end the next reception early.
  - A byte received goes to DR over what the program wrote there. The
    HAL writes 0 to clear TXE, and the model then held the next byte as
    if DR were unread.
  - No interrupt is pended again by a register read. A level line
    asserted while its handler runs pends again only at the exception
    return; pending it from the handler's own SR1, SR2 and DR reads ran
    the HAL's handler once more, which cleared ADDR before the receive
    and stalled ComIT and ComDMA after one byte.
- **SPEC-002:** the core quantum's default is 1 000 000 ps (1 µs); the text
  said 1 000 000 000.

### STM32CubeF4's examples on the NUCLEO-F446RE (the NUCLEO-F446RE plan, step 10, part 1)

- **`tests/vendor/stm32cubef4/`** builds STM32CubeF4's own examples for the
  NUCLEO-F446RE unmodified, as their STM32CubeIDE projects build them:
  - the sources each `.project` links;
  - the defines and include paths of its `.cproject`'s Debug configuration;
  - its startup file, linker script and newlib-nano.
  Each runs on `emblink/nucleo_f446re` under a scenario. CI fetches CubeF4
  at the submodule commits it pins.
- **Passing so far (15 scenarios):**
  - GPIO_IOToggle; HAL_TimeBase_TIM, _RTC_ALARM and _RTC_WKUP.
  - RCC_ClockConfig; RTC_Calendar; TIM_PWMInput (a 20 kHz, 25 % input).
  - UART_Printf; UART_ReceptionToIdle_CircularDMA (typed text echoed, also
    across the buffer's wrap).
  - PWR_CurrentConsumption in all six of its modes, each chosen by the
    define its readme uncomments: Sleep, Stop, under-drive Stop, Standby,
    Standby with the RTC, Standby with the RTC and the backup SRAM.
- **The low-power modes of the STM32F4** (RM0390 5.3):
  - **Stop.** The 1.2 V domain's clocks stop: the core, SysTick and every
    peripheral but the RTC and the IWDG, which count on LSE or LSI. HSE
    and the PLLs turn off. Any EXTI line's interrupt or event wakes the
    core on HSI. Under-drive's UDRDY sets with UDEN and LPUDS or MRUDS.
  - **Standby.** RAM is lost; the backup SRAM is kept while BRE holds its
    regulator on. The pins let go, but for the enabled WKUP pins (PA0,
    PC13, inputs with a pull-down) and RTC_OUT. No interrupt wakes it.
  - **Leaving Standby.** A WKUP pin's rising edge, an RTC alarm, wake-up,
    tamper or timestamp event, NRST or the IWDG resets the part. That
    reset is a power reset but for the backup domain: RCC_CSR's flags read
    as at power-on, and PWR_CSR keeps WUF, EWUP and BRE with SBF set.
  - **The flags and option bytes.** WUF already set ends Standby at once,
    and a WKUP pin enabled while high is a wake-up event. CWUF and CSBF
    clear their flags. With nRST_STOP or nRST_STDBY 0 in the option
    bytes, entering the mode resets the part (LPWRRSTF,
    `mcu.low_power_reset`).
  - **The core.** SCR's SLEEPDEEP selects the part's deep sleep,
    SLEEPONEXIT sleeps on the return to Thread mode, and SEVONPEND makes
    an interrupt's pending an event that ends a WFE.
  - **Not modelled:** the wake-up times (the core runs at the wake-up
    event), the regulator's settings (the supply current is not
    modelled), the debug unit's DBG_STOP and DBG_STANDBY.
- **Value probes of memory** (SPEC-007): `reg:B.PERIPH.REG`, `mem:B:ADDR:LEN`
  and `var:B:SYMBOL` read the board's memory as a debugger does. A number
  is the bytes little-endian, up to 8; text is the bytes up to the first
  NUL.
- **Fixes:**
  - The F4's RTC keeps a date register as written, even the forbidden
    values (a date or month of 0), until it next counts. ST's HAL time
    base on the RTC alarm writes DR as 0.
  - A reset asked for from outside the core's run (a pin's edge ending
    Standby) is no longer lost while the core sleeps.

### The Cortex-M core's speed (the NUCLEO-F446RE plan, before step 10)

- **Lazy device clocks on the STM32s.** Devices are no longer ticked after
  every instruction. They are told their cycles when their next event
  comes, or when the program reads or writes them, as the AVR's are.
  - A timer's DMA requests now count as events even without an interrupt.
  - A read of the NVIC's pending bits brings the devices up to date first.
- **Decoded instructions.** The Cortex-M core keeps each instruction it
  fetched from read-only memory (its halfwords, size and cycle cost). A
  region's generation moves on when its bytes change from outside the
  core: an image, a debugger's write, an erase, the memory at 0 remapped.
- **Bursts.** A core runs instructions to the quantum's end without the
  run loop's per-instruction checks while no breakpoint, analysis or
  instruction limit applies.
- **Smaller costs:** the lazy clocks' inline path, and a bus that looks
  first in the region it found last.
- **Result:** a busy firmware on the NUCLEO-F446RE at 180 MHz runs about
  31 million instructions a second on a release build, up from 21 million.
  That is a sixth of the part's own speed. A program that waits in WFI
  between interrupts runs faster than real time.

### The NUCLEO-F446RE board (the NUCLEO-F446RE plan, step 9)

- **`emblink/nucleo_f446re`** (MB1136 C-03, from UM1724 Rev 14 and the
  board's schematics):
  - **Clocks:** HSE is the ST-LINK's 8 MHz MCO (HSEBYP needed) and LSE is
    X2.
  - **LEDs and buttons:** LD2 on PA5 through 510 ohm; B1 on PC13 with R29,
    R30 and C15; B2 on NRST with C14; LD3 on +5V.
  - **Boot and VCAP:** R33 holds BOOT0 low, and C26 sits on VCAP_1.
  - **Headers:** the Arduino headers CN5, CN6, CN8 and CN9 and the morpho
    headers CN7 and CN10, every pin a port. D0 and D1 are not connected,
    as SB62/SB63 leave the factory open.
  - **Console:** the ST-LINK's virtual COM port on USART2 (PA2/PA3,
    115200).
  - **Power paths:**
    - U5V through T2 (STS7PF30L, PWR_ENn) and JP5 to +5V;
    - VIN through U3 (LD1117S50) and D4 (STPS2L30A) to E5V;
    - U4 (LD39050PU33R) to +3.3V;
    - JP6 (IDD) to VDD, VBAT and, through SB57, VDDA.
    - With VIN and JP5 left on U5V, the board stays dark, as UM1724 warns.
    - Removing JP6 and putting an ammeter across `IDD_3V3`/`IDD_VDD`
      powers the STM32 through the meter.
  - **Artwork and toolchains:** original artwork to the 70 x 82.5 mm
    outline; the linker script and the Cortex-M4F toolchain profiles.
- **New parts:** `emblink/sts7pf30l` (P-MOSFET with its body diode) and
  `emblink/stps2l30a` (Schottky), fitted to their datasheets. The
  regulator gains the LD1117S50 and LD39050PU33R presets.
- **`examples/nucleo-f446re-blink-serial`:**
  - the firmware runs at 180 MHz from the MCO;
  - LD2 blinks from SysTick and B1 is counted through EXTI13;
  - the VCP echoes text in upper case;
  - the banner reads F_SIZE and RCC_CSR.
- **Engine changes:**
  - **Initial input values:** a parameter `initially_<input>` gives an
    input its first value on one instance. A toggle switch's
    `initially_closed` is a jumper fitted at the factory.
  - **MCU supply pins:** a part's supply pin (an MCU's VDD) is no longer an
    ideal source when the circuit has a supply of its own. It takes what
    the circuit gives it, so VDD follows JP6. Supply symbols stay sources.
  - **Capacitors** have a pins-only functional model: an open circuit, as
    at DC. A board's decoupling and filter capacitors therefore run under
    every policy.
  - **Fixtures and scenarios:** they may move a board's own parts
    (`B1.JP5_E5V`). A scenario may type into a board's console (input
    `serial` on `B1`, SPEC-008). A board UART takes `rx` as SPEC-007 says.

### The STM32 watchdogs, the F446's RTC and the reset causes (the NUCLEO-F446RE plan, step 8)

- **A reset's cause** (`esk_mach.reset_cause`):
  - the runtime sets power-on and the reset pin;
  - SYSRESETREQ sets software, and the watchdogs set their own.
  - RCC_CSR records it as on the part: PORRSTF, PINRSTF and BORRSTF at
    power-on; PINRSTF with every other reset, plus SFTRSTF, IWDGRSTF or
    WWDGRSTF; the flags are kept until RMVF.
- **The backup domain:** a peripheral marked `backup` keeps its registers
  through every reset but a power-on one. RCC keeps BDCR the same way,
  and BDRST resets both.
- **`stm32-iwdg`** (both families): the keys, PR/RLR with PVU/RVU five LSI
  clocks later, LSI forced on, RLR+1 counts to the reset. The option
  bytes' hardware watchdog is loaded at every reset; the F446's option
  bytes now hold their factory values.
- **`stm32-wwdg`** (both families): the counter at PCLK1/4096/2^WDGTB;
  WDGA, the window, and T6's reset; EWI and EWIF.
- **`stm32f4-rtc`:**
  - **Calendar:** BCD, with its leap years and 12/24-hour modes, from LSE,
    LSI or HSE through the two prescalers.
  - **Setting it:** INIT (counting again four RTCCLK clocks after it), the
    write protections, the shadow registers with RSF, their locking and
    BYPSHAD.
  - **Events:** alarms A and B with their masks and subseconds, and the
    wakeup timer, through EXTI lines 17 and 22. On PC13, the timestamp and
    the tamper (which erases the backup registers) through EXTI21, and
    RTC_OUT.
  - **Adjustments:** the smooth and coarse calibrations, SHIFTR,
    ADD1H/SUB1H.
  - **Storage:** the backup registers.
  - **Time:** counted lazily in the RTC's own clock, at the core clock it
    last saw, and caught up at every RCC write, so a PLL switch keeps the
    room's time.
- **GPIO:** an F4 port without its clock keeps driving its pins as set (the
  part does): only its registers' accesses need the clock.
- **Proof (`tests/unit/test_stm32f4.c`):**
  - the reset flags through power-on, RMVF, SYSRESETREQ (RAM kept) and the
    pin;
  - the IWDG's timing, keys, LSI and hardware option;
  - the WWDG's window, EWI and reset;
  - the RTC across a leap day's midnight, its protections, the shadow
    lock, 12-hour mode and ADD1H;
  - alarm A on PC13's RTC_OUT, the wakeup timer;
  - the backup domain through a reset, the timestamp and the tamper;
  - the calendar across a switch to 180 MHz.

### The STM32F446's ADCs and DAC (the NUCLEO-F446RE plan, step 8)

- **`stm32f4-adc`** for ADC1-3 (RM0390 13):
  - **Groups:** the regular and injected groups, on software starts, the
    timers' and EXTI's triggers with their edges, or JAUTO. An injected
    group interrupts a regular conversion, which starts again after it.
  - **Modes:** scan, continuous and discontinuous.
  - **Timing:** SMPRx's sampling times and RES's 12, 10, 8 or 6 bits, at
    the ADC clock.
  - **Results and flags:** EOC with EOCS, OVR stopping the group, JOFRx's
    offsets with the signed alignments, the analog watchdog, DMA with DDS,
    and the interrupts.
  - **Inputs:** the pins of the description's new `io.analog` table,
    VREFINT, VBAT/4, and the temperature sensor at the world's
    temperature.
- **`stm32f4-adc-common`:** CSR, CCR, and the dual and triple modes
  (regular and injected simultaneous, interleaved with DELAY, alternate
  trigger) with CDR's DMA modes.
- **`stm32f4-dac`** (RM0390 14):
  - every holding register's view, and DOR after one APB1 clock, or three
    after a trigger (TIM2 and TIM4-8 TRGO, EXTI9, SWTRIG);
  - noise and triangle waves, DMA with its underrun;
  - the output buffer's range, or 15 kOhm without it;
  - its voltage drives PA4 and PA5 through a new analog output on
    `esk_mach_pin` (`aout`), which the runtime puts on the pin's net
    behind the output resistance.
- **Triggers:**
  - the timers announce TIMx_TRGO and TIMx_CCy (OCyREF, or a pulse at a
    capture or a frozen channel's match);
  - EXTI announces lines 9, 11 and 15;
  - the DMA tells the peripheral of a round's last item.
- **The part's factory bytes:** memory regions take `contents`, restored at
  every reset. The F446's device signature carries its UID, its flash size
  and the calibration values TS_CAL1, TS_CAL2 and VREFINT_CAL, which agree
  with the sensors' model.
- **The world's temperature** reaches MCU models through
  `esk_mach_host.temperature`.
- **Proof (`tests/unit/test_stm32f4.c`):**
  - a conversion's code and time;
  - RES and ALIGN;
  - the temperature at 25 and 85 degC through ST's formula with TS_CAL1/2;
  - VBAT/4, and VREFINT against VREFINT_CAL;
  - a scan into memory by circular DMA;
  - OVR;
  - an injected group interrupting a regular one, with JOFR and the
    watchdog;
  - TIM2's TRGO pacing conversions;
  - dual simultaneous mode's CDR;
  - for the DAC: DOR's timing, the pin's voltage and the buffer's floor,
    a triangle on TIM6's TRGO, the software trigger, an underrun.

### The STM32 I2Cs (the NUCLEO-F446RE plan, step 8)

- **`stm32-i2c`** for the F1's and F4's I2C (RM0008 26, RM0390 27). It
  replaces `stm32f1-i2c`, which was a master only.
- **The master:**
  - a START waits for the bus to be free;
  - 10-bit addresses with ADD10, and the read header after a repeated
    START;
  - a slave holding SCL low stretches the clock: the high time counts
    from SCL seen high;
  - a 1 sent while SDA reads 0 loses the arbitration (ARLO), and the
    interface listens on as a slave.
- **The slave:**
  - addressed by OAR1 (7 or 10 bits), OAR2 (DUALF), the general call,
    and the SMBus default, host and alert response addresses;
  - holds SCL low for ADDR, an empty DR (TXE, BTF) and a full one (BTF),
    or with NOSTRETCH sets OVR;
  - STOPF, and AF on the master's NACK.
- **Both:**
  - BUSY and BERR from the bus as the pins show it;
  - PEC with CR1.PEC and PECERR, and SMBus's TIMEOUT and SMBALERT;
  - SWRST;
  - DMAEN's requests, with LAST NACKing the byte after the DMA's EOT-1 (the
    DMA now tells the peripheral about its last two items).
- **Pins:** the F4's I2C1-3 on their alternate functions, inputs
  included; the F1's open-drain alternate-function pins let go for a 1
  (they used to read back 1 whatever the circuit did).
- **Proof:**
  - `tests/unit/test_stm32f4.c`:
    - the master against a scripted device: a write, the POS read, an
      absent address, stretching, ARLO, and no START without pull-ups;
    - the slave against a master driven by hand: ADDR and its
      stretching, BTF both ways, STOPF, the NACK's AF, the dual address,
      the general call, BERR;
    - DMA with the PEC sent by itself, and LAST's NACK;
    - I2C1 wired to I2C3: a 10-bit address, the PEC checked by the slave
      model, the slave's stretching, a read after a repeated START;
  - STM32Cube F1's I2C application against the 24C02 still passes.

### The STM32 SPIs (the NUCLEO-F446RE plan, step 8)

- **`stm32-spi`** for the F1's and F4's SPI (RM0008 25, RM0390 26). It
  replaces `stm32f1-spi`, which was a master only:
  - master and slave, modes 0 to 3, 8 or 16 bits, MSB or LSB first;
  - OVR keeps the older frame, and clears by reading DR then SR;
  - receive-only (RXONLY) and bidirectional (BIDIMODE, BIDIOE) modes; a
    receive-only master clocks frame after frame until SPE clears, and the
    frame under way when SPE clears finishes;
  - NSS by SSM/SSI, SSOE's output or the pin's input; MODF clears SPE and
    MSTR and keeps them write-protected until SR is read and CR1 written;
  - a slave drives MISO only while selected. Like the part, it keeps its
    bit count when NSS rises in the middle of a frame, and sends back the
    frame it received when nothing was written;
  - the F4's TI mode: the master's frame pulse, the slave's frame start
    on it, and FRE;
  - CRC: TXCRCR and RXCRCR with CRCPR's polynomial, CRCNEXT, the CRC
    frame and CRCERR. With DMA, the CRC frame follows the stream's last
    item by itself; the DMA now tells the peripheral about that item;
  - the DMA requests SPIx_TX and SPIx_RX, and the TXE, RXNE and error
    interrupts as levels.
- **Pins:** the F4's SPI1-4 on their alternate functions (inputs too: a
  slave's SCK and NSS, a master's NSS); the F1's on AFIO's pins, its GPIO
  passing the pins' levels to the SPIs. A GPIO update asked for while
  another runs (a slave's MISO changing on its SCK's edge) runs again
  after it.
- **Proof:**
  - `tests/unit/test_stm32f4.c`: a master's frames with OVR and 16-bit
    LSB-first mode 3; MODF and SSOE; a slave across NSS and its echo;
    CRC-8 and CRC-16 with CRCERR; full-duplex DMA with the automatic CRC;
    the bidirectional, receive-only and TI modes;
  - STM32Cube F1's SPI application still passes.

### The STM32F446's DMA (the NUCLEO-F446RE plan, step 7)

- **`stm32f4-dma`** for DMA1 and DMA2 (RM0390 9):
  - eight streams each, CHSEL's channel taking the requests of a new
    `dma_requests` table, transcribed from RM0390's tables 28 and 29;
  - a request waits while no stream takes it, and a stream enabled on its
    channel serves it at once;
  - transfers through the bus as the core's accesses go, so a peripheral
    sees the DMA read or write its data register;
  - direct mode, and the FIFO's packing, threshold, peripheral bursts and
    FS;
  - NDTR with half and full transfer flags, circular mode, double buffer
    with CT, memory-to-memory on DMA2;
  - transfer errors and the FIFO configuration error, LIFCR/HIFCR, and
    each stream's interrupt.
- **Requests:** the USARTs' DMAT and DMAR, and the timers' UDE, CCxDE (at
  the update with CCDS), TDE and COMDE; the timers' DMA burst through
  DCR/DMAR.
- **Proof:** `tests/unit/test_stm32f4.c`:
  - USART2 sent by DMA, the request waiting for its stream;
  - received into a circular buffer, then double buffered;
  - memory-to-memory packing bytes into words;
  - the FIFO configuration and transfer errors;
  - a timer's DMA burst sweeping ARR.

### The STM32 USARTs (the NUCLEO-F446RE plan, step 6)

- **`stm32-usart` rewritten** for every USART and UART of the SR/DR layout,
  the F4's six and the Blue Pill's three (RM0390 25, RM0008 27):
  - 8 or 9 data bits with even or odd parity, 0.5 to 2 stop bits, the
    F4's OVER8;
  - TE's idle frame, breaks (LIN's 13 bits), SBK clearing as the break
    ends;
  - the receiver's RXNE, ORE (a frame arriving while RXNE is set is lost,
    as on the part, rather than waiting), PE, FE, IDLE and LBD, cleared by
    reading SR then DR;
  - mute mode woken by an idle line or this node's address mark;
  - nCTS holding frames and nRTS signalling a full receiver, half-duplex,
    and the synchronous master clock on CK with RX sampled at its edges;
  - every interrupt source.
- **The runtime carries each frame's format:** the TX pin's waveform has the
  frame's data, parity and stop bits (`esk_mach_host.uart_frame`), and the
  RX decoder reads the receiver's format and reports framing errors and
  breaks (`esk_mach_rx_frames`); the session's serial input is paced at the
  frame's length. Other UART models keep 8N1.
- **Pins:** the F4's USART signals through `io.alternate` (TX, RX, CK, CTS,
  RTS); the F1's CK, CTS and RTS through AFIO.
- **Proof:** `tests/unit/test_stm32f4.c`'s USART tests; the Blue Pill's
  serial input test now types after the program's prompt, since bytes sent
  before it are overrun as on the part.

### The STM32 timers and SysTick (the NUCLEO-F446RE plan, step 5)

- **`stm32-tim` rewritten as the whole timer**, for the F4's TIM1-TIM14 and
  the Blue Pill's TIM1-TIM4 alike (RM0390 12-16, RM0008 14-15):
  - up, down and center-aligned counting with DIR, the repetition counter,
    the preloads and update events;
  - output compare and PWM in every counting mode, OCxCE's clear on ETRF;
  - input capture: the edge, ICxPSC, overcapture, the ICxF filter that
    drops short pulses, TI1S, reading CCRx clearing CCxIF;
  - the advanced timers' complementary outputs with dead time, MOE,
    OSSR, OSSI and idle levels, break on BKIN or BG, LOCK's levels, COM
    events with CCPC;
  - the slave modes (reset, gated, trigger, external clock 1, encoder
    1-3) on ITR0-ITR3, TI1F_ED, TI1FP1, TI2FP2 or ETRF, external clock mode
    2, TRGO to the timers that listen, by the interconnection tables;
  - the counter clocked by the clock RCC gives the timers (TIMPRE
    included), each deadline acting at its own cycle.
- **Pins:** the F4's timer signals come through the alternate functions of
  `io.alternate` (TIMx_CHy, TIMx_CHyN, TIMx_BKIN, TIMx_ETR), both ways; the
  F1's channels now also capture from their pins.
- **SysTick:** CLKSOURCE 0 counts the part's external reference
  (`core.systick.ref_div`: HCLK/8 on the STM32s), CALIB reads the part's
  value (`core.systick.calib`: RM0008's 9000 on the F103, TENMS not given
  on the F446), a long wait is counted at once.
- **Proof:** `tests/unit/test_stm32f4.c`'s timer and SysTick tests; the
  Blue Pill's timer tests and HAL applications unchanged.

### The STM32F446's GPIO, SYSCFG and EXTI (the NUCLEO-F446RE plan, step 4)

- **GPIO** (`stm32f4-gpio`, RM0390 7):
  - MODER's input, output, alternate-function and analog modes;
  - OTYPER's push-pull and open-drain; PUPDR's pulls in every mode but
    analog; IDR sampled from the pins;
  - BSRR, a set winning over a reset, written by word or by halfword (the
    older HAL's BSRRL and BSRRH);
  - LCKR's key sequence freezing the locked pins' configuration until
    reset;
  - AFRL and AFRH choosing among the alternate functions the description's
    new `io.alternate` lists for each pin. An alternate function whose
    peripheral has no model yet lets its pin go; the debug port's pins come
    out of reset in AF0 with their pulls.
- **Pin speed:** a GPIO model's speed setting (an F4's OSPEEDR, an F1's
  2, 10 and 50 MHz) picks the pin's output resistance and edge rate from the
  description's `io.electrical` tables. They were declared and never
  applied; the Blue Pill's outputs now use them too.
- **SYSCFG** (`stm32f4-syscfg`): EXTICR's port for each EXTI line; MEMRMP's
  memory at 0 (main flash, system memory, SRAM1's 112 KiB; nothing for the
  FMC banks), mapped back to the flash on reset; CMPCR's READY.
- **EXTI:** every line IMR has (23 on the F4, 19 on the Blue Pill's
  F103), the internal lines' interrupts (PVD, RTC alarm, tamper and
  wake-up, the USB wake-ups), and EMR's events, which end a WFE
  (`esk_cortexm_event`).
- **RCC's peripheral reset bits** (`*RSTR`, an F1's `IOPxRST`) reset their
  peripherals, on every STM32.
- **Importers:** `tools/importers/stm32pins.py --alternate` converts ST's
  GPIO modes file into `io.alternate` (AF15 EVENTOUT on every pin);
  `tools/importers/svd2regs.py --register` adds a register the SVD leaves
  out (the F446's SYSCFG_CFGR), and CMPCR's CMP_PD is writable as RM0390
  says.
- **Proof:** `tests/unit/test_stm32f4.c` covers the reset state, the
  modes, the lock sequence, the AF table, B1's EXTI13 on PC13 through
  SYSCFG, a software trigger on line 22, an event and the boot remap.

### The STM32F446's clocks and power (the NUCLEO-F446RE plan, step 3)

- **RCC to 180 MHz** (`stm32-rcc` on an RCC with `PLLCFGR`, RM0390 6):
  - HSI 16 MHz; HSE ready only as the board drives OSC_IN, a crystal with
    HSEBYP clear or a clock (the NUCLEO's ST-Link MCO) with it set, and
    HSEBYP kept while HSE is on; LSE from the board's crystal; LSI;
  - the main PLL from HSI or HSE: PLLM, PLLN, PLL_P and the F446's PLL_R;
    PLLRDY, PLLI2SRDY and PLLSAIRDY once the source is ready;
  - SYSCLK from HSI, HSE, PLL_P or PLL_R, switched once its source is ready
    (SWS); HCLK, PCLK1, PCLK2 and the timers' clock with DCKCFGR's TIMPRE
    (`esk_stm32_hclk`, `esk_stm32_pclk`, `esk_stm32_timclk`); the core
    runs at HCLK;
  - what SYSCLK uses cannot be turned off; BDCR is written only while
    PWR's DBP is set;
  - said once, as warnings: a VCO input outside 1 to 2 MHz or output
    outside 100 to 432 MHz, SYSCLK above the voltage scale's and
    over-drive's limit, PCLK1 above 45 MHz, PCLK2 above 90 MHz
    (`mcu.clock_out_of_spec`), and too few flash wait states for HCLK
    (`mcu.flash_wait_states`).
- **PWR** (`stm32f4-pwr`): VOSRDY with the PLL, ODRDY and ODSWRDY as the
  over-drive sequence sets them.
- **The board's clocks:** a board's HSE `source` of `external_clock` or
  `oscillator_module` makes HSE a clock that needs HSEBYP; its LSE's
  frequency reaches the part.
- **Register file:** split RCC fields joined (PLLM, PLLN, PLLP, PLLQ, SW,
  SWS) and missing ones added from RM0390 (PLLR, PLLSAION, PLLSAIRDY,
  FLASH's 4-bit LATENCY); `tools/importers/svd2regs.py` gains `--merge` and
  `--field`, recorded in the file's source.
- **Faster STM32 clock lookups:** the RCC model is the machine's clock
  model, no longer found by name on every core clock query.
- **Proof:** `tests/unit/test_stm32f4.c`; STM32CubeF4's RCC_ClockConfig
  clock functions (HSI and HSE bypass to 180 MHz with over-drive, and back)
  run unmodified on a bench board, the HAL reporting 180, 45 and 90 MHz and
  `HAL_Delay` taking its cycles. The ST examples run APB2 at 180 MHz,
  which the model reports as out of spec.

### The STM32F446RE described (the NUCLEO-F446RE plan, step 2)

- `st/stm32f446re`: the Cortex-M4F with FPU and MPU, the memory map
  (512 KiB flash, 128 KiB SRAM, system memory, OTP, option bytes, backup
  SRAM), bit-band, clocks, the LQFP64 pinout and the ADC channels.
- **Register file:** converted from ST's own STM32F446.svd (v1.9,
  Apache-2.0), vendored with its licence. Where the SVD differs from RM0390,
  the manual wins, and each correction is recorded in the file's source:
  - GPIOA's OSPEEDR reset value;
  - USART SR's reset value (TXE and TC at bits 7:6);
  - DBGMCU_IDCODE's DEV_ID (0x421);
  - the interrupts of TIM9 to TIM14, FMPI2C, the ADCs and the DAC.
- **Importer changes.** `tools/importers/svd2regs.py` gains `--reset` and
  `--interrupts`, recorded corrections. `tools/importers/stm32pins.py`
  converts ST's open pin data (BSD-3-Clause) into a package description
  and the analog map.
- **No peripheral models yet.** Each peripheral keeps its register values
  and reports unsupported access until steps 3 to 8; `docs/coverage.md`
  says 0 of 71.

### The Cortex-M core an RTOS port needs (the NUCLEO-F446RE plan, step 1)

- **The MPU** on the parts that have one: the nRF52840 and RP2040 today, the
  STM32F446 next.
  - PMSAv7 and PMSAv6 with eight regions, subregion disables,
    access permissions, XN and the background region (PRIVDEFENA).
  - HFNMIENA; the private peripheral bus and vector table reads always on
    the default map.
  - MemManage faults with DACCVIOL and MMFAR, IACCVIOL, MSTKERR, MUNSTKERR
    and MLSPERR.
- **The default memory map's execute-never regions:** a jump into peripheral,
  device or system space faults.
- **Privilege.**
  - LDRT and STRT access as unprivileged.
  - An unprivileged access to the system control space is a BusFault.
- **Stacking faults.**
  - A fault while stacking or unstacking an exception frame (an RTOS's stack
    guard, a task's stack pointer gone bad) is taken as the derived
    MemManage or BusFault, as the architecture says. It no longer locks the
    core up.
- **Lazy floating-point context stacking** as the hardware does it:
  - the frame's FP area is reserved and written by the handler's first FP
    instruction (FPCCR.LSPACT, FPCAR, the ready bits);
  - FPDSCR is loaded into a new context.
- **Proof:** a 28-case test image, `tests/firmware/cm4-core`, prints the same
  on EmbSIM's nRF52840 as on QEMU's mps2-an386. Its expected output is
  checked against QEMU in CI.

### The Uno powered from USB: fuse, switch and the VIN comparator (ADR-031, ADR-034)

- The Uno R3 board has its USB power path:
  - **Through F1 and T1.** USB's 5 V arrives on a new port, `USB_VBUS` (the
    USB-B socket's VBUS), then F1 (the MF-MSMF050-2 resettable fuse) and T1
    (the FDN340P) to +5V.
  - **The VIN comparator.** U5A (an LMV358) compares VIN/2 (RN1, 10k and
    10k) with 3.3 V and turns T1 off once VIN is above about 6.6 V.
  - **Five board fixtures,** from a 5 V host through a 0.25 ohm cable:
    - 5.0 V unloaded;
    - 4.64 V at 600 mA, held for ten minutes;
    - a 2 ohm overload: the rail sags to 3.8 V, F1 trips in about 16 s, and
      the rail falls under 0.6 V while F1 passes the 0.8 W that keeps it hot;
    - 9 V on VIN: T1 turns off and USB carries nothing.
- `emblink/fdn340p`: onsemi's FDN340P P-MOSFET as a level-1 model with its
  body diode. Fixtures check the on-resistance, off state, threshold and body
  diode.
- `emblink/lmv358`: TI's LMV358 dual rail-to-rail-output op-amp. Fixtures
  check the comparator, follower, supply rail, slew and bandwidth.
- **The op-amp's dominant pole.** SPEC-004 promised it, and the generic
  op-amp's fidelity already claimed it, but `gbw` had been ignored. Now an
  internal node integrates the input stage's drive:
  - DC gain `gain`, unity-gain frequency `gbw`.
  - A new `slew` (V/us) limits large steps.
  - The internal node stops at the output limits, so recovery from
    saturation takes tens of nanoseconds.
  - The LM358 now slews at 0.3 V/us and the LMV358 at 1 V/us, as their
    datasheets say; each has `slew` and `bandwidth` fixtures.
  - Without the pole, the Uno's comparator acted as an ideal step inside the
    T1 loop. Once F1 tripped, the solver gave up.
- **Op-amp convergence.**
  - Newton limits how far the op-amp's tanh argument moves per iteration,
    as `pnjlim` does for a junction. A follower whose input hangs on its own
    output, like the Uno's L buffer on a floating D13, no longer bounces
    between the rails while the operating point is found.
- **Fixture sampling.** Every sampled value in a fixture longer than 2 s, not
  just part outputs, is now sampled at most 20000 times. Each sample is a
  solve, so a 60 s board fixture sampling a rail at 10 kHz would take
  minutes.

### Undo keeps up with the keyboard

- A Ctrl+Z or Ctrl+Shift+Z pressed while an edit is still finishing now waits
  for it in the command stack's queue. Before, it was dropped, so pressing
  undo twice quickly undid only one edit.

### The Uno's USB fuse: a resettable PTC that heats and trips (ADR-031, ADR-034)

- `emblink/mf_msmf050`: Bourns' MF-MSMF050-2, the Uno's F1 on USB VBUS. A
  new `ptc_fuse` primitive (SPEC-004): a two-node thermal model, the polymer
  core and its package, heated by the power it passes. Its resistance rises
  gently below the polymer's switch and steeply above it, so above a
  current the heat runs away and it trips; once the fault is removed it cools
  and resets.
- Fitted to the datasheet's typical time-to-trip curve at 23 C (1.08 A to
  9 A), with 0.8 W tripped. It holds 0.5 A for 30 minutes, trips at 1.0 A,
  and at 8 A trips within the 0.15 s maximum (typically 13 ms).
- The room's temperature (its input, or the world's) moves the threshold as
  the datasheet's derating table does, from 0.77 A at -40 C to 0.29 A at
  85 C, without being fitted to it.
- Eleven fixtures run it the way Bourns tests it: a current-limited supply
  clamped at 15 V.
- Fixtures longer than 2 s sample their outputs at most 20000 times instead
  of at 10 kHz, in both `embsim catalog validate` and
  `tools/catalog/run_fixtures.py`. A half-hour fixture now runs in well under
  a second, and `run_fixtures.py` takes 60 s for the whole catalog, down from
  78 s before this part.

### The Uno R3 qualified against reference data (visual benchmark V1)

- The Uno's artwork had its headers up to 4 mm out:
  - the power and analog headers about 3.9 mm to the left;
  - D8 to SCL 1.2 mm out, with no 160 mil offset from D7;
  - both rows 0.34 mm off the edges.

  The outline's steps, the right mounting holes, the USB-B socket and the
  jack were placed by eye.
- The artwork is now drawn from `reference.json`, which takes:
  - the outline and the four Ø3.2 mm holes from Arduino's A000066
    datasheet;
  - every header pad, the NC position among them, from KiCad's
    `Arduino_UNO_R3` footprint, its symbol naming each pad;
  - the USB-B socket (overhanging the edge by 6.35 mm) and the jack from
    the footprint's fab layer.

  The 3D bench takes its header positions from the artwork, so the board
  on the bench moves with it.
- `tools/validate.py` checks a board with a `reference.json` (kind
  `board_reference`, SPEC-006 §3.1) within its 0.05 mm tolerance:
  - the artwork's size, each pad, the mounting holes and the bench
    features' connector bodies;
  - that every port has a reference position.

  What is drawn without reference data (the DIP socket, the resonator,
  the reset button, the LEDs) is listed as unqualified.
- The Design workspace's Inspector folds to a rail while nothing is
  selected and opens on a selection; folding or unfolding it by hand wins.
  At 1440 x 900 the bench grew from 44 % of the window's width to 59 %, and
  to about 73 % with its Parts & wiring list folded.
- The BB830 breadboard cites BusBoard's datasheet (BPS-DOC-(BB830) Rev 7)
  for its body, its 630-point area and its four continuous 50-point
  rails. The holes' offsets from its edges are not dimensioned there, and
  are declared as such.

### Named surface-mount package bodies

- Render BSS138 in SOT-23 and LM1117IMP-3.3 in SOT-223 from reviewed package
  drawings, with proper pin-side arrangement and the regulator's shared metal tab.
- Replace BAT54's misleading TO-92 body with an explicitly reference-only SOT-23
  family representation; retain its unverified manufacturer-source status.
- Add reusable SOT-23/SOT-223 descriptions and prevent direct breadboard insertion
  of surface-mount leads. Preserve circuit terminal IDs and electrical fidelity.

### Physical assembly feedback

- Preserve later part selections when an asynchronous breadboard placement finishes.

- Preview each leg's destination and occupancy owner before insertion, with
  target rings on the bench and rejection that leaves layout/nets untouched.
- Reserve breadboard holes for inserted legs and direct-hole jumpers; allow a
  free hole on the same strip. Keep partial insertions explicit.
- Edit physical bench coordinates/rotation independently of schematic placement.
  Carry inserted parts through breadboard moves and quarter turns in one undoable edit.
- Preserve the merged world/sensor workflow and distinguish physical bodies from
  non-interactive sensor-beam overlays when picking.

### The Uno's serial port on the host (ADR-031, ADR-032)

- The board's USB-serial bridge (`console.bridge`) becomes a pseudo-terminal
  on Linux and macOS, opened with `port.open`, `embsim serve --serial B1`, or
  the Serial panel's "Open on this computer". Any serial program (screen,
  minicom, the Arduino IDE's monitor, pyserial) then talks to the running
  sketch, paced at real time.
- The program's line rate is the bridge's: at the sketch's rate the bytes
  pass; at another rate both directions decode as a UART reads frames sent
  at the wrong rate.
- Opening the port raises DTR and resets the Uno, as the 16U2 does through
  its 100 nF capacitor. The reset switch is held for 0.7 ms, and the
  ATmega328P comes out of it with MCUSR's EXTRF set and its other flags
  kept. Closing the port does not reset.
- Bytes sent while no program has the port open are dropped, as the bridge
  drops them.
- The host's bytes are recorded inputs (text, or `{"base64"}` for binary),
  so a run replays without the host.
- `port.state` notifies when a program opens or closes the port.
- Windows reports that it has no pseudo-terminals (Q16).
- Omitted, declared in the Uno's fidelity:
  - the bridge's frames on PD0 through its 1 kOhm resistors;
  - DTR pulsed by ioctl, RTS and breaks;
  - programming through Optiboot.

### Physical bench inspection and reliable wiring

- Target projected pins/holes with named endpoint feedback, reject ambiguous or
  covered contacts, validate typed terminals and avoid duplicate jumpers.
- Cancel interrupted wiring/placement safely and release held controls.
- Add top/selection camera framing, paper/graphite/mat surfaces, label and note
  preferences, a foldable keyboard list available at compact widths, and an
  explained 2D fallback/retry for renderer failure.
- Respect board thickness, mounting height and header style consistently between
  bodies and wiring contacts; expose geometry provenance without implying qualification.

### Schematic visual language and direct manipulation

- Separate idle wire color from graphite component symbols, with persistent green,
  blue, teal and violet choices and a distinct selected-track halo/highlight.
  Selection remains visible over live analog and digital signal coloring.
- Draw connected and unconnected symbol terminals as hollow circular sockets;
  show pencil cursors at terminals/while wiring and grab/grabbing cursors for
  movable parts. Keep terminals and annotations visible during navigation.
- Display references, component names and effective primary parameter values with
  units, including catalog defaults. Choose deterministic nearby annotation slots
  to reduce body/pin/label/track collisions; offer compact reference/value labels.

### Circuit organization

- Persist named presentation groups, display selectable group outlines, and browse
  parts/groups by reference, model or group name in the Objects panel.
- Lock part/board placement through the inspector or object browser; native
  transactions enforce locks, automatic placement uses locked parts as fixed
  obstacles, and mixed locked selections cannot be moved or arranged partially.
- Support group creation, rename, reassignment, ungroup, group locking and undo;
  deleting members prunes empty groups. Complete copied component groups receive
  new group/member identities and unlocked placement.
- Disable library placement controls during pending/read-only edits so rapid
  placement cannot race a transaction already finishing.
- Export deterministic, aggregated top-level parts-list CSV through the platform
  file adapter. Inventory includes explicit parameters, not board internals or
  procurement-qualified effective part values.

### Cross-platform schematic fixes

- Remove an ambiguous case-insensitive source import that broke macOS/Windows
  workbench and desktop builds; add a portable module-path policy check.
- Keep unchanged server refreshes from cancelling active drawing drafts. Correct
  selection verification for offscreen symbols and retain failed browser traces.

### Schematic authoring workbench

- Reshape existing orthogonal tracks with draggable/keyboard segment handles,
  numeric offsets, live previews and cancellation. Preserve pin endpoints and
  same-net junction contacts; commit one layout-only undoable edit.
- Add snapped position, rotation and mirror controls, relative group movement,
  and explicit mixed-value batch editing of compatible shared parameters.
- Add keyboard-accessible contextual actions, paste at the clicked sheet position,
  net routing/drawing controls and explicit terminal disconnection.
- Add worker-backed structural/drawing checks with severity filters, search,
  target navigation, markers and applicable net-routing repair. Scope and limits
  are explicit; this is not an electrical-rule or physical-clearance checker.
- Preserve panel order and viewport through focus-mode transitions; keep foreign
  tracks fixed when a part moves or is removed at a coincident pin coordinate.
- Reject stale/concurrent edit snapshots, preserve failed undo/redo history entries,
  avoid rerouting unchanged placement, and make drawn tracks keyboard selectable.


### Manual schematic routing

- Place orthogonal wire bends with live previews and visible waypoint markers;
  flip elbows with Space, remove bends with Backspace/Delete, or cancel with Escape.
- Route with arrow keys and Enter as well as the pointer. Explicit paths bypass
  automatic arrangement and smart routing on completion, with one undoable edit.

### Canvas appearance and reload reliability

- Continuous, world-anchored canvas backgrounds eliminate the gray finite-sheet
  strip during pan and zoom. Add engineering/line/dot/cross/plain patterns, four
  theme-aware palettes and adjustable spacing, weight and contrast.
- Appearance preferences persist across reloads and apply to Design, inside and
  Run schematics; restrained system-style chrome, clearer drafting labels and
  keyboard-accessible controls.
- The native server now serves static assets while its session WebSocket is
  active, fixing browser reload stalls. Auxiliary requests preserve authority
  checks, reject another session upgrade and use a bounded header deadline.


### Schematic Studio

- Connected auto-placement, obstacle-aware orthogonal routing and layout previews
  with whole-sheet/selection scopes, two directions and spacing presets. Apply is
  one undoable layout-only transaction; stale previews cannot apply.
- Opt-in auto-arrangement after connecting, free-space component placement,
  smart wire previews, rerouting after transforms, magnetic drag guides,
  alignment/distribution, component/net finder, net spotlight, selection focus,
  side-panel focus mode, undrawn-connection counts, minimap and display controls in a new schematic tool ribbon.
- Layout planning runs in an embedded worker; desktop policy permits the bundled
  worker. Added a user guide and native-engine schematic workflow regression.


### Scenario editor reliability

- Run and Save now reject invalid visible expectation JSON, assertion timing,
  duplicate IDs, missing identifiers and invalid expectation fields. Errors are
  listed in the editor; edits clear assertion badges from a previous run.

### Debugger reliability

- Board switches clear inspected state and discard late responses from the previous
  selection. Overlapping requests keep the busy indicator active until completion.
- Added an evidence-based 1.0 release readiness checklist.

### Added
- The world (ADR-026, SPEC-003 §8): the room the circuit sits in. Fields
  (temperature, humidity, pressure, light, sound, wind) that every sensor
  bound to them reads while it is not set by hand, and bodies on the bench
  (boxes and spheres) that distance sensors' beams meet, from where they
  stand on the bench. A project declares its `world`; a field it leaves out
  keeps each sensor at its own default, so existing projects behave as
  before. The world changes through inputs (`input.set` with target `world`
  or `world.BODY`), recorded and replayed; `set_world` edits it without a
  restart; `world.get` reports it. Sensors bound: DHT22, BME280, BMP280,
  DS18B20, LM35, TMP36, TMP102, the NTCs, the MPU-6050's die, the GL5528's
  light, the HC-SR04's distance and air temperature. NewPing's
  `NewPingExample` now measures a wall moved in front of the sensor, and
  Adafruit's `DHTtester` reads the room, both unmodified and passing as
  before. The workbench gains the World panel (Design and Run), bodies drawn
  and dragged on the 3D bench, each sensor's beam drawn to what it meets
  with the distance, and an upright HC-SR04; the Controls panel marks an
  input read from the world (`tests/protocol/test_world.py`,
  `e2e/world.spec.ts`).
- The 3D bench (ADR-027; `ui/packages/bench`, Three.js 0.186): the Design
  perspective's canvas switches between the schematic and the bench, and
  the Run perspective's Board panel is the bench, live. Boards, breadboards
  and parts stand on a desk in millimetres with their bodies built from
  their packages (resistor colour bands from the resistance, LEDs, TO-92,
  TO-220, DIPs, tactile buttons, pots, modules on their headers with their
  artwork as their face, sources as boxes with binding posts) and boards
  with their description's `bench.features` (the Uno's USB-B socket,
  barrel jack, ATmega328P in its socket, reset button, L and ON LEDs).
  Dragging a part snaps its legs into a breadboard's holes and plugs it in
  (the pins join the strips' nets); lifting it unplugs it; a moved
  breadboard carries its parts; the Jumper tool wires hole or pin to hole
  or pin (flat pre-cut jumpers on the board, Dupont wires elsewhere);
  connections the schematic has that the bench does not show as dashed
  links that Build turns into jumpers; notes are callouts on the bench.
  Every edit is a transaction on the project, undone like the schematic's;
  the schematic hides breadboards and its checks skip their holes; LEDs
  light from their luminance; buttons, switches and knobs act while
  running. A list beside the scene gives every action from the keyboard;
  without WebGL the flat 2D bench stands in. `examples/uno-breadboard`
  builds uno-blink-serial on a breadboard and passes its scenario. The
  validator checks a project's bench (places, jumpers' terminals and nets)
  and a board's features against its components.
- Breadboards (ADR-027): the 830-point (BB830, the MB-102's layout), the
  400-point and the 170-point boards, generated from their geometry by
  `tools/catalog/gen_breadboard.py` (0.1 in pitch, strips of five across a
  0.3 in channel, rails of 50 and 25 in groups of five), with artwork and
  fixtures for a strip, the channel, neighbouring strips, the far strip, a
  rail's length and a rail pair. Components gain `ties` (pins joined inside
  the part, which the loader merges into one net) and `bench` (the package,
  the size and each leg's place in millimetres; `wiring_aid` keeps a
  breadboard out of the schematic). Projects gain `layout.bench`: the
  parts' places on the bench, jumper wires between terminals and notes,
  edited by `set_layout` without restarting and pruned when a part is
  removed (`tests/protocol/test_bench.py`).
- Simulation speed: an AVR's peripherals keep time lazily (their owed
  cycles paid before the core touches them, at their next event and before
  any outside input, so nothing the firmware or the pins see changes);
  quiet analog islands sleep until a touch or a breakpoint instead of being
  stepped every quantum; a lone AVR with quiet islands runs 100 µs quanta,
  its UART's frames queued on the TX pin each at its own time; the core
  fetches from flash directly. Three seconds of the TM1637 example went
  from 7.4 s to about 3.4 s on the reference machine.
- A project menu on the workbench's mark, with the project's name: New
  project (a blank sheet or a catalog board as B1, named as the schema
  allows), Open, Save and Save a copy, run as the palette's commands; the
  status bar follows the project the session holds. The design package
  gains a Menu (WAI-ARIA menu button) and useMediaQuery.
- Workbench layout: dock panels fold to a titled rail (`collapsible`,
  `defaultCollapsed`), the user's choice kept with the order and sizes. The
  Run perspective folds the oscilloscope or the logic analyzer when it has
  no probe of its kind and moves the window's actions to the other; Design's
  Library, Inspector and lower panel fold, and a window under 1200 pixels
  starts with the Inspector folded. The sheet navigator stays off a canvas
  narrower than 640 pixels.
- Meters and the oscilloscope share one SI formatter: prefixes down to
  femto, residue below that as about zero, axis labels with one prefix and
  the decimals the step needs (no more clipped "00.00 m"). Growing texts
  (a terminal's, an EEPROM's dump) stay off the meters.
- The Debug perspective demangles C++ names (Itanium ABI: nested names,
  internal linkage, constructors and destructors, const members, the
  builtin and qualified types, substitutions, simple templates), keeping the
  mangled name as the tooltip. Verify's texts lose their spec references
  and its badges show only what is unavailable.
- Component explorer with grouped categories, manufacturer/package/model filters,
  multiword search and pre-placement Overview/Pins/Models sheets.
- 18 named interactive parts plus reusable relay contacts: NTCs, GL5528, BPW34/S,
  four VO617A bins, Bourns trimmers, Omron switches/relays and Phoenix plugs;
  original symbols/artwork, six electrical primitives, 52 component fixtures and
  four GUI/CLI circuit examples. Numeric artwork text supports decimal precision.
- Manufacturer source review for 25 named parts: exact PDF revisions, hashes,
  dates and pin-map references; TMP36 TO-92 map confirmed directly. Sensor
  supply endpoint and shifted-ground fixtures; full package pin-map regressions.
- First named-part catalog batch: 26 manufacturer/package entries for discrete
  semiconductors, regulators, LM358P, LM393P, NE555P, LM35DZ, TMP36GT9Z and
  six complete 74HC gate packages. Original symbols/artwork, searchable part
  identity and inspector details, documented approximation/source-review limits.
- Open-collector comparator, temperature-sensor and 555 behavioral primitives;
  op-amp DC output headroom; packaged HC rail sensing and per-channel timing.
- Executable catalog fixture runner in CTest and a named-part temperature alarm.

- Architecture baseline v1.0: vision, system architecture, simulation engine,
  MCU runtime and toolchains, workbench and protocol, ADR-001 to ADR-020,
  roadmap, open questions, reuse assessment.
- Specifications SPEC-001 to SPEC-014 and the requirements catalog.
- Repository: CMake build and presets, CI matrix, documentation and policy
  workflows, templates, coding standard, status page.
- M0 core: scheduler and rational clocks, nets and pin models, digital engine,
  analog engine v1, model registry and primitives, JSON loader and validator,
  session and in-process protocol, stdio and WebSocket endpoints, `embsim`
  command line, C tests and fixtures.
- M0 catalog: schemas, first components, the Blue Pill board description.
- M0 workbench: design system, shell, schematic canvas, library, inspector,
  run controls, instruments; desktop shell scaffold.

- M1 firmware: the Cortex-M3 core, bus, loaders and semihosting extracted
  from EmbCC; register files from EmbSIM JSON descriptions (ADR-020) and the
  STM32F103C8's written from RM0008; STM32F1 RCC, GPIO, AFIO, EXTI, USART,
  timer, flash and power models; the MCU runtime in quanta on the scheduler
  (ADR-019) with pins, serial lines on pins and streams, reset and boot pins.
- Debugging: breakpoints and watchpoints that stop the session, `debug.*`
  methods, a Thumb disassembler, ELF symbols, DWARF line tables, the GDB
  remote protocol server (`embsim gdb`), the workbench's Debug perspective.
- Toolchains: detection and builds from the profiles (`embsim toolchains`,
  `embsim build`, `toolchain.detect`, `toolchain.build`).
- `tools/importers/svd2regs.py`: SVD files to EmbSIM register descriptions.
- `examples/bluepill-button-led` runs its firmware: button by EXTI, LED
  through an NPN transistor, a terminal on USART1.

- M2 workbench: schematic editing reaches the engine through project
  transactions and `project.save` (place, wire, move, rotate, delete,
  copy, paste, duplicate, net labels, undo, redo, save); boards drawn on
  the schematic as blocks of their ports and in the board view with their
  artwork; UART, SPI and I2C decoders on the logic analyzer; meters with
  units; a 500-part schematic near 60 Hz with its generator and frame-time
  script; the Blue Pill example drawn with wires and labels; buses drawn
  with their live value (`layout.buses`) and `examples/counter-bus`; the
  inside of a board or composite part opened on a read-only, live sheet.
- Desktop: the Tauri shell builds and runs (Linux, WebKitGTK), opens a
  project given on its command line, waits for the engine before
  connecting, forwards the workbench's errors, has its icon; CI builds
  deb, AppImage, dmg and msi installers and runs the Playwright smoke on
  Linux, macOS and Windows.
- `embsim serve --allow-origin`; a sanitizer CI job (GCC address and
  undefined-behaviour sanitizers).

- M3 catalog and SDK: the Lua model tier (SPEC-013 §2) with Lua 5.4.7
  vendored, per-instance states, an instruction budget and memory cap, and
  I2C and SPI slaves decoded in C; `embsim catalog validate` in the engine
  (each item under each declared policy, its fixtures run), `catalog new`
  templates for components, Lua models and boards, `catalog install` into
  user libraries (`$EMBSIM_LIBRARIES`).
- The catalog grows to 101 components with 150 fixtures: 74HC and CD4000
  logic, counters, decoders, latches, the 555, regulators, displays
  (HD44780 LCD, SSD1306 OLED, MAX7219), EEPROMs, a real-time clock,
  sensors, a servo, a stepper driver and a buzzer.
- Importers: EmbLinkRTOS board YAML (`rtos2board.py`), DeviceTree sources
  (`dts2board.py`) and KiCad netlists (`kicad2project.py`);
  `examples/display-showcase` and `examples/kicad-flasher`.
- Board view artwork states with value maps, formats and text; scenario
  text expectations on value probes.

- M4 core families and boards, part 1: the AVR core extracted from EmbCC
  (cycle-exact) with the ATmega328P's ports, external and pin-change
  interrupts, three timers with PWM pins, USART0, ADC, EEPROM and clock
  prescaler, and the Arduino Uno R3; the RISC-V core (RV32IMAC) extracted
  from EmbCC with the FE310-G002's CLINT, PLIC, GPIO, UARTs and clocks, and
  the HiFive1 Rev B. Disassemblers for both; their DWARF, registers and
  peripherals in the Debug perspective.
- Projects with several boards: `examples/two-boards-uart`, an Uno and a
  Blue Pill on one serial line, and the `multiboard` determinism test;
  `examples/uno-blink-serial` and `examples/hifive1-blink`, built by clang
  and EmbCC.
- The register file takes 8-bit registers and 24-bit offsets; the runtime
  takes pins named GPIOn and gives an ADC its pin's voltage; the regulator
  model converges with a supply on its output; the board view scopes each
  artwork's styles and gradient ids and colours an LED by its parameter.
- M4 part 2: the RP2040 (Cortex-M0+) with its register file converted from
  Raspberry Pi's RP2040.svd, its resets, clocks and PLLs, watchdog tick and
  timer, IO_BANK0 and pads, SIO (GPIO, spinlocks, divider, FIFO), PL011
  UARTs, SSI and a bootrom stand-in that boots boot2, and the Raspberry Pi
  Pico; the nRF52840 (Cortex-M4F) with its register file converted from
  Nordic's nrf52840.svd, CLOCK, the GPIO ports, GPIOTE, UARTE with EasyDMA,
  TIMER and RTC, and the nRF52840 DK. `examples/pico-blink-serial` and
  `examples/nrf52840dk-blink`, built by clang and EmbCC.
- The register file's bus rules (the RP2040's atomic aliases and byte
  writes) and peripherals held in reset, shown as "in reset" in the Debug
  perspective (`debug.peripherals` reports `reset`).
- M4 part 3: the MIPS32 core (the PIC32MX's M4K with MIPS16e, and QEMU
  malta's 24Kc) and the PIC32MX795F512L with its register file converted
  from Microchip's PIC32MX device pack: the EVIC, the oscillator from the
  configuration words, Timer1-5, ports A-G with the change notices and
  UART1-6; the Explorer 16/32 with the PIC32MX795F512L PIM and
  `examples/explorer16-pic32mx`, built by clang and EmbCC.
- `tools/importers/edc2regs.py`: a PIC32's EDC to a register description
  with CLR/SET/INV registers, shared vectors and pin functions
  (`clr_set_inv`, `vectors` and `pins` in the register schema).
- The clang and EmbCC profiles build `mipsel-none-elf`; a scenario may press
  a board's own button (`B1.S3`).
- M4 part 4: the PIC24 core (the PIC24F, with EmbCC's dsPIC33E mode) and the
  PIC24FJ1024GB610 with its register file converted from Microchip's
  PIC24F-GA-GB device pack: the interrupt controller, the oscillator from
  the configuration words with OSCCON's unlock sequences and the PPS lock,
  Timer1-5, ports A-G with the interrupt-on-change, and UART1-6 timed on the
  pins the PPS gives them; the Explorer 16/32 with the PIC24FJ1024GB610 PIM
  and `examples/explorer16-pic24`, built by XC16. The `mcu.trap`
  diagnostic.
- Record and replay (M5, SPEC-012): recordings with the project's and
  images' SHA-256 and observations (interrupt entries per core, digital
  probe transitions, serial text, diagnostics); `embsim replay` with a
  first-divergence report (exit 4) and drift refusal (`--allow-drift`);
  `embsim record to-scenario`; `record.replay` over the protocol;
  `tools/verify/compare_reports.py`; a determinism and replay test over
  every example (`record_replay`).
- Coverage and profiling from the backends (M5, SPEC-009 §12): every core
  (Cortex-M, AVR, RISC-V, MIPS, PIC24, and each of the RP2040's two) watched
  instruction by instruction; `embsim run --coverage` (gcov-style text with
  the source, lcov, JSON) and `--profile` (by function, self and inclusive,
  collapsed stacks for flame graphs, JSON); `analysis.start`, `.stop`,
  `.coverage`, `.profile` over the protocol; the run report's `coverage`
  and `profile` summaries; the `analysis` test over every firmware example.
  Extracted from EmbCC's analyses, with per-core call stacks.
- Stack depths (M5, SPEC-009 §12): `embsim run --stack` (text, JSON) and
  `analysis.stack`; each core's main stack (and a Cortex-M's process
  stack): its top, its deepest point and its limit from the link
  (`__stack_limit`, the end of `.data` and `.bss`); each function's own
  frame and its depth with its callees; the error diagnostic
  `mcu.stack_overflow` when the main stack goes past its limit, naming the
  function. Extracted from EmbCC's `stack.c`; `tests/firmware/stack-overflow`.
- The fault report (M5, SPEC-009 §12): `embsim run --faults` (text, JSON)
  and `analysis.faults`; each fault as the core takes it, decoded (a
  Cortex-M's HFSR and CFSR, MMFAR and BFAR; RISC-V's mcause and mtval;
  MIPS's Cause and BadVAddr; a PIC24 trap), with the instruction, the
  registers, a backtrace from the image's `.debug_frame` (read by the new
  CFI reader in `dwarf.c`) or the shadow stack, and the handler; the error
  diagnostic `mcu.fault`. Extracted from EmbCC's `fault.c`;
  `tests/firmware/fault`.
- The analyses name a function by its strong symbol, not a weak alias at
  the same address (startup code's `Default_Handler`).
- `ctx.analog` for Lua models (SPEC-013 §2.1): `voltage(pin)` and
  `between(a, b)` read node voltages; `watch(pin, rising, falling)` makes an
  input's comparator switch at the model's own voltages, for behavioural
  sensors and detectors.
- `embsim catalog install` takes a zip archive of a library (at its top or
  in its one top directory) or an `http(s)://` URL to one (fetched with
  `curl`), as SPEC-014 §4 has it: unpacked into a staging directory,
  validated, copied, the staging removed; an entry outside the library is
  refused. EmbSIM's own DEFLATE decoder and zip reader (`core/util/`).
- The Verify perspective (M5): the scenario editor (a scenario's inputs
  and assertions edited in place, run with each result in its row, saved
  into the project: `scenario.get`, `scenario.save`), recordings replayed
  with the divergence shown or turned into a scenario
  (`record.to_scenario`), coverage with each file's source and counts and
  the functions by cycles.
- Recordings made in a running session replay identically: they begin
  with the digital probes' levels and carry `from_ps` and `from_seq`;
  inputs given at the paused instant are `immediate`.
- The Python client (`python/embsim`, `pip install ./python`): `Session`,
  `run()`, `replay()`, a pytest fixture.
- `embsim run --target BOARD --image FILE` with `--console` and
  `--until-text`: a catalog board as an emulated target for test runners,
  with no project; `--image ID=FILE` over a project. `docs/user/ci.md`.
  EmbLinkRTOS's Arduino Uno conformance suite runs this way.
- The RP2040's second core: two Cortex-M0+ cores on one clock
  (`core.cores`; each core's NVIC, SysTick, CPUID, divider and FIFO; WFE and
  SEV between them; core 1 launched through its bootrom's FIFO handshake and
  powered by the PSM; IO_BANK0's PROC1 interrupts), and
  `examples/pico-dual-core`, built by clang and EmbCC.
- `edc2regs.py` converts a PIC24's EDC (`config_words` and `pps_outputs` in
  the register schema; `core.prog_end` and `core.ram_base` in the MCU
  schema); the XC16 profile's `pic24fj1024gb610` target.

- Arduino sketches built with the unmodified ArduinoCore-avr run on the
  Uno: `tests/vendor/arduino` (Blink and a tour of the core: `delay`,
  `millis`, `analogRead`, `analogWrite`, `attachInterrupt`, `Serial`)
  builds them with the IDE's flags against a pinned checkout of the core
  and runs them under scenarios, in CI's linux-gcc job.
- ST's STM32F1 HAL on the Blue Pill: `tests/vendor/stm32cube` builds an
  application (the clock tree from HSE through the PLL, `HAL_Delay`, EXTI,
  a TIM2 interrupt, the UART both ways) with ST's unmodified HAL, CMSIS
  device files, startup and linker script, at pinned commits, and runs it
  under a scenario, in CI's linux-gcc job.
- The RP2040's bootrom functions (ADR-021): its tables and their functions
  (the lookup, bit operations, memcpy and memset, flash erase and program,
  reset_to_usb_boot, the float and double libraries) done by the host from
  `bkpt` stubs in the ROM, so unmodified Pico SDK images run;
  `tests/vendor/pico-sdk` builds one with the SDK's own CMake and runs it on
  the Pico in CI's linux-gcc job.
- SiFive's freedom-e-sdk examples on the HiFive1: `tests/vendor/freedom-metal`
  builds six of them (hello, sifive-welcome, csr, uart-interrupt,
  example-user-mode, minimal-boot) with the unmodified freedom-metal and the
  board's BSP, as the SDK's build does, and runs them under scenarios in CI's
  linux-gcc job.
- Nordic's nrfx drivers on the nRF52840 DK: `tests/vendor/nrfx` builds an
  application (nrfx_clock, nrfx_uarte both ways through its RX buffers,
  nrfx_timer, nrfx_rtc, nrfx_gpiote) with the unmodified nrfx 4.6.0 and
  its MDK's system file, startup and linker script, and runs it under a
  scenario in CI's linux-gcc job.
- The RP2040's PWM and ADC: eight PWM slices on their pins (the fractional
  divider, free-running and phase-correct, CC and TOP double-buffered,
  inversion, the wrap interrupt) and the ADC (96 clk_adc cycles a
  conversion from GPIO26-29 and the temperature sensor, START_ONCE and
  START_MANY with DIV, round robin, the FIFO and its interrupt).
  `tests/vendor/pico-sdk`'s `pwm_adc` runs the SDK's `hardware_pwm` and
  `hardware_adc` on them.
- The FE310's PWM and SPI on their pins: PWM0-2 (the count scaled to
  pwms, comparators with center, gang, deglitch and sticky, zerocmp,
  enalways and enoneshot, their PLIC sources, the channels on IOF1) and
  SPI1 and SPI2 in single-wire mode (8-entry FIFOs, modes 0 to 3, csmode
  AUTO, HOLD and OFF, the four delays, the watermark interrupts). The GPIO
  keeps IOF0's and IOF1's drive apart and applies out_xor to them.
  `tests/vendor/freedom-metal` runs SiFive's example-pwm and example-spi.
- The ATmega328P's SPI and TWI masters on their pins: the SPI (MOSI and
  SCK while DDRB makes them outputs, MISO an input, modes 0 to 3, DORD, the
  clock from SPR and SPI2X, WCOL, SPIF cleared by SPSR then SPDR or by its
  vector, SS's mode fault) and the TWI (open-drain SDA and SCL, the
  datasheet's master transmitter and receiver statuses, START, repeated
  START, STOP and STOP with START, TWEA's ACK, TWWC, SCL from TWBR and the
  prescaler, the vector while TWINT and TWIE). `tests/vendor/arduino`'s
  `spi_wire` runs the core's SPI and Wire libraries against the catalog's
  25LC040 and 24C02; `run.py` builds the core's libraries a sketch
  includes, as the IDE's builder does.
- The RP2040's SPI and I2C masters on their pins: the PL022s (eight-entry
  FIFOs, 4 to 16 bits in modes 0 to 3 at clk_peri / (CPSDVSR * (1 + SCR)),
  CSn framing each frame, loopback, overrun and the interrupts) and the
  DW_apb_i2c blocks (the command FIFO with RESTART and STOP, the receive
  FIFO, open-drain SCL and SDA at their HCNT and LCNT with the SDA hold
  time, the bus held between commands, repeated STARTs, aborts with
  IC_TX_ABRT_SOURCE, the raw interrupts and their clear registers).
  `tests/vendor/pico-sdk`'s `spi_i2c` runs the SDK's `hardware_spi` and
  `hardware_i2c` against the catalog's 25LC040 and 24C02.
- The nRF52840's SPIM, TWIM, PWM and SAADC on their pins: SPIM0-3 (EasyDMA
  transfers in modes 0 to 3, ORC, END_START), TWIM0-1 (open-drain START,
  repeated START and STOP, EasyDMA, LASTTX and LASTRX with their shorts,
  SUSPEND and RESUME, ERRORSRC), PWM0-3 (EasyDMA sequences in the four
  decodings, Up and UpAndDown counting, REFRESH, ENDDELAY, loops and
  shorts) and the SAADC (AIN0-7, VDD and VDDH/5, gains and references, 8
  to 14 bits, oversampling, limits, scans into Data RAM). The register file
  keeps TWIM's SHORTS bits beside SPIM's in their shared space, and
  answers the registers nrfx's workaround for SAADC anomaly 212 touches.
  `tests/vendor/nrfx`'s `nrfx_periph` runs nrfx_spim, nrfx_twim, nrfx_pwm
  and nrfx_saadc against the catalog's 25LC040 and 24C02, an RC low-pass
  and a 1.0 V source.
- Toolchain profiles may name the program prefixes a GNU cross toolchain
  is installed under (`detect.prefix`, `detect.alternatives`, each with its
  own link flags when its C library differs): gcc-riscv now finds the
  distributions' riscv64-unknown-elf-gcc (with picolibc) as well as xPack's
  riscv-none-elf-gcc, and `tests/protocol/test_build.py` builds and runs
  the HiFive1 example with it. `toolchain.detect` reports the program it
  found.
- A pin that turns from an input into an output takes its new level at
  once (`esk_pin_output`): an input pulled low that starts driving 0 no
  longer shows a zero-width Z on its net.
- The STM32F1's SPI and I2C masters on their pins: SPI1 and SPI2 in modes
  0 to 3 at their baud prescaler, 8 or 16 bits, TXE, RXNE and BSY; I2C1
  and I2C2 on open-drain SCL and SDA with START, repeated START, STOP,
  ACK and POS, SR1 and SR2's flags with their clearing sequences and BTF
  holding the clock, timed by CCR. `tests/vendor/stm32cube`'s `spi_i2c`
  runs ST's HAL SPI and I2C against the catalog's 25LC040 and 24C02.
- The STM32F1's timers' output compare: CCRx matches with their flags and
  interrupts, the OCxM modes with PWM 1 and 2, CCRx preload, and the
  channels' pins through AFIO's remaps; ADC1 and ADC2's regular group
  (software start, scan and continuous, the sampling times at the ADC
  clock, the pins' voltages, EOC and its interrupt, calibration).
  `tests/vendor/stm32cube`'s `adc_pwm` runs ST's HAL ADC and PWM on them.
- The FE310's always-on domain: its watchdog (the key before each write,
  the scale, the comparator, wdogzerocmp, the sticky interrupt, and a chip
  reset the domain survives, pmucause saying so) and its RTC (the 48-bit
  count, the scale, the compare and its interrupt) on the 32.768 kHz clock.
- RISC-V physical memory protection: the part's entries (`pmp_entries` in
  an MCU's core, 8 on the FE310), OFF, TOR, NA4 and NAPOT, locks that bind
  machine mode and ignore writes, user mode allowed only what an entry
  gives it, MPRV for loads and stores.
- Six more of SiFive's examples on the HiFive1: example-rtc,
  example-watchdog, example-pmp, timer-interrupt, software-interrupt and
  return-pass; `tests/vendor/freedom-metal` prints an example's exit
  status through a wrapper around freedom-metal's `metal_shutdown`, which
  on this board spins.
- A differential test of the Cortex-M4's DSP instructions against QEMU
  (`tests/firmware/dsp`, `tests/protocol/test_dsp.py`): 101 instructions
  over 4096 operand pairs each, their results and GE and Q flags hashed;
  CI's tools job checks the expected hashes against QEMU's mps2-an386.
- A register description's peripheral may name `reserved` ranges, offsets
  with no register that read 0 and ignore writes as the part documents them.
- The ATmega328P's watchdog (core/mcu/avr/wdt.c): time-outs of 2048 << WDP
  cycles of its 128 kHz oscillator, WDR, interrupt mode, system reset mode
  and both, safety level 1's timed sequence, WDRF holding WDE, and a reset
  that keeps MCUSR's flags and sets WDRF, with the watchdog still running
  at 16 ms after it. `tests/vendor/arduino`'s `watchdog` sketch, 5/5:
  avr-libc's WDT interrupt waking the part from power-down, then
  `wdt_enable` left to time out twice, a boot count kept in `.noinit`.
- The ATmega328P's analog comparator (core/mcu/avr/ac.c): AIN0 or the
  1.1 V bandgap against AIN1 or, with ACME and the ADC off, the ADC
  multiplexer's pin; ACO, ACI on the toggle, falling or rising edge, the
  ANALOG_COMP vector, ACD. On an analog net each input pin carries a
  threshold at the other's voltage, which the analog solver finds and
  bisects like a digital input's (`esk_pin_compare`, SPEC-004 §8).
  `tests/vendor/arduino`'s `comparator` sketch, 3/3, times an RC network
  with it: 6960 and 6956 us against R * C * ln 2 = 6949 us. A sketch may
  bring its own `project.json`.
- The ATmega328P's Timer/Counter1 input capture: ICP1 (PB0) or, with
  ACIC, the analog comparator; ICES1's edge; the noise canceler's four
  samples (a capture four cycles after the edge, shorter pulses ignored);
  ICR1, ICF1 and TIMER1_CAPT; no capture while ICR1 is TOP, and ICR1
  writable only then. `tests/vendor/arduino`'s `input_capture` sketch,
  4/4, measures `analogWrite(3, 64)` through a D3-to-D8 wire: 32640 cycles
  a period every time and 8192 high.
- The DHT22 behaves as its datasheet says beyond the wire protocol: it
  answers only with VCC at 3.0 V or more and after its first second, each
  read sends the previous measurement (a new one at most every 2 s), the
  element follows the air with the datasheet's response times (5 s for
  humidity, 10 s for temperature) and readings are clamped to the range.
  Its fixtures check the start-up second, the previous data and the lag.
- The HD44780 LCD keeps the controller's timing: its power-on reset
  (8-bit, one line, display off, 10 ms busy), 37 us a write and 1.52 ms
  a clear or home, with writes while busy ignored as the part does; the
  busy flag, address counter and RAM read back; the part's pull-ups on
  RS, R/W and DB0-DB7 (a floating R/W reads, so nothing is written); the
  A00 character ROM; the characters showing only with 3.0 V or more of
  contrast (VDD - V0), and a backlight output. The LiquidCrystal
  library's `HelloWorld` runs unmodified, 6/6; display-showcase writes
  its LCD at the controller's timing.
- The HC-SR04's echo is the sound's round trip at the speed of sound in
  the air's temperature (a new input, 20 C by default: 58.24 us per cm),
  measured from the trigger's falling edge after a `burst_delay`; a
  trigger during a measurement is ignored, and it does not answer below
  4.0 V. NewPing's `NewPingExample` runs unmodified, 6/6.
- The servo turns as an SG90 does: toward each pulse's target at its
  rated speed (0.1 s per 60 deg at 4.8 V, in proportion to the supply),
  only while pulses keep coming (it stays put when they stop), not for a
  change within its 10 us dead band, and not at all below 3.5 V. The
  Arduino Servo library's `Sweep` and `Knob` examples run unmodified,
  8/8 and 7/7.
- The PCF8574 reads its I2C address from A0..A2 as the part does (0x20 +
  A2A1A0; a `variant` for the PCF8574A at 0x38), so an LCD backpack with
  the three pulled up answers at 0x27; a written byte reaches the port at
  the acknowledge clock with a strong pull-up during its high time; INT_N
  is open drain, set 4 us after a pin departs from what the last read or
  write left and cleared by a read or write; below 2.5 V it does not
  acknowledge. `ctx.bus.i2c_slave` takes `address_pins` (SPEC-013).
- A 20x4 character LCD (`emblink/lcd2004`) on the same HD44780 model,
  generalised to the glass's columns and rows: rows 3 and 4 continue
  lines 1 and 2 (0x14 and 0x54), as on the module. `tools/validate.py`
  checks that scripts marked `-- Shared by:` stay identical.
  LiquidCrystal_I2C's `HelloWorld` runs unmodified on a 20x4 behind a
  PCF8574 backpack wired as the common board is, 7/7: the backlight is
  off until the sketch turns it on (and on at power-up before the
  library writes, as the expander's ports start high). The Arduino runner
  builds `.pde` sketches and names library examples `<library>/<example>`.
- The DS18B20 1-Wire thermometer (`emblink/ds18b20`), from Maxim's
  datasheet: reset and presence, write and read slots at its timing, the
  64-bit ROM code with READ, MATCH, SKIP, SEARCH ROM and ALARM SEARCH,
  CONVERT T at the resolution's time (93.75 to 750 ms, read slots 0 until
  done), the scratchpad (85 C at power-up) with its CRC, TH/TL alarms,
  the EEPROM commands, READ POWER SUPPLY and parasite power with VDD
  grounded. DallasTemperature's `Simple` example runs unmodified over
  OneWire, 3/3. The HC-SR04 and DS18B20 accept their inputs through
  `on_set`, as SPEC-013 asks (the HC-SR04's were warned as refused).
- The BME280 (`emblink/bme280`), from Bosch's datasheet: I2C at 0x76 +
  SDO and 4-wire SPI once CSB goes low; chip ID, soft reset, ctrl_hum
  applied by ctrl_meas, status, sleep, forced and normal modes with the
  typical measurement time and the standby times; raw values that Bosch's
  compensation formulas turn into the room's temperature, pressure and
  humidity at the oversampling's resolution, the IIR filter, skipped
  measurements and shadowed data registers. Adafruit's `bme280test` runs
  unmodified, 3/3; six fixtures. The Arduino runner adds prototypes for a
  sketch's functions, as the IDE's builder does.
- The MPU-6050 (`emblink/mpu6050`), from InvenSense's datasheet and
  register map: I2C at 0x68 + AD0, WHO_AM_I, asleep after power-on and
  DEVICE_RESET, the sample rate from SMPLRT_DIV and the DLPF with the
  filter's delay, the four accelerometer and gyroscope ranges saturating
  at 16 bits, the die temperature, burst reads from one sampling instant,
  standby axes and the DATA_RDY interrupt on INT. Adafruit's
  `basic_readings` runs unmodified, 3/3; nine fixtures.
- The WS2812B (`emblink/ws2812b`) and strips or rings of them
  (`emblink/ws2812b_strip`), one model from Worldsemi's datasheet: 800
  kbit/s NRZ told apart at 0.6 us, 24 bits (GRB) kept per LED and the rest
  passed on at DOUT reshaped, a low of t_reset (50 us; 280 us for the V5)
  latching the frame, dark below 3.5 V. Adafruit NeoPixel's `simple` runs
  unmodified on a 16-LED ring, 4/4; five fixtures.
- The DS3231 (`emblink/ds3231`), from Maxim's datasheet: BCD time and
  date with 12/24-hour modes, the century and a user day of the week,
  user buffers synchronised at START, the countdown chain reset by a
  seconds write, the rate off by a drift parameter and the aging offset,
  both alarms with every mask combination on INT#, the square-wave and
  32 kHz outputs, the temperature every 64 s or on CONV, OSF and RST at
  power-up. RTClib's `ds3231` runs unmodified, 2/2; nine fixtures. The
  Arduino runner fixes SOURCE_DATE_EPOCH, so __DATE__ and __TIME__ are
  the same each run.
- A 4x4 membrane keypad (`emblink/keypad_4x4`): sixteen switches in a
  row/column matrix with the membrane's contact resistance (200 Ohm) and
  5 ms bounce, so held keys join rows and columns as the real matrix
  does. The Keypad library's `HelloKeypad` runs unmodified, 2/2.
  Fixture and scenario inputs may target a composite's member
  (`KP.k5`, SPEC-007).
- The NEO-6M GPS module (`emblink/neo6m`), from u-blox's data sheet and
  protocol specification: NMEA once a second at the configured rate (RMC,
  VTG, GGA, GSA, GSV, GLL with checksums), empty fields without a fix, the
  time to first fix (cold/warm 27 s, hot 1 s) with the sky in view,
  reacquisition, the time pulse while fixed. TinyGPSPlus's
  `DeviceExample` runs unmodified over SoftwareSerial, 4/4; six fixtures.
  Component fixtures may expect what a terminal received (`serial:`,
  SPEC-005).
- The SSD1306 model rewritten from Solomon Systech's datasheet: write-only
  I2C (reads not acknowledged); segment remap, COM scan direction,
  multiplex ratio, offset, start line and the COM pins configuration
  against the panel's wiring (wrong ones interleave the rows); the panel
  lit only with the charge pump (or external VCC) and 100 ms after
  display on; horizontal and vertical scrolling at the panel's frame
  rate; a checksum output of the visible image. Adafruit's
  `ssd1306_128x64_i2c` runs unmodified, 5/5; display-showcase's OLED
  pattern now sends A1/C8 so its text shows upright.
- The MFRC522 RFID reader with a MIFARE Classic 1K card
  (`emblink/mfrc522`), from NXP's data sheets and ISO 14443-3: SPI
  register access with FIFO bursts, NRSTPD, the command set (Transceive,
  CalcCRC, MFAuthent, SoftReset...), the interrupt, error and timer
  registers, the CRC coprocessor; frames at 106 kbit/s answered after the
  frame delay time; the card's REQA/WUPA, anticollision, SELECT, HLTA,
  authentication against its sector trailers and READ/WRITE. The
  library's `DumpInfo` runs unmodified; four fixtures. `ctx.bus` helpers
  take `edges = false` to keep a bus's clock edges from `on_input`
  (SPEC-013).
- The KY-040 rotary encoder model rewritten from the EC11 data sheet and
  the module's schematic: contacts to GND with the module's 10 kOhm
  pull-ups to + (floating without it) and none on SW, 20 detents a
  revolution with both contacts open at rest, A leading clockwise,
  turning at a hand's pace, and contact bounce on every change. PJRC
  Encoder's `Basic` runs unmodified, 3/3 (Encoder pinned to its 1.4.4
  release: later commits drop AVR's IO_REG_TYPE).
- The 28BYJ-48 stepper motor, the ULN2003A and the ULN2003 driver board.
  The motor: four 50 Ohm, 3 mH half windings on red (bipolar with red
  open), a permanent-magnet rotor of 32 full steps a turn geared 63.684:1,
  its inertia, detent torque, gearbox friction and the windings' back EMF
  integrated in 20 us steps, calibrated to the data sheet's in-traction
  (above 600 Hz) and out-traction (above 1000 Hz) rates, so it misses
  steps and stalls when stepped too fast and shudders in place with its
  leads in the wrong order; nine fixtures. The ULN2003A: seven sinking
  channels at 2.4 V with the data sheet's saturation voltage and 0.25 us
  delay. The board: the chip, the motor connector and the four LEDs. The
  Arduino Stepper library's `stepper_oneRevolution` runs unmodified, 5/5:
  its 200 steps turn this motor's shaft 34.9 degrees, as on the bench.
- Fixtures and projects may probe a composite member's output
  (`output:DRV.LEDA.current`), and `tolerance` with a numeric `equals` is
  no longer reported as unused (SPEC-007).
- The TM1637 4-digit display module, from Titan Micro's data sheet: the
  two-wire interface (START and STOP with CLK high, LSB first, ACK from the
  8th clock's falling edge to the end of the 9th), the data, address and
  display-control commands with the eight pulse widths, fixed and
  auto-increment addressing, data past C5H dropped, a STOP keeping what was
  acknowledged, the key read (0xFF: no keys on the module), off at
  power-on, the colon or decimal points on SEG8, the module's 10 kOhm
  pull-ups; nine fixtures. TM1637Display's `TM1637Test` runs unmodified
  through all of its tests, 50/50. `tools/catalog/stimulus.py tm1637`
  writes its frames for fixtures.
- Fixed: a `digital_io` pin's nested `out` and `in` objects were ignored
  (the parser asked them for a `kind`), so their drive, pull and levels
  never applied.
- Fixed: `ctx.release` left a bidirectional pin an output driving Z, so its
  own pull never applied and, on an analog net, its comparator stopped
  sampling; a released pin is an input again (SPEC-013).
- The TSOP38238 IR receiver with the kit's 21-key NEC remote, from
  Vishay's data sheet: OUT active low with its 30 kOhm pull-up, low 10
  carrier cycles after a burst starts for the burst's length, nothing
  below 2.0 V; the remote's NEC frames and 108 ms repeat codes while a key
  is held; five fixtures. IRremote's `SimpleReceiver` runs unmodified,
  7/7. The Arduino runner puts an example's folder on the include path,
  compiles its other sources and resolves `.hpp` headers, as the IDE does.
- The BMP280, sharing the BME280's model (`part`): chip ID 0x58, no
  humidity registers, standby codes of 2 and 4 s; three fixtures. Adafruit's
  `bmp280test` runs unmodified at the library's default 0x77, 3/3.
- The HX711 load cell amplifier with a 5 kg load cell, from Avia's data
  sheet: DOUT low when a conversion is ready (10 or 80 Hz), 24 bits out
  MSB first on PD_SCK, 25 to 27 pulses choosing the next channel and gain,
  power-down by PD_SCK held high, the settling time, saturation, the
  ratiometric bridge with its zero balance, the input noise; eight
  fixtures. The HX711 library's `HX711_full_example` runs unmodified, 4/4.
- The Board view is a bench: parts in rows above the board, labelled, the
  project's nets drawn as wires between the artworks' pins (`pin-<name>`
  elements; ground black, supplies red, signals in colours), routed square
  and around the parts they would cross (SPEC-011 §3, SPEC-005 §8). The
  toolbar's clock shows the real-time rate. The TM1637, the 28BYJ-48 and
  the ULN2003 board name their pins.
- Board artwork for the 28BYJ-48 (the shaft turning with the output
  angle) and the ULN2003 board (its LEDs A to D); a composite's artwork
  may bind its members' outputs (`LEDA.luminance`).
- The ATmega328P's timers skip an instruction that brings no timer clock,
  which doubles the Uno's simulation speed.
- `tests/vendor/arduino` runs libraries' own examples unmodified:
  `libraries.json` pins each library to a commit (`--libs DIR --fetch`
  clones them; none is vendored), includes are followed through the
  libraries' sources as the IDE's builder does, and
  `examples/<library>/<example>/` holds the scenario. Adafruit's
  `DHTtester` reads the DHT22 on D2, 3/3.
- `docs/coverage.md`, generated by `tools/catalog/coverage.py`: each MCU's
  peripherals and whether the engine models them; the `coverage_tables`
  test fails when it is stale.

### Changed
- A CMOS output on an analog net drives its rails through `rout` (ADR-023):
  `voh` and `vol` are a datasheet's levels at the rated load, and as the
  Thevenin sources they left a light load 0.4 V from either rail. A pin
  model may give `voc_high` and `voc_low`; TTL outputs keep `voh` and
  `vol`.
- A steady analog island stops solving until something touches it
  (SPEC-004 §4): a busy MCU's 1 µs quanta made every LED and pull-up
  island solve a step each; SiFive's sifive-welcome runs 4 s of a HiFive1
  in 7 s instead of 42, with the same pin voltages.
- The analog fixtures are checked against ngspice too: `ngspice.json` beside
  each (`tools/analog/ngspice_fixture.py`, `--check` in CI), compared by
  `test_analog` within 2 mV + 0.2 %.
- A toolchain profile's `link_flags` may name the build's variables: the XC
  profiles' `-Wl,-Map=${map}` was passed through as written.
- The DWARF line reader takes XC16 images (their debug sections hold a byte
  in each 16-bit unit), so the Debug perspective shows a PIC24's source.
- Two weak pulls that disagree on a net are a divider, no longer reported
  as `net.contention`.
- A component's own parameters win over its part preset's.
- The runtime finds a UART's RX pin again when a pin's function changes.
- An MCU's GPIO pins take their high level and pulls from its description's
  supply (`io.electrical.vdd`): the nRF52840 DK's 3.0 V.
- The runtime and the loader take PIC pin names (RA0, RD6) as GPIO pins.
- An AVR timer's output pin switches at the clock that switched it, not at
  the end of the instruction it falls in, and an input capture takes the
  count at its edge's clock: the runtime gives the models `output_lag` and
  `input_lead` (SPEC-009 §7). A PWM's period no longer jitters by a cycle.
- RAM keeps its contents through a reset from the part itself (a watchdog,
  a system reset request) or from its reset pin, as an SRAM does while it
  is powered; it is erased at power-on: the session's start or reset, or a
  new image.

### Removed
- The draft 0.1 foundation (Python runtime, HTTP server, single-page UI);
  its kernel semantics and visual direction are retained in the baseline.

### Fixed
- The virtual terminal kept only the first 4 KiB it received and
  silently dropped the rest; it now keeps a 32 KiB scrollback, the oldest
  half going when it fills, as a terminal scrolls.
- A push button's or toggle switch's `bounce: true` now bounces: the
  switch primitive took only `{count, interval}`, so the components'
  boolean with `bounce_ms` did nothing. It now gives six transitions over
  `bounce_ms` (SPEC-004); a `push_button` fixture checks it.
- The Python client's stdio transport waited for the sidecar with
  select(), which Windows pipes do not support, so a wait with a timeout
  blocked instead: `test_serial_stream`'s last poll hung the windows-msvc
  job. A thread now reads the sidecar's output on every platform.
- The workbench's Serial terminal showed each piece of streamed text as a
  line of its own and mixed every port's text together: received text now
  goes on its port's open line until a newline, stamped with its first
  piece's time, the terminal shows the port chosen, and that port (the
  first one until another is picked) takes what is sent.
- An MCU's pin edge on an analog net reached the circuit after the core
  had gone on to its next edges: the next quantum started 1 ps later,
  before the edge's ramp had crossed a device's threshold, so a 25LC040
  clocked at 4 MHz through the Uno's D13 (whose L LED makes the net
  analog) answered a bit late. The next quantum now waits out the ramp
  (less than a cycle, the core going on from where it stopped), and a
  quantum that a wake-up brings earlier waits too: a second change to a
  pin had taken the place of one not yet applied, losing a clock pulse.
- Every pin change cost the core an idle cycle: the quantum after it began
  1 ps later and the core caught up to that time in whole cycles. The
  core now goes on from the cycle it stopped at, so firmware that toggles
  pins runs at its own speed (nrfx_periph's polling loops show it: 616
  RDSR polls, 94 NACKed address polls).
- The engine never sent `serial.data` (SPEC-008 §6), so the workbench's
  Serial terminal stayed empty while the firmware printed: the text was
  there only for a client that asked with `probe.read`. After each
  observation frame the session now streams every serial probe's new
  bytes, base64-encoded, and starts again from the beginning after a
  reset; `tests/protocol/test_serial_stream.py` checks the stream against
  `probe.read`.
- The RISC-V core decoded a store's offset by shifting a negative value
  left, which C leaves undefined: a store at a negative offset (as GCC's
  stack frames make) ran as intended with the compilers here, but UBSan
  stopped on it once gcc-riscv built the HiFive1 example. The offset is
  built unsigned and sign-extended; `test_riscv` stores at -64 and -2048.
- A `digital_io` pin that drove `Z` on an analog net kept its direction as
  an output, and the solver ran no comparator for it: a Lua I2C device's
  SDA, let go after its ACK, never saw the master's edges. A pin driving
  `Z` is an input (SPEC-003 §4). An MCU's open-drain output let go is an
  input too, and the runtime reads a pin on an analog net from its node's
  voltage, so an I2C master reads back its line as the slave holds it.
- The FE310 had nothing at the Debug Module's addresses (0 to 0xFFF),
  where freedom-metal's lfrosc driver reads, given the HiFive1 Rev B
  BSP's offsets as addresses: a load access fault in every example that
  asked the low-frequency clock's rate. The space reads as zeros.
- The Cortex-M4 cores lacked most of ARMv7E-M's DSP instructions (the
  parallel adds and subtracts with GE, SXTB16 and UXTB16 and their
  accumulating forms, SSAT16 and USAT16, the dual multiplies SMLAD to
  SMLSLD, USAD8 and USADA8): newlib's `strlen` for the M4 uses UADD8, and
  any image that called it took a UsageFault.
- The nRF52840's FICR had no words at 0x130 and 0x134 (the part code and
  revision, which the SVD does not list), which nrfx's MDK reads to choose
  its errata workarounds: a bus error.
- The FE310's PLIC had no register for source 0's priority, nor for the
  priority, pending and enable words of the sources past its last, which
  the PLIC specification has: freedom-metal's PLIC set-up writes and reads
  them, and each was a bus error. Source 0's priority reads 0, and the
  rest are reserved ranges.
- The RP2040's RESETS reported RESET_DONE only for the blocks with a
  register description, so the Pico SDK's start-up waited for JTAG, TBMAN
  and the rest without end; every block reports its reset done.
- The ATmega328P's timers set TOVn in fast PWM a timer clock early, as the
  counter reached TOP rather than as it went on to BOTTOM, so a read of
  TCNTn at TOP found the flag already set. The Arduino core's `micros()`
  counted that overflow twice and then ran back by 1024 µs, and `delay()`
  returned early: Blink blinked at 9 Hz, not 0.5 Hz. `test_avr` follows the flag
  clock by clock.
- `embsim build` with a GNU profile found no target for any board: the
  boards name a GNU target by `cpu`, the build looked for `target` only.
  The gcc-avr profile's convert step (`${objcopy}`, `${hex}`) and its
  target's `-DF_CPU` were not expanded, and it ignored a project's linker
  script. A project's firmware takes `link_flags` for the link alone. The
  gcc-arm profile now builds the Blue Pill, Pico and nRF52840 DK examples
  and gcc-avr the Uno, each passing its scenario.
- A digital input on an analog net heard of a threshold crossing when the
  analog step that found it began, up to a step early (0.9 ms on a slow
  ramp); it now hears of it at the crossing's own time, to the bisection's
  resolution, as a `SIGNAL` event, as SPEC-004 §8 has it. MCU inputs, gates and scripted models on analog nets all see
  the corrected times.
- A reset kept the pin configuration of the run before it (an MCU's
  pull-ups), so the analog side of a reset session started from it and ran
  apart from a freshly loaded one; a reset restores every pin's model.
- A digital probe on an analog net recorded the net's digital value (`Z`)
  at each change besides its thresholded samples; it records the
  thresholded level, when it changes.
- The Verify perspective went blank for a project without scenarios or
  probes: the scenario editor's store selectors made a new empty list on
  every render, which rendered again without end.
- Restore runtime input lists and waveform subscriptions when component placement
  or another structural edit restarts the engine; retain layout-only measurements.
- Catalog clock frequency binding, timed `tpd` application, functional pulse
  source terminal/period binding, and logic-probe output/value observation.
- Legacy current-source fixture polarity and an underdriven PMOS fixture load.
