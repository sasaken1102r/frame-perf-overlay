// /proc と /sys からの読み取り（読むだけ。書き込みは一切しない）。
#pragma once

#include "sensor_math.h"
#include "wifi_link.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

/** 値が取れなかったことを表す NaN。 */
constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();

/**
 * 消費電力センサーの 1 チャンネル分（max34417 の power*_input）。
 */
struct PowerChannel {
    std::string chip;    ///< hwmon の name（max34417_10 など）
    std::string label;   ///< power*_label（vph、apc0 など）
    double watts = kNoValue;
    double volts = kNoValue;  ///< in*_input with the same label, mV → V (NaN if the chip has none)
};

/** One cpufreq policy (a cluster of cores sharing a clock). */
struct ClusterClock {
    std::string policy;          ///< "policy0", ...
    double curMHz = kNoValue;
    double maxMHz = kNoValue;    ///< scaling_max_freq: lower than the hardware max under a Playtime cap
    double hwMaxMHz = kNoValue;  ///< cpuinfo_max_freq, the hardware max (read once)
};

/**
 * 1 回の読み取り結果。取れなかった値は kNoValue（NaN）。
 */
struct SensorSample {
    // CPU
    double cpuUsagePct = kNoValue;     ///< 全体の使用率
    double cpuMaxCorePct = kNoValue;   ///< 最も忙しいコアの使用率
    double cpuFreqMHz = kNoValue;      ///< いちばん速いクラスタの現在クロック
    double cpuBigFreqMHz = kNoValue;   ///< cpu7（大コア）の現在クロック
    // GPU
    double gpuFreqMHz = kNoValue;
    double gpuMaxFreqMHz = kNoValue;
    // 温度（℃）
    double cpuTempC = kNoValue;        ///< cpu*-thermal / cpuss*-thermal の最大
    double gpuTempC = kNoValue;        ///< gpuss-*-thermal の最大
    double ddrTempC = kNoValue;
    double batteryTempC = kNoValue;
    double displayTempC = kNoValue;    ///< ディスプレイ（左）付近（iio vadc）
    double exhaustTempC = kNoValue;    ///< ファンの排気付近（iio vadc）
    double heatsinkTempC = kNoValue;   ///< ヒートシンクのフィン付近（iio vadc）
    // 熱による制限（cooling_device の cur_state > 0）
    bool throttleCpu = false;
    bool throttleGpu = false;
    // Steam Link の直通回線
    WifiInfo wifi;
    WifiAdapters adapters;  ///< both interfaces (wifi is the combined link picked from these)
    // 電力（W）
    double powerMainW = kNoValue;      ///< 主電源レール（vph）
    double powerRailsW = kNoValue;     ///< vph 以外の全チャンネルの合計（vph から分かれたレールなので vph とは足さない）
    std::vector<PowerChannel> powerChannels;
    // バッテリー
    double batteryPct = kNoValue;
    double batteryVoltageV = kNoValue;
    double batteryCurrentMa = kNoValue;  ///< current_now（符号はドライバのまま）
    std::string batteryStatus;           ///< Charging / Discharging / Not charging / Full
    bool chargerOnline = false;
    bool haveBattery = false;            ///< the battery's power_supply was found (else its values are "not found")
    bool haveCharger = false;            ///< the charger's power_supply was found
    // その他
    double fanRpm = kNoValue;          ///< 回転数（fan1_input / 2）
    double memUsedGiB = kNoValue;
    double memTotalGiB = kNoValue;

    // The full (non-compact) rows and the new widgets. Strings are empty and ints -1 when unknown.
    std::vector<double> corePct;          ///< load per core, index = core number (NaN until the second read)
    std::vector<ClusterClock> clusters;   ///< per cpufreq policy, ascending
    double powerUnmeasuredW = kNoValue;   ///< vph − the measured rails (N/A if any rail failed to read)
    double npuTempC = kNoValue;           ///< nsph* zones, max
    double cameraTempC = kNoValue;        ///< camera* zones, max
    double videoTempC = kNoValue;
    double wifiTempC = kNoValue;          ///< vadc board spots
    double flashTempC = kNoValue;
    double nrfTempC = kNoValue;
    double connectorTempC = kNoValue;
    bool haveSource = false;              ///< the USB-C source power_supply (tcpm-source-psy-*)
    double batteryAvgCurrentMa = kNoValue;  ///< current_avg, + = into the battery
    double chargeNowMah = kNoValue;
    double chargeFullMah = kNoValue;
    double chargeDesignMah = kNoValue;
    std::string batteryHealth;
    long long batteryCycles = -1;
    std::string chargerStatusDetailed;    ///< raw status_detailed text
    int chargingEnable = -1;
    int chargingPause = -1;
    double setCurrentA = kNoValue;        ///< constant_charge_current
    double setCurrentMaxA = kNoValue;     ///< constant_charge_current_max
    bool sourceOnline = false;
    std::string usbType;                  ///< raw usb_type ("[PD] C PD_PPS")
    double sourceV = kNoValue;            ///< voltage_now of the USB-C source: the PD contract voltage
    double sourceAMax = kNoValue;         ///< current_max: the PD contract current
    double usbInV = kNoValue;             ///< vadc usb_sns (what arrives), only read while plugged in
    double usbInA = kNoValue;             ///< vadc iin_fb, only read while plugged in
    double uptimeS = kNoValue;            ///< /proc/uptime, includes suspend
    double selfCpuPct = kNoValue;         ///< own CPU, % of one core, since the previous readSelfCost()
    double selfRssMiB = kNoValue;
    double selfPowerW = kNoValue;         ///< estimate: own share of busy CPU × CPU rails (apc0-2)
};

/**
 * センサーの場所を起動時に一度だけ探し、以後は開いたままのファイルを読み直すクラス。
 */
class Sensors {
public:
    Sensors() = default;
    ~Sensors();
    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    /**
     * hwmon・thermal_zone・power_supply などを走査して、読むファイルを開いておく。
     * 走査はここだけ（起動時の 1 回）。
     * @param root 前に付けるフォルダ（テストの作り物の /sys・/proc 用。ふだんは空）
     */
    void discover(const std::string& root = "");

    // One reader per scheduler source (core/metric_registry.h SourceId). Each re-reads its open files and writes
    // only its own fields of the sample. How often they run is the scheduler's business (the intervals are in
    // core/metric_registry.cpp): power and the wireless link every 2 s, the battery and the iio temperatures every
    // 5 s, the rest every update

    /** /proc/stat → cpuUsagePct, cpuMaxCorePct, corePct (a delta: NaN on the first read after resetCpuBaseline()). */
    void readCpuUsage(SensorSample& sample);
    /** Forgets the /proc/stat baseline, so the next readCpuUsage() starts a fresh delta. */
    void resetCpuBaseline();
    /** cpufreq → cpuFreqMHz (fastest cluster), cpuBigFreqMHz, clusters. */
    void readCpuClocks(SensorSample& sample);
    /** devfreq → gpuFreqMHz, gpuMaxFreqMHz. */
    void readGpuClock(SensorSample& sample);
    /** /proc/meminfo → memUsedGiB, memTotalGiB. */
    void readMemory(SensorSample& sample);
    /** thermal_zone → cpuTempC, gpuTempC, ddrTempC, npuTempC, cameraTempC, videoTempC. */
    void readThermal(SensorSample& sample);
    /** iio vadc → displayTempC, exhaustTempC, heatsinkTempC, the Wi-Fi / flash / nRF / connector spots. */
    void readIioTemps(SensorSample& sample);
    /** cooling_device → throttleCpu, throttleGpu. */
    void readThrottle(SensorSample& sample);
    /** hwmon fan1_input → fanRpm. */
    void readFan(SensorSample& sample);
    /** max34417 → powerMainW, powerRailsW, powerUnmeasuredW, powerChannels (watts + volts). */
    void readPower(SensorSample& sample);
    /** power_supply battery + charger + USB-C source, vadc USB input → the battery / charger / source fields. */
    void readBattery(SensorSample& sample);
    /** /proc/uptime → uptimeS. */
    void readUptime(SensorSample& sample);
    /**
     * Own cost → selfCpuPct, selfRssMiB, selfPowerW. CPU and power are a delta since the previous call: NaN on the
     * first call after resetSelfBaseline().
     * @param now monotonic seconds
     */
    void readSelfCost(SensorSample& sample, double now);
    /** Forgets the self-cost baseline. */
    void resetSelfBaseline();
    /**
     * nl80211 + statistics → wifi, adapters (rates are a delta since the previous call).
     * @param now monotonic seconds
     */
    void readWifi(SensorSample& sample, double now);
    /** Starts fresh byte-counter deltas (after a pause): the next readWifi gives no rates. */
    void resetWifiCounters();

    /**
     * 見つかったセンサーの一覧（--print 用）。
     * @return 人が読むための複数行の文字列
     */
    std::string describe() const;

private:
    friend struct SensorsTestAccess;

    /** 開いたままにする読み取り専用ファイル。 */
    struct Source {
        std::string path;
        std::string name;  ///< 表示・分類用の名前
        int fd = -1;
    };

    std::vector<Source> cpuTemps_;
    std::vector<Source> gpuTemps_;
    Source ddrTemp_;
    std::vector<Source> cpuFreqs_;     ///< policy*/scaling_cur_freq
    Source cpuBigFreq_;
    Source gpuFreq_;
    double gpuMaxFreqMHz_ = kNoValue;
    std::vector<Source> powers_;       ///< name = "chip/label"
    Source fan_;
    Source batCapacity_, batVoltage_, batCurrent_, batTemp_, batStatus_;
    Source chargerOnline_;
    Source procStat_;
    Source memInfo_;
    Source displayTemp_, exhaustTemp_, heatsinkTemp_;  ///< iio の温度（m℃）
    std::vector<Source> cpuCooling_;                   ///< cooling_device（cpufreq-cpu*）の cur_state
    std::vector<Source> gpuCooling_;                   ///< cooling_device（devfreq-*gpu）の cur_state
    WifiLink wifiLink_;

    sensor_math::CpuTicks prevTicks_;  ///< /proc/stat's last read, keyed by cpuN id (empty = no baseline)

    // The full (non-compact) rows' files
    std::vector<Source> powerVolts_;    ///< in*_input matching powers_[i] by label (fd -1 if none)
    std::vector<Source> cpuMaxFreqs_;   ///< policy*/scaling_max_freq, same index as cpuFreqs_
    std::vector<double> cpuHwMaxMHz_;   ///< policy*/cpuinfo_max_freq, read once at discover()
    std::vector<Source> npuTemps_, cameraTemps_, videoTemps_;
    Source wifiTemp_, flashTemp_, nrfTemp_, connectorTemp_;
    Source batCurrentAvg_, batChargeNow_, batChargeFull_, batChargeDesign_, batHealth_, batCycles_;
    Source chgStatusDetailed_, chgEnable_, chgPause_, chgCc_, chgCcMax_;
    Source srcOnline_, srcUsbType_, srcVoltage_, srcCurrentMax_;
    Source usbInV_, usbInA_;
    Source uptime_, selfStatm_;
    std::vector<size_t> cpuRailIdx_;    ///< indexes into powers_ of apc0-2 (the CPU rails), for the self-cost estimate
    bool haveSelfPrev_ = false;
    double prevSelfCpuSec_ = 0.0;
    double prevSelfAt_ = 0.0;
    uint64_t prevSysBusy_ = 0;

    /**
     * ファイルを読み取り専用で開いて Source を作る。
     * @param path パス
     * @param name 名前
     * @return 開けなかったときは fd = -1 の Source
     */
    static Source openSource(const std::string& path, const std::string& name);

    /**
     * 開いてあるファイルを先頭から読み直す。
     * @param source 読むファイル
     * @param buffer 書き込み先
     * @param size buffer の大きさ
     * @return 読んだバイト数。失敗時は -1
     */
    static long readRaw(const Source& source, char* buffer, size_t size);

    /**
     * 整数ひとつのファイルを読む。
     * @param source 読むファイル
     * @param value 書き込み先
     * @return 読めたら true
     */
    static bool readLong(const Source& source, long long& value);

    /**
     * 1 行の文字列ファイルを読む（末尾の改行は除く）。
     * @param source 読むファイル
     * @return 読んだ文字列。失敗時は空
     */
    static std::string readText(const Source& source);

    /**
     * 温度ファイル群の最大値を℃で返す（m℃ 単位を想定）。
     * @param sources 温度ファイル群
     * @return 最大値。ひとつも読めなければ NaN
     */
    static double maxTempC(const std::vector<Source>& sources);

    /**
     * cooling_device 群のどれかが制限中（cur_state > 0）か。
     * @param sources cur_state のファイル群
     * @return 1 つでも 0 より大きければ true
     */
    static bool anyCooling(const std::vector<Source>& sources);

    /** 開いたファイルをすべて閉じる。 */
    void closeAll();
};
