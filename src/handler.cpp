#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "./server-utils.cpp"
#include "./sender.cpp"

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
    printf("[Handler] JSON data: (%s)\n", parsed_data.dump().c_str());

    tcp_server_send_data(arg, tpcb, parsed_data.dump());
  } catch (...) {
    printf("[Handler] Failed to parse data from %s\n", client_id.c_str());
  }
}

#endif