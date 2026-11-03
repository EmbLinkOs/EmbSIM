// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
// The Arduino core's everyday calls on the Uno: digitalWrite and delay (the
// L LED at 5 Hz), millis, analogRead (a potentiometer on A0), analogWrite
// (Timer1's phase correct PWM on D9, Timer0's fast PWM on D6),
// attachInterrupt (a button on D2, debounced: its contacts bounce) and Serial
// at 115200 baud, both ways.
volatile unsigned presses;
volatile unsigned long lastEdge;

// a press is a falling edge after 20 ms without an edge either way: the
// contacts chatter for a few milliseconds when pressed and when released
void pressed() {
  unsigned long now = millis();
  if (digitalRead(2) == LOW && now - lastEdge >= 20)
    presses++;
  lastEdge = now;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(2), pressed, CHANGE);
  Serial.begin(115200);
  Serial.println(F("EmbSIM Arduino core tour"));
  analogWrite(9, 64);
  analogWrite(6, 192);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  Serial.print(F("A0="));
  Serial.print(analogRead(A0));
  Serial.print(F(" presses="));
  Serial.print(presses);
  Serial.print(F(" ms="));
  Serial.println(millis());
  while (Serial.available()) {
    int c = Serial.read();
    Serial.print(F("echo "));
    Serial.write(c);
    Serial.println();
  }
}
