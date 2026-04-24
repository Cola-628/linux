#include "tcp.h"
#include "arp.h"
#include "ip.h"
#include "tap.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// 发送缓冲区结构体
#define MAX_SEND_BUFF 10
struct send_buffer {
  uint32_t seq;         // 序列号
  char data[1024];      // 数据
  int len;              // 数据长度
  time_t send_time;     // 发送时间
  int retransmit_count; // 重传次数
};

// 发送缓冲区
struct send_buffer send_buff[MAX_SEND_BUFF];
int send_buff_count = 0;
uint32_t last_ack = 0;         // 最后确认的序列号
time_t last_activity_time = 0; // 上次活动时间

// 互斥锁定义
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

uint16_t bound_port = 0;
extern int tap_fd;
extern uint8_t local_ip[4]; // 从arp.c导入

static uint8_t local_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static uint8_t gateway_mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

// 客户端连接信息
static uint8_t client_ip[4] = {0, 0, 0, 0};
static uint16_t client_port = 0;
static uint8_t client_mac[6] = {0, 0, 0, 0, 0, 0};
static int client_connected = 0;

// ====================== TCP 连接状态 ======================
static uint32_t tcp_send_seq = 12345; // 本地发送序列号（自增）
static uint32_t tcp_recv_ack = 0;     // 本地期望收到的确认号

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
// 检查超时并处理重传
void check_timeouts() {
  time_t now = time(NULL);
  for (int i = 0; i < send_buff_count; i++) {
    // 超过5秒未确认，重传
    if (now - send_buff[i].send_time > 5) {
      if (send_buff[i].retransmit_count < 3) {
        // 重传数据
        printf("TCP 超时，重传数据，序列号: %u\n", send_buff[i].seq);
        tcp_send_wdm_data(send_buff[i].data, send_buff[i].len);
        send_buff[i].send_time = now;
        send_buff[i].retransmit_count++;
      } else {
        // 达到最大重传次数，标记客户端断开
        printf("TCP 重传失败，客户端可能已断开\n");
        pthread_mutex_lock(&client_mutex);
        client_connected = 0;
        pthread_mutex_unlock(&client_mutex);
        // 清空发送缓冲区
        send_buff_count = 0;
        break;
      }
    }
  }
}

void send_tcp_syn_ack(uint8_t *buf, int len) {
  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
  struct tcp_hdr *tcp =
      (struct tcp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));

  uint8_t temp_client_mac[6];
  memcpy(temp_client_mac, eth->smac, 6);
  memcpy(eth->dmac, temp_client_mac, 6);
  memcpy(eth->smac, local_mac, 6);

  uint8_t temp_ip[4];
  memcpy(temp_ip, &ip->sip, 4);
  memcpy(&ip->sip, &ip->dip, 4);
  memcpy(&ip->dip, temp_ip, 4);

  ip->checksum = 0;
  ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));

  uint16_t temp_port = tcp->sport;
  tcp->sport = tcp->dport;
  tcp->dport = temp_port;

  tcp->flags = 0x12; // SYN+ACK
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

  memcpy(pseudo_hdr.src_ip, &ip->sip, 4);
  memcpy(pseudo_hdr.dst_ip, &ip->dip, 4);
  pseudo_hdr.zero = 0;
  pseudo_hdr.protocol = ip->protocol;
  pseudo_hdr.tcp_len = htons(tcp_len);

  uint8_t checksum_buf[sizeof(struct pseudo_header) + 1500];
  memcpy(checksum_buf, &pseudo_hdr, sizeof(struct pseudo_header));
  memcpy(checksum_buf + sizeof(struct pseudo_header), tcp, tcp_len);

  tcp->checksum = checksum((uint16_t *)checksum_buf,
                           sizeof(struct pseudo_header) + tcp_len);
  tap_write(tap_fd, buf, len);
  printf("已发送 TCP SYN-ACK 响应\n");
}

void tcp_process(uint8_t *buf, int len) {
  struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
  struct tcp_hdr *tcp =
      (struct tcp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));

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

  // 收到SYN，第一次握手
  if (tcp->flags == 0x02) {
    printf("收到 TCP 连接请求（SYN）\n");
    send_tcp_syn_ack(buf, len);

    struct eth_hdr *eth = (struct eth_hdr *)buf;

    // 加锁保护共享资源
    pthread_mutex_lock(&client_mutex);
    memcpy(client_mac, eth->smac, 6);
    memcpy(client_ip, &ip->sip, 4);
    client_port = ntohs(tcp->sport);

    // 记录客户端初始序列号
    tcp_recv_ack = ntohl(tcp->seq) + 1;
    pthread_mutex_unlock(&client_mutex);

    printf("客户端已连接：IP=%d.%d.%d.%d, 端口=%d\n", client_ip[0],
           client_ip[1], client_ip[2], client_ip[3], client_port);
  }
  // 收到ACK，第三次握手 【核心修改】
  else if (tcp->flags & 0x10) {
    printf("收到 TCP 确认包（ACK）\n");
    uint32_t ack_num = ntohl(tcp->ack);
    printf("收到 ACK，确认号: %u\n", ack_num);

    // 更新发送序列号
    tcp_send_seq = ack_num;

    // 处理确认，从发送缓冲区中移除已确认的数据
    int new_count = 0;
    for (int i = 0; i < send_buff_count; i++) {
      if (send_buff[i].seq + send_buff[i].len <= ack_num) {
        // 数据已确认，移除
        printf("数据已确认，序列号: %u\n", send_buff[i].seq);
      } else {
        // 数据未确认，保留
        send_buff[new_count++] = send_buff[i];
      }
    }
    send_buff_count = new_count;

    // 更新活动时间
    last_activity_time = time(NULL);

    // 第三次握手完成，正式标记连接建立
    if (tcp->flags == 0x10 && !client_connected) {
      // 加锁保护共享资源
      pthread_mutex_lock(&client_mutex);
      client_connected = 1;
      printf("✅ TCP三次握手完成，客户端正式上线！\n");
      pthread_mutex_unlock(&client_mutex);
    }
  }

  else if (tcp->flags == 0x11) {
    printf("收到 TCP 关闭请求（FIN-ACK）\n");
    // 加锁保护共享资源
    pthread_mutex_lock(&client_mutex);
    client_connected = 0;
    pthread_mutex_unlock(&client_mutex);
  }
}

void tcp_set_bound_port(uint16_t port) { bound_port = port; }

// 发送 WDM 数据到已连接的客户端
void tcp_send_wdm_data(char *data, int len) {
    // 检查连接是否超时（30秒无活动）
time_t now = time(NULL);
if (now - last_activity_time > 30 && last_activity_time > 0) {
    pthread_mutex_lock(&client_mutex);
    client_connected = 0;
    pthread_mutex_unlock(&client_mutex);
    printf("客户端连接超时，已断开\n");
    return;
}

  // 加锁检查连接状态
  pthread_mutex_lock(&client_mutex);
  if (!client_connected) {
    pthread_mutex_unlock(&client_mutex);
    printf("无客户端连接，无法发送WDM数据\n");
    return;
  }

  // 复制客户端信息到临时变量
  uint8_t temp_client_mac[6];
  uint8_t temp_client_ip[4];
  uint16_t temp_client_port = client_port;
  memcpy(temp_client_mac, client_mac, 6);
  memcpy(temp_client_ip, client_ip, 4);
  pthread_mutex_unlock(&client_mutex);

  uint8_t buf[2048];
  memset(buf, 0, sizeof(buf));

  struct eth_hdr *eth = (struct eth_hdr *)buf;
  struct ip_hdr *ip = (struct ip_hdr *)(buf + sizeof(struct eth_hdr));
  struct tcp_hdr *tcp =
      (struct tcp_hdr *)(buf + sizeof(struct eth_hdr) + sizeof(struct ip_hdr));

  memcpy(eth->smac, local_mac, 6);
  memcpy(eth->dmac, temp_client_mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = (4 << 4) | 5;
  ip->tos = 0;
  ip->total_len = htons(sizeof(struct ip_hdr) + sizeof(struct tcp_hdr) + len);
  ip->id = htons(0);
  ip->frag_off = htons(0);
  ip->ttl = 64;
  ip->protocol = 6;
  memcpy(&ip->sip, local_ip, 4);
  memcpy(&ip->dip, temp_client_ip, 4);
  ip->checksum = 0;
  ip->checksum = checksum((uint16_t *)ip, sizeof(struct ip_hdr));

  tcp->sport = htons(bound_port);
  tcp->dport = htons(temp_client_port);
  tcp->seq = htonl(tcp_send_seq);
  tcp->ack = htonl(tcp_recv_ack);
  tcp->data_off = (5 << 4);
  tcp->flags = 0x18;
  tcp->window = htons(1024);
  tcp->checksum = 0;

  // 拷贝数据
  uint8_t *tcp_data = (uint8_t *)tcp + sizeof(struct tcp_hdr);
  memcpy(tcp_data, data, len);

  // 伪头部校验
  int tcp_packet_len = sizeof(struct tcp_hdr) + len;
  struct pseudo_header {
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_len;
  } pseudo_hdr;

  memcpy(pseudo_hdr.src_ip, local_ip, 4);
  memcpy(pseudo_hdr.dst_ip, temp_client_ip, 4);
  pseudo_hdr.zero = 0;
  pseudo_hdr.protocol = 6;
  pseudo_hdr.tcp_len = htons(tcp_packet_len);

  uint8_t check_buf[1024];
  memcpy(check_buf, &pseudo_hdr, sizeof(pseudo_hdr));
  memcpy(check_buf + sizeof(pseudo_hdr), tcp, tcp_packet_len);
  tcp->checksum =
      checksum((uint16_t *)check_buf, sizeof(pseudo_hdr) + tcp_packet_len);
// 发送前将数据加入发送缓冲区
if (send_buff_count < MAX_SEND_BUFF) {
    struct send_buffer *buff = &send_buff[send_buff_count++];
    buff->seq = tcp_send_seq;
    memcpy(buff->data, data, len);
    buff->len = len;
    buff->send_time = time(NULL);
    buff->retransmit_count = 0;
}

  // 发送
  int total_len = sizeof(struct eth_hdr) + ntohs(ip->total_len);
  tap_write(tap_fd, buf, total_len);

  // 发送后序列号自增
  tcp_send_seq += len;

  printf("WDM数据发送成功！长度：%d\n", len);
}