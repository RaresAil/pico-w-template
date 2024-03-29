#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "./server-utils.cpp"
#include "./sender.cpp"
#include "./types.cpp"

#ifndef INCLUDE_NLOHMANN_JSON_HPP_
#include "nlohmann/json.hpp"

using json = nlohmann::json;
#endif

#ifndef __HANDLER_CPP__
#define __HANDLER_CPP__

void handle_client_response(void *arg, struct tcp_pcb *tpcb, const std::string &data) {
  const std::string client_id = get_tcp_client_id(tpcb);

  try {
    json parsed_data = json::parse(data);
    if (!parsed_data.contains("type")) {
      printf("[Handler] Client %s sent invalid data: %s\n", client_id.c_str(), data.c_str());
      return;
    }

    TCP_SERVER_T *state = static_cast<TCP_SERVER_T*>(arg);
    const int client_index = index_of_tcp_client(state, client_id);

    if (!parsed_data["type"].is_string()) {
      printf("[Handler] Client %s sent invalid data: %s\n", client_id.c_str(), data.c_str());
      return;
    }

    const std::string s_type = parsed_data["type"].get<std::string>();
    const PACKET_TYPE type = packet_type_from_string(s_type);

    if (type == PACKET_TYPE::UNKNOWN) {
      printf("[Handler] Client %s sent invalid data: %s\n", client_id.c_str(), data.c_str());
      return;
    }

    const u_int64_t now = get_datetime_ms();
    (state->clients[client_index]).second->last_ping = now;

    std::string packet_id = "";
    if (parsed_data.contains("id") && parsed_data["id"].is_string()) {
      packet_id = parsed_data["id"].get<std::string>();
    }

    json packet = {
      {"id", packet_id},
      {"client_id", client_id},
      {"type", s_type}
    };

    switch (type) {
      case PACKET_TYPE::PING: {
        tcp_server_send_data(arg, tpcb, packet.dump());
        return;
      }
      case PACKET_TYPE::INFO: {
        printf("[Handler] Sending INFO Packet to %s\n", client_id.c_str());
        char country_code[2] = {COUNTRY_CODE_0, COUNTRY_CODE_1};
        packet["data"] = {
          {"watchdog_enable_reboot", watchdog_enable_caused_reboot()},
          {"uptime", to_ms_since_boot(get_absolute_time()) / 1000},
          {"country_code", std::string(country_code, 2)},
          {"watchdog_reboot", watchdog_caused_reboot()},
          {"firmware_version", FIRMWARE_VERSION},
          {"serial_number", __flash_uid_s},
          {"type", SERVICE_TYPE},
          {"ssid", WIFI_SSID}
        };
        printf("[Handler] INFO Packet prepared for %s\n", client_id.c_str());
        tcp_server_send_data(arg, tpcb, packet.dump());
        printf("[Handler] INFO Packet sent to %s\n", client_id.c_str());
        return;
      }
      default:
        break;
    }

    json body = {};
    if (parsed_data.contains("body")) {
      body = parsed_data["body"];
    }

    packet["data"] = service_handle_packet(body, type);
    tcp_server_send_data(arg, tpcb, packet.dump());
  } catch (...) {
    printf("[Handler] Failed to parse data from %s\n", client_id.c_str());

    try {
      tcp_server_send_data(arg, tpcb, create_error_packet(client_id, "Failed to parse data"));
    } catch (...) {}
  }
}

#endif