#include "tap.h"

// 创建 TAP 设备（核心函数）
int tap_create(const char *dev_name) {
    struct ifreq ifr;
    int fd, err;

    // 打开 TUN/TAP 设备
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("open /dev/net/tun failed");
        return -1;
    }

    // 初始化 ifreq 结构体
    memset(&ifr, 0, sizeof(ifr));
    // 标志：TAP 设备（以太网帧），不使用包信息
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    // 指定设备名
    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    // 创建 TAP 设备
    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
        perror("ioctl TUNSETIFF failed");
        close(fd);
        return -1;
    }

    printf("TAP 设备创建成功：%s\n", ifr.ifr_name);
    return fd;
}

// 读取数据
int tap_read(int tap_fd, uint8_t *buf, int len) {
    int n = read(tap_fd, buf, len);
    if (n < 0) perror("tap_read failed");
    return n;
}

// 发送数据
int tap_write(int tap_fd, uint8_t *buf, int len) {
    int n = write(tap_fd, buf, len);
    if (n < 0) perror("tap_write failed");
    return n;
}