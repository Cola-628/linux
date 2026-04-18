#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "wdm_coupling.h"

// 定义AD采集相关常量
#define AD_RESOLUTION 12       // 12位AD分辨率
#define AD_MAX_VALUE 4095      // 2^12 - 1
#define REFERENCE_VOLTAGE 3.3   // 参考电压

// 滤波参数
#define FILTER_WINDOW_SIZE 5    // 滑动窗口大小

// 存储历史数据用于滤波
float input_history[FILTER_WINDOW_SIZE] = {0};
float output_history[4][FILTER_WINDOW_SIZE] = {{0}};
int history_index = 0;

// 模拟AD采集函数（实际应用中替换为真实的AD驱动）
uint16_t ad_read(int channel)
{
    // 实际应用中，这里应该调用真实的AD驱动函数
    // 例如：return adc_read(channel);
    
    // 这里使用模拟值，但添加了噪声模拟
    uint16_t base_value;
    switch(channel) {
        case 0:  // 1030nm输入
            base_value = 3500;
            break;
        case 1:  // 1030nm输出1
            base_value = 820;
            break;
        case 2:  // 1030nm输出2
            base_value = 810;
            break;
        case 3:  // 1030nm输出3
            base_value = 825;
            break;
        case 4:  // 1030nm输出4
            base_value = 815;
            break;
        default:
            base_value = 0;
    }
    
    // 添加随机噪声（±10）
    base_value += (rand() % 21) - 10;
    
    // 确保值在有效范围内
    if (base_value > AD_MAX_VALUE) base_value = AD_MAX_VALUE;
    if (base_value < 0) base_value = 0;
    
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
    history[history_index] = new_value;
    history_index = (history_index + 1) % FILTER_WINDOW_SIZE;
    
    // 计算平均值
    float sum = 0;
    for (int i = 0; i < FILTER_WINDOW_SIZE; i++) {
        sum += history[i];
    }
    
    return sum / FILTER_WINDOW_SIZE;
}

// 光电转换后：读取WDM耦合监测数据
void wdm_coupling_read(WDM_Coupling_Data *data)
{
    // ==============================
    // 从AD采集数据
    // 1030nm泵浦 → 耦合 → 1分4输出
    // ==============================
    
    // 读取AD值并转换为电压
    float input_voltage = (ad_read(0) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out1_voltage = (ad_read(1) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out2_voltage = (ad_read(2) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out3_voltage = (ad_read(3) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    float out4_voltage = (ad_read(4) * REFERENCE_VOLTAGE) / AD_MAX_VALUE;
    
    // 转换为光功率
    float input_power = voltage_to_power(input_voltage);
    float out1_power = voltage_to_power(out1_voltage);
    float out2_power = voltage_to_power(out2_voltage);
    float out3_power = voltage_to_power(out3_voltage);
    float out4_power = voltage_to_power(out4_voltage);
    
    // 应用滤波
    data->input_1030 = moving_average_filter(input_power, input_history);
    data->out1_1030 = moving_average_filter(out1_power, output_history[0]);
    data->out2_1030 = moving_average_filter(out2_power, output_history[1]);
    data->out3_1030 = moving_average_filter(out3_power, output_history[2]);
    data->out4_1030 = moving_average_filter(out4_power, output_history[3]);

    // 计算耦合效率（使用改进的算法）
    float total_output = data->out1_1030 + data->out2_1030
                       + data->out3_1030 + data->out4_1030;
    
    // 考虑损耗补偿的耦合效率计算
    // 假设每路输出有1%的固定损耗
    float loss_compensation = 1.0 / 0.96; // 4路，每路1%损耗
    data->coupling_efficiency = (total_output / data->input_1030) * 100.0f * loss_compensation;
    
    // 计算均匀性
    float average_output = total_output / 4.0;
    float max_deviation = 0;
    float outputs[4] = {data->out1_1030, data->out2_1030, data->out3_1030, data->out4_1030};
    
    for (int i = 0; i < 4; i++) {
        float deviation = fabs(outputs[i] - average_output);
        if (deviation > max_deviation) {
            max_deviation = deviation;
        }
    }
    
    data->uniformity = (1.0 - (max_deviation / average_output)) * 100.0f;

    // 告警：耦合效率 < 90% 或均匀性 < 85% 报警
    data->alarm = (data->coupling_efficiency < 90.0 || data->uniformity < 85.0) ? 1 : 0;
}

// 打包成TCP发送字符串
void wdm_coupling_pack(WDM_Coupling_Data *data, char *buf, int *len)
{
    sprintf(buf,
            "WDM_COUPLING:"
            "IN=%.1f,"
            "OUT1=%.1f,OUT2=%.1f,OUT3=%.1f,OUT4=%.1f,"
            "EFF=%.1f%%,"
            "UNIF=%.1f%%,"
            "ALARM=%d",

            data->input_1030,
            data->out1_1030, data->out2_1030,
            data->out3_1030, data->out4_1030,
            data->coupling_efficiency,
            data->uniformity,
            data->alarm);

    *len = strlen(buf);
}