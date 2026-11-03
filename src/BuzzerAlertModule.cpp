#include "BuzzerAlertModule.h"
#include "ModuleVersionCheck.h"

BuzzerAlertModule openknxBuzzerAlertModule;

/* ===========================================================================
 *  报警节奏表
 *
 *  一个周期 = beeps 次 (on + off) + pause
 *    on == 0        -> 持续长鸣（不看周期）
 *    beeps = 1, pause = 0 -> 普通的“响/停”循环
 *  改这里的顺序必须同步改 share.xml 的 PT-BuzzerRhythm（ETS 的值就是索引）。
 * ======================================================================== */
static const BuzzerAlertRhythm cRhythms[] = {
    {"持续长鸣", 0, 0, 1, 0},           //                      值 0
    {"慢速断续", 500, 500, 1, 0},       // 0.5 s 响 / 0.5 s 停   值 1
    {"中速断续", 250, 250, 1, 0},       // 0.25 / 0.25           值 2
    {"快速断续", 125, 125, 1, 0},       // 0.125 / 0.125         值 3
    {"双响", 150, 150, 2, 1000},        // 两声 + 停 1 s         值 4
    {"三响", 150, 150, 3, 1000},        // 三声 + 停 1 s         值 5
    {"长鸣 2s/2s", 2000, 0, 1, 2000},   // 响 2 s / 停 2 s       值 6
    {"短响每 10s", 300, 0, 1, 9700},    // 响 0.3 s / 停 9.7 s   值 7
};
static const uint8_t cRhythmCount = sizeof(cRhythms) / sizeof(cRhythms[0]);

// 故障点名称（顺序 = BuzzerAlertSource，日志/控制台用）
static const char *const cSourceNames[BuzzerAlert_SourceCount] = {
    "总线 SC8815 无应答", "总线 INA238 无应答", "总线温度传感器无应答", "总线输出短路",
    "总线芯片过温",       "总线输出过流",       "总线输入过压",         "总线输入欠压",
    "辅助 SC8815 无应答", "辅助 INA238 无应答", "辅助温度传感器无应答", "辅助输出短路",
    "辅助芯片过温",       "辅助输出过流",       "辅助输入过压",         "辅助输入欠压",
};

BuzzerAlertModule::BuzzerAlertModule()
{
}

const std::string BuzzerAlertModule::name()
{
    return "BuzzerAlert";
}

const std::string BuzzerAlertModule::version()
{
    return MODULE_BuzzerAlert_Version;
}

void BuzzerAlertModule::setup(bool configured)
{
    // 无源蜂鸣器：占空比先给 0（静态低，不发声），确认引脚已由 PWM 接管
    analogWrite(OPENKNX_BPS_BUZZER_ALERT_PIN, 0);

    applyParameters(configured);

    logInfoP("Buzzer: %s, %u Hz, %s (per-fault rhythms)", _enabled ? "enabled" : "disabled", _frequencyHz,
             _muted ? "muted" : "unmuted");
}

void BuzzerAlertModule::applyParameters(bool configured)
{
    if (!configured)
    {
        // 没下载过 ETS 参数：保持静音，只留控制台调试
        _enabled = false;
        _frequencyHz = 3000;
        _enables = 0;
        for (uint8_t i = 0; i < BuzzerAlert_SourceCount; i++)
            _rhythms[i] = 1; // 慢速断续
        return;
    }

    _enabled = ParamBUZ_Enabled;
    _frequencyHz = (uint16_t)ParamBUZ_Frequency * 100; // ETS 以 100 Hz 为单位（15..50）

    // 16 个故障点使能 -> 位掩码（宏不可索引，只能逐项列出）
    uint16_t enables = 0;
    if (ParamBUZ_BusSC8815NoAnswer) enables |= 1u << BuzzerAlert_BusSC8815NoAnswer;
    if (ParamBUZ_BusINANoAnswer) enables |= 1u << BuzzerAlert_BusINANoAnswer;
    if (ParamBUZ_BusTempNoAnswer) enables |= 1u << BuzzerAlert_BusTempNoAnswer;
    if (ParamBUZ_BusVbusShort) enables |= 1u << BuzzerAlert_BusVbusShort;
    if (ParamBUZ_BusOTP) enables |= 1u << BuzzerAlert_BusOTP;
    if (ParamBUZ_BusOverCurrent) enables |= 1u << BuzzerAlert_BusOverCurrent;
    if (ParamBUZ_BusInputHigh) enables |= 1u << BuzzerAlert_BusInputHigh;
    if (ParamBUZ_BusInputLow) enables |= 1u << BuzzerAlert_BusInputLow;
    if (ParamBUZ_AuxSC8815NoAnswer) enables |= 1u << BuzzerAlert_AuxSC8815NoAnswer;
    if (ParamBUZ_AuxINANoAnswer) enables |= 1u << BuzzerAlert_AuxINANoAnswer;
    if (ParamBUZ_AuxTempNoAnswer) enables |= 1u << BuzzerAlert_AuxTempNoAnswer;
    if (ParamBUZ_AuxVbusShort) enables |= 1u << BuzzerAlert_AuxVbusShort;
    if (ParamBUZ_AuxOTP) enables |= 1u << BuzzerAlert_AuxOTP;
    if (ParamBUZ_AuxOverCurrent) enables |= 1u << BuzzerAlert_AuxOverCurrent;
    if (ParamBUZ_AuxInputHigh) enables |= 1u << BuzzerAlert_AuxInputHigh;
    if (ParamBUZ_AuxInputLow) enables |= 1u << BuzzerAlert_AuxInputLow;
    _enables = enables;

    // 每个故障点各自的报警节奏（宏不可索引，只能逐项列出）
    _rhythms[BuzzerAlert_BusSC8815NoAnswer] = (uint8_t)ParamBUZ_BusSC8815NoAnswerRhythm;
    _rhythms[BuzzerAlert_BusINANoAnswer] = (uint8_t)ParamBUZ_BusINANoAnswerRhythm;
    _rhythms[BuzzerAlert_BusTempNoAnswer] = (uint8_t)ParamBUZ_BusTempNoAnswerRhythm;
    _rhythms[BuzzerAlert_BusVbusShort] = (uint8_t)ParamBUZ_BusVbusShortRhythm;
    _rhythms[BuzzerAlert_BusOTP] = (uint8_t)ParamBUZ_BusOTPRhythm;
    _rhythms[BuzzerAlert_BusOverCurrent] = (uint8_t)ParamBUZ_BusOverCurrentRhythm;
    _rhythms[BuzzerAlert_BusInputHigh] = (uint8_t)ParamBUZ_BusInputHighRhythm;
    _rhythms[BuzzerAlert_BusInputLow] = (uint8_t)ParamBUZ_BusInputLowRhythm;
    _rhythms[BuzzerAlert_AuxSC8815NoAnswer] = (uint8_t)ParamBUZ_AuxSC8815NoAnswerRhythm;
    _rhythms[BuzzerAlert_AuxINANoAnswer] = (uint8_t)ParamBUZ_AuxINANoAnswerRhythm;
    _rhythms[BuzzerAlert_AuxTempNoAnswer] = (uint8_t)ParamBUZ_AuxTempNoAnswerRhythm;
    _rhythms[BuzzerAlert_AuxVbusShort] = (uint8_t)ParamBUZ_AuxVbusShortRhythm;
    _rhythms[BuzzerAlert_AuxOTP] = (uint8_t)ParamBUZ_AuxOTPRhythm;
    _rhythms[BuzzerAlert_AuxOverCurrent] = (uint8_t)ParamBUZ_AuxOverCurrentRhythm;
    _rhythms[BuzzerAlert_AuxInputHigh] = (uint8_t)ParamBUZ_AuxInputHighRhythm;
    _rhythms[BuzzerAlert_AuxInputLow] = (uint8_t)ParamBUZ_AuxInputLowRhythm;

    // 防御：ETS 数据损坏 / 版本不匹配时回退到“慢速断续”
    for (uint8_t i = 0; i < BuzzerAlert_SourceCount; i++)
        if (_rhythms[i] >= cRhythmCount)
            _rhythms[i] = 1;

    _cycleStart = delayTimerInit();
    _activeRhythm = 0xFF; // 下一个周期重新开始
}

/**
 * @brief 当前使用的节奏
 *
 * 同时有多个“已启用且有故障”的点时，取枚举顺序最靠前的那个（总线侧优先，
 * 同侧按 BpsFault 位序）——每个故障点可以配不同节奏，现场听声就能分辨。
 */
uint8_t BuzzerAlertModule::activeRhythm() const
{
    const uint16_t mask = activeMask();
    for (uint8_t i = 0; i < BuzzerAlert_SourceCount; i++)
    {
        if (mask & ((uint16_t)1 << i))
            return _rhythms[i];
    }
    return 0;
}

void BuzzerAlertModule::loop(bool configured)
{
    const uint32_t now = delayTimerInit();

    updateBuzzer(now);

    // 报警状态变化时打日志（便于串口分析）
    const bool alarming = _enabled && (activeMask() != 0);
    if (alarming != _lastPrintedAlarm)
    {
        _lastPrintedAlarm = alarming;
        if (alarming)
            logInfoP("%u fault(s) active (mask 0x%04X) - buzzer %s", __builtin_popcount(activeMask()), activeMask(),
                     _muted ? "muted" : "on");
        else
            logInfoP("no active fault - buzzer off");
    }
}

void BuzzerAlertModule::processInputKo(GroupObject &ko)
{
    if (ko.asap() == BUZ_KoMute)
    {
        mute((bool)ko.value(DPT_Switch));
        logInfoP("mute by group object: %s", _muted ? "on" : "off");
        return;
    }

    logDebugP("processInputKo asap %u", ko.asap());
}

/* ===========================================================================
 *  蜂鸣器驱动
 * ======================================================================== */

/**
 * @brief 蜂鸣器开/关
 *
 * 无源蜂鸣器需要外部给方波，这里用 RP2040 的 PWM（analogWriteFreq + analogWrite）：
 *   开 -> 设定频率 + 50% 占空比；
 *   关 -> 占空比 0（静态低），不发声也不耗电。
 * 只在状态变化时写寄存器，避免每轮 loop 重复配置 PWM。
 */
void BuzzerAlertModule::buzzerOn(bool on)
{
    if (on == _buzzerOn)
        return;

    _buzzerOn = on;

    if (on)
    {
        analogWriteFreq(_frequencyHz);
        analogWrite(OPENKNX_BPS_BUZZER_ALERT_PIN, 128); // 50%
    }
    else
    {
        analogWrite(OPENKNX_BPS_BUZZER_ALERT_PIN, 0); // 静态低
    }
}

void BuzzerAlertModule::updateBuzzer(uint32_t now)
{
    // ---- 控制台试响：优先且与故障无关 ----
    if (_testActive)
    {
        if (delayCheckMillis(_testStart, _testDurationMS))
        {
            _testActive = false;
            buzzerOn(false);
            return;
        }

        buzzerOn(true);
        return;
    }

    // ---- 正常报警：总开关 + 未静音 + 至少一个“已启用且有故障”的点 ----
    if (!_enabled || _muted || activeMask() == 0)
    {
        buzzerOn(false);
        _activeRhythm = 0xFF;
        _cycleStart = now; // 下次报警从“响”开始
        return;
    }

    // 取“优先故障点”的节奏；节奏变了就从“响”重新开始一个周期
    const uint8_t rhythmIndex = activeRhythm();
    if (rhythmIndex != _activeRhythm)
    {
        _activeRhythm = rhythmIndex;
        _cycleStart = now;
        logDebugP("Buzzer: rhythm -> %s (active mask 0x%04X)", cRhythms[rhythmIndex].name, activeMask());
    }

    const BuzzerAlertRhythm &rhythm = cRhythms[rhythmIndex];

    bool on;
    if (rhythm.onMS == 0)
    {
        on = true; // 持续长鸣
    }
    else
    {
        const uint32_t onOff = (uint32_t)rhythm.onMS + rhythm.offMS;
        const uint32_t cycle = onOff * rhythm.beeps + rhythm.pauseMS;
        const uint32_t phase = (now - _cycleStart) % cycle;

        on = (phase < onOff * rhythm.beeps) && ((phase % onOff) < rhythm.onMS);
    }

    buzzerOn(on);
}

/* ===========================================================================
 *  对外 API
 * ======================================================================== */

void BuzzerAlertModule::setFaults(uint16_t mask)
{
    _faultMask = mask;
}

void BuzzerAlertModule::setFault(BuzzerAlertSource source, bool active)
{
    if (source >= BuzzerAlert_SourceCount)
        return;

    if (active)
        _faultMask |= (uint16_t)1 << source;
    else
        _faultMask &= ~((uint16_t)1 << source);
}

bool BuzzerAlertModule::isAlarming() const
{
    return _enabled && !_muted && (activeMask() != 0);
}

void BuzzerAlertModule::mute(bool on)
{
    _muted = on;
}

void BuzzerAlertModule::beep(uint16_t durationMS)
{
    _testActive = true;
    _testStart = delayTimerInit();
    _testDurationMS = durationMS;
}

/* ===========================================================================
 *  输出/控制台
 * ======================================================================== */

void BuzzerAlertModule::showInformations()
{
    logInfoP("Buzzer: %s, %u Hz, %s, %s", _enabled ? "enabled" : "disabled", _frequencyHz,
             _muted ? "muted" : "unmuted", _buzzerOn ? "sounding" : "silent");
    logInfoP("Buzzer: faults 0x%04X, enabled 0x%04X -> active 0x%04X", _faultMask, _enables, activeMask());

    // 逐个列出“已启用且当前存在的故障点”及其自己的节奏，便于现场定位
    for (uint8_t i = 0; i < BuzzerAlert_SourceCount; i++)
    {
        if (activeMask() & ((uint16_t)1 << i))
            logInfoP("Buzzer: alarm source %u: %s (rhythm %s)", i, cSourceNames[i], cRhythms[_rhythms[i]].name);
    }
}

void BuzzerAlertModule::showHelp()
{
    openknx.console.printHelpLine("buz", "显示蜂鸣器报警模块的帮助");
    openknx.console.printHelpLine("buz info", "显示蜂鸣器状态与当前报警的故障点");
    openknx.console.printHelpLine("buz test", "试响 1 秒（验证蜂鸣器与频率参数）");
    openknx.console.printHelpLine("buz mute on|off", "静音 / 恢复报警声（不带参数则切换）");
}

bool BuzzerAlertModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    if (cmd.compare(0, 3, "buz") != 0)
        return false;

    if (cmd.length() == 3)
    {
        showHelp();
        return true;
    }

    const std::string arguments = cmd.substr(4);

    if (arguments == "info" || arguments == "status")
    {
        if (diagnoseKo)
            openknx.console.writeDiagenoseKo("Buzzer: %s, %u Hz, rhythm %s, %s, faults 0x%04X, active 0x%04X",
                                            _enabled ? "enabled" : "disabled", _frequencyHz,
                                            cRhythms[activeRhythm()].name, _muted ? "muted" : "unmuted", _faultMask,
                                            activeMask());
        else
            showInformations();

        return true;
    }

    if (arguments == "test")
    {
        beep(1000);
        logInfoP("Buzzer: test beep 1 s at %u Hz", _frequencyHz);
        return true;
    }

    if (arguments.rfind("mute", 0) == 0)
    {
        if (arguments == "mute on")
            mute(true);
        else if (arguments == "mute off")
            mute(false);
        else
            mute(!_muted);

        logInfoP("Buzzer: mute %s", _muted ? "on" : "off");
        return true;
    }

    logInfoP("buz: bad arguments");
    if (diagnoseKo)
        openknx.console.writeDiagenoseKo("buz: bad arguments");

    showHelp();
    return true;
}
