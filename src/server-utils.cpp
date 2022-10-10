#include "pico/util/datetime.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <functional>
#include <algorithm>
#include <stdlib.h>
#include <climits>
#include <stdio.h>
#include <sstream>
#include <vector>
#include <random>
#include <time.h>
#include <memory>
#include <ctime>
#include <string>

#include "./config.h"

#ifndef __SERVER_UTILS_CPP__
#define __SERVER_UTILS_CPP__

#include "Crypto/BlockCipher.cpp"
#include "Crypto/AESCommon.cpp"
#include "Crypto/Crypto.cpp"
#include "Crypto/AES256.cpp"
#include "Crypto/Cipher.cpp"
#include "Crypto/CTR.cpp"


#include "cpp-base64/base64.cpp"

typedef struct TCP_CLIENT_T_ {
  uint8_t buffer_sent[TCP_SERVER_BUF_SIZE];
  uint8_t buffer_recv[TCP_SERVER_BUF_SIZE];
  u_int64_t last_packet_tt = 0;
  u_int64_t last_ping = 0;
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

TCP_SERVER_T *tcp_server_state;

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

u_int64_t get_datetime_ms() {
  std::tm epoch_start;
  epoch_start.tm_sec = 0;
  epoch_start.tm_min = 0;
  epoch_start.tm_hour = 0;
  epoch_start.tm_mday = 1;
  epoch_start.tm_mon = 0;
  epoch_start.tm_year = 1970 - 1900;

  std::time_t basetime = std::mktime(&epoch_start);

  datetime_t dt;
  rtc_get_datetime(&dt);

  std::tm now = {};
  now.tm_year = dt.year - 1900;
  now.tm_mon = dt.month - 1;
  now.tm_mday = dt.day;
  now.tm_hour = dt.hour;
  now.tm_min = dt.min;
  now.tm_sec = dt.sec;

  const u_int64_t ms = std::difftime(std::mktime(&now), basetime);
  return ms * 1000;
}

/* #region Encryption */

u_int8_t* randomBytes(u_int8_t size) {
  try {
    static std::default_random_engine randomEngine(get_datetime_ms());
    static std::uniform_int_distribution<u_int8_t> uniformDist(CHAR_MIN, CHAR_MAX);

    std::vector<u_int8_t> data(size);
    std::generate(data.begin(), data.end(), [] () {
      return uniformDist(randomEngine);
    });

    u_int8_t* buffer = new u_int8_t[size];

    for (u_int8_t i = 0; i < size; i++) {
      buffer[i] = u_int8_t(data[i]);
    }

    return buffer;
  } catch (...) {
    return nullptr;
  }
}

std::string encrypt_256_aes_ctr(const std::string& value) {
  try {
    u_int8_t plaintext[value.length() + 1];

    memcpy(plaintext, value.c_str(), value.length());
    plaintext[value.length()] = '\0';

    u_int8_t key[32];
    memcpy(key, base64_decode(std::string(AES_ENCRYPTION_KEY)).c_str(), 32);

    const u_int8_t* iv = randomBytes(16);
    if (iv == nullptr) {
      return "";
    }

    CTR<AES256> ctr;
    ctr.clear();
    ctr.setKey(key, 32);
    ctr.setIV(iv, 16);
    ctr.setCounterSize(4);

    u_int8_t output[value.length() + 1];
    ctr.encrypt(output, plaintext, value.length());
    output[value.length()] = '\0';

    return base64_encode(std::string(iv, iv + 16) + std::string(output, output + value.length()));
  } catch (...) {
    return "";
  }
}

std::string decrypt_256_aes_ctr(const std::string& value) {
  try {
    const std::string decoded_value = base64_decode(value);

    const std::string s_iv = decoded_value.substr(0, 16);
    const std::string raw = decoded_value.substr(16, decoded_value.length());

    u_int8_t chifertext[raw.length() + 1];

    memcpy(chifertext, raw.c_str(), raw.length());
    chifertext[raw.length()] = '\0';

    u_int8_t iv[16];
    memcpy(iv, s_iv.c_str(), 16);

    u_int8_t key[32];
    memcpy(key, base64_decode(std::string(AES_ENCRYPTION_KEY)).c_str(), 32);

    CTR<AES256> ctr;
    ctr.clear();
    ctr.setKey(key, 32);
    ctr.setIV(iv, 16);
    ctr.setCounterSize(4);
    
    u_int8_t output[raw.length() + 1];
    ctr.encrypt(output, chifertext, raw.length());
    output[raw.length()] = '\0';

    return std::string(output, output + raw.length());
  } catch (...) {
    return "";
  }
}

/* #endregion */

#endif