#include "hardware/watchdog.h"
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

alarm_pool_t* core_1_alarm_pool;

#if SERVICE_TYPE == 1
#include "./services/thermostat.cpp"
#elif SERVICE_TYPE == 2
#include "./services/desk.cpp"
#endif

#include "./ntp.cpp"
#include "./server.cpp"

void core1_entry() {
  core_1_alarm_pool = alarm_pool_create(0, PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS);
  printf("[Main][Core-1] Starting core\n");
  
  service.ready();

  while (true) {
    #ifdef __HAS_LOOP
      service.loop();
    #else
      tight_loop_contents();
    #endif
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

bool watchdog_callback(struct repeating_timer *t) {
  try {
    watchdog_update();
    printf("[Main] Watchdog updated\n");
    return true;
  } catch (...) {
    printf("[Main] Failed update watchdog\n");
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
  struct repeating_timer watchdog_timer;

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
    watchdog_reboot(0, 0, 5);
    return -1;
  }

#ifdef __HAS_UPDATE_NETWORK
  service.update_network("...");
#endif
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

#ifdef __HAS_UPDATE_NETWORK
    service.update_network("FAIL");
#endif

    printf("[Main] WiFi connection failed\n\n");
    watchdog_reboot(0, 0, 5);
    return -1;
  }

  printf("[Main] WiFi connect success\n");

#ifdef __HAS_UPDATE_NETWORK
  service.update_network("NTP");
#endif

  if (!setup_ntp()) {
    cancel_repeating_timer(&timer);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    printf("[Main] NTP setup failed\n");
    cyw43_arch_deinit();
    watchdog_reboot(0, 0, 5);
    return -1;
  }

#ifdef __HAS_UPDATE_NETWORK
  service.update_network("ON");
#endif

  cancel_repeating_timer(&timer);
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  // Start watchdog
  watchdog_enable(8000, false);
  add_repeating_timer_ms(5000, watchdog_callback, NULL, &watchdog_timer);

  start_tcp_server_module();

  printf("[Main] Shuting down successfully\n");

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
  cyw43_arch_deinit();
  watchdog_reboot(0, 0, 5);
  return 0;
}