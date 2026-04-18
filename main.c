#include "tap.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "wdm_coupling.h"
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int tap_fd;
#define TAP_DEV "tap0"

void *net_loop(void *arg) {
    uint8_t buf[2048];
    int len;
    printf("协议栈主循环启动，开始监听数据包...\n");

    while (1) {
        len = tap_read(tap_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        struct eth_hdr *eth = (struct eth_hdr *)buf;
        uint16_t type = ntohs(eth->type);

        if (type == 0x0806) {
            arp_process(buf, len);
        } else if (type == 0x0800) {
            ip_process(buf, len);
        }
    }
    return NULL;
}

void *wdm_sender_thread(void *arg)
{
    char send_buf[256];
    int send_len;
    WDM_Coupling_Data wdm;

    while (1)
    {
        wdm_coupling_read(&wdm);

        printf("\n===== WDM光波导耦合监测 =====\n");
        printf("1030nm输入: %.1f\n", wdm.input_1030);
        printf("OUT1:%.1f OUT2:%.1f OUT3:%.1f OUT4:%.1f\n",
               wdm.out1_1030, wdm.out2_1030,
               wdm.out3_1030, wdm.out4_1030);
        printf("耦合效率: %.1f%%\n", wdm.coupling_efficiency);
        printf("告警状态: %s\n", wdm.alarm ? "异常" : "正常");

        wdm_coupling_pack(&wdm, send_buf, &send_len);

        extern void tcp_send_data(uint8_t *src_ip, uint8_t *dst_ip,
                                 uint16_t src_port, uint16_t dst_port,
                                 char *data, int len);

        uint8_t src_ip[4] = {127,0,0,1};
        uint8_t dst_ip[4] = {127,0,0,1};
        tcp_send_data(src_ip, dst_ip, 8080, 12345, send_buf, send_len);

        sleep(2);
    }
    return NULL;
}



int main() {
    tap_fd = tap_create(TAP_DEV);
    if (tap_fd < 0) return -1;

    // 绑定端口
    tcp_set_bound_port(8080);

    pthread_t tid;
    pthread_create(&tid, NULL, net_loop, NULL);

    pthread_t wdm_tid;
    pthread_create(&wdm_tid, NULL, wdm_sender_thread, NULL);

    pthread_join(tid, NULL);
    return 0;
}