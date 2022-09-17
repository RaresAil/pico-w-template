#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "./config.h"
#include "./server-utils.cpp"

#ifndef __SENDER_CPP__
#define __SENDER_CPP__

std::string create_error_packet(const std::string &client_id, const std::string &message) {
  json packet = {
    {"type", PACKET_TYPES(PACKET_TYPE::ERROR)},
    {"client_id", client_id},
    {"message", message}
  };

  return packet.dump();
}

std::string parse_data_to_be_sent(const std::string &data, const std::string &client_id) {
  std::string data_to_be_sent = data;
  if (USE_ENCRYPTION) {
    printf("[Sender] Encrypting packet for %s\n", client_id.c_str());
    data_to_be_sent = encrypt_256_aes_ctr(data);

    if (data_to_be_sent == "") {
      return "";
    }
  }

  int data_length = data_to_be_sent.size();
  return std::to_string(data_length) + std::string(";") + data_to_be_sent;
}

err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb, const std::string &data) {
  const std::string client_id = get_tcp_client_id(tpcb);
  const std::string data_to_be_sent = parse_data_to_be_sent(data, client_id);
  if (data_to_be_sent == "") {
    return ERR_VAL;
  }

  if (data_to_be_sent.size() > TCP_SERVER_BUF_SIZE) {
    printf("[Sender] Data too large to send\n");
    return ERR_VAL;
  }

  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

  const int client_index = index_of_tcp_client(state, client_id);

  if (client_index == -1) {
    printf("[Sender] Client %s not found\n", client_id.c_str());
    tcp_close_client(tpcb);
    return ERR_VAL;
  }

  std::shared_ptr<TCP_CLIENT_T> client = (state->clients[client_index]).second;

  std::fill_n(client->buffer_sent, TCP_SERVER_BUF_SIZE, 0);
  for(int i = 0; i < data_to_be_sent.size(); i++) {
    client->buffer_sent[i] = (uint8_t)data_to_be_sent[i];
  }

  printf("[Sender] Writing %ld bytes to client (%s)\n", data_to_be_sent.size(), client_id.c_str());

  cyw43_arch_lwip_check();
  err_t err = tcp_write(tpcb, client->buffer_sent, data_to_be_sent.size(), TCP_WRITE_FLAG_COPY);

  if (err != ERR_OK) {
    printf("[Sender] Failed to write data %d (%s)\n", err, client_id.c_str());
    return err;
  }

  return ERR_OK;
}

#endif