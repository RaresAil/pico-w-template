#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <optional>
#include <stdint.h>
#include <stdlib.h>
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

#define __HAS_LOOP

#define RELAY_IN_01 27
#define RELAY_IN_02 26

#define BUTTON_DOWN 0
#define BUTTON_UP 1

#define MS_TO_REACH_MAX_BOTTOM 10500.0
#define MS_TO_REACH_MAX_TOP 15500.0

class Desk {
  private:
    alarm_id_t moving_check_alarm;

    uint32_t *now_ptr;

    uint32_t _button_press_at = 0;
    int8_t _button_hold = -1;
    bool _ready = false;

    uint32_t stop_at_start = 0;
    bool stop_at_up = false;
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
      send_get_packet_to_all(this->get_data());
    }

    void button_reset() {
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

    void handle_ongoing_check() {
      if (this->moving_check_alarm <= 0) {
        return;
      }

      if (!alarm_pool_cancel_alarm(core_1_alarm_pool, this->moving_check_alarm)) {
        printf("[Desk] Failed to cancel check alarm\n");
        this->moving_check_alarm = 0;
        return;
      }

      printf("[Desk] Cancelled check alarm\n");

      this->moving_check_alarm = 0;

      const uint32_t now = to_ms_since_boot(get_absolute_time());
      const uint16_t diff = now - this->stop_at_start;
      double diff_percent = this->calculate_percent_diff(diff, this->stop_at_up);

      this->current_height = diff_percent;
    }

    static int64_t check_alarm_callback(alarm_id_t id, void *user_data) {
      Desk *desk = static_cast<Desk *>(user_data);
      desk->moving_check_alarm = 0;

      printf("[Desk] Alarm callback called\n");

      desk->button_reset();
      desk->current_height = desk->target_height;
      desk->send_get_packet();

      return 0;
    }

    void button_handler() {
      *this->now_ptr = to_ms_since_boot(get_absolute_time());

      if (gpio_get(BUTTON_UP) && !gpio_get(BUTTON_DOWN)) {
        this->handle_ongoing_check();

        if (this->_button_hold != BUTTON_UP) {
          this->_button_hold = BUTTON_UP;
          this->_button_press_at = *this->now_ptr;

          this->calculate_button_press_time(true, true);
          this->button_pressed(true, true);
        }
      }
      
      if (gpio_get(BUTTON_DOWN) && !gpio_get(BUTTON_UP)) {
        this->handle_ongoing_check();

        if (this->_button_hold != BUTTON_DOWN) {
          this->_button_hold = BUTTON_DOWN;
          this->_button_press_at = *this->now_ptr;

          this->calculate_button_press_time(false, true);
          this->button_pressed(false, true);
        }
      }

      if (
        !gpio_get(BUTTON_DOWN) && 
        !gpio_get(BUTTON_UP) && 
        this->_button_hold >= 0 &&
        (*this->now_ptr - this->_button_press_at) > 5
      ) {
        this->calculate_button_press_time(this->_button_hold == BUTTON_UP, false);

        this->_button_hold = -1;
        this->button_reset();
        this->send_get_packet();
      }
    }
  public:
    Desk() {
      printf("[Desk] Service starting\n");

      this->now_ptr = static_cast<uint32_t*>(malloc(sizeof(uint32_t)));
      assert(("now_ptr is not allocated!", this->now_ptr != NULL));

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
      if (!this->is_ready()) {
        return;
      }

      this->button_handler();
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
        this->handle_ongoing_check();
        uint8_t state = this->get_position_state();
        uint16_t diff = 0;

        if (state == 0) { // Down
          diff = static_cast<uint16_t>(((this->current_height - this->target_height) / 100.0) * MS_TO_REACH_MAX_BOTTOM);
          this->button_pressed(false, true);
        } else if (state == 1) { // UP
          diff = static_cast<uint16_t>(((this->target_height - this->current_height) / 100.0) * MS_TO_REACH_MAX_TOP);
          this->button_pressed(true, true);
        } else {
          this->button_reset();
          return;
        }

        this->stop_at_up = state == 1;
        this->moving_check_alarm = alarm_pool_add_alarm_in_ms(core_1_alarm_pool, diff, Desk::check_alarm_callback, this, true);
        this->stop_at_start = to_ms_since_boot(get_absolute_time());
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

#endif