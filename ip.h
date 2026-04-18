#ifndef IP_H
#define IP_H

#include <stdint.h>

// IPv4 头部
struct ip_hdr {
    uint8_t version_ihl;      // 版本(4位) + 头部长度(4位)
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;         // 协议：6=TCP  1=ICMP
    uint16_t checksum;
    uint8_t sip[4];
    uint8_t dip[4];
};

// 处理 IP 数据包
void ip_process(uint8_t *buf, int len);

#endif