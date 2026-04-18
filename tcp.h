#ifndef TCP_H
#define TCP_H

#include <stdint.h>

// TCP 头部
struct tcp_hdr {
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_off;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

void tcp_process(uint8_t *buf, int len);
void tcp_set_bound_port(uint16_t port);
void tcp_send_wdm_data(char *data, int len);

#endif