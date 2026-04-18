#ifndef ARP_H
#define ARP_H

#include <stdint.h>   // 新增：必须头文件
#include <string.h>

// 以太网帧头部
struct eth_hdr {
    uint8_t dmac[6];    // 目标MAC
    uint8_t smac[6];    // 源MAC
    uint16_t type;      // 类型：0x0806=ARP  0x0800=IP
};

// ARP 协议头部
struct arp_hdr {
    uint16_t hw_type;   // 硬件类型：1=以太网
    uint16_t proto_type;// 协议类型：0x0800=IPv4
    uint8_t hw_len;     // MAC长度：6
    uint8_t proto_len;  // IP长度：4
    uint16_t op;        // 操作码：1=请求 2=响应
    uint8_t smac[6];    // 发送端MAC
    uint8_t sip[4];     // 发送端IP
    uint8_t dmac[6];    // 目标MAC
    uint8_t dip[4];     // 目标IP
};

// 处理收到的 ARP 数据包
void arp_process(uint8_t *buf, int len);

// 发送 ARP 请求
void arp_request(uint8_t *dip);

#endif