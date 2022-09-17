#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <optional>
#include <stdio.h>
#include <string>

#include "types.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __SERVICE_CPP__
#define __SERVICE_CPP__

class Desk {
  private:
    bool _ready = false;

    float target_height = 0;
    float current_height = 0;
  public:
    void update_network(const std::string &network) {
      // Do nothing because this service doesn't have a display
    }

    Desk() {
      printf("[Desk] Service starting\n");
    }

    void ready() {
      printf("[Desk] Service ready\n");
      this->_ready = true;
    }

    void loop() {
      if (!this->_ready) {
        return;
      }

      tight_loop_contents();
    }

    // Setters
    void set_target_height(const float &height) {
      this->target_height = height;
    }

    // Getters

    bool is_ready() {
      return this->_ready;
    }

    float get_target_height() {
      return this->target_height;
    }

    float get_current_height() {
      return this->current_height;
    }

    /** 
     * DECREASING = 0
     * INCREASING = 1
     * STOPPED = 2
    */
    uint8_t get_position_state() {
      if (this->current_height > this->target_height) {
        return 0;
      }
      
      if (this->current_height < this->target_height) {
        return 1;
      }

      return 2;
    }
};

Desk service = Desk();

json service_handle_packet(const json &body, const PACKET_TYPE &type) {
  if (!service.is_ready()) {
    return {};
  }

  switch (type) {
    case PACKET_TYPE::GET:
      break;
    case PACKET_TYPE::SET: {
      float target_height = body.value(
        "target_height", 
        service.get_target_height()
      );

      service.set_target_height(target_height);
      break;
    }
    default:
      return {};
  }

  return {
    {"target_height", service.get_target_height()},
    {"current_height", service.get_current_height()},
    {"position_state", service.get_position_state()}
  };
}

void service_main() {
  service.ready();

  while (true) {
    service.loop();
  }
}

#endif