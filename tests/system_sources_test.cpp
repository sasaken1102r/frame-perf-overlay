// End-to-end test of the system sensor readers: a fake /sys + /proc tree is built under a temp dir, Sensors
// discovers it, the scheduler runs the sources, and the metric store is checked.
// POSIX only (mkdtemp, nftw, pread via Sensors). The tree is built in code, not checked in: iio device names
// contain ':' and can't exist on NTFS.
#include "metric_registry.h"
#include "metric_store.h"
#include "scheduler.h"
#include "sensors.h"
#include "system_metrics.h"

#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using frame_perf_overlay::MetricStore;
using frame_perf_overlay::MetricValue;
using frame_perf_overlay::Reason;
using frame_perf_overlay::Scheduler;
using frame_perf_overlay::SourceId;
using frame_perf_overlay::WidgetType;
namespace metric = frame_perf_overlay::metric;

int failures = 0;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #expr);                  \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

// Metric checks: print the metric id and what was found, so a failure says which one
void fail(int line, uint16_t id, const char* what) {
    std::printf("FAIL line %d: metric %u: %s\n", line, static_cast<unsigned>(id), what);
    ++failures;
}

void checkF32(const MetricStore& st, uint16_t id, double expected, double tol, int line) {
    const MetricValue* v = st.get(id);
    if (v == nullptr || !v->ok) {
        fail(line, id, v == nullptr ? "unknown id" : "expected a value, got N/A");
    } else if (std::fabs(static_cast<double>(v->f32) - expected) > tol) {
        std::printf("FAIL line %d: metric %u: expected %g, got %g\n", line, static_cast<unsigned>(id), expected,
                    static_cast<double>(v->f32));
        ++failures;
    }
}

void checkNa(const MetricStore& st, uint16_t id, Reason reason, int line) {
    const MetricValue* v = st.get(id);
    if (v == nullptr) {
        fail(line, id, "unknown id");
    } else if (v->ok) {
        fail(line, id, "expected N/A, got a value");
    } else if (v->reason != reason) {
        std::printf("FAIL line %d: metric %u: expected reason %s, got %s\n", line, static_cast<unsigned>(id),
                    frame_perf_overlay::reasonText(reason), frame_perf_overlay::reasonText(v->reason));
        ++failures;
    }
}

void checkEnum(const MetricStore& st, uint16_t id, uint32_t expected, int line) {
    const MetricValue* v = st.get(id);
    if (v == nullptr || !v->ok) {
        fail(line, id, "expected a value, got N/A");
    } else if (v->enumValue != expected) {
        std::printf("FAIL line %d: metric %u: expected %u, got %u\n", line, static_cast<unsigned>(id), expected,
                    v->enumValue);
        ++failures;
    }
}

#define F32_IS(st, id, expected) checkF32(st, id, expected, 1e-3, __LINE__)
#define F32_NEAR(st, id, expected, tol) checkF32(st, id, expected, tol, __LINE__)
#define NA_IS(st, id, reason) checkNa(st, id, reason, __LINE__)
#define ENUM_IS(st, id, expected) checkEnum(st, id, expected, __LINE__)

// ---------------------------------------------------------------------------------------------
// Fake filesystem

bool mkdirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

/** Writes root/rel (parents created, truncated in place so an open fd keeps seeing it) + a newline. */
void put(const std::string& root, const std::string& rel, const std::string& value) {
    const std::string path = root + "/" + rel;
    mkdirs(path.substr(0, path.rfind('/')));
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    out << value << "\n";
    if (!out) {
        std::printf("FAIL cannot write %s\n", path.c_str());
        ++failures;
    }
}

int removeEntry(const char* path, const struct stat*, int, struct FTW*) {
    return ::remove(path);
}

void removeTree(const std::string& root) {
    ::nftw(root.c_str(), removeEntry, 16, FTW_DEPTH | FTW_PHYS);
}

std::string makeTempRoot() {
    const char* base = std::getenv("TMPDIR");
    std::string templ = std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/frame-perf-overlay-sysfs-XXXXXX";
    std::vector<char> buf(templ.begin(), templ.end());
    buf.push_back('\0');
    if (::mkdtemp(buf.data()) == nullptr) {
        std::printf("FAIL mkdtemp\n");
        std::exit(2);
    }
    return std::string(buf.data());
}

const char* kStatA =
    "cpu  367360 15068 256399 6245980 687 89570 79807 0 0 0\n"
    "cpu0 46199 1714 33845 761685 19 30759 5426 0 0 0\n"
    "cpu1 47552 2961 29886 791051 31 8510 894 0 0 0\n"
    "intr 123456 1 2 3\nctxt 999\n";
// Same, after 400 total jiffies: total +100 busy (25%), cpu0 +50 of 100 (50%), cpu1 idle (0%)
const char* kStatB =
    "cpu  367460 15068 256399 6246280 687 89570 79807 0 0 0\n"
    "cpu0 46249 1714 33845 761735 19 30759 5426 0 0 0\n"
    "cpu1 47552 2961 29886 791151 31 8510 894 0 0 0\n"
    "intr 123999 1 2 3\nctxt 1999\n";

struct TreeOptions {
    bool plugged = false;
    bool withIio = true;
    bool withPowerSupply = true;
    bool throttledCpu = false;
};

void buildTree(const std::string& root, const TreeOptions& o) {
    // hwmon: three max34417 chips + the fan controller
    struct Chip {
        const char* dir;
        const char* name;
        const char* labels[4];
        long uw[4];
    };
    const Chip chips[] = {
        {"hwmon0", "max34417_12", {"apc0", "apc2", "apc1", "nsp1"}, {519995, 806278, 231063, 201112}},
        {"hwmon1", "max34417_1a", {"gfx", "nsp2", "bob", "unused"}, {439515, 85414, 491827, 46}},
        {"hwmon2", "max34417_10", {"vph", "s1c", "s3c", "s6c"}, {9188324, 505967, 292181, 898119}},
    };
    for (const Chip& c : chips) {
        const std::string base = std::string("sys/class/hwmon/") + c.dir;
        put(root, base + "/name", c.name);
        for (int n = 1; n <= 4; ++n) {
            put(root, base + "/power" + std::to_string(n) + "_input", std::to_string(c.uw[n - 1]));
            put(root, base + "/power" + std::to_string(n) + "_label", c.labels[n - 1]);
        }
    }
    put(root, "sys/class/hwmon/hwmon3/name", "slg4ax46073v");
    put(root, "sys/class/hwmon/hwmon3/fan1_input", "7314");

    // thermal zones (m°C)
    const struct {
        const char* type;
        long milli;
    } zones[] = {
        {"cpuss-0-thermal", 68900}, {"cpu-1-0-thermal", 66000}, {"gpuss-0-thermal", 52000},
        {"gpuss-1-thermal", 55500}, {"gpuss-2-thermal", 51000}, {"ddr-thermal", 47000},
        {"max1720x_bat_7-36", 35700},
    };
    int zone = 0;
    for (const auto& z : zones) {
        const std::string base = "sys/class/thermal/thermal_zone" + std::to_string(zone++);
        put(root, base + "/type", z.type);
        put(root, base + "/temp", std::to_string(z.milli));
    }
    put(root, "sys/class/thermal/cooling_device0/type", "cpufreq-cpu0");
    put(root, "sys/class/thermal/cooling_device0/cur_state", o.throttledCpu ? "3" : "0");
    put(root, "sys/class/thermal/cooling_device4/type", "devfreq-3d00000.gpu");
    put(root, "sys/class/thermal/cooling_device4/cur_state", "0");

    // cpufreq (kHz)
    const struct {
        const char* policy;
        const char* cur;
    } policies[] = {{"policy0", "2265600"}, {"policy2", "3148800"}, {"policy5", "2841600"}, {"policy7", "2995200"}};
    for (const auto& p : policies) {
        put(root, std::string("sys/devices/system/cpu/cpufreq/") + p.policy + "/scaling_cur_freq", p.cur);
    }
    put(root, "sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq", "2995200");
    put(root, "sys/class/devfreq/3d00000.gpu/cur_freq", "680000000");
    put(root, "sys/class/devfreq/3d00000.gpu/max_freq", "900000000");

    if (o.withPowerSupply) {
        const std::string bat = "sys/class/power_supply/max1720x_bat_7-36/";
        put(root, bat + "type", "Battery");
        put(root, bat + "capacity", "56");
        put(root, bat + "voltage_now", "7765936");
        put(root, bat + "current_now", o.plugged ? "1100000" : "-1503750");
        put(root, bat + "temp", "357");
        put(root, bat + "status", o.plugged ? "Charging" : "Discharging");

        const std::string chg = "sys/class/power_supply/pm8550b-charger/";
        put(root, chg + "type", "USB");
        put(root, chg + "online", o.plugged ? "1" : "0");
    }

    if (o.withIio) {
        const std::string iio = "sys/bus/iio/devices/iio:device0/";
        put(root, iio + "name", "c400000.spmi:pmic@0:vadc@9000");
        put(root, iio + "in_temp_display_left_therm_input", "64294");
        put(root, iio + "in_temp_pcba_at_fan_exhaust_input", "48500");
        put(root, iio + "in_temp_pcba_at_heat_sink_fins_input", "51000");
    }

    put(root, "proc/stat", kStatA);
    put(root, "proc/meminfo",
        "MemTotal:       16777216 kB\nMemFree:         1000000 kB\nMemAvailable:    8388608 kB\nBuffers: 1 kB");
}

/** Everything the scheduler needs, built once and never moved (the readers capture references). */
struct Rig {
    std::string root;
    MetricStore store;
    Scheduler sched;
    Sensors sensors;

    explicit Rig(const std::string& r) : root(r) {}

    void start(const std::vector<WidgetType>& widgets) {
        sensors.discover(root);
        registerSystemSources(sched, sensors, store);
        sched.setEnabledWidgets(widgets);
        frame_perf_overlay::DemandInputs d;
        d.hudShown = true;
        d.hmdInUse = true;
        sched.setDemand(d);
    }
};

const std::vector<WidgetType> kAllSystemWidgets = {WidgetType::Cpu,     WidgetType::GpuRam,  WidgetType::Power,
                                                   WidgetType::Battery, WidgetType::Temp,    WidgetType::General};

// ---------------------------------------------------------------------------------------------
// Scenarios

void checkPower(const MetricStore& st) {
    F32_IS(st, metric::kPowerVphW, 9.188324);
    // every channel but vph: 10 rails, the "unused" channel (46 µW) is never opened, and never the 11-channel sum
    F32_NEAR(st, metric::kPowerRailsW, 4.471471, 1e-5);
}

void testUnplugged() {
    const std::string root = makeTempRoot();
    buildTree(root, TreeOptions());
    {
        Rig rig(root);
        rig.start(kAllSystemWidgets);
        const MetricStore& st = rig.store;

        // discover(): the "unused" channel is skipped, the rest (vph + 10 rails) are found
        const std::string described = rig.sensors.describe();
        CHECK(described.find("unused") == std::string::npos);
        CHECK(described.find("消費電力: 11 ch") != std::string::npos);

        CHECK(rig.sched.runDue(10.0) > 0);

        // Power
        checkPower(st);

        // Battery, on battery
        F32_IS(st, metric::kBatteryPct, 56);
        F32_IS(st, metric::kBatteryVoltageV, 7.765936);
        F32_IS(st, metric::kBatteryCurrentMa, -1503.75);
        F32_IS(st, metric::kTempBatteryC, 35.7);
        F32_IS(st, metric::kBatteryPowerW, 7.765936 * -1.50375);
        CHECK(st.f32Or(metric::kBatteryPowerW, 0.0) < 0);
        ENUM_IS(st, metric::kBatteryStatus, static_cast<uint32_t>(frame_perf_overlay::BatteryStatus::Discharging));
        ENUM_IS(st, metric::kBatteryChargerOnline, 0);

        // Temps
        F32_IS(st, metric::kTempCpuC, 68.9);
        F32_IS(st, metric::kTempGpuC, 55.5);
        F32_IS(st, metric::kTempDdrC, 47.0);
        F32_IS(st, metric::kTempDisplayC, 64.294);
        F32_IS(st, metric::kTempExhaustC, 48.5);
        F32_IS(st, metric::kTempHeatsinkC, 51.0);
        ENUM_IS(st, metric::kThrottleCpu, 0);
        ENUM_IS(st, metric::kThrottleGpu, 0);
        F32_IS(st, metric::kFanRpm, 3657);

        // Clocks (kHz → MHz, Hz → MHz)
        F32_IS(st, metric::kCpuFastestClockMhz, 3148.8);
        F32_IS(st, metric::kGpuClockMhz, 680.0);
        F32_IS(st, metric::kGpuMaxClockMhz, 900.0);

        // RAM and the clock
        F32_IS(st, metric::kRamTotalGib, 16.0);
        F32_IS(st, metric::kRamUsedGib, 8.0);
        const MetricValue* clock = st.get(metric::kClock);
        CHECK(clock != nullptr && clock->ok && clock->text.size() == 19 && clock->text[4] == '-' &&
              clock->text[10] == ' ' && clock->text[13] == ':');

        // CPU: the first read only sets the baseline
        NA_IS(st, metric::kCpuUsagePct, Reason::NotSampledYet);
        NA_IS(st, metric::kCpuMaxCorePct, Reason::NotSampledYet);

        // Second /proc/stat snapshot: rewrite in place (same inode, the open fd re-reads it)
        put(root, "proc/stat", kStatB);
        CHECK(rig.sched.runDue(10.5) > 0);
        F32_IS(st, metric::kCpuUsagePct, 25.0);
        F32_IS(st, metric::kCpuMaxCorePct, 50.0);
    }
    removeTree(root);
}

void testPlugged() {
    const std::string root = makeTempRoot();
    TreeOptions o;
    o.plugged = true;
    buildTree(root, o);
    {
        Rig rig(root);
        rig.start(kAllSystemWidgets);
        const MetricStore& st = rig.store;
        CHECK(rig.sched.runDue(10.0) > 0);

        ENUM_IS(st, metric::kBatteryStatus, static_cast<uint32_t>(frame_perf_overlay::BatteryStatus::Charging));
        ENUM_IS(st, metric::kBatteryChargerOnline, 1);
        F32_IS(st, metric::kBatteryCurrentMa, 1100.0);
        F32_IS(st, metric::kBatteryPowerW, 7.765936 * 1.1);
        CHECK(st.f32Or(metric::kBatteryPowerW, 0.0) > 0);
        // Power doesn't depend on the plug
        checkPower(st);
    }
    removeTree(root);
}

void testThrottle() {
    const std::string root = makeTempRoot();
    TreeOptions o;
    o.throttledCpu = true;
    buildTree(root, o);
    {
        Rig rig(root);
        rig.start({WidgetType::Temp});
        CHECK(rig.sched.runDue(1.0) > 0);
        ENUM_IS(rig.store, metric::kThrottleCpu, 1);
        ENUM_IS(rig.store, metric::kThrottleGpu, 0);
    }
    removeTree(root);
}

void testMissingFiles() {
    // No iio device and no power_supply: board temps can't be read, the battery isn't there at all
    {
        const std::string root = makeTempRoot();
        TreeOptions o;
        o.withIio = false;
        o.withPowerSupply = false;
        buildTree(root, o);
        {
            Rig rig(root);
            rig.start(kAllSystemWidgets);
            const MetricStore& st = rig.store;
            CHECK(rig.sched.runDue(10.0) > 0);

            NA_IS(st, metric::kTempDisplayC, Reason::ReadFailed);
            NA_IS(st, metric::kTempExhaustC, Reason::ReadFailed);
            NA_IS(st, metric::kTempHeatsinkC, Reason::ReadFailed);
            // The rest of the Temp widget is untouched
            F32_IS(st, metric::kTempCpuC, 68.9);
            F32_IS(st, metric::kFanRpm, 3657);

            NA_IS(st, metric::kBatteryPct, Reason::SensorMissing);
            NA_IS(st, metric::kBatteryVoltageV, Reason::SensorMissing);
            NA_IS(st, metric::kBatteryCurrentMa, Reason::SensorMissing);
            NA_IS(st, metric::kBatteryPowerW, Reason::SensorMissing);
            NA_IS(st, metric::kBatteryStatus, Reason::SensorMissing);
            NA_IS(st, metric::kBatteryChargerOnline, Reason::SensorMissing);
            NA_IS(st, metric::kTempBatteryC, Reason::SensorMissing);
            checkPower(st);
        }
        removeTree(root);
    }

    // Nothing at all under the root: every reader copes, nothing crashes
    {
        const std::string root = makeTempRoot();
        {
            Rig rig(root);
            rig.start(kAllSystemWidgets);
            const MetricStore& st = rig.store;
            CHECK(rig.sched.runDue(10.0) > 0);
            CHECK(rig.sched.runDue(10.5) > 0);
            CHECK(rig.sched.runDue(12.0) > 0);
            NA_IS(st, metric::kPowerVphW, Reason::SensorMissing);
            NA_IS(st, metric::kPowerRailsW, Reason::SensorMissing);
            NA_IS(st, metric::kCpuFastestClockMhz, Reason::ReadFailed);
            NA_IS(st, metric::kBatteryPct, Reason::SensorMissing);
            NA_IS(st, metric::kTempCpuC, Reason::ReadFailed);
            NA_IS(st, metric::kTempDisplayC, Reason::ReadFailed);
            NA_IS(st, metric::kFanRpm, Reason::ReadFailed);
            NA_IS(st, metric::kRamTotalGib, Reason::ReadFailed);
            const MetricValue* clock = st.get(metric::kClock);
            CHECK(clock != nullptr && clock->ok);  // the clock doesn't come from the tree
        }
        removeTree(root);
    }
}

/** The intervals of every source in the compiled-in registry, in seconds. */
void checkIntervals(const Scheduler& sched, double perUpdate) {
    const struct {
        SourceId source;
        double sec;
    } plan[] = {
        // read every panel update (update_interval_ms)
        {SourceId::CpuLoad, perUpdate},  {SourceId::CpuClock, perUpdate}, {SourceId::GpuClock, perUpdate},
        {SourceId::FrameTiming, perUpdate}, {SourceId::Ram, perUpdate},   {SourceId::Temps, perUpdate},
        {SourceId::Throttle, perUpdate}, {SourceId::Fan, perUpdate},
        // fixed
        {SourceId::Power, 2.0},          {SourceId::Network, 2.0},        {SourceId::BoardTemps, 5.0},
        {SourceId::Battery, 5.0},        {SourceId::Clock, 1.0},          {SourceId::Controllers, 10.0},
    };
    for (const auto& p : plan) {
        if (std::fabs(sched.interval(p.source) - p.sec) > 1e-9) {
            std::printf("FAIL interval of %s: expected %g s, got %g s\n", frame_perf_overlay::sourceName(p.source),
                        p.sec, sched.interval(p.source));
            ++failures;
        }
    }
}

void testIntervals() {
    Scheduler sched;  // compiled-in registry
    checkIntervals(sched, 0.5);
    // update_interval_ms moves only the per-update sources
    sched.setUpdateInterval(0.1);
    checkIntervals(sched, 0.1);
    sched.setUpdateInterval(1.0);
    checkIntervals(sched, 1.0);
    sched.setUpdateInterval(0.0);  // ignored
    sched.setUpdateInterval(-1.0);
    checkIntervals(sched, 1.0);
}

void runSteps(Rig& rig, double from, double to, double step) {
    for (double t = from; t <= to + 1e-9; t += step) rig.sched.runDue(t);
}

// "Pause while hidden": a source is read only while its widget is enabled AND someone is looking
void testDemand() {
    const std::string root = makeTempRoot();
    buildTree(root, TreeOptions());
    {
        Rig rig(root);
        rig.start({WidgetType::Power});
        Scheduler& sc = rig.sched;
        CHECK(sc.refCount(SourceId::Power) == 1);
        CHECK(sc.refCount(SourceId::Battery) == 0);

        // 10 s of fake time at 2 Hz polling: Power's 2 s interval gives 5-6 reads, nothing else is read
        runSteps(rig, 100.0, 110.0, 0.5);
        const uint64_t powerReads = sc.readCount(SourceId::Power);
        CHECK(powerReads >= 5 && powerReads <= 6);
        CHECK(sc.readCount(SourceId::Battery) == 0);
        CHECK(sc.readCount(SourceId::CpuLoad) == 0);
        CHECK(sc.readCount(SourceId::Temps) == 0);
        CHECK(sc.readCount(SourceId::Clock) == 0);
        F32_IS(rig.store, metric::kPowerVphW, 9.188324);
        NA_IS(rig.store, metric::kBatteryPct, Reason::NotSampledYet);  // never read

        // Nobody looking: no more reads
        sc.setDemand(frame_perf_overlay::DemandInputs());
        CHECK(sc.refCount(SourceId::Power) == 0);
        int ran = 0;
        for (double t = 111.0; t <= 120.0; t += 0.5) ran += sc.runDue(t);
        CHECK(ran == 0);
        CHECK(sc.readCount(SourceId::Power) == powerReads);

        // Back, with only Battery enabled: Battery reads every 5 s, Power stays quiet
        sc.setEnabledWidgets({WidgetType::Battery});
        frame_perf_overlay::DemandInputs d;
        d.hudShown = true;
        d.hmdInUse = true;
        sc.setDemand(d);
        CHECK(sc.refCount(SourceId::Battery) >= 1);
        CHECK(sc.refCount(SourceId::Power) == 0);
        runSteps(rig, 121.0, 131.0, 0.5);
        const uint64_t batteryReads = sc.readCount(SourceId::Battery);
        CHECK(batteryReads >= 2 && batteryReads <= 3);
        CHECK(sc.readCount(SourceId::Power) == powerReads);
        F32_IS(rig.store, metric::kBatteryPct, 56);

        // Headset off the head (hmd not in use), HUD on: not looking either
        d.hmdInUse = false;
        sc.setDemand(d);
        CHECK(sc.refCount(SourceId::Battery) == 0);
        const uint64_t before = sc.readCount(SourceId::Battery);
        runSteps(rig, 132.0, 140.0, 0.5);
        CHECK(sc.readCount(SourceId::Battery) == before);
    }
    removeTree(root);
}

// The HUD hidden (wrist faded out, tracking lost, alpha 0) while the headset is on the head and the editor is
// closed: nothing is read; showing it again resumes with a fresh baseline
void testHiddenPausesReads() {
    const std::string root = makeTempRoot();
    buildTree(root, TreeOptions());
    {
        Rig rig(root);
        rig.start({WidgetType::Cpu});
        Scheduler& sc = rig.sched;
        const MetricStore& st = rig.store;
        CHECK(rig.sched.runDue(10.0) > 0);
        NA_IS(st, metric::kCpuUsagePct, Reason::NotSampledYet);  // the first read is only the baseline
        put(root, "proc/stat", kStatB);
        CHECK(rig.sched.runDue(10.5) > 0);
        F32_IS(st, metric::kCpuUsagePct, 25.0);

        frame_perf_overlay::DemandInputs d;
        d.hudShown = false;
        d.hmdInUse = true;
        d.editorOpen = false;
        sc.setDemand(d);
        CHECK(sc.refCount(SourceId::CpuLoad) == 0);
        const uint64_t reads = sc.readCount(SourceId::CpuLoad);
        int ran = 0;
        for (double t = 11.0; t <= 20.0; t += 0.5) ran += sc.runDue(t);
        CHECK(ran == 0);
        CHECK(sc.readCount(SourceId::CpuLoad) == reads);

        d.hudShown = true;
        sc.setDemand(d);
        CHECK(sc.runDue(21.0) > 0);
        CHECK(sc.readCount(SourceId::CpuLoad) == reads + 1);
        NA_IS(st, metric::kCpuUsagePct, Reason::NotSampledYet);  // resumed: no average over the pause
    }
    removeTree(root);
}

// ---------------------------------------------------------------------------------------------
// The readouts (PR 2): per-core load, clusters, rails, extra temps, battery details, uptime

void checkI32(const MetricStore& st, uint16_t id, int32_t expected, int line) {
    const MetricValue* v = st.get(id);
    if (v == nullptr || !v->ok) {
        fail(line, id, "expected a value, got N/A");
    } else if (v->i32 != expected) {
        std::printf("FAIL line %d: metric %u: expected %d, got %d\n", line, static_cast<unsigned>(id), expected, v->i32);
        ++failures;
    }
}

void checkText(const MetricStore& st, uint16_t id, const char* expected, int line) {
    const MetricValue* v = st.get(id);
    if (v == nullptr || !v->ok) {
        fail(line, id, "expected a value, got N/A");
    } else if (v->text != expected) {
        std::printf("FAIL line %d: metric %u: expected \"%s\", got \"%s\"\n", line, static_cast<unsigned>(id), expected,
                    v->text.c_str());
        ++failures;
    }
}

#define I32_IS(st, id, expected) checkI32(st, id, expected, __LINE__)
#define TEXT_IS(st, id, expected) checkText(st, id, expected, __LINE__)

/** The extra files the readouts read, on top of buildTree(). */
void addReadoutFiles(const std::string& root, bool plugged) {
    // the chips' own volts (mV), matched to the power channels by label
    put(root, "sys/class/hwmon/hwmon0/in0_label", "apc0");
    put(root, "sys/class/hwmon/hwmon0/in0_input", "862");
    put(root, "sys/class/hwmon/hwmon1/in0_label", "gfx");
    put(root, "sys/class/hwmon/hwmon1/in0_input", "5");
    put(root, "sys/class/hwmon/hwmon2/in0_label", "vph");
    put(root, "sys/class/hwmon/hwmon2/in0_input", "3804");
    // NPU (two zones: the max counts), camera, video
    const struct {
        const char* type;
        long milli;
    } zones[] = {{"nsph0-thermal", 50000}, {"nsph1-thermal", 52500}, {"camera-0-thermal", 44000}, {"video-thermal", 41000}};
    int zone = 20;
    for (const auto& z : zones) {
        const std::string base = "sys/class/thermal/thermal_zone" + std::to_string(zone++);
        put(root, base + "/type", z.type);
        put(root, base + "/temp", std::to_string(z.milli));
    }
    // clusters: policy0 capped to 1.5 GHz under a Playtime-style limit, the rest at their hardware max
    const struct {
        const char* policy;
        const char* max;
        const char* hw;
    } caps[] = {{"policy0", "1500000", "2265600"}, {"policy2", "3148800", "3148800"},
                {"policy5", "2841600", "2841600"}, {"policy7", "2995200", "2995200"}};
    for (const auto& c : caps) {
        put(root, std::string("sys/devices/system/cpu/cpufreq/") + c.policy + "/scaling_max_freq", c.max);
        put(root, std::string("sys/devices/system/cpu/cpufreq/") + c.policy + "/cpuinfo_max_freq", c.hw);
    }
    const std::string iio = "sys/bus/iio/devices/iio:device0/";
    put(root, iio + "in_temp_pcba_at_wifi_input", "41000");
    put(root, iio + "in_temp_pcba_at_flash_input", "39000");
    put(root, iio + "in_temp_pcba_at_nrf_input", "38000");
    put(root, iio + "in_temp_pcba_at_40pin_connector_input", "37000");
    put(root, iio + "in_voltage_pm8550b_usb_sns_v_div_16_input", "8700000");  // uV
    put(root, iio + "in_current_pm8550b_iin_fb_input", "1200000");            // uA
    // battery: 2000 of 4000 mAh, 4500 design
    const std::string bat = "sys/class/power_supply/max1720x_bat_7-36/";
    put(root, bat + "current_avg", plugged ? "1000000" : "-1500000");
    put(root, bat + "charge_now", "2000000");
    put(root, bat + "charge_full", "4000000");
    put(root, bat + "charge_full_design", "4500000");
    put(root, bat + "health", "Good");
    put(root, bat + "cycle_count", "123");
    const std::string chg = "sys/class/power_supply/pm8550b-charger/";
    put(root, chg + "status_detailed", plugged ? "CC: 1\nCV: 0\nICL: 0\nJEITA: 0\nBoost: 0" : "CC: 0\nCV: 0");
    put(root, chg + "charging_enable", "1");
    put(root, chg + "charging_pause", "0");
    put(root, chg + "constant_charge_current", "3000000");
    put(root, chg + "constant_charge_current_max", "5000000");
    const std::string src = "sys/class/power_supply/tcpm-source-psy-usbpd0/";
    put(root, src + "type", "USB");
    put(root, src + "online", plugged ? "1" : "0");
    put(root, src + "usb_type", "C [PD] PD_PPS");
    put(root, src + "voltage_now", "9000000");
    put(root, src + "current_max", "3000000");
    put(root, "proc/uptime", "8914.95 62459.80");
    put(root, "proc/self/statm", "617 292 271 7 0 146 0");
}

void testReadouts() {
    const std::string root = makeTempRoot();
    buildTree(root, TreeOptions());
    addReadoutFiles(root, false);
    {
        Rig rig(root);
        rig.start(kAllSystemWidgets);
        const MetricStore& st = rig.store;
        CHECK(rig.sched.runDue(10.0) > 0);

        // per-core load: the first read is a baseline (every slot N/A), then cpu0 50 %, cpu1 0 %, and the six
        // slots past the last core are SensorMissing
        for (uint16_t i = 0; i < metric::kCpuCoreSlots; ++i) NA_IS(st, metric::kCpuCore0Pct + i, Reason::NotSampledYet);
        put(root, "proc/stat", kStatB);
        CHECK(rig.sched.runDue(10.5) > 0);
        F32_IS(st, metric::kCpuCore0Pct, 50.0);
        F32_IS(st, metric::kCpuCore0Pct + 1, 0.0);
        for (uint16_t i = 2; i < metric::kCpuCoreSlots; ++i) NA_IS(st, metric::kCpuCore0Pct + i, Reason::SensorMissing);

        // clusters: current, cap and hardware max; policy0 is capped (1500 < 2265.6)
        F32_IS(st, metric::kCpuCluster0Mhz, 2265.6);
        F32_IS(st, metric::kCpuCluster0MaxMhz, 1500.0);
        F32_IS(st, metric::kCpuCluster0HwMaxMhz, 2265.6);
        CHECK(st.f32Or(metric::kCpuCluster0MaxMhz, 0) < st.f32Or(metric::kCpuCluster0HwMaxMhz, 0));
        F32_IS(st, metric::kCpuCluster0Mhz + 1, 3148.8);
        F32_IS(st, metric::kCpuCluster0MaxMhz + 1, 3148.8);
        F32_IS(st, metric::kCpuCluster0HwMaxMhz + 1, 3148.8);
        F32_IS(st, metric::kCpuCluster0Mhz + 3, 2995.2);

        // temps
        F32_IS(st, metric::kTempNpuC, 52.5);
        F32_IS(st, metric::kTempCameraC, 44.0);
        F32_IS(st, metric::kTempVideoC, 41.0);
        F32_IS(st, metric::kTempWifiC, 41.0);
        F32_IS(st, metric::kTempFlashC, 39.0);
        F32_IS(st, metric::kTempNrfC, 38.0);
        F32_IS(st, metric::kTempConnectorC, 37.0);

        // rails: watts and volts by label; gfx's volts are N/A InvalidReading; vph - rails is what's left
        F32_NEAR(st, metric::kPowerUnmeasuredW, 9.188324 - 4.471471, 1e-5);
        F32_IS(st, metric::kPowerVphV, 3.804);
        F32_IS(st, metric::kPowerApc0W, 0.519995);
        F32_IS(st, metric::kPowerApc0W + metric::kPowerRailVoltsOffset, 0.862);
        F32_IS(st, metric::kPowerGfxW, 0.439515);
        NA_IS(st, metric::kPowerGfxW + metric::kPowerRailVoltsOffset, Reason::InvalidReading);
        NA_IS(st, metric::kPowerApc1W + metric::kPowerRailVoltsOffset, Reason::ReadFailed);  // the chip has no in1

        // battery on battery: 2000 mAh at 1.5 A = 4800 s to empty, no time to full, nothing plugged
        F32_IS(st, metric::kBatteryChargeNowMah, 2000.0);
        F32_IS(st, metric::kBatteryChargeFullMah, 4000.0);
        F32_IS(st, metric::kBatteryChargeDesignMah, 4500.0);
        TEXT_IS(st, metric::kBatteryHealth, "Good");
        I32_IS(st, metric::kBatteryCycles, 123);
        I32_IS(st, metric::kBatteryTimeToEmptyS, 4800);
        NA_IS(st, metric::kBatteryTimeToFullS, Reason::NotConnected);
        TEXT_IS(st, metric::kBatteryChargerMode, "Unplugged");
        ENUM_IS(st, metric::kBatteryPlugged, 0);
        F32_IS(st, metric::kBatterySetCurrentA, 3.0);
        F32_IS(st, metric::kBatterySetCurrentMaxA, 5.0);
        NA_IS(st, metric::kBatteryPdOfferedV, Reason::NotConnected);
        NA_IS(st, metric::kBatteryInputV, Reason::NotConnected);  // the ADC is not even read unplugged
        NA_IS(st, metric::kBatterySagV, Reason::NotConnected);

        // uptime (whole seconds) and the HUD's own RSS
        I32_IS(st, metric::kUptimeS, 8914);
        F32_NEAR(st, metric::kSelfRssMib, 292.0 * static_cast<double>(::sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0), 1e-3);
    }
    removeTree(root);
}

void testReadoutsPlugged() {
    const std::string root = makeTempRoot();
    TreeOptions o;
    o.plugged = true;
    buildTree(root, o);
    addReadoutFiles(root, true);
    {
        Rig rig(root);
        rig.start({WidgetType::Battery});
        const MetricStore& st = rig.store;
        CHECK(rig.sched.runDue(10.0) > 0);
        // charging: (4000 - 2000) mAh at 1.0 A = 7200 s, nothing to empty
        I32_IS(st, metric::kBatteryTimeToFullS, 7200);
        NA_IS(st, metric::kBatteryTimeToEmptyS, Reason::InvalidReading);
        TEXT_IS(st, metric::kBatteryChargerMode, "CC");
        ENUM_IS(st, metric::kBatteryPlugged, 1);
        // the USB-C contract (PD, 9 V x 3 A) against what arrives (8.7 V x 1.2 A): 0.3 V lost
        TEXT_IS(st, metric::kBatteryUsbType, "PD");
        F32_IS(st, metric::kBatteryPdOfferedV, 9.0);
        F32_IS(st, metric::kBatteryPdOfferedA, 3.0);
        F32_IS(st, metric::kBatteryPdOfferedW, 27.0);
        F32_IS(st, metric::kBatteryInputV, 8.7);
        F32_IS(st, metric::kBatteryInputA, 1.2);
        F32_IS(st, metric::kBatteryInputW, 8.7 * 1.2);
        F32_NEAR(st, metric::kBatterySagV, 0.3, 1e-4);
    }
    removeTree(root);
}

}  // namespace

int main() {
    testUnplugged();
    testPlugged();
    testThrottle();
    testMissingFiles();
    testIntervals();
    testDemand();
    testHiddenPausesReads();
    testReadouts();
    testReadoutsPlugged();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("system-sources: all checks passed\n");
    return 0;
}
