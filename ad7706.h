// ad7706.h
#ifndef AD7706_H
#define AD7706_H

#include <stdint.h>

// AD7706 寄存器定义
#define AD7706_REG_COMM     0x00  // 通信寄存器
#define AD7706_REG_SETUP    0x10  // 设置寄存器
#define AD7706_REG_CLOCK    0x20  // 时钟寄存器
#define AD7706_REG_DATA     0x30  // 数据寄存器
#define AD7706_REG_TEST     0x40  // 测试寄存器
#define AD7706_REG_NOP      0x50  // 空操作
#define AD7706_REG_OFFSET   0x60  // 偏移寄存器
#define AD7706_REG_GAIN     0x70  // 增益寄存器

// 通道定义
#define AD7706_CH_AIN1      0x00  // 通道 1
#define AD7706_CH_AIN2      0x01  // 通道 2
#define AD7706_CH_AIN3      0x02  // 通道 3
#define AD7706_CH_AIN4      0x03  // 通道 4
#define AD7706_CH_AIN5      0x04  // 通道 5
#define AD7706_CH_AIN6      0x05  // 通道 6

// 增益定义
#define AD7706_GAIN_1       0x00  // 增益 1
#define AD7706_GAIN_2       0x01  // 增益 2
#define AD7706_GAIN_4       0x02  // 增益 4
#define AD7706_GAIN_8       0x03  // 增益 8
#define AD7706_GAIN_16      0x04  // 增益 16
#define AD7706_GAIN_32      0x05  // 增益 32
#define AD7706_GAIN_64      0x06  // 增益 64
#define AD7706_GAIN_128     0x07  // 增益 128

// 初始化 AD7706
int ad7706_init(void);

// 读取指定通道的 AD 值
uint16_t ad7706_read(uint8_t channel);

// 设置 AD7706 增益
void ad7706_set_gain(uint8_t channel, uint8_t gain);

// 校准 AD7706
int ad7706_calibrate(uint8_t channel);

#endif /* AD7706_H */
