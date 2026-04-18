#ifndef TAP_H
#define TAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdint.h>   // 新增：定义 uint8_t/uint16_t

// 创建并打开 TAP 设备，返回文件描述符
int tap_create(const char *dev_name);

// 从 TAP 读取以太网帧
int tap_read(int tap_fd, uint8_t *buf, int len);

// 向 TAP 写入以太网帧
int tap_write(int tap_fd, uint8_t *buf, int len);

#endif