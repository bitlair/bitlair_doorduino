/*
 * EEPROM Clear
 *
 * Sets all of the bytes of the EEPROM to 0.
 * Please see eeprom_iteration for a more in depth
 * look at how to traverse the EEPROM.
 *
 * This example code is in the public domain.
 */

#include "Arduino.h"
//#include <EEPROM.h>
#include "Wire.h"

#define EEPROMDEVICEADDRESS    0x50
#define EEPROMSIZE             2048


void writeEEPROM(unsigned int eeaddress, byte data )
{
  Wire.beginTransmission(EEPROMDEVICEADDRESS);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.write(data);
  Wire.endTransmission();

  delay(5);
}

byte readEEPROM(unsigned int eeaddress )
{
  byte rdata = 0xFF;

  Wire.beginTransmission(EEPROMDEVICEADDRESS);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.endTransmission();

  Wire.requestFrom(EEPROMDEVICEADDRESS,1);

  if (Wire.available()) rdata = Wire.read();

  return rdata;
}

void setup() {
  // initialize the LED pin as an output.
  pinMode(13, OUTPUT);
  Serial.begin(115200);

  /***
    Iterate through each byte of the EEPROM storage.
    Larger AVR processors have larger EEPROM sizes, E.g:
    - Arduino Duemilanove: 512 B EEPROM storage.
    - Arduino Uno:         1 kB EEPROM storage.
    - Arduino Mega:        4 kB EEPROM storage.
    Rather than hard-coding the length, you should use the pre-provided length function.
    This will make your code portable to all AVR processors.
  ***/

  delay(1000);
  Serial.println("BEGIN");

  for (int i = 0 ; i < EEPROMSIZE ; i++) {
    // writeEEPROM(i, 0xFF);
    Serial.print(readEEPROM(i),HEX);
  }

  // turn the LED on when we're done
  digitalWrite(13, HIGH);

  Serial.println("");
  Serial.println("END");
}

void loop() {
  /** Empty loop. **/
}