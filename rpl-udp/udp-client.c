#include "contiki.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <msp430.h>
#include "dev/lpm.h"

#include "sys/log.h"

#include <stdio.h>
#include <stdlib.h>
#include "dev/sht11/sht11-sensor.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define WITH_SERVER_REPLY  1
#define UDP_CLIENT_PORT	8800
#define UDP_SERVER_PORT	5700

#define SEND_INTERVAL		  (5 * CLOCK_SECOND)

enum p_type{SYN, DATA};

int get_temperature(){
  return ((sht11_sensor.value(SHT11_SENSOR_TEMP)/10)-396)/10;
}

static struct simple_udp_connection udp_conn;
uint8_t p_data[32];

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client");
PROCESS(udp_client_sleep, "UDP client data");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/

static void encode_packet(uint8_t* dest, uint16_t datalen, uint8_t* data, enum p_type type){
  int i;
  (*dest) = type;
  for(i = 0;i < datalen; i++){
    dest[i+2] = data[i];
  }
}

static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  P5OUT &= ~(1<<5);
  static char str[32];
  LOG_INFO("Received '%.*s' from ", datalen, (char *) data);
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");
  LOG_INFO("Sending Temperature: %u\n", get_temperature());
  snprintf(str, sizeof(str), "%u", get_temperature());
  simple_udp_sendto(&udp_conn, str, strlen(str), sender_addr);
  P5OUT |= (1<<5);
  process_exit(&udp_client_sleep);
  process_start(&udp_client_sleep, NULL);

#if LLSEC802154_CONF_ENABLED
  LOG_INFO_(" LLSEC LV:%d", uipbuf_get_attr(UIPBUF_ATTR_LLSEC_LEVEL));
#endif

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic_timer;
  static char str[32];
  uip_ipaddr_t dest_ipaddr;

  P5DIR |= 0x70;

  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);
  lpm_on();
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      P5OUT &= ~(1<<4);
      /* Send to DAG root */
      LOG_INFO("Sending SYN to ");
      LOG_INFO_6ADDR(&dest_ipaddr);
      LOG_INFO_("\n");

      snprintf(str, sizeof(str), "SYN");
      encode_packet(p_data, 0, (uint8_t*)NULL, SYN);
      simple_udp_sendto(&udp_conn, p_data, 2, &dest_ipaddr);
      P5OUT |= (1<<4);
      process_exit(&udp_client_sleep);
      process_start(&udp_client_sleep, NULL);
      break;
    } else {
      LOG_INFO("Not reachable yet\n");
    }

    /* Add some jitter */
    etimer_set(&periodic_timer, SEND_INTERVAL
      - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_sleep, ev, data)
{
  static struct etimer periodic_timer;
  etimer_set(&periodic_timer, 5 * CLOCK_SECOND);
  PROCESS_BEGIN();
    LOG_INFO("Going to sleep\n");
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    LPM4;
  PROCESS_END();
}