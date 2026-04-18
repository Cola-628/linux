#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>   // 新增！解决 uint16_t 报错

// 简易 Socket 初始化
void mini_socket_init();

// 绑定端口
int mini_bind(uint16_t port);

#endif