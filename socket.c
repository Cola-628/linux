#include "socket.h"
#include "tcp.h"
#include <stdio.h>

void mini_socket_init() {
    printf("迷你 Socket 接口初始化完成\n");
}

int mini_bind(uint16_t port) {
    printf("Socket 绑定端口：%d\n", port);
    // 通知TCP模块绑定的端口
    tcp_set_bound_port(port);
    return 0;
}