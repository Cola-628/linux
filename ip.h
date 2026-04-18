#ifndef IP_H
#define IP_H

#include <stdint.h>
#include "arp.h"

// IP 头部 【必须加 packed】
struct ip_hdr {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t sip;
    uint32_t dip;
} __attribute__((packed));

void ip_process(uint8_t *buf, int len);

#endif