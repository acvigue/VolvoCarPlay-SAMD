#include <Arduino.h>
#include <Wire.h>

byte tfp410_read(byte reg) {
  Wire.beginTransmission(0b0111000);
  for (byte i=1; i<1; i++) Wire.write(0x00);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(0b0111000,1);
  byte val = Wire.read();
  return val;
}

void tfp410_write(byte reg, byte value) {
  Wire.beginTransmission(0b0111000);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

byte tmds261b_read(byte reg) {
  Wire.beginTransmission(0b0101100);
  for (byte i=1; i<1; i++) Wire.write(0x00);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(0b0101100,1);
  byte val = Wire.read();
  return val;
}

void tmds261b_write(byte reg, byte value) {
  Wire.beginTransmission(0b0101100);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}