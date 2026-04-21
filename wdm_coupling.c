#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "wdm_coupling.h"

// 修改为AD7706的16位分辨率
#define AD_RESOLUTION 16       // 16位AD分辨率
#define AD_MAX_VALUE 65535      // 2^16 - 1
#define REFERENCE_VOLTAGE 3.3   // 参考电压（根据硬件实际值调整）

// 滤波参数
#define FILTER_WINDOW_SIZE 5    // 滑动窗口大小

// 存储历史数据用于滤波
float input_1030_history[FILTER_WINDOW_SIZE] = {0}; // 1030nm输入
float output_1030_history[4][FILTER_WINDOW_SIZE] = {{0}}; // 4路1030nm输出
int history_index = 0;

// 模拟AD采集函数（实际应用中替换为真实的AD驱动）
uint16_t ad_read(int channel)
{
    // 实际应用中，这里应该调用真实的AD驱动函数
    // 例如：return adc_read(channel);
    
    // 这里使用模拟值，但添加了噪声模拟
    // 调整为AD7706的16位范围（0-65535）
    // 通道分配：
    // 0: 1030nm输入
    // 1-4: 4路1030nm输出
    uint16_t base_value;
    switch(channel) {
        case 0:  // 1030nm输入
            base_value = 32768;  // 对应~1.64V，约74.5mW
            break;
        case 1:  // 1030nm输出1
            base_value = 4369;  // 对应~0.22V，约10mW
            break;
        case 2:  // 1030nm输出2
            base_value = 4300;  // 对应~0.217V，约9.9mW
            break;
        case 3:  // 1030nm输出3
            base_value = 4400;  // 对应~0.222V，约10.1mW
            break;
        case 4:  // 1030nm输出4
            base_value = 4350;  // 对应~0.219V，约9.95mW
            break;
        default:
            base_value = 0;
    }
    
    // 添加随机噪声（±200，适配16位范围）
    base_value += (rand() % 401) - 200;
    
    // 确保值在有效范围内
    if (base_value > AD_MAX_VALUE) base_value = AD_MAX_VALUE;// 限制在有效范围内
    if (base_value < 0) base_value = 0;// 限制在有效范围内
    
    return base_value;
}

// 电压转光功率的校准函数
float voltage_to_power(float voltage)
{
    // 实际应用中，需要根据光电传感器的特性进行校准
    // 这里使用简化的线性模型
    // 假设0V对应0mW，3.3V对应150mW
    return (voltage / REFERENCE_VOLTAGE) * 150.0;
}

// 滑动平均滤波
float moving_average_filter(float new_value, float *history)
{
    // 更新历史数据
    history[history_index] = new_value;//将新的传感器读数存储到历史数据组的当前位置
    history_index = (history_index + 1) % FILTER_WINDOW_SIZE;//移动索引到下一个位置，使用模运算实现循环
    
    // 计算平均值
    float sum = 0;
    for (int i = 0; i < FILTER_WINDOW_SIZE; i++) {
        sum += history[i];
    }
    
    return sum / FILTER_WINDOW_SIZE;// 返回滑动平均值，默认窗口为5
}

// 是 WDM 光波导耦合监测系统的核心数据处理函数，负责从 AD 转换器读取数据，计算光功率、耦合损耗和均匀性，并判断系统是否正常运行。
void wdm_coupling_read(WDM_Coupling_Data *data)
{
    // ==============================
    // 从AD采集数据
    // 1030nm泵浦 → 耦合 → 1分4输出
    // ==============================
    
    // 从5个通道读取AD值并转换为电压
    float input_voltage = (ad_read(0) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out1_voltage = (ad_read(1) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out2_voltage = (ad_read(2) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out3_voltage = (ad_read(3) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out4_voltage = (ad_read(4) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    
    // 转换为光功率，把电压值转换成光功率值，这样我们就能知道光的强度了
    float input_power = voltage_to_power(input_voltage);
    float out1_power = voltage_to_power(out1_voltage);
    float out2_power = voltage_to_power(out2_voltage);
    float out3_power = voltage_to_power(out3_voltage);
    float out4_power = voltage_to_power(out4_voltage);
    
    // 应用滤波，对每个光功率值应用滑动平均滤波，减少噪声干扰
    data->input_1030 = moving_average_filter(input_power, input_1030_history);
    data->out1_1030 = moving_average_filter(out1_power, output_1030_history[0]);
    data->out2_1030 = moving_average_filter(out2_power, output_1030_history[1]);
    data->out3_1030 = moving_average_filter(out3_power, output_1030_history[2]);
    data->out4_1030 = moving_average_filter(out4_power, output_1030_history[3]);

    // 计算总输出功率，把 4 路输出的功率加起来，得到总输出功率
    float total_output = data->out1_1030 + data->out2_1030
                       + data->out3_1030 + data->out4_1030;
    
    // 计算耦合损耗（dB）：损耗 = 10 * log10(输入功率 / 总输出功率)
    // 注意：如果输出功率大于输入功率，会得到负值，表示增益
    if (total_output > 0 && data->input_1030 > 0) {
        data->coupling_efficiency = 10 * log10(data->input_1030 / total_output);
    } else {
        data->coupling_efficiency = 99.9; // 避免除零错误
    }
    
    // 计算均匀性
    float average_output = total_output / 4.0;// 计算平均输出功率
    float max_deviation = 0;// 初始化最大偏差为0
    float outputs[4] = {data->out1_1030, data->out2_1030, data->out3_1030, data->out4_1030};// 存储4路输出功率
    
    for (int i = 0; i < 4; i++) {
        float deviation = fabs(outputs[i] - average_output);// 计算每个输出功率与平均输出功率的偏差
        if (deviation > max_deviation) {
            max_deviation = deviation;// 更新最大偏差
        }
    }
    
    data->uniformity = (1.0 - (max_deviation / average_output)) * 100.0f;// 计算均匀性，单位为%

    // 告警：耦合损耗 > 3.0dB 或均匀性 < 85% 报警
    data->alarm = (data->coupling_efficiency > 3.0 || data->uniformity < 85.0) ? 1 : 0;
}

// 数据打包函数，用于将 WDM 光波导耦合监测数据格式化为字符串，以便通过 TCP 协议发送给客户端
void wdm_coupling_pack(WDM_Coupling_Data *data, char *buf, int *len)
{
    sprintf(buf,
            "WDM_COUPLING:"
            "IN_1030=%.1f,"
            "OUT1_1030=%.1f,OUT2_1030=%.1f,OUT3_1030=%.1f,OUT4_1030=%.1f,"
            "LOSS=%.1fdB,"
            "UNIF=%.1f%%,"
            "ALARM=%d",
// 数据格式化数据，将 WDM 光波导耦合监测数据格式化为字符串，以便通过 TCP 协议发送给客户端
            data->input_1030,
            data->out1_1030, data->out2_1030,
            data->out3_1030, data->out4_1030,
            data->coupling_efficiency,
            data->uniformity,
            data->alarm);
    *len = strlen(buf);// 返回打包后的字符串长度
}