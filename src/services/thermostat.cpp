#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string>

#include "./types.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __SERVICE_CPP__
#define __SERVICE_CPP__

json service_handle_packet(const json &body, const PACKET_TYPE &type) {
  json data = {};
  return data;
}

void service_sio_irq() {
  uint32_t data;
  while (multicore_fifo_rvalid()) {
    data = multicore_fifo_pop_blocking();
    printf("[Service][1] IRQ: %d\n", data);
  }

  printf("[Service][1] IRQ done\n");

  multicore_fifo_clear_irq();
}

void service_main() {
  printf("[Service][1] Service started\n");
  while (true) {
    tight_loop_contents();
  }
}

#endif