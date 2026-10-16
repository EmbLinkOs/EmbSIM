// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
// spi_wire -- the Arduino core's SPI and Wire libraries on the Uno against
// parts from the catalog:
//   - SPI at 4 MHz, mode 0, to a 25LC040 with its CS on D10: WREN, RDSR,
//     WRITE, RDSR polled through its write cycle, READ;
//   - Wire at 100 kHz (A4 SDA, A5 SCL, 4.7 kOhm pull-ups) to a 24C02 at
//     0x50: a page write, address-only writes NACKed through its write
//     cycle, a write without STOP then a read after a repeated START, the
//     same at 400 kHz, and an address nobody answers.
#include <SPI.h>
#include <Wire.h>

const int CS = 10;

static uint8_t eeprom_status()
{
  digitalWrite(CS, LOW);
  SPI.transfer(0x05);
  uint8_t s = SPI.transfer(0);
  digitalWrite(CS, HIGH);
  return s;
}

static void spi_eeprom()
{
  char text[8] = { 0 };
  unsigned polls = 0;
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);
  SPI.begin();
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS, LOW);
  SPI.transfer(0x06); // WREN
  digitalWrite(CS, HIGH);
  uint8_t wel = eeprom_status();
  digitalWrite(CS, LOW);
  SPI.transfer(0x02); // WRITE at 0x50
  SPI.transfer(0x50);
  for (const char *p = "Uno328"; *p; p++)
    SPI.transfer(*p);
  digitalWrite(CS, HIGH);
  while (eeprom_status() & 1)
    polls++;
  digitalWrite(CS, LOW);
  SPI.transfer(0x03); // READ from 0x50
  SPI.transfer(0x50);
  for (int i = 0; i < 6; i++)
    text[i] = SPI.transfer(0);
  digitalWrite(CS, HIGH);
  SPI.endTransaction();
  Serial.print(F("spi: status 0"));
  Serial.print(wel, HEX);
  Serial.print(F(" after WREN, write cycle over after "));
  Serial.print(polls);
  Serial.print(F(" polls, read "));
  Serial.println(text);
}

static void random_read(uint8_t at, int n)
{
  char text[8] = { 0 };
  Wire.beginTransmission(0x50);
  Wire.write(at);
  uint8_t err = Wire.endTransmission(false); // no STOP: a repeated START next
  uint8_t got = Wire.requestFrom(0x50, n);
  for (int i = 0; i < got && Wire.available(); i++)
    text[i] = Wire.read();
  Serial.print(err);
  Serial.print(' ');
  Serial.print(got);
  Serial.print(' ');
  Serial.println(text);
}

static void i2c_eeprom()
{
  unsigned busy = 0;
  Wire.begin();
  Wire.beginTransmission(0x50);
  Wire.write(0x60);
  Wire.write("Wire!");
  Serial.print(F("wire: page write "));
  Serial.println(Wire.endTransmission());
  for (;;) {
    Wire.beginTransmission(0x50);
    if (Wire.endTransmission() == 0)
      break;
    busy++;
  }
  Serial.print(F("wire: ready again after "));
  Serial.print(busy);
  Serial.println(F(" NACKs"));
  Serial.print(F("wire: random read at 100 kHz "));
  random_read(0x61, 4);
  Wire.setClock(400000);
  Serial.print(F("wire: random read at 400 kHz "));
  random_read(0x60, 5);
  Wire.beginTransmission(0x51);
  Serial.print(F("wire: 0x51 "));
  Serial.println(Wire.endTransmission());
}

void setup()
{
  Serial.begin(115200);
  Serial.println(F("EmbSIM Arduino SPI and Wire"));
  spi_eeprom();
  i2c_eeprom();
  Serial.println(F("done"));
}

void loop()
{
}
