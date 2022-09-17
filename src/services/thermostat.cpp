#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <optional>
#include <stdio.h>
#include <string>

#include "types.cpp"
#include "extras/Display.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __SERVICE_CPP__
#define __SERVICE_CPP__

#define TRIGGER_INTERVAL_MS     300000
#define TEMPERATURE_CORRECTION  -6.0f

#define PLUS_TEMP_GPIO_PIN      17
#define MINUS_TEMP_GPIO_PIN     16
#define RELAY_GPIO_PIN          15

class Thermostat {
  private:
    bool _ready = false;
    // Trigger the update a second time for better accuracy
    bool alarm_triggered = true;
    bool button_pressed = false;

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

    /** Display **/
    Display display;
    std::string c_network = "";
    std::string c_s_temp = "";
    std::string c_heat = "";
    bool c_winter = false;
    int target_timeout = 5;
    bool show_target_temp = false;
    const unsigned char sun_icon[32] = {
      0x01, 0x80, 0x01, 0x80, 0x20, 0x04, 0x10, 0x08, 0x03, 0xc0, 0x06, 0x60, 0x0c, 0x30, 0xc8, 0x13, 
      0xc8, 0x13, 0x0c, 0x30, 0x06, 0x60, 0x03, 0xc0, 0x10, 0x08, 0x20, 0x04, 0x01, 0x80, 0x01, 0x80
    };
  
    void trigger_display_update(const bool &heating) {
      if (!this->_ready) {
        return;
      }

      mutex_enter_blocking(&this->m_t_display);

      const std::string s_temp_f = std::to_string(
        this->show_target_temp ? this->target_temperature : this->temperature
      );
      std::istringstream iss_temp(s_temp_f);

      std::string major_temp;
      std::string minor_temp;
      std::getline(iss_temp, major_temp, '.');
      std::getline(iss_temp, minor_temp, '.');

      minor_temp = minor_temp.substr(0, 1);

      const std::string s_temp = 
        major_temp + 
        std::string(".") + 
        minor_temp + 
        std::string(" C");

      const std::string heat = this->show_target_temp ? "Target Temp" : (heating  ? "HEAT" : "");

      if (
        this->c_network == this->display.get_network() &&
        this->c_s_temp == s_temp &&
        this->c_heat == heat &&
        this->c_winter == this->get_winter_mode()
      ) {
        mutex_exit(&this->m_t_display);
        return;
      }

      this->c_network = this->display.get_network();
      this->c_s_temp = s_temp;
      this->c_heat = heat;
      this->c_winter = this->get_winter_mode();

      this->display.update_display(
        s_temp,
        new uint8_t[2] { 29, 8 },
        heat,
        new uint8_t[2] { 0, 25 },
        this->show_target_temp ? false : !this->c_winter,
        this->sun_icon,
        new uint8_t[2] { 111, 15 }
      );
      mutex_exit(&this->m_t_display);
    }
    /** Display **/

    void update_temperature() {
      mutex_enter_blocking(&this->m_read_temp);
      printf("[Thermostat] Updating temperature\n");
      const float conversion_factor = 3.3f / (1 << 12);

      float adc = (float)adc_read() * conversion_factor;
      float temp = 27.0f - (adc - 0.706f) / 0.001721f;

      this->temperature = std::ceil((temp + TEMPERATURE_CORRECTION) * 10.0) / 10.0;
      printf("[Thermostat] Temperature: %f - %f\n", this->temperature, temp);
      mutex_exit(&this->m_read_temp);
    }

    static bool check(struct repeating_timer *rt) {
      try {
        Thermostat *instance = (Thermostat *)rt->user_data;

        if (instance->show_target_temp) {
          if (instance->target_timeout <= 0) {
            instance->show_target_temp = false;
          } else {
            instance->target_timeout--;
          }
        }

        const bool heating = instance->is_heating();
        gpio_put(RELAY_GPIO_PIN, heating);
        instance->trigger_display_update(heating);
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
      mutex_init(&this->m_read_temp);
      mutex_init(&this->m_heating);
      mutex_init(&this->m_t_display);

      gpio_init(PLUS_TEMP_GPIO_PIN);
      gpio_set_dir(PLUS_TEMP_GPIO_PIN, GPIO_IN);
      gpio_init(MINUS_TEMP_GPIO_PIN);
      gpio_set_dir(MINUS_TEMP_GPIO_PIN, GPIO_IN);

      gpio_init(RELAY_GPIO_PIN);
      gpio_set_dir(RELAY_GPIO_PIN, GPIO_OUT);
      gpio_put(RELAY_GPIO_PIN, 0);

      printf("[Thermostat] Service starting\n");
    }

    void update_network(const std::string& network) {
      this->display.update_network(network);
    }

    void center_message(const std::string& message, const bool &freeze = false) {
      this->display.center_message(message, freeze);
    }

    void ready() {
      this->display.setup();
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

      if (gpio_get(PLUS_TEMP_GPIO_PIN) && !gpio_get(MINUS_TEMP_GPIO_PIN)) {
        if (!this->button_pressed) {
          this->button_pressed = true;
          this->set_target_temperature(this->target_temperature + 0.5);
          this->target_timeout = 5;
          this->show_target_temp = true;
          this->trigger_display_update(false);

          sleep_ms(50);
        }
      } else if (!gpio_get(PLUS_TEMP_GPIO_PIN) && gpio_get(MINUS_TEMP_GPIO_PIN)) {
        if (!this->button_pressed) {
          this->button_pressed = true;
          this->set_target_temperature(this->target_temperature - 0.5);
          this->target_timeout = 5;
          this->show_target_temp = true;
          this->trigger_display_update(false);

          sleep_ms(50);
        }
      } else {
        this->button_pressed = false;
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

Thermostat service = Thermostat();

json service_handle_packet(const json &body, const PACKET_TYPE &type) {
  if (!service.is_ready()) {
    return {};
  }

  switch (type) {
    case PACKET_TYPE::GET:
      break;
    case PACKET_TYPE::SET: {
      double target_temperature = body.value(
        "target_temperature", 
        service.get_target_temperature()
      );
      bool celsius = body.value(
        "celsius",
        service.get_is_celsius()
      );
      bool winter = body.value(
        "winter",
        service.get_winter_mode()
      );

      service.set_target_temperature(target_temperature);
      service.set_is_celsius(celsius);
      service.set_winter_mode(winter);
      break;
    }
    default:
      return {};
  }

  return {
    {"target_temperature", service.get_target_temperature()},
    {"temperature", service.get_temperature()},
    {"celsius", service.get_is_celsius()},
    {"winter", service.get_winter_mode()},
    {"humidity", service.get_humidity()},
    {"heating", service.is_heating()}
  };
}

void service_main() {
  service.ready();

  while (true) {
    service.loop();
  }
}

#endif