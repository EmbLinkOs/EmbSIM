/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* mcu.h -- the MCU runtime (SPEC-009 §2, §7, §9): a board's MCU as a
 * component of the session whose pins are its package's signals, running a
 * firmware image on the machine (mach.h) in quanta on the scheduler.
 *
 * Time: the core's cycles become picoseconds at the core clock the clock
 * model reports. A quantum is a CORE event; it runs the core until the next
 * event of the session that is not an analog step, at most `quantum_ps`
 * (ADR-019), and ends early when a pin changes, so the circuit sees every
 * edge at the instruction that made it. A sleeping core (WFI, an idle loop)
 * costs nothing: its next quantum is the time its devices' next event comes,
 * or a pin's change that wakes it.
 *
 * Pins: a GPIO configuration becomes the pin's model at the time of the
 * write: push-pull or open-drain, the weak pull, input or analog. A pin's
 * level comes back from its net (the comparator's state on an analog net).
 * NRST held low holds the core in reset; BOOT0 high at reset boots the
 * system memory, which is not shipped, and is reported.
 *
 * Serial: each UART frame is the transmitted byte, appended to the board's
 * serial text (observed as `serial.UNIT`), and the TX pin's waveform when
 * the pin is set to its alternate function. Received bytes come from the
 * session's serial input (`serial`, `serial:UNIT`) at the UART's baud rate,
 * and from the RX pin's waveform, sampled at mid-bit. */
#ifndef ESK_MCU_MCU_H
#define ESK_MCU_MCU_H

#include "engine/engine.h"
#include "mcu/mach.h"

extern const esk_model_ops esk_mcu_ops;

typedef struct esk_mcu_setup {
    const esk_json *mcu;  /* the MCU description */
    const esk_json *regs; /* its register description, or NULL */
    u32 hse_hz;           /* the board's external oscillator, 0 for none */
    const unsigned char *image; /* the firmware, copied */
    size_t image_len;
    const char *image_name;
    es_time_t quantum_ps; /* 0: 10 us */
    const char *console;  /* the default serial unit ("USART1") */
} esk_mcu_setup;

/* Give a component made with esk_mcu_ops its machine and firmware. Errors
 * name the problem in `why`. */
es_status esk_mcu_attach(esk_comp *c, const esk_mcu_setup *su, char *why, size_t cap);
int esk_mcu_is(const esk_comp *c);
/* a new firmware image (a rebuild): loaded, then the MCU resets */
es_status esk_mcu_set_image(esk_comp *c, const unsigned char *image, size_t len, char *why, size_t cap);
/* the machine, for the debugger; NULL for another component */
struct esk_mach *esk_mcu_machine(esk_comp *c);
/* the firmware image as loaded, for symbols */
const unsigned char *esk_mcu_image(const esk_comp *c, size_t *len);
/* the engine time of the machine's current cycle */
es_time_t esk_mcu_time(const esk_comp *c);
/* stopped by a breakpoint, a watchpoint or a step: 1 and why */
int esk_mcu_stopped(const esk_comp *c, const char **why);
/* resume a stopped core (`steps` instructions first when nonzero) */
void esk_mcu_resume(esk_comp *c, u64 steps);
/* stop the core where it is, as a debugger's interrupt does */
void esk_mcu_halt(esk_comp *c);

#endif
