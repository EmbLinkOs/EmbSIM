/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* registry.h -- the model registry (SPEC-005 §10): primitive names to
 * esk_model_ops, for digital primitives and for the analog devices wrapped
 * as components. Built per session (CS-18: no mutable globals). */
#ifndef ESK_REGISTRY_H
#define ESK_REGISTRY_H

#include "engine/engine.h"
#include "util/map.h"

typedef struct esk_registry {
    esk_alloc *a;
    esk_map by_name;             /* name -> esk_model_ops* */
    esk_model_ops *wrappers;     /* generated ops for analog devices */
    size_t nwrappers;
} esk_registry;

es_status esk_registry_init(esk_registry *r, esk_alloc *a);
void esk_registry_free(esk_registry *r);
const esk_model_ops *esk_registry_find(const esk_registry *r, const char *name);
/* the digital primitives (digital.c) */
const esk_model_ops *const *esk_digital_models(size_t *n);
/* a Wi-Fi access point on the session's air (wifi_ap.c) */
extern const esk_model_ops esk_wifi_ap_ops;
/* a Bluetooth LE scanner on the session's air (ble_scanner.c) */
extern const esk_model_ops esk_ble_scanner_ops;
/* a phone's Wi-Fi analyzer on the session's air (wifi_scanner.c) */
extern const esk_model_ops esk_wifi_scanner_ops;
/* a Wi-Fi or Bluetooth LE sniffer on the session's air (air_sniffer.c) */
extern const esk_model_ops esk_air_sniffer_ops;
/* a computer's USB port: the host on a USB cable (usb_host.c) */
extern const esk_model_ops esk_usb_host_ops;
/* its CDC ACM serial port, for a host port (session/hostport.c): the bytes
 * come so far (the last *len of *total), whether it is open, room to send,
 * what is sent, and the terminal's side of the line (a program holds the
 * port: DTR with it, its baud rate as the line coding) */
const uint8_t *esk_usb_host_cdc_rx(struct esk_comp *c, uint64_t *total, size_t *len);
int esk_usb_host_cdc_open(struct esk_comp *c);
size_t esk_usb_host_cdc_room(struct esk_comp *c);
size_t esk_usb_host_cdc_write(struct esk_comp *c, const uint8_t *b, size_t n);
void esk_usb_host_cdc_line(struct esk_comp *c, int held, int dtr, uint32_t baud);
/* ST's ST-LINK/V2-1 debug probe (st_link.c): its command interface as its
 * USB bulk endpoints carry it, a 16-byte command `cmd` with a write's `out`
 * data, answered into `in` (at most `cap` bytes; the length answered);
 * whether a component is one; its serial number */
extern const esk_model_ops esk_st_link_ops;
size_t esk_stlink_command(struct esk_comp *c, const uint8_t cmd[16], const uint8_t *out, size_t nout, uint8_t *in,
                          size_t cap);
int esk_stlink_is(const struct esk_comp *c);
/* the probe's trace read (its trace endpoint, stlink-server's READ_SWO):
 * up to `cap` bytes of the SWO it has received */
size_t esk_stlink_trace(struct esk_comp *c, uint8_t *in, size_t cap);
const char *esk_stlink_serial(struct esk_comp *c);
/* the Raspberry Pi Debug Probe: CMSIS-DAP v2 and a UART on its USB (debug_probe.c) */
extern const esk_model_ops esk_debug_probe_ops;
/* a web server on the internet beyond the routers (web_server.c) */
extern const esk_model_ops esk_web_server_ops;
/* an 8-channel logic analyzer, the Saleae Logic 8 (logic_analyzer.c) */
extern const esk_model_ops esk_logic_analyzer_ops;
/* its capture's transitions since `since` (at most `max`, 0: a default), as JSON into `out` */
es_status esk_logic_analyzer_read(esk_comp *c, uint64_t since, size_t max, esk_json *out);
/* a phone connecting to a BLE peripheral and browsing its GATT database (ble_central.c) */
extern const esk_model_ops esk_ble_central_ops;
/* a JSON array of {name, kind: "digital"|"electrical"} */
esk_json *esk_registry_list(const esk_registry *r);

#endif
