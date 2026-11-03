#pragma once

#include "OpenKNX.h"
#include "hardware.h"
#include "knxprod.h"

/* ============================================================================
 *  ETS 模块 "蜂鸣器报警" (BuzzerAlert)
 * ----------------------------------------------------------------------------
 *  无源蜂鸣器 + 电源系统故障报警：
 *
 *    - 控制引脚 : OPENKNX_BPS_BUZZER_ALERT_PIN（无源蜂鸣器 -> 固件产生方波）
 *    - 频率可调 : ETS “报警频率” 1.5 / 2.0 ... 5.0 kHz（无源蜂鸣器需对方波，
 *                 不同型号谐振点不同，靠这个参数选到最响的点）
 *    - 节奏可调 : **每个故障点各自** 在 ETS 里选节奏（持续 / 中断 / 双响 / 三响 /
 *                 长鸣-停 / 短响每 10 s），不再有全局“报警节奏”
 *    - 故障点独立开关 : ETS 里 16 个故障点（总线侧 8 + 辅助侧 8）各自可以启用/关闭
 *
 *  多个故障同时存在时，使用**优先故障点**（枚举顺序最靠前，即总线侧优先）的节奏；
 *  节奏切换时从“响”重新开始一个周期。
 *
 *  故障状态由电源模块推送（setFaults()），本模块不反向依赖 SC8815 驱动，
 *  因此是一个独立的 OpenKNX 模块：任何模块只要按下面的位序推故障位就能用它。
 *  没有电源模块时也可以在控制台用 `buz test` 验证硬件。
 * ========================================================================== */

/// 供别的模块用 __has_include 判断本模块是否存在
#define OPENKNX_BUZZER_ALERT_AVAILABLE 1

/**
 * @brief 报警源编号（**顺序必须与 share.xml 里 16 个使能参数一一对应**）
 *
 * 与电源模块的故障代码位定义对齐：
 *   0..7  = 总线侧 BpsFault 的 bit0..bit7（SC8815 无应答 / INA238 无应答 /
 *           温度传感器无应答 / 输出短路 / 过温 / 过流 / 输入过压 / 输入欠压）
 *   8..15 = 辅助侧同样的 8 位
 */
enum BuzzerAlertSource : uint8_t
{
    BuzzerAlert_BusSC8815NoAnswer = 0,
    BuzzerAlert_BusINANoAnswer,
    BuzzerAlert_BusTempNoAnswer,
    BuzzerAlert_BusVbusShort,
    BuzzerAlert_BusOTP,
    BuzzerAlert_BusOverCurrent,
    BuzzerAlert_BusInputHigh,
    BuzzerAlert_BusInputLow,
    BuzzerAlert_AuxSC8815NoAnswer,
    BuzzerAlert_AuxINANoAnswer,
    BuzzerAlert_AuxTempNoAnswer,
    BuzzerAlert_AuxVbusShort,
    BuzzerAlert_AuxOTP,
    BuzzerAlert_AuxOverCurrent,
    BuzzerAlert_AuxInputHigh,
    BuzzerAlert_AuxInputLow,
    BuzzerAlert_SourceCount
};

/**
 * @brief 报警节奏（索引 = ETS 里每个故障点的“报警节奏”取值，改顺序要同步改 share.xml）
 * @param onMS   单个响声长度；= 0 表示“持续长鸣”
 * @param offMS  一个周期内两次响声之间的停顿时长
 * @param beeps  一个周期内响几声
 * @param pauseMS 一个周期结束后的停顿时长
 */
struct BuzzerAlertRhythm
{
    const char *name;
    uint16_t onMS;
    uint16_t offMS;
    uint8_t beeps;
    uint16_t pauseMS;
};

class BuzzerAlertModule : public OpenKNX::Module
{
  public:
    BuzzerAlertModule();

    const std::string name() override;
    const std::string version() override;

    void setup(bool configured) override;
    void loop(bool configured) override;
    void processInputKo(GroupObject &ko) override;

    bool processCommand(const std::string cmd, bool diagnoseKo) override;
    void showHelp() override;
    void showInformations() override;

    // ---- 对外 API（给电源等模块推送故障）----
    void setFaults(uint16_t mask); ///< bit i = BuzzerAlertSource i，1 = 该故障存在
    void setFault(BuzzerAlertSource source, bool active);
    uint16_t faults() const { return _faultMask; }
    bool isAlarming() const;  ///< 有“已启用且 ETS 使能”的故障
    void mute(bool on);       ///< 手动静音（KO 或控制台）
    bool isMuted() const { return _muted; }
    void beep(uint16_t durationMS); ///< 试响一声（控制台调试用）

  private:
    uint16_t _faultMask = 0;        // 各故障源当前状态（与 ETS 使能无关）
    uint16_t _enables = 0;          // ETS：哪些故障点参与报警
    uint8_t _rhythms[BuzzerAlert_SourceCount] = {}; // ETS：每个故障点各自的节奏索引
    uint16_t _frequencyHz = 3000;   // ETS：报警频率（Hz）
    bool _enabled = false;          // ETS：总开关
    bool _muted = false;            // KO 静音
    bool _buzzerOn = false;         // 蜂鸣器当前是否在发声（避免重复驱动）
    uint32_t _cycleStart = 0;       // 当前节奏周期起点
    uint8_t _activeRhythm = 0;      // 当前正在使用的节奏（0xFF = 无）
    bool _testActive = false;       // 控制台试响中（与故障无关）
    uint32_t _testStart = 0;
    uint16_t _testDurationMS = 1000;
    bool _lastPrintedAlarm = false; // 报警状态变化时打日志

    void applyParameters(bool configured);
    uint16_t activeMask() const { return _enables & _faultMask; }
    uint8_t activeRhythm() const;   // 当前活动故障中“最靠前”那个点的节奏
    void updateBuzzer(uint32_t now);
    void buzzerOn(bool on);
};

extern BuzzerAlertModule openknxBuzzerAlertModule;
