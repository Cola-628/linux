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

//以太网头部
struct eth_hdr {
  uint8_t dmac[6];// 目标 MAC 地址
  uint8_t smac[6];// 源 MAC 地址
  uint16_t type;// 协议类型
};

//ARP头部，发个ARP根据IP地址找mac地址
struct arp_hdr {
  uint16_t hw_type;// 硬件类型，1=以太网
  uint16_t proto_type;// 协议类型，0x0800=IPv4
  uint8_t hw_len;// 硬件地址长度，6字节
  uint8_t proto_len;// 协议地址长度，4字节
  uint16_t op;// 操作码，1=请求（问），2=响应（答）
  uint8_t sender_mac[6];// 发送者 MAC 地址
  uint8_t sender_ip[4];// 发送者 IP 地址
  uint8_t target_mac[6];// 目标 MAC 地址
  uint8_t target_ip[4];// 目标 IP 地址
};

//IPv4头部，写清楚发件人IP，收件人IP，包裹总大小，里面装的是TCP还是UDP
struct ip_hdr {
  uint8_t version_ihl;//版本和头部长度（4表示IPv4，5表示头部长度为20字节）
  uint8_t tos;// 服务类型（0表示无特殊服务类型）
  uint16_t total_len;// 总长度（字节）
  uint16_t id;// 分片标识
  uint16_t frag_off;// 分片偏移量（字节）
  uint8_t ttl;// 生存时间（秒）
  uint8_t protocol;// 协议类型（6表示TCP）
  uint16_t checksum;// 校验和
  uint8_t sip[4];// 源 IP 地址
  uint8_t dip[4];// 目标 IP 地址
};

//TCP头部，挂号信的详细收件人，收发件端口，序列号（这封信是第几个字节），确认号（我收到了第几个字节），标志位（SYN/ACK/FIN)
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
uint8_t local_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};// 本地 MAC 地址
uint8_t server_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};// 服务器 MAC 地址
uint8_t client_ip[4] = {192, 168, 3, 22};// 客户端 IP 地址
uint8_t server_ip[4] = {192, 168, 3, 100};// 服务器 IP 地址
uint16_t server_port = 8080;// 服务器在监听8080端口

uint32_t client_seq = 0x123456;// 客户端序列号，我发的第一个字节编号是 0x123456
uint32_t server_ack = 0;// 服务器确认号，一开始是0，没收到任何东西

// 计算校验和【反码求和】
static uint16_t checksum(uint16_t *buf, int len) {
  uint32_t sum = 0;
  while (len > 1) {
    sum += *buf++;//把数据按16位（2字节）为单位全部累加
    len -= 2;
  }
  if (len == 1)//如果数据长度是奇数，把最后8位加进去
    sum += *(uint8_t *)buf;
  sum = (sum >> 16) + (sum & 0xFFFF);//累加可能超过16位，把高16位和低16位加起来
  sum += sum >> 16;//再加一次防止还有进位
  return ~sum;//取反得最终校验和
}

//ARP请求发送函数，你不知道服务器（192.168.3.100）的网卡地址，所以发一个广播问所有人："谁是这个IP？请告诉我你的MAC地址！
void send_arp(int fd) {
  uint8_t buf[2048] = {0};// ARP 请求缓冲区
  struct eth_hdr *eth = (struct eth_hdr *)buf;// 以太网头部
  struct arp_hdr *arp = (struct arp_hdr *)(eth + 1);// ARP头部

  memset(eth->dmac, 0xff, 6);// 目标 MAC 地址设置为广播地址，广播给所有人
  memcpy(eth->smac, local_mac, 6);// 源 MAC 地址
  eth->type = htons(0x0806);// 协议类型，0x0806=ARP

  arp->hw_type = htons(1);// 1是请求（问），2是响应（答）
  arp->proto_type = htons(0x0800);// 协议类型，0x0800=IPv4
  arp->hw_len = 6;// 硬件地址长度，6字节
  arp->proto_len = 4;// 协议地址长度，4字节
  arp->op = htons(1);// 操作码，1=请求，2=响应

  memcpy(arp->sender_mac, local_mac, 6);// 发送者 MAC 地址
  memcpy(arp->sender_ip, client_ip, 4);// 发送者 IP 地址
  memset(arp->target_mac, 0, 6);// 目标 MAC 地址设置为0，还不知道，所以空着
  memcpy(arp->target_ip, server_ip, 4);// 目标 IP 地址

  int ret = send(fd, buf, 14 + 28, 0);// 发送ARP请求，4字节以太网头 + 28字节ARP头 = 42字节
  if (ret < 0) {
    perror("send ARP failed");// 发送ARP请求失败
    exit(1);// 退出程序
  }
  printf("ARP 请求发送成功\n");
  sleep(1);// 等待1秒，确保服务器收到ARP响应
}

//用于发送TCP连接请求（SYN包），你想和服务器（192.168.3.100:8080）建立TCP连接，这是三次握手的第一步，你发送一个SYN包说："你好，我想和你建立连接，这是我的序列号123456"
void send_tcp_syn(int fd) {
  uint8_t buf[2048] = {0};//创建一个2048字节的缓冲区，初始化0
  struct eth_hdr *eth = (struct eth_hdr *)buf;// 以太网头部
  struct ip_hdr *ip = (struct ip_hdr *)(eth + 1);// IPv4头部
  struct tcp_hdr *tcp = (struct tcp_hdr *)(ip + 1);// TCP头部

  memcpy(eth->dmac, server_mac, 6);// 目标 MAC 地址
  memcpy(eth->smac, local_mac, 6);// 源 MAC 地址
  eth->type = htons(0x0800);// 协议类型，0x0800=IPv4

//IP头部（快递单）
  ip->version_ihl = 0x45;// 版本和头部长度（4表示IPv4，5表示头部长度为20字节）
  ip->protocol = 6;// 协议类型（6表示TCP）
  ip->total_len = htons(40);// 总长度（字节）
  ip->ttl = 64;// 生存时间（秒）
  memcpy(ip->sip, client_ip, 4);// 源 IP 地址
  memcpy(ip->dip, server_ip, 4);// 目标 IP 地址
  ip->checksum = checksum((uint16_t *)ip, 20);// 计算IPv4校验和

//TCP头部（挂号信）
  tcp->sport = htons(54321);//我的端口
  tcp->dport = htons(server_port);//对方端口8080
  tcp->seq = htonl(client_seq);//我的序列号
  tcp->flags = 0x02;//SYN标志位，其他标志：ACK=0x10, FIN=0x01, RST=0x04
  tcp->data_off = 0x50;//头部长度20字节（5 * 4）
  tcp->window = htons(1024);//窗口大小1024（一次能收这么多）
//伪头部（TCP校验和的特殊计算）
  struct pseudo_hdr {
    uint32_t src, dst;// 源 IP 地址，目标 IP 地址
    uint8_t z;// 保留位，必须为0
    uint8_t p;// 协议类型，6表示TCP
    uint16_t len;// 总长度（字节）
  } p;
  memcpy(&p.src, client_ip, 4);// 把客户端IP（192.168.3.22）放进去
  memcpy(&p.dst, server_ip, 4);// 把服务器IP（192.168.3.100）放进去
  p.z = 0;// 保留位，必须为0
  p.p = 6;// 协议类型，6表示TCP
  p.len = htons(20);// 总长度（字节）20字节

  uint8_t tmp[64];
  memcpy(tmp, &p, 12);// 把伪头部（12字节）放进去
  memcpy(tmp + 12, tcp, 20);// 把TCP头部（20字节）放进去
  tcp->checksum = checksum((uint16_t *)tmp, 32);// 计算这32字节TCP校验和

  send(fd, buf, 14 + 20 + 20, 0);
  printf("SYN 连接请求发送成功\n");
}

// 回复ACK 完成三次握手
void send_tcp_ack(int fd) {
  uint8_t buf[2048] = {0};//在内存里划出一块2048字节的空间，全部清零，当作"信封"来用。
  struct eth_hdr *eth = (struct eth_hdr *)buf;// 把这块内存当以太网头部来写
  struct ip_hdr *ip = (struct ip_hdr *)(eth + 1);// IPv4头部
  struct tcp_hdr *tcp = (struct tcp_hdr *)(ip + 1);// TCP头部

  memcpy(eth->dmac, server_mac, 6);//把服务器的MAC地址（6字节）复制到以太网头部的"目标MAC地址"位置，就像在信封上写"收件人是服务器"。
  memcpy(eth->smac, local_mac, 6);//把客户端的MAC地址（6字节）复制到以太网头部的"源MAC地址"位置，就像在信封上写"发件人是客户端"。
  eth->type = htons(0x0800);//设置信封上写"里面装的是IP包"（0x0800就是IP协议的编号）。htons是为了把数据转成网络字节序（大端模式）。

  ip->version_ihl = 0x45;// 版本和头部长度（4表示IPv4，5表示头部长度为20字节）
  ip->protocol = 6;// 协议类型（6表示TCP）
  ip->total_len = htons(40);// 总长度（字节）整个IP包总长度是40字节（20字节IP头 + 20字节TCP头，没有数据）。
  ip->ttl = 64;// 生存时间（秒），表示这个包最多经过64个路由器，防止在网络里无限循环。
  memcpy(ip->sip, client_ip, 4);// 把客户端的IP地址（192.168.3.22）复制到"源IP地址"位置。
  memcpy(ip->dip, server_ip, 4);// 把服务器的IP地址（192.168.3.100）复制到"目标IP地址"位置。
  ip->checksum = checksum((uint16_t *)ip, 20);// 计算IPv4校验和

  tcp->sport = htons(54321);//把服务器的IP地址（192.168.3.100）复制到"目标IP地址"位置。
  tcp->dport = htons(server_port);//找服务器上8080号窗口的服务
  tcp->seq = htonl(client_seq + 1);//序列号 = 原来的序列号(0x123456) + 1。因为SYN包占用了一个序列号，所以下一个包要+1。
  tcp->ack = htonl(client_seq + 1);//确认号也填client_seq+1，意思说"我确认收到了你的SYN包"。
  tcp->flags = 0x10;//ACK标志位，其他标志：SYN=0x02, FIN=0x01, RST=0x04
  tcp->data_off = 0x50;//数据偏移量=0x50，右移4位得到5，表示TCP头部长度20字节（5×4=20）。
  tcp->window = htons(1024);//窗口大小1024（一次能收这么多）
//TCP伪头部，用于计算TCP校验和。
  struct pseudo_hdr {
    uint32_t src, dst;
    uint8_t z, p;
    uint16_t len;
  } p;
  memcpy(&p.src, client_ip, 4);// 把客户端IP（192.168.3.22）放进去
  memcpy(&p.dst, server_ip, 4);// 把服务器IP（192.168.3.100）放进去
  p.z = 0;// 保留位，必须为0
  p.p = 6;// 协议类型，6表示TCP
  p.len = htons(20);// 总长度（字节）20字节

  uint8_t tmp[64];
  memcpy(tmp, &p, 12);// 把伪头部（12字节）放进去
  memcpy(tmp + 12, tcp, 20);// 把TCP头部（20字节）放进去
  tcp->checksum = checksum((uint16_t *)tmp, 32);// 计算这32字节TCP校验和

  send(fd, buf, 14 + 20 + 20, 0);//通过网卡手柄fd，把缓冲区里的数据发出去，发送长度 = 14(以太网头) + 20(IP头) + 20(TCP头) = 54字节。
  printf("三次握手完成！TCP连接成功！\n");
}

// 在 main 函数中修改
int main() {
  int fd = socket(AF_PACKET, SOCK_RAW, htons(0x0800));// 创建一个原始套接字，用于发送和接收以太网数据包。
  if (fd < 0) {
    perror("socket");//如果上面创建套接字失败（返回值小于0），就打印错误信息"socket"，然后程序返回1（表示非正常退出）
    return 1;
  }

  struct sockaddr_ll sll = {0};// 定义一个 sockaddr_ll 结构体变量，用于绑定套接字到指定的网络接口。
  sll.sll_family = AF_PACKET;//我要操作的是链路层（网卡层面）的地址"
  sll.sll_ifindex = if_nametoindex("tap0");//将网络接口名转换为接口索引，就像通过名字可以查到他的身份证号
  if (sll.sll_ifindex == 0) {
    fprintf(stderr, "tap0 not found\n");//如果上面将网络接口名转换为接口索引失败（返回值为0），就打印错误信息"tap0 not found"，然后程序返回1（表示非正常退出）
    close(fd);//关闭套接字fd
    return 1;
  }
  sll.sll_protocol = htons(ETH_P_ALL); // 接收所有协议"，意思是这个套接字要接收所有类型的数据包（ARP、IP、IPv6 等），不只是 IP 包。

  if (bind(fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("bind");//把套接字手柄 fd 绑定到上面设置好的网卡上。如果绑定失败（返回值小于0），打印错误信息，关闭手柄，退出程序。绑定就像把手柄和指定网卡"焊死"在一起，以后收发都走这个网卡。
    close(fd);
    return 1;
  }

  send_arp(fd);//发送ARP请求包
  send_tcp_syn(fd);//发送SYN包

  // 等待服务器的 SYN-ACK 响应
  printf("等待服务器 SYN-ACK 响应...\n");
  uint8_t buf[2048];// 定义一个2048字节的缓冲区，用于接收服务器发送的数据包。
  int len;// 声明接收的数据包长度
  while (1) {
    len = recv(fd, buf, sizeof(buf), 0);//从原始套接字接收数据包。如果返回值小于0，说明没有数据可接收，继续等待。
    if (len < 54)//如果收到的数据不足 54 字节（以太网头14 + IP头20 + TCP头20），说明不是完整的 TCP 包，跳过这次循环，继续等下一个包。
      continue;

    struct tcp_hdr *tcp = (void *)(buf + 14 + 20);// 让 tcp 指针指向缓冲区第 34 字节的位置（跳过以太网头的 14 字节和 IP 头的 20 字节），把那个位置当作 TCP 头部来解读。
    if (tcp->flags == 0x12) { // SYN-ACK 标志位
      printf("收到服务器 SYN-ACK 响应\n");
      break;
    }
  }

  send_tcp_ack(fd); // 完成第三次握手

  printf("\n=== 等待WDM结构化数据 ===\n\n");

  // 接收 WDM 数据的代码...

  while (1) {
    int len = recv(fd, buf, sizeof(buf), 0);//从套接字接收数据包，存到 buf，实际长度存到 len
    if (len < 54)//如果收到的数据不足 54 字节（以太网头14 + IP头20 + TCP头20），说明不是完整的 TCP 包，跳过这次循环，继续等下一个包。
      continue;

    struct ip_hdr *ip = (void *)(buf + 14);// 让 ip 指针指向缓冲区第 14 字节的位置（跳过以太网头的 14 字节），把那个位置当作 IP 头部来解读。
    struct tcp_hdr *tcp = (void *)(buf + 14 + 20);// 让 tcp 指针指向缓冲区第 34 字节的位置（跳过以太网头的 14 字节和 IP 头的 20 字节），把那个位置当作 TCP 头部来解读。

    // 只处理源端口8080的数据包，过滤杂包
    if (ntohs(tcp->sport) != 8080)
      continue;

    int data_off = (tcp->data_off >> 4) * 4;//计算 TCP 数据偏移（头部长度）
    uint8_t *data = buf + 14 + 20 + data_off;// 让 data 指针指向真正数据开始的位置 
    int data_len = len - 14 - 20 - data_off;// 计算数据长度

    if (data_len <= 0)//如果数据长度小于等于0，说明数据包无效，跳过这次循环，继续等下一个包。
      continue;

   // 修改客户端解析逻辑
printf("=====================================\n");
printf("收到WDM数据包，长度：%d 字节\n", data_len);

// 服务器发送的是字符串格式，直接打印
data[data_len] = '\0'; // 确保字符串结束
printf("WDM数据: %s\n", data);

// 简化解析，直接提取关键信息
char *in_ptr = strstr((char*)data, "IN=");
char *out1_ptr = strstr((char*)data, "OUT1=");
char *out2_ptr = strstr((char*)data, "OUT2=");
char *out3_ptr = strstr((char*)data, "OUT3=");
char *out4_ptr = strstr((char*)data, "OUT4=");
char *eff_ptr = strstr((char*)data, "EFF=");
char *alarm_ptr = strstr((char*)data, "ALARM=");

if (in_ptr && out1_ptr && out2_ptr && out3_ptr && out4_ptr && eff_ptr && alarm_ptr) {
    float input = atof(in_ptr + 3);
    float out1 = atof(out1_ptr + 5);
    float out2 = atof(out2_ptr + 5);
    float out3 = atof(out3_ptr + 5);
    float out4 = atof(out4_ptr + 5);
    float eff = atof(eff_ptr + 4);
    int alarm = atoi(alarm_ptr + 6);
    
    printf("\n解析结果:\n");
    printf("1030nm输入: %.1f\n", input);
    printf("OUT1:%.1f OUT2:%.1f OUT3:%.1f OUT4:%.1f\n", out1, out2, out3, out4);
    printf("耦合效率: %.1f%%\n", eff);
    printf("告警状态: %s\n", alarm == 0 ? "正常" : "异常");
}
printf("=====================================\n\n");
    usleep(200000); // 放慢速度，不刷屏，让程序暂停 0.2 秒（200000 微秒）。这样做是为了防止数据来得太快时屏幕刷得太快看不清，给用户留出阅读时间
  }
}