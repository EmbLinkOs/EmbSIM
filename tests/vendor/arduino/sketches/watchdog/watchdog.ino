// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// avr-libc's watchdog on the Uno: the WDT interrupt waking the part from
// power-down four times, then wdt_enable() in system reset mode, kicked by
// wdt_reset() for 100 ms and then left to time out. A boot count kept in
// .noinit (RAM keeps it through a watchdog reset; power-on clears MCUSR's
// other flags) says which boot this is, MCUSR why.

#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

static uint8_t boots __attribute__((section(".noinit")));
static volatile uint8_t ticks;

ISR(WDT_vect) { ticks++; }

void setup() {
  uint8_t why = MCUSR;
  MCUSR = 0;
  wdt_disable(); // WDRF cleared first: the timed sequence can clear WDE
  if (why & _BV(PORF))
    boots = 0;
  boots++;
  Serial.begin(115200);
  Serial.print("boot ");
  Serial.print(boots);
  Serial.print(" mcusr ");
  Serial.println(why, HEX);

  if (boots == 1) {
    // interrupt mode, 32 ms: WDIE with WDP0 through the timed sequence
    cli();
    wdt_reset();
    WDTCSR = _BV(WDCE) | _BV(WDE);
    WDTCSR = _BV(WDIE) | _BV(WDP0);
    sei();
    Serial.flush();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    while (ticks < 4)
      sleep_mode();
    wdt_disable();
    Serial.print("woken by the watchdog ");
    Serial.print(ticks);
    Serial.println(" times");
  }

  if (boots < 3) {
    wdt_enable(WDTO_60MS);
    unsigned long t0 = millis();
    while (millis() - t0 < 100)
      wdt_reset();
    Serial.println("kicked for 100 ms, now hanging");
    Serial.flush();
    for (;;) {
    }
  }
  Serial.println("done");
}

void loop() {}
