#include "tap.h"
#include "arp.h"
#include "ip.h"
#include "socket.h"
#include <pthread.h>
#include <arpa/inet.h>   // 新增：定义 ntohs/htons 网络字节序函数

// 全局变量：TAP 文件描述符
int tap_fd;
// TAP 设备名
#define TAP_DEV "tap0"

// 协议栈主循环：持续收包
void *net_loop(void *arg) {
    uint8_t buf[2048];  // 数据缓冲区
    int len;

    printf("协议栈主循环启动，开始监听数据包...\n");

    while (1) {
        // 从 TAP 读取数据包
        len = tap_read(tap_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        struct eth_hdr *eth = (struct eth_hdr *)buf;
        uint16_t type = ntohs(eth->type);

        // 分发数据包
        if (type == 0x0806) {       // ARP
            arp_process(buf, len);
        } else if (type == 0x0800) { // IPv4
            ip_process(buf, len);
        }
    }
    return NULL;
}

int main() {
    // 1. 创建 TAP 设备
    tap_fd = tap_create(TAP_DEV);
    if (tap_fd < 0) return -1;

    // 2. 初始化 Socket 接口
    mini_socket_init();
    mini_bind(8080);  // 绑定 8080 端口

    // 3. 启动协议栈循环（线程）
    pthread_t tid;
    pthread_create(&tid, NULL, net_loop, NULL);

    // 等待线程
    pthread_join(tid, NULL);
    return 0;
}