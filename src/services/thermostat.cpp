#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <optional>
#include <stdio.h>
#include <string>

#include "./types.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __SERVICE_CPP__
#define __SERVICE_CPP__

#define TRIGGER_INTERVAL_MS     300000

class Thermostat {
  private:
    bool _ready = false;
    bool alarm_triggered = true;

    struct repeating_timer timer;

    mutex_t m_read_temp;
    mutex_t m_heating;
    mutex_t m_t_display;

    bool prev_heating = false;

    double target_temperature = 10;
    bool winter_mode = false;
    double temperature = 0;
    bool is_celsius = true;
    int humidity = 0;

    void update_temperature() {
      printf("[Thermostat] Updating temperature\n");
    }

    static bool check(struct repeating_timer *rt) {
      try {
        Thermostat *instance = (Thermostat *)rt->user_data;

        // if (instance->show_target_temp) {
        //   if (instance->target_timeout <= 0) {
        //     instance->show_target_temp = false;
        //   } else {
        //     instance->target_timeout--;
        //   }
        // }

        const bool heating = instance->is_heating();
        // gpio_put(RELAY_GPIO_PIN, heating);
        // instance->trigger_display_update(heating);
      } catch (...) {
        printf("[Thermostat]:[ERROR]: While checking the heating mode\n");
      }

      return true;
    }

    static int64_t alarm_callback(alarm_id_t id, void *user_data) {
      Thermostat *instance = (Thermostat *)user_data;
      instance->alarm_triggered = true;

      return TRIGGER_INTERVAL_MS * 1000;
    }
  public:
    Thermostat() {
      mutex_init(&m_read_temp);
      mutex_init(&m_heating);
      mutex_init(&m_t_display);

      printf("[Thermostat] Service starting\n");
    }

    void ready() {
      this->update_temperature();

      add_repeating_timer_ms(1000, Thermostat::check, this, &this->timer);
      add_alarm_in_ms(TRIGGER_INTERVAL_MS, alarm_callback, this, false);

      _ready = true;
      printf("[Thermostat][Core-1] Service ready\n");
    }

    void loop() {
      if (!_ready) {
        return;
      }

      if (this->alarm_triggered) {
        this->alarm_triggered = false;
        this->update_temperature();
      }
    }

    // SETTERS

    void set_target_temperature(double t) {
      if (t < 10) {
        t = 10;
      } else if (t > 38) {
        t = 38;
      }

      this->target_temperature = t;
    }

    /*
     * @param winter false disables winter mode
     */
    void set_winter_mode(bool winter) {
      this->winter_mode = winter;
    }

    /*
     * @param unit true if celsius, false if fahrenheit
     */
    void set_is_celsius(bool celsius) {
      this->is_celsius = celsius;
    }

    // GETTERS
    bool is_ready() {
      return this->_ready;
    }

    double get_target_temperature() {
      return this->target_temperature;
    }

    double get_temperature() {
      return this->temperature;
    }

    int get_humidity() {
      return this->humidity;
    }

    bool get_is_celsius() {
      return this->is_celsius;
    }

    bool get_winter_mode() {
      return this->winter_mode;
    }

    bool is_heating() {
      mutex_enter_blocking(&this->m_heating);

      if (!this->get_winter_mode()) {
        this->prev_heating = false;
      } else {
        if (!this->prev_heating) {
          if (this->temperature < this->target_temperature) {
            this->prev_heating = true;
          }
        } else {
          if (this->temperature >= (this->target_temperature + 1)) {
            this->prev_heating = false;
          }
        }
      }

      mutex_exit(&this->m_heating);
      return this->prev_heating;
    }
};

Thermostat thermostat = Thermostat();

json service_handle_packet(const json &body, const PACKET_TYPE &type) {
  if (!thermostat.is_ready()) {
    return {};
  }

  switch (type) {
    case PACKET_TYPE::GET:
      break;
    case PACKET_TYPE::SET: {
      double target_temperature = body.value(
        "target_temperature", 
        thermostat.get_target_temperature()
      );
      bool celsius = body.value(
        "celsius",
        thermostat.get_is_celsius()
      );
      bool winter = body.value(
        "winter",
        thermostat.get_winter_mode()
      );

      thermostat.set_target_temperature(target_temperature);
      thermostat.set_is_celsius(celsius);
      thermostat.set_winter_mode(winter);
      break;
    }
    default:
      return {};
  }

  return {
    {"target_temperature", thermostat.get_target_temperature()},
    {"temperature", thermostat.get_temperature()},
    {"celsius", thermostat.get_is_celsius()},
    {"winter", thermostat.get_winter_mode()},
    {"humidity", thermostat.get_humidity()},
    {"heating", thermostat.is_heating()}
  };
}

void service_main() {
  thermostat.ready();

  while (true) {
    thermostat.loop();
  }
}

#endif