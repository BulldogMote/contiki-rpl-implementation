/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "string.h"
#include "random.h"
#include "dev/sht11/sht11-sensor.h"
#include <msp430.h>
#include <stdlib.h>

#include "dev/lpm.h"

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define UDP_CLIENT_PORT	8800	// 8765
#define UDP_SERVER_PORT	5700	// 5678

#define SEND_INTERVAL		  (1 * CLOCK_SECOND)
#define ATTEMPTS 5

static struct simple_udp_connection udp_conn;
uip_ipaddr_t* sink_addrs;
uint8_t sink_addrs_len = 0;
uint16_t rec_data = 0;
uint8_t a_i;
uint8_t s_i;

PROCESS(udp_server_process, "UDP server");
AUTOSTART_PROCESSES(&udp_server_process);

void init_sinks(){
  int j;
  for(j = 0;j < 16;j++){
    uip_ip6addr(&sink_addrs[j], 0, 0, 0, 0, 0, 0, 0, 0);
  }
}

void add_sink(const uip_ipaddr_t* sink){
  int j;
  for(j = 0;j < sink_addrs_len;j++){
    if(uip_ipaddr_cmp(&sink_addrs[j], sink)){
      break;
    }
  }
  if(j == sink_addrs_len){
    sink_addrs[sink_addrs_len] = *sink;
    sink_addrs_len++;
  }
}

void set_rec_data(const uip_ipaddr_t* sink, uint8_t data){
  int j;
  for(j = 0;j < sink_addrs_len;j++){
    if(uip_ipaddr_cmp(&sink_addrs[j], sink)){
      rec_data &= ~(1<<j);
      rec_data |= data << j;
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{ 

  LOG_INFO("Received '%.*s' from ", datalen, (char *) data);
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");

  char syn[] = "SYN";
  if(strcmp((char *)data,syn) == 0){
	  add_sink(sender_addr);
    LOG_INFO("Add sender sink: ");
    LOG_INFO_6ADDR(&sink_addrs[sink_addrs_len-1]);
    LOG_INFO_("\n\n");
	}else{
    LOG_INFO("Set sink rec_data to 1: ");
    LOG_INFO_6ADDR(sender_addr);
    LOG_INFO_("\n\n");
    set_rec_data(sender_addr, 1);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
 // P5DIR |= 0x32;
 char req[] = "REQ";
 static struct etimer periodic_timer;
 uip_ipaddr_t zero_addr;
 uip_ip6addr(&zero_addr, 0, 0, 0, 0, 0, 0, 0, 0);

  PROCESS_BEGIN();
 sink_addrs = (uip_ipaddr_t*) malloc(16 * sizeof(uip_ipaddr_t));
  init_sinks();
  lpm_on();
  /* Initialize DAG root */
  NETSTACK_ROUTING.root_start();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);
  while(1){
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    for(s_i = 0; s_i < sink_addrs_len; s_i++){
      if(!uip_ipaddr_cmp(&sink_addrs[s_i], &zero_addr)){
        for(a_i = 0; a_i < ATTEMPTS; a_i++){
          if((rec_data & (1<<s_i)) > 0) break;
          LOG_INFO("Sending '%.*s' to ", strlen(req), (char *) req);
          LOG_INFO_6ADDR(&sink_addrs[s_i]);
          LOG_INFO_("\n");
          simple_udp_sendto(&udp_conn, (char *) req, strlen(req), &sink_addrs[s_i]);
          etimer_set(&periodic_timer, SEND_INTERVAL + (random_rand() % (5 * CLOCK_SECOND)));
          PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
        }
        if((rec_data & (1<<s_i)) == 0){
          LOG_INFO("Sink unresponsive: ");
          LOG_INFO_6ADDR(&sink_addrs[s_i]);
          LOG_INFO_("\n");
        }
      }
    }
    rec_data = 0;
    etimer_set(&periodic_timer, 20 * CLOCK_SECOND  + (random_rand() % (1 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/