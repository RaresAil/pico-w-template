#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#ifndef __FLASH_CPP__
#define __FLASH_CPP__

// 10 years in microseconds
#define MULTICORE_LOCKOUT_TIMEOUT (uint64_t)10 * 365 * 24 * 60 * 60 * 1000 * 1000
#define FLASH_TARGET_OFFSET (256 * 1024)
const uint8_t* flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

uint32_t last_flash_write = 0;

// SDK 1.4.0 and 1.4.1 has a bug where the lockout is not working
// No fix yet, so i found a workaround
// The workaround is to use the multicore lockout timeout set to 10 years

// Can only be called from core 0
void save_flash_data() {
  printf("[Flash] Programming flash.\n");

  printf("[Flash] Requesting core 1 to lockout.\n");
  const bool locked = multicore_lockout_start_timeout_us(MULTICORE_LOCKOUT_TIMEOUT);

  if (locked) {
    printf("[Flash] Disable IRQ on core 0.\n");

    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (uint8_t*) &flash_data, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);

    printf("[Flash] Restore IRQ on core 0\n");

    printf("[Flash] Removing core 1 lockout.\n");
    bool unlocked = false;

    do {
      unlocked = multicore_lockout_end_timeout_us(MULTICORE_LOCKOUT_TIMEOUT);
    } while(!unlocked);

    printf("[Flash] Done.\n");
  } else {
    printf("[Flash] Failed to lock core 1.\n");
  }
}

void read_flash_data() {
  memcpy(&flash_data, flash_target_contents, sizeof(flash_data));
}

void flash_main_loop() {
  if (save_to_flash) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    // Add a timeout to prevent flash from being written too often
    if (now - last_flash_write > 1000) {
      last_flash_write = now;
      save_to_flash = false;
      save_flash_data();
    }
  }
}

#endif