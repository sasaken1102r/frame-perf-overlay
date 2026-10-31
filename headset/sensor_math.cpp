#include "sensor_math.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sensor_math {

bool parseProcStat(const char* text, CpuTicks& ticks) {
    ticks = CpuTicks();
    if (text == nullptr) return false;
    bool haveTotal = false;
    const char* line = text;
    while (line != nullptr && std::strncmp(line, "cpu", 3) == 0) {
        // "cpu  user nice system idle iowait irq softirq steal guest guest_nice" or "cpu<n> ..."
        const char* p = line + 3;
        size_t index = 0;
        if (*p >= '0' && *p <= '9') {
            char* end = nullptr;
            index = static_cast<size_t>(std::strtoul(p, &end, 10)) + 1;
            p = end;
        }
        uint64_t fields[8] = {};
        int count = 0;
        while (count < 8) {
            char* end = nullptr;
            const unsigned long long v = std::strtoull(p, &end, 10);
            if (end == p) break;
            fields[count++] = v;
            p = end;
        }
        if (count >= 5 && index < 1024) {
            uint64_t all = 0;
            for (int i = 0; i < count; ++i) all += fields[i];  // guest is already inside user, so stop at steal
            const uint64_t idle = fields[3] + fields[4];       // idle + iowait
            if (ticks.total.size() <= index) {
                ticks.total.resize(index + 1, 0);
                ticks.busy.resize(index + 1, 0);
                ticks.present.resize(index + 1, false);
            }
            ticks.busy[index] = all - idle;
            ticks.total[index] = all;
            ticks.present[index] = true;
            if (index == 0) haveTotal = true;
        }
        const char* next = std::strchr(line, '\n');
        line = next != nullptr ? next + 1 : nullptr;
    }
    return haveTotal;
}

std::vector<double> cpuPercents(const CpuTicks& prev, const CpuTicks& cur) {
    std::vector<double> pct(cur.total.size(), kNaN);
    for (size_t i = 0; i < cur.total.size(); ++i) {
        if (!cur.present[i] || i >= prev.total.size() || !prev.present[i]) continue;
        if (cur.total[i] <= prev.total[i] || cur.busy[i] < prev.busy[i]) continue;
        const double dt = static_cast<double>(cur.total[i] - prev.total[i]);
        const double db = static_cast<double>(cur.busy[i] - prev.busy[i]);
        pct[i] = 100.0 * db / dt;
    }
    return pct;
}

PowerTree powerTree(const std::vector<RailReading>& rails) {
    PowerTree tree;
    double sum = 0.0;
    bool anyRail = false;
    bool railFailed = false;
    for (const RailReading& r : rails) {
        if (r.label == "vph") {
            if (std::isnan(tree.vphW)) {
                tree.vphW = r.watts;
                tree.vphV = r.volts;
            }
            continue;
        }
        if (std::isnan(r.watts)) {
            railFailed = true;
            continue;
        }
        sum += r.watts;
        anyRail = true;
    }
    // A partial sum would inflate "unmeasured", so one failed rail makes both N/A
    if (anyRail && !railFailed) tree.railsW = sum;
    if (!std::isnan(tree.vphW) && !std::isnan(tree.railsW)) tree.unmeasuredW = tree.vphW - tree.railsW;
    return tree;
}

bool railVoltsMeaningful(const std::string& label) {
    return label != "gfx";
}

std::map<std::string, int> parseStatusDetailed(const std::string& text) {
    std::map<std::string, int> flags;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        const size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) continue;
        std::string key = line.substr(0, colon);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!key.empty() && key.front() == ' ') key.erase(0, 1);
        const char* value = line.c_str() + colon + 1;
        while (*value == ' ' || *value == '\t') ++value;
        char* after = nullptr;
        const long v = std::strtol(value, &after, 10);
        if (after == value) continue;
        while (*after == ' ' || *after == '\r') ++after;
        if (*after != '\0' || key.empty()) continue;
        flags[key] = static_cast<int>(v);
    }
    return flags;
}

std::string activeUsbType(const std::string& usbType) {
    const size_t open = usbType.find('[');
    const size_t close = open == std::string::npos ? std::string::npos : usbType.find(']', open);
    if (open == std::string::npos || close == std::string::npos) {
        std::string text = usbType;
        while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.pop_back();
        return text;
    }
    return usbType.substr(open + 1, close - open - 1);
}

std::string chargeMode(const ChargeModeInputs& in) {
    if (!in.haveBattery && !in.haveCharger) return "";
    if (!in.plugged) return "Unplugged";
    const auto flag = [&in](const char* key) {
        const auto it = in.flags.find(key);
        return it == in.flags.end() ? 0 : it->second;
    };
    if (flag("Boost") == 1) return "Boost";
    if (in.batteryStatus == "Full") return "Full";
    if (in.chargingPause == 1) return "Paused";
    if (in.chargingEnable == 0) return "Off";
    std::string pills;
    const auto add = [&pills](const char* pill) {
        if (!pills.empty()) pills += "+";
        pills += pill;
    };
    if (flag("CC") == 1) add("CC");
    if (flag("CV") == 1) add("CV");
    if (flag("ICL") == 1) add("ICL");
    if (flag("JEITA") != 0) add("JEITA");
    if (!pills.empty()) return pills;
    return in.batteryStatus == "Charging" ? "Charging" : "Plugged";
}

double timeLeftSeconds(bool toFull, const std::string& status, double amps, double chargeNowMah,
                       double chargeFullMah) {
    if (std::isnan(amps) || std::fabs(amps) < 0.05 || std::isnan(chargeNowMah)) return kNaN;
    if (toFull) {
        if (status != "Charging" || amps <= 0 || std::isnan(chargeFullMah)) return kNaN;
        return std::fmax(0.0, chargeFullMah - chargeNowMah) / 1000.0 / amps * 3600.0;
    }
    if (status != "Discharging" || amps >= 0) return kNaN;
    return std::fmax(0.0, chargeNowMah) / 1000.0 / -amps * 3600.0;
}

double sagVolts(double offeredV, double arrivingV) {
    if (std::isnan(offeredV) || std::isnan(arrivingV)) return kNaN;
    return std::fmax(0.0, offeredV - arrivingV);
}

bool parseUptime(const char* text, double& seconds) {
    if (text == nullptr) return false;
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text || v < 0) return false;
    seconds = v;
    return true;
}

bool parseStatmResident(const char* text, long long& residentPages) {
    if (text == nullptr) return false;
    char* end = nullptr;
    std::strtoll(text, &end, 10);  // size
    if (end == text) return false;
    const char* p = end;
    const long long resident = std::strtoll(p, &end, 10);
    if (end == p || resident < 0) return false;
    residentPages = resident;
    return true;
}

double selfWatts(double ownCpuSec, double systemBusySec, double cpuRailsW) {
    if (std::isnan(ownCpuSec) || std::isnan(systemBusySec) || std::isnan(cpuRailsW)) return kNaN;
    if (systemBusySec <= 0 || ownCpuSec < 0) return kNaN;
    // The system's busy time is counted in 10 ms ticks and our own in ns, so the share can poke over 1
    const double share = std::fmin(1.0, ownCpuSec / systemBusySec);
    return share * cpuRailsW;
}

}  // namespace sensor_math
