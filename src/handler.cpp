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

    switch (type) {
      case PACKET_TYPE::UNKNOWN:
        return;
      case PACKET_TYPE::PING: {
        const u_int64_t now = get_datetime_ms();
        client->last_ping = now;

        json ping_packet = {
          {"id", 0},
          {"type", PACKET_TYPES(PACKET_TYPE::PING)},
          {"client_id", client_id}
        };

        tcp_server_send_data(arg, tpcb, ping_packet.dump());
        return;
      }
      default:
        const u_int64_t now = get_datetime_ms();
        client->last_ping = now;
        break;
    }
    
    printf("[Handler] JSON data: (%s)\n", parsed_data.dump().c_str());

    tcp_server_send_data(arg, tpcb, parsed_data.dump());
  } catch (...) {
    printf("[Handler] Failed to parse data from %s\n", client_id.c_str());
  }
}

#endif