#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

struct icmp_hdr {
    uint8_t type;     // 8=请求 0=响应
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
};

void icmp_process(uint8_t *buf, int len);

#endif