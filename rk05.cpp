#include <stdint.h>
#include <Arduino.h>
#include <SD.h>
#include "avr11.h"
#include "unibus.h"
#include "rk05.h"
#include "cpu.h"

uint32_t RKBA, RKDS, RKER, RKCS, RKWC;
uint32_t drive, sector, surface, cylinder;

bool running;

File rkdata;

int32_t rkread16(int32_t a){
  switch (a) {
  case 0777400:
    return RKDS;
  case 0777402:
    return RKER;
  case 0777404:
    return RKCS | (RKBA&0x30000)>>12;
  case 0777406:
    return RKWC;
  case 0777410:
    return RKBA & 0xFFFF;
  case 0777412:
    return (sector) | (surface << 4) | (cylinder << 5) | (drive << 13);
  default:
    Serial.println(F("rkread16: invalid read"));
    panic();
  }
}

void rknotready() {
  digitalWrite(13, 1);
  RKDS &= ~(1 << 6);
  RKCS &= ~(1 << 7);
}

void rkready() {
  RKDS |= 1 << 6;
  RKCS |= 1 << 7;
  digitalWrite(13, 0);
}

void rkerror(uint16_t e) { 
}

void rkstep() {
  bool w;
  if (!running) {
    return;
  }

  switch ((RKCS & 017) >> 1) {
  case 0:
    return;
  case 1:
    w = true; 
    break;
  case 2:
    w = false; 
    break;
  default:
    Serial.println(F("unimplemented RK05 operation")); //  %#o", ((r.RKCS & 017) >> 1)))
    panic();
  }

/**
  Serial.print("rkstep: RKBA: "); 
  Serial.print(RKBA, DEC);
  Serial.print(" RKWC: "); 
  Serial.print(RKWC, DEC);
  Serial.print(" cylinder: "); 
  Serial.print(cylinder, DEC);
  Serial.print(" sector: "); 
  Serial.print(sector, DEC); 
  Serial.print(" write: ");
  Serial.println(w ? "true" : "false");
  */

  if (drive != 0) {
    rkerror(RKNXD);
  }
  if (cylinder > 0312) {
    rkerror(RKNXC);
  }
  if (sector > 013) {
    rkerror(RKNXS);
  }

  int32_t pos = (cylinder*24 + surface*12 + sector) * 512;
  if (!rkdata.seek(pos)) {
    Serial.println(F("rkstep: failed to seek"));
    panic();
  }

  uint16_t i;
  uint16_t val;
  for (i = 0; i < 256 && RKWC != 0; i++) {
    if (w) {
      val = unibus.read16(RKBA);
      rkdata.write(val & 0xFF);
      rkdata.write((val >> 8) & 0xFF);
    } 
    else {
      unibus.write16(RKBA, rkdata.read() | (rkdata.read()<<8)); 
    }
    RKBA += 2;
    pos += 2;
    RKWC = (RKWC + 1) & 0xFFFF;
  }
  sector++;
  if (sector > 013) {
    sector = 0;
    surface++;
    if (surface > 1) {
      surface = 0;
      cylinder++;
      if (cylinder > 0312) {
        rkerror(RKOVR);
      }
    }
  }
  if (RKWC == 0) {
    running = false;
    rkready();
    if (RKCS&(1<<6)) {
      interrupt(INTRK, 5);
    }
  }
}


void rkwrite16(int32_t a, uint16_t v) {
  //printf("rkwrite: %06o\n",a);
  switch (a) {
  case 0777400:
    break;
  case 0777402:
    break;
  case 0777404:
    RKBA = (RKBA & 0xFFFF) | ((v & 060) << 12);
    v &= 017517; // writable bits
    RKCS &= ~017517;
    RKCS |= v & ~1; // don't set GO bit
    if (v&1) {
  switch ((RKCS & 017) >> 1) {
  case 0:
    rkreset(); 
    break;
  case 1:
  case 2:
    running = true;
    rknotready(); 
    break;
  default:
    Serial.println(F("unimplemented RK05 operation")); // %#o", ((r.RKCS & 017) >> 1)))
    panic();
  }
    }
    break;
  case 0777406:
    RKWC = v; 
    break;
  case 0777410:
    RKBA = (RKBA & 0x30000) | (v); 
    break;
  case 0777412:
    drive = v >> 13;
    cylinder = (v >> 5) & 0377;
    surface = (v >> 4) & 1;
    sector = v & 15;
    break;
  default:
    Serial.println(F("rkwrite16: invalid write"));
    panic();
  }
}

void rkreset() {
  RKDS = (1 << 11) | (1 << 7) | (1 << 6);
  RKER = 0;
  RKCS = 1 << 7;
  RKWC = 0;
  RKBA = 0;
}

void rkinit() { 
  // On the Ethernet Shield, CS is pin 4. It's set as an output by default.
  // Note that even if it's not used as the CS pin, the hardware SS pin 
  // (10 on most Arduino boards, 53 on the Mega) must be left as an output 
  // or the SD library functions will not work. 
  pinMode(53, OUTPUT);

  pinMode(13, OUTPUT); // d13 is our sdcard access indicator

  if (!SD.begin(4)) {
    Serial.println(F("initialization failed!"));
    panic();
  }
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  rkdata = SD.open("UNIXV6.RK0", FILE_WRITE);

  // if the file is available, write to it:
  if (!rkdata) {
    Serial.println(F("rkinit: could not open rk0"));
    panic();
  } 
}



