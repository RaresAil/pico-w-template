#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <optional>
#include <stdio.h>
#include <string>

#include "types.cpp"
#include "server-utils.cpp"
#include "sender.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __SERVICE_CPP__
#define __SERVICE_CPP__

#define RELAY_IN_01 27
#define RELAY_IN_02 26

#define BUTTON_DOWN 0
#define BUTTON_UP 1

#define MS_TO_REACH_MAX_BOTTOM 10000.0
#define MS_TO_REACH_MAX_TOP 15000.0

class Desk {
  private:
    struct repeating_timer timer;
    uint32_t _button_press_at = 0;
    int8_t _button_hold = -1;
    bool _ready = false;

    bool stop_at_up = false;
    uint32_t stop_at = 0;
    uint32_t hold_at = 0;

    /**
     * The value is in % to that position
     */
    double target_height = 0;
    /**
     * The value is in % to that position
     */
    double current_height = 0;

    void send_get_packet() {
      send_get_packet_to_all(tcp_server_state, this->get_data());
    }

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

    void calculate_button_press_time(const bool up, const bool hold) {
      if (hold) {
        this->hold_at = to_ms_since_boot(get_absolute_time());
        return;
      }

      const uint32_t now = to_ms_since_boot(get_absolute_time());
      uint16_t diff = now - this->hold_at;
      double diff_percent = this->calculate_percent_diff(diff, up);

      this->hold_at = 0;
      this->set_target_height(diff_percent);
      this->current_height = this->target_height;

      printf("DIF height: D: %u - DP: %f - CH: %f (UP : %d)\n", diff, diff_percent, current_height, up);
    }

    double calculate_percent_diff(const uint16_t diff, const bool up) {
      double double_diff = static_cast<double>(diff);
      double diff_percent = 0;

      if (up) {
        diff_percent = ((double_diff / MS_TO_REACH_MAX_TOP) * 100.0) + this->current_height;
      } else {
        diff_percent = this->current_height - ((double_diff / MS_TO_REACH_MAX_BOTTOM) * 100.0);
      }

      return diff_percent;
    }

    void check() {
      if (this->stop_at == 0) {
        return;
      }

      const uint32_t now = to_ms_since_boot(get_absolute_time());
      if (now >= this->stop_at) {
        this->button_reset();
        this->current_height = this->target_height;
        this->send_get_packet();
      }
    }

    void complete_was_moving() {
      const uint32_t now = to_ms_since_boot(get_absolute_time());
      if (this->stop_at == 0 || now >= this->stop_at) {
        return;
      }

      const uint16_t diff = this->stop_at - now;
      double diff_percent = 100 - this->calculate_percent_diff(diff, this->stop_at_up);
      this->set_target_height(diff_percent);
      this->current_height = this->target_height;
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

        const uint32_t now = to_ms_since_boot(get_absolute_time());

        if (gpio_get(BUTTON_UP) && !gpio_get(BUTTON_DOWN)) {
          this->complete_was_moving();
          this->stop_at = 0;

          if (this->_button_hold != BUTTON_UP) {
            this->_button_hold = BUTTON_UP;
            this->_button_press_at = now;

            this->calculate_button_press_time(true, true);
            this->button_pressed(true, true);
          }
        }
        
        if (gpio_get(BUTTON_DOWN) && !gpio_get(BUTTON_UP)) {
          this->complete_was_moving();
          this->stop_at = 0;

          if (this->_button_hold != BUTTON_DOWN) {
            this->_button_hold = BUTTON_DOWN;
            this->_button_press_at = now;

            this->calculate_button_press_time(false, true);
            this->button_pressed(false, true);
          }
        }

        if (
          !gpio_get(BUTTON_DOWN) && 
          !gpio_get(BUTTON_UP) && 
          this->_button_hold >= 0 &&
          (now - this->_button_press_at) > 5
        ) {
          this->calculate_button_press_time(this->_button_hold == BUTTON_UP, false);

          this->_button_hold = -1;
          this->button_reset();
          this->send_get_packet();
        }

        this->check();
      } catch (...) { }
    }

    // Setters
    void set_target_height(const double &height, const bool &set_movement = false) {
      if (set_movement && this->_button_hold >= 0) {
        return;
      }

      if (height < 0) {
        this->target_height = 0;
      } else if (height > 100) {
        this->target_height = 100;
      } else {
        this->target_height = height;
      }

      if (set_movement) {
        complete_was_moving();
        this->stop_at = 0;
        uint8_t state = this->get_position_state();
        uint16_t diff = 0;

        if (state == 0) { // Down
          diff = static_cast<uint16_t>((this->current_height - this->target_height) / 100.0 * MS_TO_REACH_MAX_BOTTOM);
          this->button_pressed(false, true);
        } else if (state == 1) { // UP
          diff = static_cast<uint16_t>((this->target_height - this->current_height) / 100.0 * MS_TO_REACH_MAX_TOP);
          this->button_pressed(true, true);
        } else {
          this->button_reset();
          return;
        }

        this->stop_at_up = state == 1;

        const uint32_t now = to_ms_since_boot(get_absolute_time());
        this->stop_at = now + diff;
      }
    }

    // Getters

    bool is_ready() {
      return this->_ready;
    }

    double get_target_height() {
      return this->target_height;
    }

    double get_current_height() {
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

    json get_data() {
      return {
        {"position_state", this->get_position_state()},
        {"current_height", this->get_current_height()},
        {"target_height", this->get_target_height()}
      };
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
        double target_height = body.value(
          "target_height", 
          service.get_target_height()
        );

        service.set_target_height(target_height, true);
        break;
      }
      default:
        return {};
    }

    return service.get_data();
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