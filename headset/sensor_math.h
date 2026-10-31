// Parsing and derivation for the system sensors, kept free of file I/O so the unit tests can feed it
// fixture text. Sensors (sensors.cpp) does the reading; SensorBridge stores the results as metrics.
//
// The charger-mode, time-left and sag rules follow FramePerf, a Windows battery tool for the Frame.
#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sensor_math {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---------------------------------------------------------------------------------------------
// CPU

/** Busy / total jiffies per CPU line of /proc/stat. Index 0 = the "cpu" total line, index n+1 = "cpu<n>". */
struct CpuTicks {
    std::vector<uint64_t> busy;
    std::vector<uint64_t> total;
    std::vector<bool> present;  ///< false for a core whose line is missing (offline)
};

/**
 * Parses the cpu lines at the top of /proc/stat.
 * busy = user + nice + system + irq + softirq + steal (fields 1-8 minus idle + iowait; guest is inside user).
 * @param text the file's text (only the leading cpu lines are looked at)
 * @param ticks result; sized to the highest core number seen + 2
 * @return true if at least the total line parsed
 */
bool parseProcStat(const char* text, CpuTicks& ticks);

/**
 * Load in % per line between two /proc/stat reads.
 * @return same indexing as CpuTicks; NaN where either read lacks that line or no time passed
 */
std::vector<double> cpuPercents(const CpuTicks& prev, const CpuTicks& cur);

// ---------------------------------------------------------------------------------------------
// Power tree

/** One max34417 channel as read. */
struct RailReading {
    std::string label;      ///< power*_label: vph, apc0, gfx, ...
    double watts = kNaN;    ///< power*_input / 1e6
    double volts = kNaN;    ///< in*_input / 1e3 (same label), NaN if the chip has no matching in*_label
};

/** The headset's draw as a tree: vph on top, the measured rails under it, and what's left. */
struct PowerTree {
    double vphW = kNaN;
    double vphV = kNaN;
    double railsW = kNaN;       ///< sum of every measured rail except vph; NaN if any rail failed to read
    double unmeasuredW = kNaN;  ///< vphW - railsW; NaN if either is NaN. Not clamped: chips sample at
                                ///< slightly different times, so it can dip a little below 0 under a load step
};

/**
 * Builds the tree. Never sums vph with the rails (that double-counts: the rails are fed from vph).
 * @param rails every used channel (unused already skipped), vph included
 */
PowerTree powerTree(const std::vector<RailReading>& rails);

/**
 * Whether a rail's own voltage sense means anything. gfx's reads 2-7 mV while the rail draws ~0.4 W
 * (probe 2026-10-11), so it's shown as "—".
 */
bool railVoltsMeaningful(const std::string& label);

// ---------------------------------------------------------------------------------------------
// Battery, charger, USB-PD

/**
 * Parses pm8550b-charger's status_detailed ("CC: 1\nCV: 0\n...").
 * @return flag name -> integer value; lines without an integer value are skipped
 */
std::map<std::string, int> parseStatusDetailed(const std::string& text);

/**
 * @param usbType power_supply usb_type, e.g. "[C] PD PD_PPS"
 * @return the bracketed (active) entry ("C"); the whole text if nothing is bracketed; "" for ""
 */
std::string activeUsbType(const std::string& usbType);

/** What the charger-mode decision looks at. Unknown ints are -1. */
struct ChargeModeInputs {
    bool haveBattery = false;
    bool haveCharger = false;
    bool plugged = false;            ///< charger online || USB source online
    std::string batteryStatus;       ///< power_supply status: Charging / Discharging / Full / Not charging
    int chargingEnable = -1;         ///< charging_enable
    int chargingPause = -1;          ///< charging_pause
    std::map<std::string, int> flags;  ///< parseStatusDetailed()
};

/**
 * Charging phase as a short pill: "Unplugged", "Boost", "Full", "Paused", "Off", "CC", "CV", "CC+ICL",
 * "JEITA", ..., "Charging", "Plugged" (plugged in, not charging). Same order of checks as FramePerf.
 * @return "" when neither the battery nor the charger was found
 */
std::string chargeMode(const ChargeModeInputs& in);

/**
 * Time to full (charging) or to empty (discharging) from the gauge's own mAh and the current, assuming the
 * current holds. FramePerf found the gauge's own time_to_*_now barely moved when the current changed 4x
 * (9511 s at 2.68 A vs 9528 s at 0.67 A, 2026-10-10), so this is computed instead.
 * @param toFull true for time to full, false for time to empty
 * @param status power_supply status
 * @param amps battery current in A, positive = into the battery (prefer current_avg)
 * @param chargeNowMah charge_now
 * @param chargeFullMah charge_full
 * @return seconds; NaN when the battery isn't in that state, the current is under 0.05 A, or an input is NaN
 */
double timeLeftSeconds(bool toFull, const std::string& status, double amps, double chargeNowMah,
                       double chargeFullMah);

/**
 * Voltage lost between the charger and the headset's USB input (cable, plugs, the charger itself).
 * @return max(0, offered - arriving); NaN if either is NaN
 */
double sagVolts(double offeredV, double arrivingV);

// ---------------------------------------------------------------------------------------------
// General

/**
 * @param text /proc/uptime ("8914.95 62459.80")
 * @param seconds result (first field: since boot, including suspend)
 */
bool parseUptime(const char* text, double& seconds);

/**
 * @param text /proc/self/statm ("617 292 271 7 0 146 0")
 * @param residentPages result (second field)
 */
bool parseStatmResident(const char* text, long long& residentPages);

/**
 * This overlay's estimated power: its share of all busy CPU time × the CPU rails' watts. An estimate
 * (shown with "≈"): it assumes every busy CPU second costs the same, whichever core and clock.
 * @param ownCpuSec own CPU time over the interval (all threads)
 * @param systemBusySec busy CPU time of the whole system over the same interval (all cores)
 * @param cpuRailsW apc0 + apc1 + apc2 watts
 * @return watts; NaN if an input is NaN or the system was idle
 */
double selfWatts(double ownCpuSec, double systemBusySec, double cpuRailsW);

}  // namespace sensor_math
