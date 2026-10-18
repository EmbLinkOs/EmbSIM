// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Timer/Counter1's input capture measuring a PWM signal, as a frequency
// counter does: analogWrite(3, 64) puts Timer2's phase-correct PWM on D3
// (490.196 Hz: 16 MHz / 64 / 510), wired to ICP1 (D8). TIMER1_CAPT
// timestamps rising edges for the period, then alternates ICES1 for the
// high time, with the noise canceler on: a period of 32640 cycles, high for
// 2 * 64 * 64 = 8192.

#include <avr/interrupt.h>

static volatile uint16_t stamp[8];
static volatile uint8_t n;
static volatile uint8_t toggle;

ISR(TIMER1_CAPT_vect) {
  if (n < 8)
    stamp[n++] = ICR1;
  if (toggle)
    TCCR1B ^= _BV(ICES1); // the next edge is the other one
}

static void captureEdges(uint8_t alternate) {
  noInterrupts();
  n = 0;
  toggle = alternate;
  TCCR1A = 0;
  TCCR1B = _BV(ICNC1) | _BV(ICES1) | _BV(CS10); // clk/1, rising first
  TIFR1 = _BV(ICF1);
  TIMSK1 = _BV(ICIE1);
  interrupts();
  while (n < 8) {
  }
  TIMSK1 = 0;
}

void setup() {
  Serial.begin(115200);
  analogWrite(3, 64);
  delay(10);

  captureEdges(0);
  uint16_t period = stamp[7] - stamp[6];
  bool steady = true;
  for (uint8_t i = 1; i < 8; i++)
    if ((uint16_t)(stamp[i] - stamp[i - 1]) != period)
      steady = false;
  Serial.print("period ");
  Serial.print(period);
  Serial.print(" cycles, ");
  Serial.print(16000000.0 / period, 3);
  Serial.println(steady ? " Hz, steady" : " Hz, varying");

  captureEdges(1);
  Serial.print("high ");
  Serial.print((uint16_t)(stamp[1] - stamp[0]));
  Serial.print(" low ");
  Serial.println((uint16_t)(stamp[2] - stamp[1]));
  Serial.println("done");
}

void loop() {}
