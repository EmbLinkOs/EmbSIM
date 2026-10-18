// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The analog comparator timing an RC network, as the datasheet's
// application of it does: D8 charges a 1 uF capacitor on AIN0 (D6) through
// 10 kOhm while AIN1 (D7) sits at 2.5 V on a divider. The comparator's
// interrupt catches the crossing on its rising edge, then on its falling
// edge as D8 discharges it; each takes R * C * ln 2, about 6.93 ms.

#include <avr/interrupt.h>

static volatile unsigned long crossed;
static volatile uint8_t fired;

ISR(ANALOG_COMP_vect) {
  crossed = micros();
  fired++;
}

static unsigned long timeCrossing(uint8_t level, uint8_t edge) {
  fired = 0;
  ACSR = _BV(ACI) | edge;      // clear ACI, choose the edge
  ACSR = _BV(ACIE) | edge;     // then enable the interrupt
  unsigned long t0 = micros();
  digitalWrite(8, level);
  while (!fired) {
  }
  ACSR = edge;                 // interrupt off
  return crossed - t0;
}

void setup() {
  Serial.begin(115200);
  pinMode(8, OUTPUT);
  digitalWrite(8, LOW);
  delay(100);                  // discharged: 15 time constants
  Serial.print("aco at rest ");
  Serial.println((ACSR & _BV(ACO)) ? 1 : 0);
  unsigned long rise = timeCrossing(HIGH, _BV(ACIS1) | _BV(ACIS0));
  delay(100);
  Serial.print("aco charged ");
  Serial.println((ACSR & _BV(ACO)) ? 1 : 0);
  unsigned long fall = timeCrossing(LOW, _BV(ACIS1));
  Serial.print("rise ");
  Serial.print(rise);
  Serial.print(" us, fall ");
  Serial.print(fall);
  Serial.print(" us, interrupts ");
  Serial.println(fired);
  Serial.println("done");
}

void loop() {}
