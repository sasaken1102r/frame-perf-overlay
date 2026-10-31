// The metric registry: one table that says, for every metric, which widget owns it and which source
// reads it.
//
// Rules (enforced by validateRegistry(), run by the unit tests):
// - every id is unique, and ids are stable forever: new metrics get new ids, retired ids are never reused,
// - every metric has exactly one owner widget (no owner, or the same metric under two owners, fails),
// - every metric names a source and a read interval.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace frame_perf_overlay {

/**
 * Widget types. Stable: they go into saved layouts. Numbers without a widget yet (10, 12, 13) are
 * reserved, never reused.
 */
enum class WidgetType : uint8_t {
    None = 0,
    Frame = 1,
    Cpu = 2,
    GpuRam = 3,
    Power = 4,
    Battery = 5,
    Temp = 6,
    CpuItems = 7,  ///< processes by CPU
    RamItems = 8,  ///< processes by memory
    Network = 9,
    General = 11,
};

/** Number of WidgetType values including None and the reserved gaps (for arrays indexed by type). */
constexpr size_t kWidgetTypeCount = 12;

/**
 * A source is one thing that gets read (a sysfs file group, a /proc file, an nl80211 query, an OpenVR call).
 * Internal: source ids aren't saved anywhere, so they may be renumbered.
 */
enum class SourceId : uint8_t {
    None = 0,
    CpuLoad,      ///< /proc/stat
    CpuClock,     ///< cpufreq scaling_cur_freq
    GpuClock,     ///< devfreq cur_freq
    Ram,          ///< /proc/meminfo
    Temps,        ///< thermal_zone*/temp
    BoardTemps,   ///< vadc iio board temperatures
    Throttle,     ///< cooling_device*/cur_state
    Fan,          ///< hwmon fan1_input
    Power,        ///< max34417 power*_input
    Battery,      ///< power_supply battery + charger
    Network,      ///< nl80211 + interface statistics
    FrameTiming,  ///< OpenVR GetFrameTimings, batched
    Controllers,  ///< OpenVR controller battery properties
    Clock,        ///< local time
    Processes,    ///< /proc/[pid] scan (CPU items, RAM items)
    SceneApp,     ///< OpenVR current scene app (also updated on the scene-app event)
    Uptime,       ///< /proc/uptime
    SelfCost,     ///< own CPU time and memory, CPU rails
    Count,
};

/** Number of SourceId values including None and excluding Count. */
constexpr size_t kSourceCount = static_cast<size_t>(SourceId::Count);

/** How a value is typed. */
enum class WireType : uint8_t {
    F32 = 0,
    I32 = 1,
    Enum = 2,         ///< a small unsigned code, meaning documented next to the metric
    ShortString = 3,  ///< up to kShortStringMax bytes of UTF-8
};

/**
 * The interval of the metrics read on every panel update, like the fixed panel did (frame timing, CPU, GPU clock,
 * memory, temperatures, throttle, fan). It's update_interval_ms's default: the scheduler moves these sources to the
 * configured interval (Scheduler::setUpdateInterval). Sources with any other interval keep it.
 */
constexpr uint32_t kPerUpdateMs = 500;

/** Longest short string a metric may carry (bytes). */
constexpr size_t kShortStringMax = 31;

/** One registry entry. */
struct MetricDef {
    uint16_t id;          ///< stable id
    const char* name;     ///< dotted name, unique ("cpu.usage_pct")
    const char* unit;     ///< display unit ("%", "W", "" for none)
    WidgetType owner;     ///< the one widget that may show it
    SourceId source;      ///< what reads it
    uint32_t intervalMs;  ///< how often the source reads it while needed
    WireType type;
};

// Metric ids. Grouped in blocks of 100 by owner widget, only for readability.
namespace metric {
// Frame (widget 1), read from FrameTiming
constexpr uint16_t kFrameAppFps = 100;
constexpr uint16_t kFrameDisplayHz = 101;
constexpr uint16_t kFrameGpuMs = 102;
constexpr uint16_t kFrameCpuMs = 103;
constexpr uint16_t kFrameGpuMaxMs = 104;
constexpr uint16_t kFrameCpuMaxMs = 105;
constexpr uint16_t kFrameReprojectedPct = 106;
constexpr uint16_t kFrameDropped = 107;
constexpr uint16_t kFrameThrottled = 108;  ///< 0 = not throttled, 1 = half rate, 2 = third
constexpr uint16_t kFrameCount = 109;      ///< frames aggregated since the last read
// CPU (widget 2)
constexpr uint16_t kCpuUsagePct = 200;
constexpr uint16_t kCpuMaxCorePct = 201;
constexpr uint16_t kCpuFastestClockMhz = 202;
// 203 is retired (the big core's clock; the CPU card shows the fastest cluster). Never reuse it.
constexpr uint16_t kCpuCore0Pct = 210;  ///< 210-217: load of core 0-7 (N/A SensorMissing past the last core)
constexpr uint16_t kCpuCoreSlots = 8;
constexpr uint16_t kCpuCluster0Mhz = 220;       ///< 220-223: current clock of cluster 0-3 (cpufreq policies, ascending)
constexpr uint16_t kCpuCluster0MaxMhz = 224;    ///< 224-227: scaling_max_freq of cluster 0-3 (drops under a Playtime cap)
constexpr uint16_t kCpuCluster0HwMaxMhz = 228;  ///< 228-231: cpuinfo_max_freq of cluster 0-3 ("capped" = max < this)
constexpr uint16_t kCpuClusterSlots = 4;
// GPU & RAM (widget 3)
constexpr uint16_t kGpuBusyPct = 300;  ///< GPU load (frame GPU ms × app fps), see core/gpu_load.h
constexpr uint16_t kGpuClockMhz = 301;
constexpr uint16_t kGpuMaxClockMhz = 302;
constexpr uint16_t kRamUsedGib = 303;
constexpr uint16_t kRamTotalGib = 304;
// Power (widget 4)
constexpr uint16_t kPowerVphW = 400;
// 401 is retired (the "All ch" sum, which double-counts vph). Never reuse it.
constexpr uint16_t kPowerRailsW = 402;  ///< sum of the measured rails under vph (every channel but vph)
constexpr uint16_t kPowerUnmeasuredW = 403;  ///< vph − rails: buck losses + everything without a sense resistor
constexpr uint16_t kPowerVphV = 420;
// Rail watts 410-419 and volts 430-439, same order (rail + 20 = its volts). gfx volts are always
// N/A InvalidReading: its sense reads 2-7 mV at ~0.4 W, so it isn't a real reading (shown as "—").
constexpr uint16_t kPowerRailVoltsOffset = 20;
constexpr uint16_t kPowerApc0W = 410;
constexpr uint16_t kPowerApc1W = 411;
constexpr uint16_t kPowerApc2W = 412;
constexpr uint16_t kPowerGfxW = 413;
constexpr uint16_t kPowerNsp1W = 414;
constexpr uint16_t kPowerNsp2W = 415;
constexpr uint16_t kPowerS1cW = 416;
constexpr uint16_t kPowerS3cW = 417;
constexpr uint16_t kPowerS6cW = 418;
constexpr uint16_t kPowerBobW = 419;
// Battery (widget 5)
constexpr uint16_t kBatteryPct = 500;
constexpr uint16_t kBatteryVoltageV = 501;
constexpr uint16_t kBatteryCurrentMa = 502;
constexpr uint16_t kBatteryStatus = 503;         ///< enum, see BatteryStatus
constexpr uint16_t kBatteryChargerOnline = 504;  ///< enum, 0 / 1
constexpr uint16_t kBatteryChargeNowMah = 505;
constexpr uint16_t kBatteryChargeFullMah = 506;
constexpr uint16_t kBatteryChargeDesignMah = 507;
constexpr uint16_t kBatteryPowerW = 508;         ///< V × A of the battery, + = into it, − = out of it
constexpr uint16_t kBatteryTimeToFullS = 509;    ///< i32: mAh left ÷ current_avg, N/A unless charging
constexpr uint16_t kBatteryTimeToEmptyS = 510;   ///< i32: mAh ÷ current_avg, N/A unless discharging
constexpr uint16_t kBatteryHealth = 511;         ///< short string, power_supply health ("Good")
constexpr uint16_t kBatteryCycles = 512;         ///< i32
constexpr uint16_t kBatteryChargerMode = 513;    ///< short string: Unplugged / Boost / Full / Paused / Off /
                                                 ///< CC / CV / CC+ICL / JEITA / Charging / Plugged
constexpr uint16_t kBatterySetCurrentA = 514;    ///< constant_charge_current
constexpr uint16_t kBatterySetCurrentMaxA = 515; ///< constant_charge_current_max
constexpr uint16_t kBatteryUsbType = 516;        ///< short string, active usb_type of the USB-C source ("PD", "C")
constexpr uint16_t kBatteryPlugged = 517;        ///< enum, 0 / 1: charger or USB source online
constexpr uint16_t kBatteryPdOfferedV = 518;     ///< the USB-C contract: what the charger offers, not what flows
constexpr uint16_t kBatteryPdOfferedA = 519;
constexpr uint16_t kBatteryPdOfferedW = 520;
constexpr uint16_t kBatteryInputV = 521;         ///< what arrives at the USB input (charger PMIC ADC, usb_sns)
constexpr uint16_t kBatteryInputA = 522;         ///< (iin_fb)
constexpr uint16_t kBatteryInputW = 523;
constexpr uint16_t kBatterySagV = 524;           ///< offered V − arriving V (cable, plugs, charger)
// Temp (widget 6)
constexpr uint16_t kTempCpuC = 600;
constexpr uint16_t kTempGpuC = 601;
constexpr uint16_t kTempDdrC = 602;
constexpr uint16_t kTempBatteryC = 603;  ///< read by the Battery source, owned by Temp
constexpr uint16_t kTempDisplayC = 604;
constexpr uint16_t kTempExhaustC = 605;
constexpr uint16_t kTempHeatsinkC = 606;
constexpr uint16_t kThrottleCpu = 607;   ///< enum, 0 / 1
constexpr uint16_t kThrottleGpu = 608;   ///< enum, 0 / 1
constexpr uint16_t kFanRpm = 609;
constexpr uint16_t kTempNpuC = 610;        ///< nsph* thermal zones, max
constexpr uint16_t kTempCameraC = 611;     ///< camera* thermal zones, max
constexpr uint16_t kTempVideoC = 612;      ///< video thermal zone
constexpr uint16_t kTempWifiC = 613;       ///< board spots from the vadc: near the Wi-Fi chip
constexpr uint16_t kTempFlashC = 614;      ///< near the flash
constexpr uint16_t kTempNrfC = 615;        ///< near the nRF radio
constexpr uint16_t kTempConnectorC = 616;  ///< near the 40-pin connector
// CPU items (widget 7). The rows themselves aren't metrics (WidgetContext::processes); these describe the
// scan both lists come from
constexpr uint16_t kProcScanned = 700;   ///< processes read in the last scan
constexpr uint16_t kProcVanished = 701;  ///< processes that exited mid-scan (skipped)
constexpr uint16_t kProcScanMs = 702;    ///< wall time of the last scan
constexpr uint16_t kCpuItemsRows = 703;  ///< rows in the CPU list (N/A NotSampledYet on the first scan)
// RAM items (widget 8)
constexpr uint16_t kRamItemsRows = 800;
// Network (widget 9). 900-907: the combined link (the dongle if it has a peer, else home Wi-Fi), which an
// "auto" Network widget shows. 910-922 / 930-942: one block per adapter, same layout (kNetAdapter* offsets)
constexpr uint16_t kNetInterfaceUp = 900;  ///< enum, 0 / 1
constexpr uint16_t kNetConnected = 901;    ///< enum, 0 / 1
constexpr uint16_t kNetHomeWifi = 902;     ///< enum, 0 = dongle, 1 = home Wi-Fi
constexpr uint16_t kNetSignalDbm = 903;
constexpr uint16_t kNetTxLinkMbps = 904;
constexpr uint16_t kNetRxLinkMbps = 905;
constexpr uint16_t kNetRxMbps = 906;
constexpr uint16_t kNetTxMbps = 907;
constexpr uint16_t kNetDongleBase = 910;  ///< dongle AP (nl80211 type AP, `wlanap`): 910-922
constexpr uint16_t kNetHomeBase = 930;    ///< home Wi-Fi (nl80211 type station, `wlan0`): 930-942
// Offsets inside an adapter block
constexpr uint16_t kNetAdapterPresent = 0;     ///< enum, 0 / 1: the interface exists
constexpr uint16_t kNetAdapterConnected = 1;   ///< enum, 0 / 1: it has a peer (PC on the dongle, AP at home)
constexpr uint16_t kNetAdapterRxMbps = 2;      ///< received, from /sys/class/net/<if>/statistics
constexpr uint16_t kNetAdapterTxMbps = 3;
constexpr uint16_t kNetAdapterTxLinkMbps = 4;  ///< PHY rate Frame → peer
constexpr uint16_t kNetAdapterRxLinkMbps = 5;  ///< PHY rate peer → Frame
constexpr uint16_t kNetAdapterSignalDbm = 6;   ///< dongle: ack signal (its `signal` reads 0 dBm); home: signal
constexpr uint16_t kNetAdapterIp = 7;          ///< short string, first IPv4 address
constexpr uint16_t kNetAdapterFreqMhz = 8;     ///< i32, control channel frequency
constexpr uint16_t kNetAdapterChannel = 9;     ///< i32
constexpr uint16_t kNetAdapterWidthMhz = 10;   ///< i32, channel width
constexpr uint16_t kNetAdapterBand = 11;       ///< enum, see WifiBand
constexpr uint16_t kNetAdapterIfName = 12;     ///< short string, interface name
constexpr uint16_t kNetAdapterCount = 13;
// General (widget 11)
constexpr uint16_t kCtrlLeftPct = 1100;       ///< N/A NotConnected when the controller is absent
constexpr uint16_t kCtrlLeftCharging = 1101;  ///< enum, 0 / 1
constexpr uint16_t kCtrlRightPct = 1102;
constexpr uint16_t kCtrlRightCharging = 1103;
constexpr uint16_t kSceneAppKey = 1104;   ///< short string, e.g. "steam.app.438100" (cut to 31 B)
constexpr uint16_t kSceneAppName = 1105;  ///< short string, e.g. "VRChat" (cut to 31 B)
constexpr uint16_t kScenePid = 1106;      ///< i32, 0 = no scene app (the compositor is showing)
// 1107-1109 are reserved
constexpr uint16_t kUptimeS = 1110;  ///< i32, since cold boot, sleep included (/proc/uptime)
constexpr uint16_t kClock = 1111;  ///< short string, local "YYYY-MM-DD HH:MM:SS"
constexpr uint16_t kSelfCpuPct = 1112;  ///< this overlay's own CPU, % of one core (all threads)
constexpr uint16_t kSelfRssMib = 1113;
constexpr uint16_t kSelfPowerW = 1114;  ///< estimate, shown with "≈": own share of busy CPU × CPU rails (apc0-2)
}  // namespace metric

/** Values of the per-adapter kNetAdapterBand metric. */
enum class WifiBand : uint32_t { Unknown = 0, Ghz2_4 = 1, Ghz5 = 2, Ghz6 = 3, Ghz60 = 4 };

/** Values of metric::kBatteryStatus. */
enum class BatteryStatus : uint32_t { Unknown = 0, Charging = 1, Discharging = 2, Full = 3, NotCharging = 4 };

/**
 * The registry compiled into this build.
 * @return all entries (static storage)
 */
const std::vector<MetricDef>& registry();

/**
 * Looks a metric up by id.
 * @param id metric id
 * @return the entry, or nullptr if this build doesn't know the id
 */
const MetricDef* findMetric(uint16_t id);

/**
 * Checks a registry table against the rules at the top of this file.
 * @param defs the table
 * @return one English line per problem; empty if the table is valid
 */
std::vector<std::string> validateRegistry(const std::vector<MetricDef>& defs);

/**
 * @param type a widget type
 * @return its name for logs and the debug dump ("Frame", "GPU & RAM", ...)
 */
const char* widgetName(WidgetType type);

/**
 * @param source a source id
 * @return its name for logs and the debug dump ("cpu_load", ...)
 */
const char* sourceName(SourceId source);

}  // namespace frame_perf_overlay
