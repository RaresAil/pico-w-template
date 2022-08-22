### General

The current features of this template. For the defaults of the features check the `Config` section

- Set a specific number of simultaneous clients.
- Max packet size in bytes.
- Set TCP port.
- Set the Wifi SSID and Password at compile time
- JSON format for packet's data

The `server.cpp` could stay untouched, for parsing the data you can just check `handler.cpp`, after the request is processed the data is sent to the handler along with the client from which you can respond back.

You can keep the connection alive in order to have real time responses

### Packets

The packet format is `number_of_characters;data` e.g. `4;demo`

### Config file

- Path: `src/config.h`
- Code:

  ```h
  #ifndef __CONFIG_H__
  #define __CONFIG_H__

  #include "pico/cyw43_arch.h"

  #define COUNTRY_CODE_0                  'U'
  #define COUNTRY_CODE_1                  'S'

  #define WIFI_AUTH                       CYW43_AUTH_WPA2_AES_PSK
  #define WIFI_PASSWORD                   "PASSWORD"
  #define WIFI_SSID                       "SSID"

  // TCP SERVER

  #define TCP_SERVER_PORT                 8098
  #define TCP_SERVER_BUF_SIZE             2048
  #define TCP_SERVER_POLL_TIME_S          5
  #define TCP_SERVER_MAX_CLIENTS          5

  #endif
  ```
