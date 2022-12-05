/**
 *  The file was originally written by
 * 
 *  Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *  SPDX-License-Identifier: BSD-3-Clause
 *  
 *  And modifed by me to fit the needs of the projecutc->
 * 
 * 
 *  Original file: https://github.com/raspberrypi/pico-examples/blob/master/pico_w/ntp_client/picow_ntp_client.c
 **/

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/rtc.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#ifndef __NTP_CPP__
#define __NTP_CPP__

typedef struct NTP_T_ {
  ip_addr_t ntp_server_address;
  bool dns_request_sent;
  struct udp_pcb *ntp_pcb;
  alarm_id_t ntp_resend_alarm;
  bool is_complete = false;

  struct repeating_timer ntp_timer;
  bool dns_failed_log = false;
} NTP_T;

#define NTP_SERVER "pool.ntp.org"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_RESEND_TIME (10 * 1000)

// Called with results of operation
static void ntp_result(NTP_T* state, int status, time_t *result) {
  if (status == 0 && result) {
    struct tm *utc = gmtime(result);

    datetime_t dt = {
      .year  = (int16_t)(utc->tm_year + 1900),
      .month = (int8_t)(utc->tm_mon + 1),
      .day   = (int8_t)utc->tm_mday,
      .dotw  = (int8_t)utc->tm_wday, 
      .hour  = (int8_t)utc->tm_hour,
      .min   = (int8_t)utc->tm_min,
      .sec   = (int8_t)utc->tm_sec
    };

    // Save the time to the RTC
    rtc_set_datetime(&dt);

    printf(
      "[NTP] Response: %02d/%02d/%04d %02d:%02d:%02d\n", 
      utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
      utc->tm_hour, utc->tm_min, utc->tm_sec
    );

    // Exit the loop and continue
    state->is_complete = true;
  }

  if (state->ntp_resend_alarm > 0) {
    cancel_alarm(state->ntp_resend_alarm);
    state->ntp_resend_alarm = 0;
  }

  if (!(status == 0 && result)) {
    // Try again if unsuccessful
    state->dns_request_sent = false;
  }
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data);

// Make an NTP request
static void ntp_request(NTP_T *state) {
  cyw43_arch_lwip_begin();
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
  uint8_t *req = static_cast<uint8_t*>(p->payload);
  memset(req, 0, NTP_MSG_LEN);
  req[0] = 0x1b;
  udp_sendto(state->ntp_pcb, p, &state->ntp_server_address, NTP_PORT);
  pbuf_free(p);
  cyw43_arch_lwip_end();
}

static int64_t ntp_failed_handler(alarm_id_t id, void *user_data) {
    NTP_T* state = static_cast<NTP_T*>(user_data);
    printf("[NTP] Request failed\n");
    ntp_result(state, -1, NULL);
    return 0;
}

// Call back with a DNS result
static void ntp_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
  NTP_T *state = static_cast<NTP_T*>(arg);
  if (ipaddr) {
    state->ntp_server_address = *ipaddr;
    printf("[NTP] Address %s\n", ip4addr_ntoa(ipaddr));
    ntp_request(state);
  } else {
    if (!state->dns_failed_log) {
      printf("[NTP] DNS request failed\n");
      state->dns_failed_log = true;
    }
    ntp_result(state, -1, NULL);
  }
}

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
  NTP_T *state = static_cast<NTP_T*>(arg);
  uint8_t mode = pbuf_get_at(p, 0) & 0x7;
  uint8_t stratum = pbuf_get_at(p, 1);

  // Check the result
  if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
    mode == 0x4 && stratum != 0) {
    uint8_t seconds_buf[4] = {0};
    pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
    uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
    uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
    time_t epoch = seconds_since_1970;
    ntp_result(state, 0, &epoch);
  } else {
    printf("[NTP] Invalid response\n");
    ntp_result(state, -1, NULL);
  }

  pbuf_free(p);
}

// Perform initialisation
static NTP_T* ntp_init(void) {
  NTP_T *state = static_cast<NTP_T*>(calloc(1, sizeof(NTP_T)));
  if (!state) {
    printf("[NTP] Failed to allocate state\n");
    return NULL;
  }

  state->ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
  if (!state->ntp_pcb) {
    printf("[NTP] Failed to create pcb\n");
    free(state);
    return NULL;
  }

  udp_recv(state->ntp_pcb, ntp_recv, state);
  return state;
}

void clean_ntp(NTP_T *state) {
  printf("[NTP] Free memory\n");
  free(state);
}

static bool ntp_check(struct repeating_timer *rt) {
  try {
    NTP_T* state = static_cast<NTP_T*>(rt->user_data);
    if (state->is_complete) {
      clean_ntp(state);
      return false;
    }

    if (!state->dns_request_sent) {
      state->ntp_resend_alarm = add_alarm_in_ms(NTP_RESEND_TIME, ntp_failed_handler, state, true);

      cyw43_arch_lwip_begin();
      int err = dns_gethostbyname(NTP_SERVER, &state->ntp_server_address, ntp_dns_found, state);
      cyw43_arch_lwip_end();

      state->dns_request_sent = true;
      if (err == ERR_OK) {
        ntp_request(state);
      } else if (err != ERR_INPROGRESS) {
        if (!state->dns_failed_log) {
          printf("[NTP] DNS request failed\n");
          state->dns_failed_log = true;
        }

        ntp_result(state, -1, NULL);
      }
    }

    cyw43_arch_poll();
  } catch (...) {
    printf("[NTP]:[ERROR]: In timer\n");
  }

  return true;
}

bool setup_ntp() {
  rtc_init();
  NTP_T *state = ntp_init();
  if (!state) {
    return false;
  }

  add_repeating_timer_ms(1000, ntp_check, state, &state->ntp_timer);

  return true;
}

#endif