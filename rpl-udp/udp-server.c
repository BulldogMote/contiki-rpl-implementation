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
uip_ipaddr_t* leaf_address;
uint8_t rec_data = 0;

PROCESS(udp_server_process, "UDP server");
AUTOSTART_PROCESSES(&udp_server_process);

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
	  *leaf_address = *sender_addr;
    LOG_INFO_("Set leaf to sender: ");
    LOG_INFO_6ADDR(leaf_address);
    LOG_INFO_("\n");
	}else{
    rec_data = 1;
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
 // P5DIR |= 0x32;
 char req[] = "REQ";
 int i = 0;
 static struct etimer periodic_timer;
 uip_ipaddr_t zero_addr;
 uip_ip6addr(&zero_addr, 0, 0, 0, 0, 0, 0, 0, 0);
 leaf_address = (uip_ipaddr_t*) malloc(1 * sizeof(uip_ipaddr_t));
 uip_ip6addr(leaf_address, 0, 0, 0, 0, 0, 0, 0, 0);
 LOG_INFO_("Initial: ");
  LOG_INFO_6ADDR(leaf_address);
  LOG_INFO_("\n");
  PROCESS_BEGIN();
  lpm_on();
  /* Initialize DAG root */
  NETSTACK_ROUTING.root_start();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);
  while(1){
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    LOG_INFO_("Before comparing to zero: ");
    LOG_INFO_6ADDR(leaf_address);
    LOG_INFO_("\n");
    if(!uip_ip6addr_cmp(leaf_address, &zero_addr)){
      for(i = 0; i < ATTEMPTS; i++){
        if(rec_data == 1) break;
        LOG_INFO("Sending '%.*s' to ", strlen(req), (char *) req);
        LOG_INFO_6ADDR(leaf_address);
        LOG_INFO_("\n");
        simple_udp_sendto(&udp_conn, (char *) req, strlen(req), leaf_address);
        etimer_set(&periodic_timer, SEND_INTERVAL + (random_rand() % (5 * CLOCK_SECOND)));
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      }
      if(rec_data == 0){
        *leaf_address = zero_addr;
        LOG_INFO_("Set leaf to zero: ");
        LOG_INFO_6ADDR(leaf_address);
        LOG_INFO_("\n");
      }
      rec_data = 0;
    }
    etimer_set(&periodic_timer, 20 * CLOCK_SECOND  + (random_rand() % (1 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
