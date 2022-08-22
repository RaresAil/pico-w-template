#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <memory>
#include <string>

#include "./config.h"

#ifndef __SERVER_UTILS_CPP__
#define __SERVER_UTILS_CPP__

typedef struct TCP_CLIENT_T_ {
  uint8_t buffer_sent[TCP_SERVER_BUF_SIZE];
  uint8_t buffer_recv[TCP_SERVER_BUF_SIZE];
  uint32_t last_packet_tt = 0;
  struct tcp_pcb *client_pcb;
  int packet_len = -1;
  int data_len = 0;
  int recv_len;
} TCP_CLIENT_T;

typedef struct TCP_SERVER_T_ {
  std::pair<std::string, std::shared_ptr<TCP_CLIENT_T>> clients[TCP_SERVER_MAX_CLIENTS];
  struct tcp_pcb *server_pcb;
  bool opened;
} TCP_SERVER_T;

static std::string get_tcp_client_id(struct tcp_pcb *client) {
  return std::string(ip4addr_ntoa(&client->remote_ip)) + ":" + std::to_string(client->remote_port);
}

static int index_of_tcp_client(TCP_SERVER_T *state, const std::string &id) {
  for (int i = 0; i < TCP_SERVER_MAX_CLIENTS; i++) {
    if (state->clients[i].first == id) {
      return i;
    }
  }

  return -1;
}

static err_t tcp_close_client(struct tcp_pcb *tpcb) {
  err_t err = ERR_OK;

  if (tpcb == NULL) {
    return err;
  }

  printf("[Server] Closing connection for %s\n", get_tcp_client_id(tpcb).c_str());

  tcp_arg(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_err(tpcb, NULL);
  err = tcp_close(tpcb);

  if (err != ERR_OK) {
    printf("[Server] Close failed %d, calling abort\n", err);
    tcp_abort(tpcb);
    err = ERR_ABRT;
  }

  return err;
}

#endif