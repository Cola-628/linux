#include "ip.h"
#include "arp.h"
#include "tcp.h"
#include "icmp.h"   // 新增
#include <arpa/inet.h>
#include <stdio.h>

extern uint8_t local_ip[4];

void ip_process(uint8_t *buf, int len) {
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));

    if (memcmp(ip->dip, local_ip, 4) != 0)
        return;

    printf("收到 IPv4 包，协议：%d\n", ip->protocol);

    if (ip->protocol == 6) {
        tcp_process(buf, len);
    } 
    else if (ip->protocol == 1) {  // 1 = ICMP
        icmp_process(buf, len);
    }
}