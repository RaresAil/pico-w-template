#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string>

#include "./info.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#include "./config.h"

#if SERVICE_TYPE == 1
#include "./services/thermostat.cpp"
#elif SERVICE_TYPE == 2
#include "./services/desk.cpp"
#endif

#include "./ntp.cpp"
#include "./server.cpp"

void core1_entry() {
  multicore_lockout_victim_init();
  printf("[Main][Core-1] Starting core\n");
  
  service.ready();

  while (true) {
    service.loop();
  }
}

bool led_blink_timer(struct repeating_timer *t) {
  try {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN));
    return true;
  } catch (...) {
    printf("[Main] Failed blinking status led\n");
    return false;
  }
}

int main() {
#ifdef IS_DEBUG_MODE
  stdio_usb_init();
  stdio_filter_driver(&stdio_usb);
  stdio_set_translate_crlf(&stdio_usb, true);
  stdio_flush();
#endif

#if SERVICE_TYPE == 1
  adc_init();
  adc_set_temp_sensor_enabled(true);
  adc_select_input(4);
#endif

  uint8_t connection_retries = 10;
  struct repeating_timer timer;

#ifdef IS_DEBUG_MODE
  sleep_ms(2000);
  printf("[Main] Debug Mode Enabled\n");
#endif

  printf("[Main] Booting up\n");

  read_chip_uid();

  multicore_launch_core1(core1_entry);

  if (cyw43_arch_init_with_country(CYW43_COUNTRY(COUNTRY_CODE_0, COUNTRY_CODE_1, 0))) {
    add_repeating_timer_ms(2000, led_blink_timer, NULL, &timer);
    printf("[Main] WiFi init failed");
    return -1;
  }

  service.update_network("...");
  printf("[Main] WiFi init success (Hostname: %s)\n", CYW43_HOST_NAME);

  cyw43_arch_enable_sta_mode();
  add_repeating_timer_ms(500, led_blink_timer, NULL, &timer);

  printf("[Main] WiFi connecting to (%s, %s)\n", WIFI_SSID, WIFI_PASSWORD);

  while (connection_retries > 0) {
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, WIFI_AUTH, 8000)) {
      printf("[Main] WiFi connection failed (%u/10)\n\n", connection_retries);
      connection_retries--;
      sleep_ms(1000);
    } else {
      break;
    }
  }

  if (connection_retries <= 0) {
    cancel_repeating_timer(&timer);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    service.update_network("FAIL");
    printf("[Main] WiFi connection failed\n\n");
    return -1;
  }

  printf("[Main] WiFi connect success\n");
  service.update_network("NTP");

  if (!setup_ntp()) {
    cancel_repeating_timer(&timer);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    printf("[Main] NTP setup failed\n");
    cyw43_arch_deinit();
    return -1;
  }

  service.update_network("ON");
  cancel_repeating_timer(&timer);
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  start_tcp_server_module();

  printf("[Main] Shuting down successfully\n");

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
  cyw43_arch_deinit();
  return 0;
}