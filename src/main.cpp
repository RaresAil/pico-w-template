#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string>

#include "./config.h"
#include "./server.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

bool led_blink_timer(struct repeating_timer *t) {
  try {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN));
    return true;
  } catch (...) {
    printf("Failed blinking status led\n");
    return false;
  }
}

int main() {
  stdio_usb_init();
  stdio_filter_driver(&stdio_usb);
  stdio_set_translate_crlf(&stdio_usb, true);
  stdio_flush();

  uint8_t connection_retries = 10;
  struct repeating_timer timer;

#ifdef IS_DEBUG_MODE
  sleep_ms(2000);
  printf("Debug Mode Enabled\n");
#endif

  printf("Booting up\n");

  if (cyw43_arch_init_with_country(CYW43_COUNTRY(COUNTRY_CODE_0, COUNTRY_CODE_1, 0))) {
    add_repeating_timer_ms(2000, led_blink_timer, NULL, &timer);
    printf("WiFi init failed");
    return -1;
  }

  printf("WiFi init success (Hostname: %s)\n", CYW43_HOST_NAME);

  cyw43_arch_enable_sta_mode();
  add_repeating_timer_ms(500, led_blink_timer, NULL, &timer);

  printf("WiFi connecting to (%s, %s)\n", WIFI_SSID, WIFI_PASSWORD);

  while (connection_retries > 0) {
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, WIFI_AUTH, 8000)) {
      printf("WiFi connection failed (%u/10)\n\n", connection_retries);
      connection_retries--;
      sleep_ms(1000);
    } else {
      break;
    }
  }

  cancel_repeating_timer(&timer);
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

  if (connection_retries <= 0) {
    printf("WiFi connection failed\n\n");
    return -1;
  }

  printf("WiFi connect success\n");
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  start_tcp_server_module();

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
  cyw43_arch_deinit();
  return 0;
}