#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include "tap.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdint.h>

uint16_t bound_port = 0;
extern int tap_fd;

static uint8_t local_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static uint8_t gateway_mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

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

void send_tcp_syn_ack(uint8_t *buf, int len) {
    struct eth_hdr *eth = (struct eth_hdr *)buf;
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
    struct tcp_hdr *tcp = (struct tcp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));

    uint8_t client_mac[6];
    memcpy(client_mac, eth->smac, 6);
    memcpy(eth->dmac, client_mac, 6);
    memcpy(eth->smac, local_mac, 6);

    uint8_t temp_ip[4];
    memcpy(temp_ip, ip->sip, 4);
    memcpy(ip->sip, ip->dip, 4);
    memcpy(ip->dip, temp_ip, 4);

    ip->checksum = 0;
    ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));

    uint16_t temp_port = tcp->sport;
    tcp->sport = tcp->dport;
    tcp->dport = temp_port;

    tcp->flags = 0x12;
    tcp->ack = htonl(ntohl(tcp->seq) + 1);
    tcp->seq = htonl(12345);

    tcp->checksum = 0;
    int tcp_len = ntohs(ip->total_len) - sizeof(struct ip_hdr);

    struct pseudo_header {
        uint8_t src_ip[4];
        uint8_t dst_ip[4];
        uint8_t zero;
        uint8_t protocol;
        uint16_t tcp_len;
    } pseudo_hdr;

    memcpy(pseudo_hdr.src_ip, ip->sip, 4);
    memcpy(pseudo_hdr.dst_ip, ip->dip, 4);
    pseudo_hdr.zero = 0;
    pseudo_hdr.protocol = ip->protocol;
    pseudo_hdr.tcp_len = htons(tcp_len);

    uint8_t checksum_buf[sizeof(struct pseudo_header) + 1500];
    memcpy(checksum_buf, &pseudo_hdr, sizeof(struct pseudo_header));
    memcpy(checksum_buf + sizeof(struct pseudo_header), tcp, tcp_len);

    tcp->checksum = checksum((uint16_t *)checksum_buf, sizeof(struct pseudo_header) + tcp_len);
    tap_write(tap_fd, buf, len);
    printf("已发送 TCP SYN-ACK 响应\n");
}

void tcp_process(uint8_t *buf, int len) {
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
    struct tcp_hdr *tcp = (struct tcp_hdr *)(
        buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr)
    );

    uint16_t dport = ntohs(tcp->dport);
    uint16_t sport = ntohs(tcp->sport);

    if (bound_port != 0 && dport != bound_port) {
        return;
    }

    printf("TCP 包：源端口=%d  目标端口=%d\n", sport, dport);
    printf("TCP 标志位：0x%02x\n", tcp->flags);

    int data_offset = (tcp->data_off >> 4) * 4;
    int tcp_data_len = ntohs(ip->total_len) - sizeof(struct ip_hdr) - data_offset;

    if (tcp_data_len > 0) {
        uint8_t *data = (uint8_t *)tcp + data_offset;
        printf("TCP 数据长度：%d 字节\n", tcp_data_len);
        printf("TCP 数据：");
        for (int i = 0; i < tcp_data_len && i < 64; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    if (tcp->flags == 0x02) {
        printf("收到 TCP 连接请求（SYN）\n");
        send_tcp_syn_ack(buf, len);
    } else if (tcp->flags == 0x10) {
        printf("收到 TCP 确认包（ACK）\n");
    } else if (tcp->flags == 0x18) {
        printf("收到 TCP 数据传输包（PSH-ACK）\n");
    } else if (tcp->flags == 0x11) {
        printf("收到 TCP 关闭请求（FIN-ACK）\n");
    }
}

void tcp_set_bound_port(uint16_t port) {
    bound_port = port;
}

// ===================== WDM 光波导发送函数（零报错） =====================
void tcp_send_data(uint8_t *src_ip, uint8_t *dst_ip,
                   uint16_t src_port, uint16_t dst_port,
                   char *data, int len)
{
    uint8_t buf[2048];
    memset(buf, 0, sizeof(buf));

    struct eth_hdr *eth = (struct eth_hdr *)buf;
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
    struct tcp_hdr *tcp = (struct tcp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));

    memcpy(eth->smac, local_mac, 6);
    memcpy(eth->dmac, gateway_mac, 6);
    eth->type = htons(0x0800);

    ip->version_ihl = (4 << 4) | 5;
    ip->tos = 0;
    ip->total_len = htons(sizeof(struct ip_hdr) + sizeof(struct tcp_hdr) + len);
    ip->id = htons(0);
    ip->frag_off = htons(0);
    ip->ttl = 64;
    ip->protocol = 6;
    memcpy(ip->sip, src_ip, 4);
    memcpy(ip->dip, dst_ip, 4);
    ip->checksum = 0;
    ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));

    tcp->sport = htons(src_port);
    tcp->dport = htons(dst_port);
    tcp->seq = htonl(12345);
    tcp->ack = htonl(0);
    tcp->data_off = (5 << 4);
    tcp->flags = 0x18;
    tcp->window = htons(1024);
    tcp->checksum = 0;

    uint8_t *tcp_data = (uint8_t *)tcp + sizeof(struct tcp_hdr);
    memcpy(tcp_data, data, len);

    int tcp_packet_len = sizeof(struct tcp_hdr) + len;
    struct pseudo_header {
        uint8_t src_ip[4];
        uint8_t dst_ip[4];
        uint8_t zero;
        uint8_t protocol;
        uint16_t tcp_len;
    } pseudo_hdr;

    memcpy(pseudo_hdr.src_ip, src_ip, 4);
    memcpy(pseudo_hdr.dst_ip, dst_ip, 4);
    pseudo_hdr.zero = 0;
    pseudo_hdr.protocol = 6;
    pseudo_hdr.tcp_len = htons(tcp_packet_len);

    uint8_t check_buf[1024];
    memcpy(check_buf, &pseudo_hdr, sizeof(pseudo_hdr));
    memcpy(check_buf + sizeof(pseudo_hdr), tcp, tcp_packet_len);
    tcp->checksum = checksum((uint16_t *)check_buf, sizeof(pseudo_hdr) + tcp_packet_len);

    int total_len = sizeof(struct eth_hdr) + ntohs(ip->total_len);
    tap_write(tap_fd, buf, total_len);

    printf("TCP发送成功：%s\n", data);
}