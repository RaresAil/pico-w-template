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
      return;
    }

    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    const int client_index = index_of_tcp_client(state, client_id);
    std::shared_ptr<TCP_CLIENT_T> client = (state->clients[client_index]).second;

    const std::string s_type = parsed_data["type"].get<std::string>();
    const PACKET_TYPE type = packet_type_from_string(s_type);

    if (type == PACKET_TYPE::UNKNOWN) {
      return;
    }

    const u_int64_t now = get_datetime_ms();
    client->last_ping = now;

    uint packet_id = 0;
    if (parsed_data.contains("id")) {
      packet_id = parsed_data["id"].get<uint>();
    }

    json packet = {
      {"id", packet_id},
      {"client_id", client_id}
    };

    switch (type) {
      case PACKET_TYPE::PING: {
        packet["type"] = PACKET_TYPES(PACKET_TYPE::PING);
        tcp_server_send_data(arg, tpcb, packet.dump());
        return;
      }
      case PACKET_TYPE::INFO: {
        char country_code[2] = {COUNTRY_CODE_0, COUNTRY_CODE_1};
        packet["type"] = PACKET_TYPES(PACKET_TYPE::INFO);
        packet["data"] = {
          {"uptime", to_ms_since_boot(get_absolute_time()) / 1000},
          {"country_code", std::string(country_code, 2)},
          {"firmware_version", FIRMWARE_VERSION},
          {"serial_number", SERIAL_NUMBER},
          {"ssid", WIFI_SSID}
        };
        tcp_server_send_data(arg, tpcb, packet.dump());
        return;
      }
      default:
        break;
    }

    tcp_server_send_data(arg, tpcb, packet.dump());
  } catch (...) {
    printf("[Handler] Failed to parse data from %s\n", client_id.c_str());
  }
}

#endif