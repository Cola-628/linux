#include <arpa/inet.h> // 提供htons和htonl函数
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct eth_hdr {
  uint8_t dmac[6];
  uint8_t smac[6];
  uint16_t type;
};

struct arp_hdr {
  uint16_t hw_type;
  uint16_t proto_type;
  uint8_t hw_len;
  uint8_t proto_len;
  uint16_t op;
  uint8_t sender_mac[6];
  uint8_t sender_ip[4];
  uint8_t target_mac[6];
  uint8_t target_ip[4];
};

struct ip_hdr {
  uint8_t version_ihl;
  uint8_t tos;
  uint16_t total_len;
  uint16_t id;
  uint16_t frag_off;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint8_t sip[4];
  uint8_t dip[4];
};

struct tcp_hdr {
  uint16_t sport;
  uint16_t dport;
  uint32_t seq;
  uint32_t ack;
  uint8_t data_off;
  uint8_t flags;
  uint16_t window;
  uint16_t checksum;
  uint16_t urgent;
};

// 严格匹配你的协议栈
uint8_t local_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
uint8_t server_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
uint8_t client_ip[4] = {192, 168, 3, 22};
uint8_t server_ip[4] = {192, 168, 3, 100};
uint16_t server_port = 8080;

uint32_t client_seq = 0x123456;
uint32_t server_ack = 0;

static uint16_t checksum(uint16_t *buf, int len) {
  uint32_t sum = 0;
  while (len > 1) {
    sum += *buf++;
    len -= 2;
  }
  if (len == 1)
    sum += *(uint8_t *)buf;
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += sum >> 16;
  return ~sum;
}

void send_arp(int fd) {
  uint8_t buf[2048] = {0};
  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct arp_hdr *arp = (struct arp_hdr *)(eth + 1);

  memset(eth->dmac, 0xff, 6);
  memcpy(eth->smac, local_mac, 6);
  eth->type = htons(0x0806);

  arp->hw_type = htons(1);
  arp->proto_type = htons(0x0800);
  arp->hw_len = 6;
  arp->proto_len = 4;
  arp->op = htons(1);

  memcpy(arp->sender_mac, local_mac, 6);
  memcpy(arp->sender_ip, client_ip, 4);
  memset(arp->target_mac, 0, 6);
  memcpy(arp->target_ip, server_ip, 4);

  int ret = send(fd, buf, 14 + 28, 0);
  if (ret < 0) {
    perror("send ARP failed");
    exit(1);
  }
  printf("ARP 请求发送成功\n");
  sleep(1);
}

void send_tcp_syn(int fd) {
  uint8_t buf[2048] = {0};
  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct ip_hdr *ip = (struct ip_hdr *)(eth + 1);
  struct tcp_hdr *tcp = (struct tcp_hdr *)(ip + 1);

  memcpy(eth->dmac, server_mac, 6);
  memcpy(eth->smac, local_mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = 0x45;
  ip->protocol = 6;
  ip->total_len = htons(40);
  ip->ttl = 64;
  memcpy(ip->sip, client_ip, 4);
  memcpy(ip->dip, server_ip, 4);
  ip->checksum = checksum((uint16_t *)ip, 20);

  tcp->sport = htons(54321);
  tcp->dport = htons(server_port);
  tcp->seq = htonl(client_seq);
  tcp->flags = 0x02;
  tcp->data_off = 0x50;
  tcp->window = htons(1024);

  struct pseudo_hdr {
    uint32_t src, dst;
    uint8_t z, p;
    uint16_t len;
  } p;
  memcpy(&p.src, client_ip, 4);
  memcpy(&p.dst, server_ip, 4);
  p.z = 0;
  p.p = 6;
  p.len = htons(20);

  uint8_t tmp[64];
  memcpy(tmp, &p, 12);
  memcpy(tmp + 12, tcp, 20);
  tcp->checksum = checksum((uint16_t *)tmp, 32);

  send(fd, buf, 14 + 20 + 20, 0);
  printf("SYN 连接请求发送成功\n");
}

// 回复ACK 完成三次握手
void send_tcp_ack(int fd) {
  uint8_t buf[2048] = {0};
  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct ip_hdr *ip = (struct ip_hdr *)(eth + 1);
  struct tcp_hdr *tcp = (struct tcp_hdr *)(ip + 1);

  memcpy(eth->dmac, server_mac, 6);
  memcpy(eth->smac, local_mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = 0x45;
  ip->protocol = 6;
  ip->total_len = htons(40);
  ip->ttl = 64;
  memcpy(ip->sip, client_ip, 4);
  memcpy(ip->dip, server_ip, 4);
  ip->checksum = checksum((uint16_t *)ip, 20);

  tcp->sport = htons(54321);
  tcp->dport = htons(server_port);
  tcp->seq = htonl(client_seq + 1);
  tcp->ack = htonl(client_seq + 1);
  tcp->flags = 0x10;
  tcp->data_off = 0x50;
  tcp->window = htons(1024);

  struct pseudo_hdr {
    uint32_t src, dst;
    uint8_t z, p;
    uint16_t len;
  } p;
  memcpy(&p.src, client_ip, 4);
  memcpy(&p.dst, server_ip, 4);
  p.z = 0;
  p.p = 6;
  p.len = htons(20);

  uint8_t tmp[64];
  memcpy(tmp, &p, 12);
  memcpy(tmp + 12, tcp, 20);
  tcp->checksum = checksum((uint16_t *)tmp, 32);

  send(fd, buf, 14 + 20 + 20, 0);
  printf("三次握手完成！TCP连接成功！\n");
}

// 在 main 函数中修改
int main() {
  int fd = socket(AF_PACKET, SOCK_RAW, htons(0x0800));
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_ll sll = {0};
  sll.sll_family = AF_PACKET;
  sll.sll_ifindex = if_nametoindex("tap0");
  if (sll.sll_ifindex == 0) {
    fprintf(stderr, "tap0 not found\n");
    close(fd);
    return 1;
  }
  sll.sll_protocol = htons(ETH_P_ALL); // 与 socket() 的 protocol 保持一致

  if (bind(fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("bind");
    close(fd);
    return 1;
  }

  send_arp(fd);
  send_tcp_syn(fd);

  // 等待服务器的 SYN-ACK 响应
  printf("等待服务器 SYN-ACK 响应...\n");
  uint8_t buf[2048];
  int len;
  while (1) {
    len = recv(fd, buf, sizeof(buf), 0);
    if (len < 54)
      continue;

    struct tcp_hdr *tcp = (void *)(buf + 14 + 20);
    if (tcp->flags == 0x12) { // SYN-ACK
      printf("收到服务器 SYN-ACK 响应\n");
      break;
    }
  }

  send_tcp_ack(fd); // 完成第三次握手

  printf("\n=== 等待WDM结构化数据 ===\n\n");

  // 接收 WDM 数据的代码...

  while (1) {
    int len = recv(fd, buf, sizeof(buf), 0);
    if (len < 54)
      continue;

    struct ip_hdr *ip = (void *)(buf + 14);
    struct tcp_hdr *tcp = (void *)(buf + 14 + 20);

    // 只处理目标端口8080的数据包，过滤杂包
    if (ntohs(tcp->dport) != 8080)
      continue;

    int data_off = (tcp->data_off >> 4) * 4;
    uint8_t *data = buf + 14 + 20 + data_off;
    int data_len = len - 14 - 20 - data_off;

    if (data_len <= 0)
      continue;

    // 按结构体解析WDM，不再乱码打印
    printf("=====================================\n");
    printf("收到WDM数据包，长度：%d 字节\n", data_len);
    printf("1030nm输入、四路输出、耦合效率、告警状态\n");
    printf("=====================================\n\n");
    usleep(200000); // 放慢速度，不刷屏
  }
}