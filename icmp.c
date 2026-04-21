#include "icmp.h"
#include "ip.h"
#include "arp.h"
#include "tap.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

extern int tap_fd;
extern uint8_t local_ip[4];
extern uint8_t local_mac[6];

// 计算校验和（ICMP/IP 必须要）
static uint16_t checksum(uint16_t *buf, int len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t *)buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

// 处理 ICMP 包（核心：回复 ping）
void icmp_process(uint8_t *buf, int len) {
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
    struct icmp_hdr *icmp = (struct icmp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));

    // 只处理 ping 请求（类型 8）
    if (icmp->type != 8) {
        return;
    }

    printf("✅ 收到 Ping 请求，正在回复...\n");

    // 构造 ICMP 响应
    icmp->type = 0;  // 0 = 响应
    icmp->checksum = 0;
    icmp->checksum = checksum((uint16_t *)icmp, len - sizeof(struct eth_hdr) - sizeof(struct ip_hdr));

    // 交换 IP 源/目标
    memcpy((void *)&ip->dip, (const void *)&ip->sip, 4);
    memcpy((void *)&ip->sip, (const void *)local_ip, 4);

    // 重新计算 IP 头部校验和
    ip->checksum = 0;
    ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));

    // 交换 MAC
    memcpy(eth->dmac, eth->smac, 6);
    memcpy(eth->smac, local_mac, 6);

    // 发送回包
    tap_write(tap_fd, buf, len);
}