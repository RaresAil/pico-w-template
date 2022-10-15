#include <stdio.h>
#include <iomanip>
#include <ios>
#include <iostream>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#ifndef __INFO_CPP__
#define __INFO_CPP__

#define FLASH_RUID_DATA_BYTES 8

uint8_t __flash_uid[FLASH_RUID_DATA_BYTES];
char __flash_uid_s[FLASH_RUID_DATA_BYTES * 2] = "";

void read_chip_uid() {
  uint32_t interrupts = save_and_disable_interrupts();
  flash_get_unique_id(__flash_uid);
  restore_interrupts(interrupts);
  sprintf(__flash_uid_s, "%02X%02X%02X%02X%02X%02X%02X%02X", __flash_uid[0], __flash_uid[1], __flash_uid[2], __flash_uid[3], __flash_uid[4], __flash_uid[5], __flash_uid[6], __flash_uid[7]);
}

#endif