#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <string.h>

// 以太网帧头部 【必须加 packed】
struct eth_hdr {
    uint8_t dmac[6];
    uint8_t smac[6];
    uint16_t type;
} __attribute__((packed));

// ARP 协议头部 【必须加 packed】
struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_len;
    uint8_t proto_len;
    uint16_t op;
    uint8_t smac[6];
    uint8_t sip[4];
    uint8_t dmac[6];
    uint8_t dip[4];
} __attribute__((packed));

void arp_process(uint8_t *buf, int len);
void arp_request(uint8_t *dip);

#endif