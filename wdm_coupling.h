#ifndef WDM_COUPLING_H
#define WDM_COUPLING_H

// 完全对应你的WDM结构：4路1310 + 1路1030 → 耦合 → 4路1030输出
typedef struct {
    // 输入
    float input_1030;       // 1030nm 泵浦光输入功率

    // 输出（4路1030）
    float out1_1030;
    float out2_1030;
    float out3_1030;
    float out4_1030;

    // 你最关心的耦合指标
    float coupling_efficiency;  // 耦合效率（%）
    float uniformity;           // 4路均匀性
    int alarm;                  // 告警（0=正常 1=异常）
} WDM_Coupling_Data;

// 读取WDM耦合数据（光电转换后的数据）
void wdm_coupling_read(WDM_Coupling_Data *data);

// 打包成TCP发送数据
void wdm_coupling_pack(WDM_Coupling_Data *data, char *buf, int *len);

#endif