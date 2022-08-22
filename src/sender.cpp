#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "./config.h"
#include "./server-utils.cpp"

#ifndef __SENDER_CPP__
#define __SENDER_CPP__

std::string parse_data_to_be_sent(const std::string &data) {
  int data_length = data.size();
  return std::to_string(data_length) + std::string(";") + data;
}

err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb, const std::string &data) {
  const std::string data_to_be_sent = parse_data_to_be_sent(data);

  if (data_to_be_sent.size() > TCP_SERVER_BUF_SIZE) {
    printf("[Server] Data too large to send\n");
    return ERR_VAL;
  }

  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

  const std::string client_id = get_tcp_client_id(tpcb);
  const int client_index = index_of_tcp_client(state, client_id);

  if (client_index == -1) {
    printf("[Server] Client %s not found\n", client_id.c_str());
    tcp_close_client(tpcb);
    return ERR_VAL;
  }

  std::shared_ptr<TCP_CLIENT_T> client = (state->clients[client_index]).second;

  std::fill_n(client->buffer_sent, TCP_SERVER_BUF_SIZE, 0);
  for(int i = 0; i < data_to_be_sent.size(); i++) {
    client->buffer_sent[i] = (uint8_t)data_to_be_sent[i];
  }

  printf("[Server] Writing %ld bytes to client (%s)\n", data_to_be_sent.size(), client_id.c_str());

  cyw43_arch_lwip_check();
  err_t err = tcp_write(tpcb, client->buffer_sent, data_to_be_sent.size(), TCP_WRITE_FLAG_COPY);

  if (err != ERR_OK) {
    printf("[Server] Failed to write data %d (%s)\n", err, client_id.c_str());
    return err;
  }

  return ERR_OK;
}

#endif