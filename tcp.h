#ifndef TCP_H
#define TCP_H

#include <stdint.h>

// TCP 头部
struct tcp_hdr {
    uint16_t sport;    // 源端口
    uint16_t dport;    // 目标端口
    uint32_t seq;      // 序列号
    uint32_t ack;      // 确认号
    uint8_t data_off;  // 数据偏移 + 保留
    uint8_t flags;     // 标志位：SYN=0x02 ACK=0x10 FIN=0x01
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
};

// 处理 TCP 数据包
void tcp_process(uint8_t *buf, int len);

// 设置绑定端口
void tcp_set_bound_port(uint16_t port);

// 发送 TCP 数据
void tcp_send_data(uint8_t *src_ip, uint8_t *dst_ip,
                   uint16_t src_port, uint16_t dst_port,
                   char *data, int len);

#endif