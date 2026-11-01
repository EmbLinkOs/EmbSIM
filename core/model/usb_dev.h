/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb_dev.h -- the device side of a USB part (a mouse, a flash drive) on a
 * cable of the bench (engine/usb.h): what every USB device's controller and
 * chapter 9 firmware do, for the class code of the part.
 *
 *   - attached (its pull-up on D+ at full speed, on D- at low speed) while
 *     VBUS at its VBUS pin is above 4.4 V (a bus-powered device; unwired:
 *     always), and its `plugged` input is true;
 *   - the bus's reset puts it in the default state at address 0; 3 ms
 *     without frames suspends it, any activity resumes it;
 *   - EP0 answers the standard requests (USB 2.0 9.4): GET_DESCRIPTOR
 *     (device, configuration, strings), SET_ADDRESS (in effect after its
 *     status stage), SET_CONFIGURATION, GET_CONFIGURATION, GET_STATUS,
 *     SET/CLEAR_FEATURE (ENDPOINT_HALT, DEVICE_REMOTE_WAKEUP), GET/SET_
 *     INTERFACE; the class's requests go to the part; anything else
 *     STALLs; IN data in packets of bMaxPacketSize0, cut to wLength;
 *   - other endpoints: the part's in() and out(); the data toggles are
 *     kept per endpoint and direction, a repeated OUT packet is ACKed and
 *     dropped, a halted endpoint STALLs.
 * Inside: no bit-level signalling, no isochronous endpoints. */
#ifndef ESK_MODEL_USB_DEV_H
#define ESK_MODEL_USB_DEV_H

#include "engine/engine.h"

#define USBD_EPS 16

typedef struct usbd usbd;

typedef struct usbd_class {
    /* a class or vendor request (bmRequestType's type not standard): the
     * IN data to *out (up to cap bytes, *len set), or the OUT data in
     * `data`; ESK_USB_ACK, ESK_USB_STALL */
    int (*setup)(usbd *u, const uint8_t setup[8], const uint8_t *data, uint8_t *out, uint32_t cap, uint32_t *len);
    /* an IN token on endpoint `ep` (1-15): ACK with a packet, NAK, STALL */
    int (*in)(usbd *u, int ep, uint8_t *data, uint32_t cap, uint32_t *len);
    /* an OUT packet on endpoint `ep`: ACK (taken), NAK (no room), STALL */
    int (*out)(usbd *u, int ep, const uint8_t *data, uint32_t len);
    /* the configuration was set (cfg 0: unconfigured) or the bus reset */
    void (*configured)(usbd *u, int cfg);
} usbd_class;

struct usbd {
    esk_comp *c;
    esk_usb_dev link;
    int linked;
    esk_pin *vbus;
    int low_speed;
    int plugged;
    const uint8_t *dev_desc, *cfg_desc;
    uint32_t cfg_len;
    const char *strings[8]; /* string descriptors 1..7 (ASCII) */
    const usbd_class *cls;
    void *cls_ctx;
    /* chapter 9 state */
    int addr, new_addr, addr_pending, config, suspended, remote_wakeup, alt;
    uint32_t halted_in, halted_out; /* endpoint bits */
    int pid_in[USBD_EPS], pid_out[USBD_EPS];
    /* EP0's control transfer */
    uint8_t setup[8];
    uint8_t ep0[1024];
    uint32_t ep0_len, ep0_pos, ep0_out_want;
    int ep0_dir_in, ep0_stalled, ep0_status_zlp_sent;
    int ep0_last_full; /* a full last packet: a ZLP may still be asked */
    uint64_t frames, setups;
};

/* set up the device: its pins VBUS (optional), DP, DM; descriptors kept by
 * the caller; strings[i] is string descriptor i + 1 */
es_status usbd_init(usbd *u, esk_comp *c, int low_speed, const uint8_t *dev_desc, const uint8_t *cfg_desc,
                    uint32_t cfg_len, const char *const *strings, int nstrings, const usbd_class *cls,
                    void *cls_ctx);
void usbd_destroy(usbd *u);
void usbd_reset(usbd *u);
/* the VBUS pin changed or `plugged`: the host is told */
void usbd_power(usbd *u);
/* a part's EP0 for its standard parts: wMaxPacketSize0 */
uint32_t usbd_mps0(const usbd *u);
/* the device's own state, for outputs */
const char *usbd_state(const usbd *u);

#endif
