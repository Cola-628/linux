/* ad7706.c
#include "ad7706.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

// SPI 设备文件
#define SPI_DEVICE "/dev/spidev0.0"

// SPI 配置
#define SPI_MODE SPI_MODE_3
#define SPI_BITS_PER_WORD 8
#define SPI_SPEED_HZ 3200000  // 3.2MHz，AD7706 最大支持

// 全局变量
static int spi_fd = -1;

// 向 AD7706 写入命令
static int ad7706_write(uint8_t reg, uint8_t value) {
    uint8_t tx_buf[2];
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = 0,
        .len = 2,
        .speed_hz = SPI_SPEED_HZ,
        .bits_per_word = SPI_BITS_PER_WORD,
    };

    tx_buf[0] = reg;
    tx_buf[1] = value;

    return ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

// 从 AD7706 读取数据
static uint16_t ad7706_read_raw(uint8_t reg) {
    uint8_t tx_buf[1] = {reg};
    uint8_t rx_buf[2] = {0};
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = 1,
        .speed_hz = SPI_SPEED_HZ,
        .bits_per_word = SPI_BITS_PER_WORD,
    };

    // 发送读取命令
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        return 0;
    }

    // 读取数据
    tr.len = 2;
    tr.tx_buf = 0;
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        return 0;
    }

    return (rx_buf[0] << 8) | rx_buf[1];
}

// 等待数据就绪
static void ad7706_wait_ready(void) {
    // 简单实现：等待固定时间
    // 实际应用中应该读取状态寄存器
    usleep(10000);  // 10ms
}

// 初始化 SPI 接口
static int spi_init(void) {
    int mode = SPI_MODE;
    int bits = SPI_BITS_PER_WORD;
    int speed = SPI_SPEED_HZ;

    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("Failed to set SPI mode");
        close(spi_fd);
        return -1;
    }

    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("Failed to set SPI bits");
        close(spi_fd);
        return -1;
    }

    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("Failed to set SPI speed");
        close(spi_fd);
        return -1;
    }

    return 0;
}

// 初始化 AD7706
int ad7706_init(void) {
    // 初始化 SPI
    if (spi_init() < 0) {
        return -1;
    }

    // 复位 AD7706
    for (int i = 0; i < 32; i++) {
        ad7706_write(AD7706_REG_COMM, 0xFF);
    }

    // 配置时钟寄存器：主时钟 1MHz，输出更新率 50Hz
    ad7706_write(AD7706_REG_CLOCK, 0x0C);

    // 配置所有通道为单端输入，增益 1
    for (int i = 0; i < 6; i++) {
        ad7706_write(AD7706_REG_SETUP | i, 0x20 | AD7706_GAIN_1);
    }

    printf("AD7706 initialized successfully\n");
    return 0;
}

// 读取指定通道的 AD 值
uint16_t ad7706_read(uint8_t channel) {
    if (channel > 5) {
        return 0;
    }

    // 选择通道并启动转换
    ad7706_write(AD7706_REG_COMM, 0x08 | (channel << 4));

    // 等待转换完成
    ad7706_wait_ready();

    // 读取数据
    return ad7706_read_raw(AD7706_REG_DATA | channel);
}

// 设置 AD7706 增益
void ad7706_set_gain(uint8_t channel, uint8_t gain) {
    if (channel > 5 || gain > 7) {
        return;
    }

    // 读取当前设置
    uint8_t setup = 0;  // 实际应用中应该读取当前值
    // 设置新的增益
    ad7706_write(AD7706_REG_SETUP | channel, setup | gain);
}

// 校准 AD7706
int ad7706_calibrate(uint8_t channel) {
    if (channel > 5) {
        return -1;
    }

    // 执行系统校准
    ad7706_write(AD7706_REG_COMM, 0x10 | (channel << 4));
    ad7706_wait_ready();

    // 执行偏移校准
    ad7706_write(AD7706_REG_COMM, 0x20 | (channel << 4));
    ad7706_wait_ready();

    // 执行增益校准
    ad7706_write(AD7706_REG_COMM, 0x30 | (channel << 4));
    ad7706_wait_ready();

    return 0;
}



/*修改 main.c
#include "wdm_coupling.h"

int main() {
    // 初始化 TAP 设备
    tap_fd = tap_create(TAP_DEV);
    if (tap_fd < 0) return -1;

    // 初始化 WDM 耦合监测（包括 AD7706）
    wdm_coupling_init();

    // 绑定端口 8080
    tcp_set_bound_port(8080);

    // 创建线程
    pthread_t tid;
    pthread_create(&tid, NULL, net_loop, NULL);

    pthread_t wdm_tid;
    pthread_create(&wdm_tid, NULL, wdm_sender_thread, NULL);

    pthread_join(tid, NULL);
    return 0;
}


OBJ = main.o tap.o arp.o ip.o tcp.o socket.o icmp.o wdm_coupling.o ad7706.o

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm
*/