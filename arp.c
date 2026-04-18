#include "arp.h"
#include "tap.h"
#include <arpa/inet.h>   // 新增 ntohs/htons
#include <stdio.h>      // 新增 printf

// 本机配置（可修改）
uint8_t local_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
uint8_t local_ip[4]  = {192, 168, 3, 100};
extern int tap_fd;  // TAP 文件描述符（来自 main）

// 处理 ARP 包
void arp_process(uint8_t *buf, int len) {
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    struct arp_hdr *arp = (struct arp_hdr *)(buf + sizeof(struct eth_hdr));

    // 只处理 IPv4 + 以太网 的 ARP
    if (ntohs(arp->hw_type) != 1 || ntohs(arp->proto_type) != 0x0800)
        return;

    // 判断目标是不是本机 IP
    if (memcmp(arp->dip, local_ip, 4) != 0)
        return;

    printf("收到 ARP 请求，目标是本机 IP\n");

    // 构造 ARP 响应
    eth->type = htons(0x0806);  // ARP 类型
    memcpy(eth->dmac, eth->smac, 6);
    memcpy(eth->smac, local_mac, 6);

    arp->op = htons(2);  // 2=响应
    memcpy(arp->dmac, arp->smac, 6);
    memcpy(arp->dip, arp->sip, 4);
    memcpy(arp->smac, local_mac, 6);
    memcpy(arp->sip, local_ip, 4);

    // 发送 ARP 响应
    tap_write(tap_fd, buf, len);
    printf("ARP 响应已发送\n");
}