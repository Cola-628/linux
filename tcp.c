#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include "tap.h"
#include <stdio.h>
#include <arpa/inet.h>   // 新增 ntohs
#include <string.h>

// 全局变量：绑定的端口
uint16_t bound_port = 0;
extern int tap_fd;  // TAP 文件描述符，用TAP0网卡

// 计算校验和
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

// 发送 TCP SYN-ACK 响应
void send_tcp_syn_ack(uint8_t *buf, int len) {
    struct eth_hdr *eth = (struct eth_hdr *)buf;//协议报文解析，把收到的二进制数据拆成以太网头【链路层】
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));//拆成IP头【网络层】
    struct tcp_hdr *tcp = (struct tcp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));//拆成TCP头【传输层】

    // 保存客户端 MAC 地址
    uint8_t client_mac[6];
    memcpy(client_mac, eth->smac, 6);
    
    // 设置目标 MAC 地址为客户端 MAC 地址
    memcpy(eth->dmac, client_mac, 6);
    // 设置源 MAC 地址为本地 MAC 地址，交换以太网地址，回复数据必须把源目MAC反过来
    extern uint8_t local_mac[6];
    memcpy(eth->smac, local_mac, 6);

    // 交换 IP 地址
    uint8_t temp_ip[4];
    memcpy(temp_ip, ip->sip, 4);
    memcpy(ip->sip, ip->dip, 4);
    memcpy(ip->dip, temp_ip, 4);

    // 重新计算 IP 校验和，IP头改了必须重新校验
    ip->checksum = 0;
    ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));

    // 交换TCP端口
    uint16_t temp_port = tcp->sport;
    tcp->sport = tcp->dport;
    tcp->dport = temp_port;

    // 设置 SYN-ACK 标志=同意连接
    tcp->flags = 0x12;  // SYN + ACK

    // 设置确认号，这次发了1，等着下次发2
    tcp->ack = htonl(ntohl(tcp->seq) + 1);

    // 设置序列号，自己的起始编号
    tcp->seq = htonl(12345);  // 简单的初始序列号

    // 重新计算 TCP 校验和（包含伪头部）
    tcp->checksum = 0;
    int tcp_len = ntohs(ip->total_len) - sizeof(struct ip_hdr);
    
    // 构建伪头部（标准强制要求）
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
    
    // 计算校验和
    uint8_t checksum_buf[sizeof(struct pseudo_header) + 1500];
    memcpy(checksum_buf, &pseudo_hdr, sizeof(struct pseudo_header));
    memcpy(checksum_buf + sizeof(struct pseudo_header), tcp, tcp_len);
    
    tcp->checksum = checksum((uint16_t *)checksum_buf, sizeof(struct pseudo_header) + tcp_len);

    // 发送 SYN-ACK 响应
    tap_write(tap_fd, buf, len);
    printf("已发送 TCP SYN-ACK 响应\n");
}
// 处理 TCP 包
void tcp_process(uint8_t *buf, int len) //拆包
{
    struct eth_hdr *eth = (struct eth_hdr *)buf;//以太网
    struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));//IP
    struct tcp_hdr *tcp = (struct tcp_hdr *)(
        buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr)
    );//TCP

    uint16_t dport = ntohs(tcp->dport);
    uint16_t sport = ntohs(tcp->sport);//网络序转主机序，把网络格式转成能看到的端口号

    // 只处理绑定端口的数据包，不是发给我的端口，直接丢掉
    if (bound_port != 0 && dport != bound_port) {
        return;
    }

    printf("TCP 包：源端口=%d  目标端口=%d\n", sport, dport);
    printf("TCP 标志位：0x%02x\n", tcp->flags);

    // 计算数据偏移，算TCP头有多长，数据从哪开始
    int data_offset = (tcp->data_off >> 4) * 4;
    // 计算TCP数据长度
    int tcp_data_len = ntohs(ip->total_len) - sizeof(struct ip_hdr) - data_offset;

    // 如果有数据，打印数据内容
    if (tcp_data_len > 0) {
        uint8_t *data = (uint8_t *)tcp + data_offset;
        printf("TCP 数据长度：%d 字节\n", tcp_data_len);
        printf("TCP 数据内容：");
        for (int i = 0; i < tcp_data_len && i < 64; i++) {
            printf("%02x ", data[i]);
        }
        printf("\n");
    }

    // 处理 SYN 请求（连接请求）
    if (tcp->flags == 0x02) {
        printf("收到 TCP 连接请求（SYN）\n");
        // 发送 SYN-ACK 响应
        send_tcp_syn_ack(buf, len);
    }

    // 处理 ACK 包（确认包）
    else if (tcp->flags == 0x10) {
        printf("收到 TCP 确认包（ACK）\n");
    }

    // 处理 PSH-ACK 包（数据传输）
    else if (tcp->flags == 0x18) {
        printf("收到 TCP 数据传输包（PSH-ACK）\n");
    }

    // 处理 FIN-ACK 包（连接关闭）
    else if (tcp->flags == 0x11) {
        printf("收到 TCP 连接关闭请求（FIN-ACK）\n");
    }
}

// 设置绑定端口，外部设置监听端口
void tcp_set_bound_port(uint16_t port) {
    bound_port = port;
}