// /proc と /sys からの読み取りの実装。すべて O_RDONLY で開き、書き込みはしない。
#include "sensors.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

/**
 * ディレクトリの中の名前を並べて返す（. と .. を除く）。
 * @param dir ディレクトリのパス
 * @param prefix この文字列で始まる名前だけにする（空ならすべて）
 * @return 名前の一覧（自然順: hwmon2 < hwmon10）
 */
std::vector<std::string> listDir(const std::string& dir, const std::string& prefix) {
    std::vector<std::string> names;
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return names;
    while (const dirent* entry = ::readdir(d)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (!prefix.empty() && name.compare(0, prefix.size(), prefix) != 0) continue;
        names.push_back(name);
    }
    ::closedir(d);
    // 末尾の数字を数値として比べる（hwmon2 と hwmon10 の順番を正しくする）
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        const size_t ia = a.find_last_not_of("0123456789") + 1;
        const size_t ib = b.find_last_not_of("0123456789") + 1;
        const std::string headA = a.substr(0, ia);
        const std::string headB = b.substr(0, ib);
        if (headA != headB || ia == a.size() || ib == b.size()) return a < b;
        return std::atol(a.c_str() + ia) < std::atol(b.c_str() + ib);
    });
    return names;
}

/**
 * 小さなテキストファイルを 1 回だけ読む（走査時の name / type / label 用）。
 * @param path パス
 * @return 中身（末尾の改行は除く）。読めなければ空
 */
std::string readSmallFile(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return "";
    char buffer[256];
    const ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (n <= 0) return "";
    std::string text(buffer, static_cast<size_t>(n));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

/**
 * ファイルがあるか調べる。
 * @param path パス
 * @return あれば true
 */
bool fileExists(const std::string& path) {
    return ::access(path.c_str(), F_OK) == 0;
}

/**
 * 文字列が指定の文字列で始まるか。
 * @param text 調べる文字列
 * @param prefix 先頭
 * @return 始まっていれば true
 */
bool startsWith(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

/** fan1_input を回転数（rpm）にするための割る数（SteamOS 同梱のファン制御の設定と同じ換算）。 */
constexpr double kFanRpmScale = 2.0;

}  // namespace

Sensors::~Sensors() {
    closeAll();
}

Sensors::Source Sensors::openSource(const std::string& path, const std::string& name) {
    Source source;
    source.path = path;
    source.name = name;
    source.fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    return source;
}

void Sensors::closeAll() {
    const auto closeOne = [](Source& s) {
        if (s.fd >= 0) ::close(s.fd);
        s.fd = -1;
    };
    for (auto* list : {&cpuTemps_, &gpuTemps_, &cpuFreqs_, &powers_}) {
        for (auto& s : *list) closeOne(s);
        list->clear();
    }
    for (auto* list : {&cpuCooling_, &gpuCooling_, &powerVolts_, &cpuMaxFreqs_, &npuTemps_, &cameraTemps_,
                       &videoTemps_}) {
        for (auto& s : *list) closeOne(s);
        list->clear();
    }
    cpuHwMaxMHz_.clear();
    for (Source* s : {&ddrTemp_, &cpuBigFreq_, &gpuFreq_, &fan_, &batCapacity_, &batVoltage_, &batCurrent_,
                      &batTemp_, &batStatus_, &chargerOnline_, &procStat_, &memInfo_, &displayTemp_, &exhaustTemp_,
                      &heatsinkTemp_}) {
        closeOne(*s);
    }
    for (Source* s : {&wifiTemp_, &flashTemp_, &nrfTemp_, &connectorTemp_, &batCurrentAvg_, &batChargeNow_,
                      &batChargeFull_, &batChargeDesign_, &batHealth_, &batCycles_, &chgStatusDetailed_, &chgEnable_,
                      &chgPause_, &chgCc_, &chgCcMax_, &srcOnline_, &srcUsbType_, &srcVoltage_, &srcCurrentMax_,
                      &usbInV_, &usbInA_, &uptime_, &selfStatm_}) {
        closeOne(*s);
    }
    cpuRailIdx_.clear();
}

void Sensors::discover(const std::string& root) {
    closeAll();

    // 温度: thermal_zone の type で分類する（番号は起動ごとに変わりうる）
    const std::string thermalDir = root + "/sys/class/thermal/";
    for (const auto& zone : listDir(thermalDir, "thermal_zone")) {
        const std::string base = thermalDir + zone;
        const std::string type = readSmallFile(base + "/type");
        if (startsWith(type, "cpu")) {
            cpuTemps_.push_back(openSource(base + "/temp", type));
        } else if (startsWith(type, "gpuss")) {
            gpuTemps_.push_back(openSource(base + "/temp", type));
        } else if (startsWith(type, "ddr") && ddrTemp_.fd < 0) {
            ddrTemp_ = openSource(base + "/temp", type);
        } else if (startsWith(type, "nsph")) {
            npuTemps_.push_back(openSource(base + "/temp", type));
        } else if (startsWith(type, "camera")) {
            cameraTemps_.push_back(openSource(base + "/temp", type));
        } else if (startsWith(type, "video")) {
            videoTemps_.push_back(openSource(base + "/temp", type));
        }
    }

    // hwmon: name で探す（消費電力 max34417_*、ファン）
    const std::string hwmonDir = root + "/sys/class/hwmon/";
    struct Channel {
        Source watts;
        Source volts;
    };
    std::vector<Channel> channels;
    for (const auto& hw : listDir(hwmonDir, "hwmon")) {
        const std::string base = hwmonDir + hw;
        const std::string name = readSmallFile(base + "/name");
        if (startsWith(name, "max34417")) {
            // The chip's own voltage sense: in<N>_label carries the same label as its power channel (in0 = power1 =
            // apc0 on max34417_12), so match by label, not by number
            std::vector<std::pair<std::string, std::string>> voltsByLabel;
            for (int ch = 0; ch <= 8; ++ch) {
                const std::string label = readSmallFile(base + "/in" + std::to_string(ch) + "_label");
                if (!label.empty()) voltsByLabel.emplace_back(label, base + "/in" + std::to_string(ch) + "_input");
            }
            for (int ch = 1; ch <= 8; ++ch) {
                const std::string input = base + "/power" + std::to_string(ch) + "_input";
                if (!fileExists(input)) continue;
                std::string label = readSmallFile(base + "/power" + std::to_string(ch) + "_label");
                if (label.empty()) label = "power" + std::to_string(ch);
                if (label == "unused") continue;
                Channel c;
                c.watts = openSource(input, name + "/" + label);
                for (const auto& v : voltsByLabel) {
                    if (v.first == label) {
                        c.volts = openSource(v.second, name + "/" + label);
                        break;
                    }
                }
                channels.push_back(c);
            }
        } else if (fan_.fd < 0 && fileExists(base + "/fan1_input")) {
            fan_ = openSource(base + "/fan1_input", name);
        }
    }
    // チップ名の順に並べる（表示を毎回同じ順にする）
    std::stable_sort(channels.begin(), channels.end(), [](const Channel& a, const Channel& b) {
        return a.watts.name.substr(0, a.watts.name.find('/')) < b.watts.name.substr(0, b.watts.name.find('/'));
    });
    for (const Channel& c : channels) {
        const std::string label = c.watts.name.substr(c.watts.name.find('/') + 1);
        if (label == "apc0" || label == "apc1" || label == "apc2") cpuRailIdx_.push_back(powers_.size());
        powers_.push_back(c.watts);
        powerVolts_.push_back(c.volts);
    }

    // CPU クロック: cpufreq の policy ごと
    const std::string cpufreqDir = root + "/sys/devices/system/cpu/cpufreq/";
    for (const auto& policy : listDir(cpufreqDir, "policy")) {
        cpuFreqs_.push_back(openSource(cpufreqDir + policy + "/scaling_cur_freq", policy));
        cpuMaxFreqs_.push_back(openSource(cpufreqDir + policy + "/scaling_max_freq", policy));
        // The hardware max never changes, so it's read once (the CPU card compares it to scaling_max_freq)
        Source hwMax = openSource(cpufreqDir + policy + "/cpuinfo_max_freq", policy);
        long long khz = 0;
        cpuHwMaxMHz_.push_back(readLong(hwMax, khz) ? static_cast<double>(khz) / 1000.0 : kNoValue);
        if (hwMax.fd >= 0) ::close(hwMax.fd);
    }
    // 大コア（いちばん番号の大きい CPU）
    for (int cpu = 15; cpu >= 0; --cpu) {
        const std::string path =
            root + "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq";
        if (fileExists(path)) {
            cpuBigFreq_ = openSource(path, "cpu" + std::to_string(cpu));
            break;
        }
    }

    // GPU クロック: devfreq の名前に gpu を含むもの
    const std::string devfreqDir = root + "/sys/class/devfreq/";
    for (const auto& dev : listDir(devfreqDir, "")) {
        if (dev.find("gpu") == std::string::npos) continue;
        gpuFreq_ = openSource(devfreqDir + dev + "/cur_freq", dev);
        const std::string maxText = readSmallFile(devfreqDir + dev + "/max_freq");
        if (!maxText.empty()) gpuMaxFreqMHz_ = std::atof(maxText.c_str()) / 1e6;
        break;
    }

    // バッテリーと充電器: power_supply の type / 名前で探す
    const std::string psDir = root + "/sys/class/power_supply/";
    for (const auto& ps : listDir(psDir, "")) {
        const std::string base = psDir + ps;
        const std::string type = readSmallFile(base + "/type");
        if (type == "Battery" && batCapacity_.fd < 0) {
            batCapacity_ = openSource(base + "/capacity", ps);
            batVoltage_ = openSource(base + "/voltage_now", ps);
            batCurrent_ = openSource(base + "/current_now", ps);
            batTemp_ = openSource(base + "/temp", ps);
            batStatus_ = openSource(base + "/status", ps);
            batCurrentAvg_ = openSource(base + "/current_avg", ps);
            batChargeNow_ = openSource(base + "/charge_now", ps);
            batChargeFull_ = openSource(base + "/charge_full", ps);
            batChargeDesign_ = openSource(base + "/charge_full_design", ps);
            batHealth_ = openSource(base + "/health", ps);
            batCycles_ = openSource(base + "/cycle_count", ps);
        } else if (ps.find("charger") != std::string::npos && chargerOnline_.fd < 0) {
            chargerOnline_ = openSource(base + "/online", ps);
            chgStatusDetailed_ = openSource(base + "/status_detailed", ps);
            chgEnable_ = openSource(base + "/charging_enable", ps);
            chgPause_ = openSource(base + "/charging_pause", ps);
            chgCc_ = openSource(base + "/constant_charge_current", ps);
            chgCcMax_ = openSource(base + "/constant_charge_current_max", ps);
        } else if ((startsWith(ps, "tcpm-source-psy") || type == "USB") && srcOnline_.fd < 0) {
            srcOnline_ = openSource(base + "/online", ps);
            srcUsbType_ = openSource(base + "/usb_type", ps);
            srcVoltage_ = openSource(base + "/voltage_now", ps);
            srcCurrentMax_ = openSource(base + "/current_max", ps);
        }
    }

    // iio の温度: name で vadc を探す（device 番号は変わりうる）
    const std::string iioDir = root + "/sys/bus/iio/devices/";
    for (const auto& dev : listDir(iioDir, "iio:device")) {
        const std::string base = iioDir + dev + "/";
        if (readSmallFile(base + "name") != "c400000.spmi:pmic@0:vadc@9000") continue;
        // display_right は -17℃ などおかしな値なので読まない
        displayTemp_ = openSource(base + "in_temp_display_left_therm_input", "display_left");
        exhaustTemp_ = openSource(base + "in_temp_pcba_at_fan_exhaust_input", "fan_exhaust");
        heatsinkTemp_ = openSource(base + "in_temp_pcba_at_heat_sink_fins_input", "heat_sink_fins");
        wifiTemp_ = openSource(base + "in_temp_pcba_at_wifi_input", "wifi");
        flashTemp_ = openSource(base + "in_temp_pcba_at_flash_input", "flash");
        nrfTemp_ = openSource(base + "in_temp_pcba_at_nrf_input", "nrf");
        connectorTemp_ = openSource(base + "in_temp_pcba_at_40pin_connector_input", "40pin_connector");
        // What actually arrives over USB, measured by the charger PMIC's ADC (µV / µA, already scaled). The *_hs
        // twins on vadc@9100 return EINVAL
        usbInV_ = openSource(base + "in_voltage_pm8550b_usb_sns_v_div_16_input", "usb_sns");
        usbInA_ = openSource(base + "in_current_pm8550b_iin_fb_input", "iin_fb");
        break;
    }

    // 熱による制限: cooling_device の type で CPU と GPU を分ける
    const std::string coolingDir = root + "/sys/class/thermal/";
    for (const auto& dev : listDir(coolingDir, "cooling_device")) {
        const std::string base = coolingDir + dev + "/";
        const std::string type = readSmallFile(base + "type");
        if (startsWith(type, "cpufreq-cpu")) {
            cpuCooling_.push_back(openSource(base + "cur_state", type));
        } else if (startsWith(type, "devfreq-") && type.find("gpu") != std::string::npos) {
            gpuCooling_.push_back(openSource(base + "cur_state", type));
        }
    }

    procStat_ = openSource(root + "/proc/stat", "stat");
    memInfo_ = openSource(root + "/proc/meminfo", "meminfo");
    uptime_ = openSource(root + "/proc/uptime", "uptime");
    selfStatm_ = openSource(root + "/proc/self/statm", "statm");
    resetCpuBaseline();
    resetSelfBaseline();
}

void Sensors::resetCpuBaseline() {
    prevTicks_ = sensor_math::CpuTicks();
}

void Sensors::resetSelfBaseline() {
    haveSelfPrev_ = false;
}

long Sensors::readRaw(const Source& source, char* buffer, size_t size) {
    if (source.fd < 0 || size == 0) return -1;
    // sysfs / procfs は先頭（offset 0）から読み直すと最新の値が作り直される
    const ssize_t n = ::pread(source.fd, buffer, size - 1, 0);
    if (n < 0) return -1;
    buffer[n] = '\0';
    return static_cast<long>(n);
}

bool Sensors::readLong(const Source& source, long long& value) {
    char buffer[64];
    if (readRaw(source, buffer, sizeof(buffer)) <= 0) return false;
    char* end = nullptr;
    value = std::strtoll(buffer, &end, 10);
    return end != buffer;
}

std::string Sensors::readText(const Source& source) {
    char buffer[64];
    if (readRaw(source, buffer, sizeof(buffer)) <= 0) return "";
    std::string text(buffer);
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}

double Sensors::maxTempC(const std::vector<Source>& sources) {
    double best = kNoValue;
    for (const auto& s : sources) {
        long long milli = 0;
        if (!readLong(s, milli)) continue;
        const double c = static_cast<double>(milli) / 1000.0;
        if (std::isnan(best) || c > best) best = c;
    }
    return best;
}

void Sensors::readCpuUsage(SensorSample& sample) {
    // cpu 行は先頭にあるので 4KB で足りる（後ろの intr 行などは読まない）
    char buffer[4096];
    sample.corePct.clear();
    sensor_math::CpuTicks current;
    if (readRaw(procStat_, buffer, sizeof(buffer)) <= 0 || !sensor_math::parseProcStat(buffer, current)) return;

    // Cores are keyed by their cpuN id, so an offline core is a NaN in its own slot and the others stay put
    if (!prevTicks_.total.empty()) {
        std::vector<double> pct = sensor_math::cpuPercents(prevTicks_, current);
        // Same cpu lines as last time: a line with no tick between the two reads is 0%, as before (cpuPercents says
        // NaN). When the set changed (a core went offline or came back), it stays NaN
        if (current.present == prevTicks_.present) {
            for (size_t i = 0; i < pct.size(); ++i) {
                if (current.present[i] && current.total[i] == prevTicks_.total[i]) pct[i] = 0.0;
            }
        }
        sample.cpuUsagePct = pct[0];
        double maxCore = kNoValue;
        for (size_t i = 1; i < pct.size(); ++i) {
            sample.corePct.push_back(pct[i]);
            if (!std::isnan(pct[i]) && (std::isnan(maxCore) || pct[i] > maxCore)) maxCore = pct[i];
        }
        sample.cpuMaxCorePct = maxCore;
    }
    prevTicks_ = std::move(current);
}

void Sensors::readMemory(SensorSample& sample) {
    char buffer[2048];
    if (readRaw(memInfo_, buffer, sizeof(buffer)) <= 0) return;
    long long totalKb = -1;
    long long availableKb = -1;
    const char* line = buffer;
    while (line != nullptr && *line != '\0') {
        if (std::strncmp(line, "MemTotal:", 9) == 0) totalKb = std::atoll(line + 9);
        if (std::strncmp(line, "MemAvailable:", 13) == 0) availableKb = std::atoll(line + 13);
        const char* next = std::strchr(line, '\n');
        line = next != nullptr ? next + 1 : nullptr;
    }
    if (totalKb > 0 && availableKb >= 0) {
        sample.memTotalGiB = static_cast<double>(totalKb) / (1024.0 * 1024.0);
        sample.memUsedGiB = static_cast<double>(totalKb - availableKb) / (1024.0 * 1024.0);
    }
}

void Sensors::readPower(SensorSample& sample) {
    sample.powerMainW = kNoValue;
    sample.powerRailsW = kNoValue;
    sample.powerUnmeasuredW = kNoValue;
    sample.powerChannels.clear();
    long long v = 0;
    double rails = 0.0;
    bool anyRail = false;
    std::vector<sensor_math::RailReading> readings;
    for (size_t i = 0; i < powers_.size(); ++i) {
        const Source& s = powers_[i];
        PowerChannel ch;
        const size_t slash = s.name.find('/');
        ch.chip = s.name.substr(0, slash);
        ch.label = s.name.substr(slash + 1);
        if (i < powerVolts_.size() && readLong(powerVolts_[i], v)) ch.volts = static_cast<double>(v) / 1e3;  // mV → V
        if (readLong(s, v)) {
            ch.watts = static_cast<double>(v) / 1e6;  // µW → W
            if (ch.label == "vph") {
                if (std::isnan(sample.powerMainW)) sample.powerMainW = ch.watts;
            } else {
                // vph 以外は vph から分かれたレール。vph と足すと二重に数える
                rails += ch.watts;
                anyRail = true;
            }
        }
        sample.powerChannels.push_back(ch);
        readings.push_back({ch.label, ch.watts, ch.volts});
    }
    if (anyRail) sample.powerRailsW = rails;
    // Strict: one rail that failed to read makes it N/A (a partial sum would inflate it)
    sample.powerUnmeasuredW = sensor_math::powerTree(readings).unmeasuredW;
}

void Sensors::readBattery(SensorSample& sample) {
    long long v = 0;
    sample.batteryPct = readLong(batCapacity_, v) ? static_cast<double>(v) : kNoValue;
    sample.batteryVoltageV = readLong(batVoltage_, v) ? static_cast<double>(v) / 1e6 : kNoValue;     // µV → V
    sample.batteryCurrentMa = readLong(batCurrent_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;  // µA → mA
    sample.batteryTempC = readLong(batTemp_, v) ? static_cast<double>(v) / 10.0 : kNoValue;           // 0.1℃ → ℃
    sample.batteryStatus = readText(batStatus_);
    sample.chargerOnline = readLong(chargerOnline_, v) && v != 0;
    sample.haveBattery = batCapacity_.fd >= 0;
    sample.haveCharger = chargerOnline_.fd >= 0;

    // The full (non-compact) rows: capacity, health, the charger's state and the USB-C source
    const auto scaled = [&v](const Source& s, double divisor) {
        return readLong(s, v) ? static_cast<double>(v) / divisor : kNoValue;
    };
    sample.haveSource = srcOnline_.fd >= 0;
    sample.batteryAvgCurrentMa = scaled(batCurrentAvg_, 1000.0);  // µA → mA, + = into the battery
    sample.chargeNowMah = scaled(batChargeNow_, 1000.0);          // µAh → mAh
    sample.chargeFullMah = scaled(batChargeFull_, 1000.0);
    sample.chargeDesignMah = scaled(batChargeDesign_, 1000.0);
    sample.batteryHealth = readText(batHealth_);
    sample.batteryCycles = readLong(batCycles_, v) ? v : -1;
    // status_detailed is ~25 lines, longer than readText's buffer
    char detailed[1024];
    sample.chargerStatusDetailed = readRaw(chgStatusDetailed_, detailed, sizeof(detailed)) > 0 ? detailed : "";
    sample.chargingEnable = readLong(chgEnable_, v) ? static_cast<int>(v) : -1;
    sample.chargingPause = readLong(chgPause_, v) ? static_cast<int>(v) : -1;
    sample.setCurrentA = scaled(chgCc_, 1e6);  // µA → A
    sample.setCurrentMaxA = scaled(chgCcMax_, 1e6);
    sample.sourceOnline = readLong(srcOnline_, v) && v != 0;
    sample.usbType = readText(srcUsbType_);
    sample.sourceV = scaled(srcVoltage_, 1e6);  // µV → V
    sample.sourceAMax = scaled(srcCurrentMax_, 1e6);
    // Unplugged, these ADC channels read noise (3 mV, −14 mA) and each costs ~1.2 ms of conversion, so they're only
    // read while something is plugged in
    if (sample.chargerOnline || sample.sourceOnline) {
        sample.usbInV = scaled(usbInV_, 1e6);
        sample.usbInA = scaled(usbInA_, 1e6);
    }
}

void Sensors::readIioTemps(SensorSample& sample) {
    long long v = 0;
    sample.displayTempC = readLong(displayTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;  // m℃ → ℃
    sample.exhaustTempC = readLong(exhaustTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
    sample.heatsinkTempC = readLong(heatsinkTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
    sample.wifiTempC = readLong(wifiTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
    sample.flashTempC = readLong(flashTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
    sample.nrfTempC = readLong(nrfTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
    sample.connectorTempC = readLong(connectorTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
}

void Sensors::readUptime(SensorSample& sample) {
    char buffer[128];
    double seconds = 0;
    sample.uptimeS = readRaw(uptime_, buffer, sizeof(buffer)) > 0 && sensor_math::parseUptime(buffer, seconds)
                         ? seconds
                         : kNoValue;
}

void Sensors::readSelfCost(SensorSample& sample, double now) {
    // RSS
    char buffer[256];
    long long pages = 0;
    if (readRaw(selfStatm_, buffer, sizeof(buffer)) > 0 && sensor_math::parseStatmResident(buffer, pages)) {
        static const long pageSize = ::sysconf(_SC_PAGESIZE);
        sample.selfRssMiB = static_cast<double>(pages) * static_cast<double>(pageSize) / (1024.0 * 1024.0);
    }
    // Own CPU time in ns (all threads). /proc/self/stat counts 10 ms ticks: at about 1% that's 2 ticks per 2 s
    // read, far too coarse
    timespec ts {};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return;
    const double ownSec = static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
    // Busy jiffies of the whole system: the first line of /proc/stat
    static const long ticksPerSec = ::sysconf(_SC_CLK_TCK);
    char stat[256];
    sensor_math::CpuTicks ticks;
    const bool haveSys = readRaw(procStat_, stat, sizeof(stat)) > 0 && sensor_math::parseProcStat(stat, ticks);
    const uint64_t sysBusy = haveSys ? ticks.busy[0] : 0;
    // CPU rails right now (they update every 1.5-2 s, about the read interval)
    double railsW = cpuRailIdx_.size() == 3 ? 0.0 : kNoValue;
    long long v = 0;
    for (size_t idx : cpuRailIdx_) {
        if (!readLong(powers_[idx], v)) {
            railsW = kNoValue;
            break;
        }
        railsW += static_cast<double>(v) / 1e6;
    }
    if (haveSelfPrev_ && now > prevSelfAt_) {
        const double ownDelta = ownSec - prevSelfCpuSec_;
        sample.selfCpuPct = 100.0 * ownDelta / (now - prevSelfAt_);
        if (haveSys && ticksPerSec > 0 && sysBusy >= prevSysBusy_) {
            const double sysDelta = static_cast<double>(sysBusy - prevSysBusy_) / static_cast<double>(ticksPerSec);
            sample.selfPowerW = sensor_math::selfWatts(ownDelta, sysDelta, railsW);
        }
    }
    haveSelfPrev_ = true;
    prevSelfCpuSec_ = ownSec;
    prevSelfAt_ = now;
    prevSysBusy_ = haveSys ? sysBusy : UINT64_MAX;  // no system baseline: the next W is N/A
}

bool Sensors::anyCooling(const std::vector<Source>& sources) {
    for (const auto& s : sources) {
        long long state = 0;
        if (readLong(s, state) && state > 0) return true;
    }
    return false;
}

void Sensors::readCpuClocks(SensorSample& sample) {
    long long v = 0;
    sample.cpuFreqMHz = kNoValue;
    sample.clusters.clear();
    for (size_t i = 0; i < cpuFreqs_.size(); ++i) {
        ClusterClock cluster;
        cluster.policy = cpuFreqs_[i].name;
        if (i < cpuMaxFreqs_.size() && readLong(cpuMaxFreqs_[i], v)) cluster.maxMHz = static_cast<double>(v) / 1000.0;
        if (i < cpuHwMaxMHz_.size()) cluster.hwMaxMHz = cpuHwMaxMHz_[i];
        if (readLong(cpuFreqs_[i], v)) {
            const double mhz = static_cast<double>(v) / 1000.0;  // kHz → MHz
            cluster.curMHz = mhz;
            if (std::isnan(sample.cpuFreqMHz) || mhz > sample.cpuFreqMHz) sample.cpuFreqMHz = mhz;
        }
        sample.clusters.push_back(cluster);
    }
    sample.cpuBigFreqMHz = readLong(cpuBigFreq_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
}

void Sensors::readGpuClock(SensorSample& sample) {
    long long v = 0;
    sample.gpuFreqMHz = readLong(gpuFreq_, v) ? static_cast<double>(v) / 1e6 : kNoValue;  // Hz → MHz
    sample.gpuMaxFreqMHz = gpuMaxFreqMHz_;
}

void Sensors::readThermal(SensorSample& sample) {
    long long v = 0;
    sample.cpuTempC = maxTempC(cpuTemps_);
    sample.gpuTempC = maxTempC(gpuTemps_);
    sample.ddrTempC = readLong(ddrTemp_, v) ? static_cast<double>(v) / 1000.0 : kNoValue;
    sample.npuTempC = maxTempC(npuTemps_);
    sample.cameraTempC = maxTempC(cameraTemps_);
    sample.videoTempC = maxTempC(videoTemps_);
}

void Sensors::readThrottle(SensorSample& sample) {
    // 軽い（1 回 0.03ms 程度）
    sample.throttleCpu = anyCooling(cpuCooling_);
    sample.throttleGpu = anyCooling(gpuCooling_);
}

void Sensors::readFan(SensorSample& sample) {
    long long v = 0;
    // fan1_input はタコパルスの周期数で、1 回転 = 2 周期。Valve の SteamOS に入っている
    // ファン制御の設定も 2 で割って回転数にしているので、それに合わせる
    sample.fanRpm = readLong(fan_, v) ? static_cast<double>(v) / kFanRpmScale : kNoValue;
}

void Sensors::readWifi(SensorSample& sample, double now) {
    sample.adapters = wifiLink_.readAdapters(now);
    sample.wifi = combineAdapters(sample.adapters);
}

void Sensors::resetWifiCounters() {
    wifiLink_.resetCounters();
}

std::string Sensors::describe() const {
    std::string out;
    const auto state = [&](const Source& s) { out += s.fd >= 0 ? s.path : "（見つからない）"; };
    const auto count = [&](size_t n, const char* unit) {
        out += std::to_string(n);
        out += unit;
    };
    const auto names = [&](const std::vector<Source>& sources) {
        for (const auto& s : sources) {
            out += ' ';
            out += s.name;
        }
    };
    out += "CPU 温度: ";
    count(cpuTemps_.size(), " 個");
    names(cpuTemps_);
    out += "\nGPU 温度: ";
    count(gpuTemps_.size(), " 個");
    names(gpuTemps_);
    out += "\nDDR 温度: ";
    state(ddrTemp_);
    out += "\nCPU クロック: ";
    count(cpuFreqs_.size(), " policy / 大コア ");
    state(cpuBigFreq_);
    out += "\nGPU クロック: ";
    state(gpuFreq_);
    out += "\n消費電力: ";
    count(powers_.size(), " ch");
    for (const auto& s : powers_) {
        out += "\n  ";
        out += s.name;
        out += " -> ";
        out += s.path;
    }
    out += "\nファン: ";
    state(fan_);
    out += "\nバッテリー: ";
    state(batCapacity_);
    out += "\n充電器: ";
    state(chargerOnline_);
    out += "\niio 温度: ";
    state(displayTemp_);
    out += " / ";
    state(exhaustTemp_);
    out += " / ";
    state(heatsinkTemp_);
    out += "\n熱制限: CPU ";
    count(cpuCooling_.size(), " 個");
    names(cpuCooling_);
    out += " / GPU ";
    count(gpuCooling_.size(), " 個");
    names(gpuCooling_);
    return out;
}
