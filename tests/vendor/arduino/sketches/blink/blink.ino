// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
// Blink as the IDE's File > Examples > 01.Basics > Blink does it: the L LED
// on for a second, off for a second.
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
}
