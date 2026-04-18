#include <stdio.h>
#include <string.h>
#include "wdm_coupling.h"

// 光电转换后：读取WDM耦合监测数据
void wdm_coupling_read(WDM_Coupling_Data *data)
{
    // ==============================
    // 你的WDM结构真实模拟值
    // 1030nm泵浦 → 耦合 → 1分4输出
    // ==============================
    data->input_1030  = 100.0;   // 1030输入光功率

    // 4路1030输出光
    data->out1_1030 = 23.5;
    data->out2_1030 = 23.3;
    data->out3_1030 = 23.6;
    data->out4_1030 = 23.4;

    // 耦合效率（你最关心的指标）
    float total_output = data->out1_1030 + data->out2_1030
                       + data->out3_1030 + data->out4_1030;

    data->coupling_efficiency = (total_output / data->input_1030) * 100.0f;

    // 告警：耦合效率 < 90% 报警（希望尽可能大）
    data->alarm = (data->coupling_efficiency < 90.0) ? 1 : 0;
}

// 打包成TCP发送字符串
void wdm_coupling_pack(WDM_Coupling_Data *data, char *buf, int *len)
{
    sprintf(buf,
            "WDM_COUPLING:"
            "IN=%.1f,"
            "OUT1=%.1f,OUT2=%.1f,OUT3=%.1f,OUT4=%.1f,"
            "EFF=%.1f%%,"
            "ALARM=%d",

            data->input_1030,
            data->out1_1030, data->out2_1030,
            data->out3_1030, data->out4_1030,
            data->coupling_efficiency,
            data->alarm);

    *len = strlen(buf);
}