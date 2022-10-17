/**
 *  The file was originally written by
 * 
 *  Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *  SPDX-License-Identifier: BSD-3-Clause
 *  
 *  And modifed by me to fit the needs of the project.
 * 
 * 
 *  Original file: https://github.com/raspberrypi/pico-examples/blob/master/pico_w/tcp_server/picow_tcp_server.c
 **/

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

#include "./server-utils.cpp"
#include "./handler.cpp"
#include "./flash.cpp"

#ifndef __SERVER_CPP__
#define __SERVER_CPP__

static TCP_SERVER_T* tcp_server_init(void) {
  TCP_SERVER_T *state = (TCP_SERVER_T*)calloc(1, sizeof(TCP_SERVER_T));

  if (!state) {
    printf("[Server] Failed to allocate state\n");
    return NULL;
  }

  return state;
}

static int first_empty_client_slot(TCP_SERVER_T *state) {
  for (int i = 0; i < TCP_SERVER_MAX_CLIENTS; i++) {
    if (state->clients[i].first == "") {
      return i;
    }
  }

  return -1;
}

static err_t tcp_close_client_by_index(TCP_SERVER_T *state, const int &index) {
  const err_t err = tcp_close_client(state->clients[index].second->client_pcb);
  state->clients[index].second.reset();
  state->clients[index].first = "";

  return err;
}

static void close_all_tcp_clients(TCP_SERVER_T *state) {
  for (int i = 0; i < TCP_SERVER_MAX_CLIENTS; i++) {
    if (state->clients[i].first != "") {
      tcp_close_client_by_index(state, i);
    }
  }
}

static err_t tcp_server_close(void *arg) {
  printf("[Server] Closing the server\n");

  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
  state->opened = false;
  close_all_tcp_clients(state);

  if (state->server_pcb) {
    tcp_arg(state->server_pcb, NULL);
    tcp_close(state->server_pcb);
    state->server_pcb = NULL;
  }

  return ERR_OK;
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
  printf("[Server] %u bytes sent to client %s\n", len, get_tcp_client_id(tpcb).c_str());
  return ERR_OK;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
  try {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    const std::string client_id = get_tcp_client_id(tpcb);

    if (err != 0) {
      printf("[Server] Receiver error %d (%s)\n", err, client_id.c_str());
    }

    const int client_index = index_of_tcp_client(state, client_id);

    if (client_index == -1) {
      printf("[Server] Client %s not found\n", client_id.c_str());
      tcp_close_client(tpcb);
      pbuf_free(p);
      return ERR_VAL;
    }

    if (!p) {
      tcp_close_client_by_index(state, client_index);
      pbuf_free(p);
      return ERR_VAL;
    }

    std::shared_ptr<TCP_CLIENT_T> client = (state->clients[client_index]).second;

    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
      const u_int64_t now = get_datetime_ms();
      if (now - client->last_packet_tt > 1500) {
        client->packet_len = -1;
      }

      client->last_packet_tt = now;

      if (client->packet_len == -1) {
        client->recv_len = 0;
      }

      printf("[Server] Received %d bytes (%d are from previous packets) from (%s)\n", p->tot_len, client->recv_len, client_id.c_str());

      const uint16_t buffer_left = TCP_SERVER_BUF_SIZE - client->recv_len;
      client->recv_len += pbuf_copy_partial(
        p, client->buffer_recv + client->recv_len,
        p->tot_len > buffer_left ? buffer_left : p->tot_len, 0\
      );

      tcp_recved(tpcb, p->tot_len);
    }

    if (client->packet_len == -1) {
      std::string partial_packet((char*)client->buffer_recv, client->recv_len);
      std::string packet_length_s;

      std::istringstream iss_input(partial_packet);
      std::getline(iss_input, packet_length_s, ';');

      if (packet_length_s != partial_packet) {
        int packet_length = std::stoi(packet_length_s);
        if (packet_length > 0) {
          client->packet_len = packet_length + packet_length_s.size() + 1;
          client->data_len = packet_length_s.size() + 1;
        }
      }
    }
    
    if (client->recv_len >= client->packet_len) {
      std::string packet((char*)client->buffer_recv, client->data_len, client->packet_len - client->data_len);

#ifdef AES_ENCRYPTION_KEY
      printf("[Server] Decrypting packet from %s\n", client_id.c_str());
      packet = decrypt_256_aes_ctr(packet);
      printf("[Server] Packet decrypted from %s (%s)\n", client_id.c_str(), packet.c_str());
#endif

      if (packet != "") {
        handle_client_response(arg, tpcb, packet);
      }

      client->packet_len = -1;
      client->recv_len = 0;
    }

    pbuf_free(p);
    return ERR_OK;
  } catch (const std::exception &e) {
    printf("[Server] Exception: %s\n", e.what());
    pbuf_free(p);
    return ERR_VAL;
  }
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb) {
  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
  const std::string client_id = get_tcp_client_id(tpcb);
  const int client_index = index_of_tcp_client(state, client_id);

  try {
    std::shared_ptr<TCP_CLIENT_T> client = (state->clients[client_index]).second;
    const u_int64_t now = get_datetime_ms();
    const u_int64_t diff = (now - client->last_ping) / 1000;
    if (diff > TCP_SERVER_INACTIVE_TIME_S) {
      printf("[Server] Client %s is inactive for %s seconds\n", client_id.c_str(), std::to_string(diff).c_str());
      tcp_close_client_by_index(state, client_index);
    }
  } catch (...) {
    printf("[Server] Poll error for %s\n", client_id.c_str());
    tcp_close_client_by_index(state, client_index);
  }

  return ERR_OK;
}

static void tcp_server_err(void *arg, err_t err) {
  if (err != ERR_ABRT) {
    printf("[Server] Client thrown error (%d)\n", err);
  } else {
    printf("[Server] Client aborted error\n");
  }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

  try {
    if (err != ERR_OK || client_pcb == NULL) {
      printf("[Server] Failure in accept\n");
      return ERR_VAL;
    }

    const std::string client_id = get_tcp_client_id(client_pcb);

    if (index_of_tcp_client(state, client_id) >= 0) {
      printf("[Server] Client already connected (%s)\n", client_id.c_str());
      return ERR_OK;
    }

    const int empty_index = first_empty_client_slot(state);
    if (empty_index < 0) {
      printf("[Server] No empty client slot\n");
      tcp_close_client(client_pcb);
      return ERR_ABRT;
    }

    printf("[Server] Client connected (%s) on (%d)\n", client_id.c_str(), empty_index);

    std::shared_ptr<TCP_CLIENT_T> client = std::make_shared<TCP_CLIENT_T>();
    state->clients[empty_index] = std::make_pair(client_id, client);

    const u_int64_t now = get_datetime_ms();

    client->client_pcb = client_pcb;
    client->last_ping = now;
    tcp_arg(client_pcb, state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, TCP_SERVER_POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
  } catch (...) {
    const std::string client_id = get_tcp_client_id(client_pcb);
    printf("[Server] Exception in accept (%s)\n", client_id.c_str());

    tcp_close_client_by_index(state, index_of_tcp_client(state, client_id));
    return ERR_ABRT;
  }
}

static bool tcp_server_open(void *arg) {
  TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
  printf("[Server] Starting (%s:%u)\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_SERVER_PORT);

  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    printf("[Server] Failed to create pcb\n");
    return false;
  }

  err_t err = tcp_bind(pcb, NULL, TCP_SERVER_PORT);
  if (err) {
    printf("[Server] Failed to bind to port %d\n");
    return false;
  }

  state->server_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!state->server_pcb) {
    printf("[Server] Failed to listen\n");
    if (pcb) {
      tcp_close(pcb);
    }

    return false;
  }

  printf("[Server] Successfully started\n");
  state->opened = true;

  tcp_arg(state->server_pcb, state);
  tcp_accept(state->server_pcb, tcp_server_accept);

  return true;
}

uint32_t last_wifi_check = 0;

void start_tcp_server_module() {
  TCP_SERVER_T *tcp_server_state = tcp_server_init();
  if (!tcp_server_state) {
    return;
  }

  if (!tcp_server_open(tcp_server_state)) {
    return;
  }

  while(tcp_server_state->opened) {
    flash_main_loop();
    sender_main_loop(tcp_server_state);

    const uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - last_wifi_check > 10000) {
      last_wifi_check = now;

      const int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
      switch(status) {
        case CYW43_LINK_DOWN:
        case CYW43_LINK_FAIL:
        case CYW43_LINK_NONET:
          printf("[Wifi-Check] WiFi down\n");
          cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, WIFI_AUTH);
          break;
        case CYW43_LINK_BADAUTH:
          printf("[Wifi-Check] WiFi bad auth\n");
          break;
        case CYW43_LINK_JOIN:
          printf("[Wifi-Check] WiFi join\n");
          break;
        case CYW43_LINK_NOIP:
          printf("[Wifi-Check] WiFi no IP\n");
          break;
      }
    }

#if PICO_CYW43_ARCH_POLL
    cyw43_arch_poll();
    sleep_ms(1);
#else
    tight_loop_contents();
#endif
  }

  printf("[Server] Closed\n");
  free(tcp_server_state);
}

#endif