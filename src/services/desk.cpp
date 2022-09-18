#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <optional>
#include <stdio.h>
#include <string>

#include "types.cpp"
#include "server-utils.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __SERVICE_CPP__
#define __SERVICE_CPP__

#define RELAY_IN_01 17
#define RELAY_IN_02 16

#define BUTTON_DOWN 14
#define BUTTON_UP 15

#define MS_TO_REACH_MAX 10 * 1000 // Seconds to reach max

class Desk {
  private:
    struct repeating_timer timer;
    uint8_t _button_hold = 0;
    bool _ready = false;

    uint32_t stop_at = 0;
    uint32_t hold_at = 0;

    /**
     * The value is in microseconds to that position
     */
    uint16_t target_height = 0;
    /**
     * The value is in microseconds to that position
     */
    uint16_t current_height = 0;

    void button_reset() {
      this->stop_at = 0;
      gpio_put(RELAY_IN_01, 1);
      gpio_put(RELAY_IN_02, 1);
    }

    void button_pressed(bool up, bool hold) {
      if (up) {
        gpio_put(RELAY_IN_01, hold ? 0 : 1);
        gpio_put(RELAY_IN_02, 1);
        return;
      }

      gpio_put(RELAY_IN_01, 1);
      gpio_put(RELAY_IN_02, hold ? 0 : 1);
    }

    void calculate_button_press_time(const bool up) {
      if (this->hold_at == 0) {
        this->hold_at = to_ms_since_boot(get_absolute_time());
        return;
      }

      const uint32_t now = to_ms_since_boot(get_absolute_time());
      uint16_t diff = 0;

      if (up) {
        diff = (now - this->hold_at) + this->current_height;
      } else {
        if (this->current_height < (now - this->hold_at)) {
          diff = 0;
        } else {
          diff = this->current_height - (now - this->hold_at);
        }
      }

      this->hold_at = 0;
      this->set_target_height(diff);
      this->current_height = this->target_height;
    }

    void check() {
      if (this->stop_at == 0) {
        return;
      }

      const uint32_t now = to_ms_since_boot(get_absolute_time());
      if (now >= this->stop_at) {
        const uint16_t diff = now - this->stop_at;
        this->button_reset();
        this->set_target_height(this->target_height + diff);
        this->current_height = this->target_height;
      }
    }
  public:
    void update_network(const std::string &network) {
      // Do nothing because this service doesn't have a display
    }

    Desk() {
      printf("[Desk] Service starting\n");

      gpio_init(BUTTON_DOWN);
      gpio_set_dir(BUTTON_DOWN, GPIO_IN);
      gpio_init(BUTTON_UP);
      gpio_set_dir(BUTTON_UP, GPIO_IN);

      gpio_init(RELAY_IN_01);
      gpio_init(RELAY_IN_02);
      gpio_set_dir(RELAY_IN_01, GPIO_OUT);
      gpio_set_dir(RELAY_IN_02, GPIO_OUT);
      gpio_put(RELAY_IN_01, 1);
      gpio_put(RELAY_IN_02, 1);
    }

    void ready() {
      printf("[Desk] Service ready\n");
      this->_ready = true;
    }

    void loop() {
      try {
        if (!this->_ready) {
          return;
        }

        if (gpio_get(BUTTON_UP)) {
          if (this->_button_hold != BUTTON_UP) {
            this->_button_hold = BUTTON_UP;
            this->stop_at = 0;

            this->calculate_button_press_time(true);

            this->button_pressed(true, true);
          }
        } else if (gpio_get(BUTTON_DOWN)) {
          if (this->_button_hold != BUTTON_DOWN) {
            this->_button_hold = BUTTON_DOWN;
            this->stop_at = 0;

            this->calculate_button_press_time(false);
            this->button_pressed(false, true);
          }
        } else if (this->_button_hold != 0) {
          this->calculate_button_press_time(this->_button_hold == BUTTON_UP);

          this->_button_hold = 0;
          this->button_reset();
        }

        this->check();
      } catch (...) { }
    }

    // Setters
    void set_target_height(const uint16_t &height, const bool &set_movement = false) {
      if (height < 0) {
        this->target_height = 0;
      } else if (height > MS_TO_REACH_MAX) {
        this->target_height = MS_TO_REACH_MAX;
      } else {
        this->target_height = height;
      }

      if (set_movement) {
        uint8_t state = this->get_position_state();
        if (state == 0) {
          this->button_pressed(false, true);
        } else if (state == 1) {
          this->button_pressed(true, true);
        } else {
          this->button_reset();
          return;
        }

        uint16_t diff = 0;
        if (this->current_height < this->target_height) {
          diff = this->target_height - this->current_height;
        } else {
          diff = this->current_height - this->target_height;
        }

        const uint32_t now = to_ms_since_boot(get_absolute_time());
        this->stop_at = now + diff;
      }
    }

    // Getters

    bool is_ready() {
      return this->_ready;
    }

    uint16_t get_target_height() {
      return this->target_height;
    }

    uint16_t get_current_height() {
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
  try {
    if (!service.is_ready()) {
      return {};
    }

    switch (type) {
      case PACKET_TYPE::GET:
        break;
      case PACKET_TYPE::SET: {
        uint16_t target_height = body.value(
          "target_height", 
          service.get_target_height()
        );

        service.set_target_height(target_height, true);
        break;
      }
      default:
        return {};
    }

    return {
      {"position_state", service.get_position_state()},
      {"current_height", service.get_current_height()},
      {"target_height", service.get_target_height()}
    };
  } catch (...) {
    return {};
  }
}

void service_main() {
  service.ready();

  while (true) {
    service.loop();
  }
}

#endif