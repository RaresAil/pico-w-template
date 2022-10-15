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

std::string FLASH_SERIAL_NUMBER = "0000000000000000";

std::string uint8_to_hex_string(const uint8_t *v, const size_t s) {
  std::stringstream ss;

  ss << std::hex << std::setfill('0');
  for (int i = 0; i < s; i++) {
    ss << std::hex << std::setw(2) << static_cast<int>(v[i]);
  }

  return ss.str();
}

void read_chip_uid() {
	uint8_t id[FLASH_RUID_DATA_BYTES];
  uint32_t interrupts = save_and_disable_interrupts();
  flash_get_unique_id(id);
  restore_interrupts(interrupts);

  FLASH_SERIAL_NUMBER = uint8_to_hex_string(id, FLASH_RUID_DATA_BYTES);
}

#endif