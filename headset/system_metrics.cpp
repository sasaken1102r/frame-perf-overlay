#include "system_metrics.h"

#include "sensor_math.h"

#include <cmath>
#include <ctime>

using frame_perf_overlay::Reason;
using frame_perf_overlay::SourceId;
namespace metric = frame_perf_overlay::metric;

namespace {

/** Measured rails by max34417 label; volts are at id + kPowerRailVoltsOffset. vph has its own ids. */
struct RailId {
    const char* label;
    uint16_t wattsId;
};
constexpr RailId kRails[] = {
    {"apc0", metric::kPowerApc0W}, {"apc1", metric::kPowerApc1W}, {"apc2", metric::kPowerApc2W},
    {"gfx", metric::kPowerGfxW},   {"nsp1", metric::kPowerNsp1W}, {"nsp2", metric::kPowerNsp2W},
    {"s1c", metric::kPowerS1cW},   {"s3c", metric::kPowerS3cW},   {"s6c", metric::kPowerS6cW},
    {"bob", metric::kPowerBobW},
};

/** Stores a NaN-able seconds value as i32, or N/A with the reason. */
void setSeconds(frame_perf_overlay::MetricStore& st, uint16_t id, double seconds, double now, Reason nanReason) {
    if (std::isnan(seconds)) {
        st.setNa(id, nanReason, now);
    } else {
        st.setI32(id, static_cast<int32_t>(std::lround(seconds)), now);
    }
}

/** Stores a short string, or N/A with the reason when it's empty. */
void setTextOr(frame_perf_overlay::MetricStore& st, uint16_t id, const std::string& text, double now,
               Reason emptyReason) {
    if (text.empty()) {
        st.setNa(id, emptyReason, now);
    } else {
        st.setText(id, text, now);
    }
}

uint32_t batteryStatusCode(const std::string& status) {
    using frame_perf_overlay::BatteryStatus;
    if (status == "Charging") return static_cast<uint32_t>(BatteryStatus::Charging);
    if (status == "Discharging") return static_cast<uint32_t>(BatteryStatus::Discharging);
    if (status == "Full") return static_cast<uint32_t>(BatteryStatus::Full);
    if (status == "Not charging") return static_cast<uint32_t>(BatteryStatus::NotCharging);
    return static_cast<uint32_t>(BatteryStatus::Unknown);
}

void storePower(frame_perf_overlay::MetricStore& st, const SensorSample& s, double now) {
    bool haveVph = false;
    for (const PowerChannel& ch : s.powerChannels) haveVph = haveVph || ch.label == "vph";
    st.setF32(metric::kPowerVphW, s.powerMainW, now, haveVph ? Reason::ReadFailed : Reason::SensorMissing);
    // Never vph + rails: the rails are fed from vph, so that sum double-counts
    const bool haveRails = s.powerChannels.size() > (haveVph ? 1u : 0u);
    st.setF32(metric::kPowerRailsW, s.powerRailsW, now, haveRails ? Reason::ReadFailed : Reason::SensorMissing);

    // The full (non-compact) tree: vph's volts, each rail's W + V, and what no rail measures
    const PowerChannel* vph = nullptr;
    for (const PowerChannel& ch : s.powerChannels) {
        if (ch.label == "vph" && vph == nullptr) vph = &ch;
    }
    const Reason noVph = haveVph ? Reason::ReadFailed : Reason::SensorMissing;
    st.setF32(metric::kPowerVphV, vph != nullptr ? vph->volts : std::nan(""), now, noVph);
    st.setF32(metric::kPowerUnmeasuredW, s.powerUnmeasuredW, now, noVph);
    for (const RailId& rail : kRails) {
        const uint16_t voltsId = rail.wattsId + metric::kPowerRailVoltsOffset;
        const PowerChannel* found = nullptr;
        for (const PowerChannel& ch : s.powerChannels) {
            if (ch.label == rail.label) {
                found = &ch;
                break;
            }
        }
        if (found == nullptr) {
            st.setNa(rail.wattsId, Reason::SensorMissing, now);
            st.setNa(voltsId, Reason::SensorMissing, now);
            continue;
        }
        st.setF32(rail.wattsId, found->watts, now);
        if (!sensor_math::railVoltsMeaningful(found->label)) {
            st.setNa(voltsId, Reason::InvalidReading, now);  // gfx: shown as "—"
        } else {
            st.setF32(voltsId, found->volts, now);
        }
    }
}

void storeBattery(frame_perf_overlay::MetricStore& st, const SensorSample& s, double now) {
    const Reason bat = s.haveBattery ? Reason::ReadFailed : Reason::SensorMissing;
    st.setF32(metric::kBatteryPct, s.batteryPct, now, bat);
    st.setF32(metric::kBatteryVoltageV, s.batteryVoltageV, now, bat);
    st.setF32(metric::kBatteryCurrentMa, s.batteryCurrentMa, now, bat);
    st.setF32(metric::kTempBatteryC, s.batteryTempC, now, bat);
    if (s.haveBattery) {
        st.setEnum(metric::kBatteryStatus, batteryStatusCode(s.batteryStatus), now);
    } else {
        st.setNa(metric::kBatteryStatus, Reason::SensorMissing, now);
    }
    if (s.haveCharger) {
        st.setEnum(metric::kBatteryChargerOnline, s.chargerOnline ? 1 : 0, now);
    } else {
        st.setNa(metric::kBatteryChargerOnline, Reason::SensorMissing, now);
    }
    // Driver sign: positive = into the battery (checked while charging)
    st.setF32(metric::kBatteryPowerW, s.batteryVoltageV * s.batteryCurrentMa / 1000.0, now, bat);

    // The full (non-compact) rows
    const Reason chg = s.haveCharger ? Reason::ReadFailed : Reason::SensorMissing;
    st.setF32(metric::kBatteryChargeNowMah, s.chargeNowMah, now, bat);
    st.setF32(metric::kBatteryChargeFullMah, s.chargeFullMah, now, bat);
    st.setF32(metric::kBatteryChargeDesignMah, s.chargeDesignMah, now, bat);
    setTextOr(st, metric::kBatteryHealth, s.batteryHealth, now, bat);
    if (s.batteryCycles >= 0) {
        st.setI32(metric::kBatteryCycles, static_cast<int32_t>(s.batteryCycles), now);
    } else {
        st.setNa(metric::kBatteryCycles, bat, now);
    }
    const bool plugged = s.chargerOnline || s.sourceOnline;
    if (s.haveCharger || s.haveSource) {
        st.setEnum(metric::kBatteryPlugged, plugged ? 1 : 0, now);
    } else {
        st.setNa(metric::kBatteryPlugged, Reason::SensorMissing, now);
    }
    // Time to full / empty from the gauge's mAh and the averaged current
    const double amps = (std::isnan(s.batteryAvgCurrentMa) ? s.batteryCurrentMa : s.batteryAvgCurrentMa) / 1000.0;
    setSeconds(st, metric::kBatteryTimeToFullS,
               sensor_math::timeLeftSeconds(true, s.batteryStatus, amps, s.chargeNowMah, s.chargeFullMah), now,
               !s.haveBattery ? Reason::SensorMissing : plugged ? Reason::InvalidReading : Reason::NotConnected);
    setSeconds(st, metric::kBatteryTimeToEmptyS,
               sensor_math::timeLeftSeconds(false, s.batteryStatus, amps, s.chargeNowMah, s.chargeFullMah), now,
               s.haveBattery ? Reason::InvalidReading : Reason::SensorMissing);
    sensor_math::ChargeModeInputs mode;
    mode.haveBattery = s.haveBattery;
    mode.haveCharger = s.haveCharger;
    mode.plugged = plugged;
    mode.batteryStatus = s.batteryStatus;
    mode.chargingEnable = s.chargingEnable;
    mode.chargingPause = s.chargingPause;
    mode.flags = sensor_math::parseStatusDetailed(s.chargerStatusDetailed);
    setTextOr(st, metric::kBatteryChargerMode, sensor_math::chargeMode(mode), now, Reason::SensorMissing);
    st.setF32(metric::kBatterySetCurrentA, s.setCurrentA, now, chg);
    st.setF32(metric::kBatterySetCurrentMaxA, s.setCurrentMaxA, now, chg);
    // USB-PD: what the charger offers (the contract) vs what arrives at the USB input
    const Reason src = !s.haveSource ? Reason::SensorMissing : Reason::NotConnected;
    if (s.sourceOnline) {
        setTextOr(st, metric::kBatteryUsbType, sensor_math::activeUsbType(s.usbType), now, Reason::ReadFailed);
        st.setF32(metric::kBatteryPdOfferedV, s.sourceV, now);
        st.setF32(metric::kBatteryPdOfferedA, s.sourceAMax, now);
        st.setF32(metric::kBatteryPdOfferedW, s.sourceV * s.sourceAMax, now);
    } else {
        for (uint16_t id : {metric::kBatteryUsbType, metric::kBatteryPdOfferedV, metric::kBatteryPdOfferedA,
                            metric::kBatteryPdOfferedW}) {
            st.setNa(id, src, now);
        }
    }
    if (plugged) {
        st.setF32(metric::kBatteryInputV, s.usbInV, now);
        st.setF32(metric::kBatteryInputA, s.usbInA, now);
        st.setF32(metric::kBatteryInputW, s.usbInV * s.usbInA, now);
    } else {
        for (uint16_t id : {metric::kBatteryInputV, metric::kBatteryInputA, metric::kBatteryInputW}) {
            st.setNa(id, Reason::NotConnected, now);
        }
    }
    if (plugged && s.sourceOnline) {
        st.setF32(metric::kBatterySagV, sensor_math::sagVolts(s.sourceV, s.usbInV), now);
    } else {
        st.setNa(metric::kBatterySagV, s.haveSource ? Reason::NotConnected : Reason::SensorMissing, now);
    }
}

/** Stores one adapter's block (metric::kNetDongleBase / kNetHomeBase + kNetAdapter* offsets). */
void storeAdapter(frame_perf_overlay::MetricStore& st, uint16_t base, const AdapterInfo& a, bool resumed, double now) {
    using namespace metric;
    st.setEnum(base + kNetAdapterPresent, a.present ? 1 : 0, now);
    const uint16_t rest[] = {kNetAdapterConnected,  kNetAdapterRxMbps,    kNetAdapterTxMbps,  kNetAdapterTxLinkMbps,
                             kNetAdapterRxLinkMbps, kNetAdapterSignalDbm, kNetAdapterIp,      kNetAdapterFreqMhz,
                             kNetAdapterChannel,    kNetAdapterWidthMhz,  kNetAdapterBand,    kNetAdapterIfName};
    if (!a.present) {
        for (uint16_t offset : rest) st.setNa(base + offset, Reason::SensorMissing, now);
        return;
    }
    st.setText(base + kNetAdapterIfName, a.ifName, now);
    st.setEnum(base + kNetAdapterConnected, a.connected ? 1 : 0, now);
    // Byte counters work without a peer; NaN = the first read after a (re)start
    const Reason firstRead = resumed ? Reason::NotSampledYet : Reason::ReadFailed;
    st.setF32(base + kNetAdapterRxMbps, a.rxMbps, now, firstRead);
    st.setF32(base + kNetAdapterTxMbps, a.txMbps, now, firstRead);
    const Reason noPeer = a.connected ? Reason::ReadFailed : Reason::NotConnected;
    st.setF32(base + kNetAdapterTxLinkMbps, a.txLinkMbps, now, noPeer);
    st.setF32(base + kNetAdapterRxLinkMbps, a.rxLinkMbps, now, noPeer);
    st.setF32(base + kNetAdapterSignalDbm, a.signalDbm, now, noPeer);
    setTextOr(st, base + kNetAdapterIp, a.ip, now, Reason::NotConnected);
    // nl80211 reports a channel only while the interface is on one
    if (a.freqMhz > 0) {
        st.setI32(base + kNetAdapterFreqMhz, a.freqMhz, now);
        const int channel = channelFromFreq(a.freqMhz);
        if (channel > 0) {
            st.setI32(base + kNetAdapterChannel, channel, now);
        } else {
            st.setNa(base + kNetAdapterChannel, Reason::InvalidReading, now);
        }
        st.setEnum(base + kNetAdapterBand, bandFromFreq(a.freqMhz), now);
    } else {
        st.setNa(base + kNetAdapterFreqMhz, Reason::NotConnected, now);
        st.setNa(base + kNetAdapterChannel, Reason::NotConnected, now);
        st.setNa(base + kNetAdapterBand, Reason::NotConnected, now);
    }
    if (a.widthMhz > 0) {
        st.setI32(base + kNetAdapterWidthMhz, a.widthMhz, now);
    } else {
        st.setNa(base + kNetAdapterWidthMhz, a.freqMhz > 0 ? Reason::ReadFailed : Reason::NotConnected, now);
    }
}

void storeWifi(frame_perf_overlay::MetricStore& st, const WifiInfo& w, bool resumed, double now) {
    st.setEnum(metric::kNetInterfaceUp, w.interfaceUp ? 1 : 0, now);
    st.setEnum(metric::kNetConnected, w.connected ? 1 : 0, now);
    st.setEnum(metric::kNetHomeWifi, w.homeWifi ? 1 : 0, now);
    const Reason missing = w.connected ? Reason::ReadFailed : Reason::NotConnected;
    st.setF32(metric::kNetSignalDbm, w.signalDbm, now, missing);
    st.setF32(metric::kNetTxLinkMbps, w.txLinkMbps, now, missing);
    st.setF32(metric::kNetRxLinkMbps, w.rxLinkMbps, now, missing);
    // The rates are a delta: NaN on the first read after a (re)start
    const Reason noRate = !w.connected ? Reason::NotConnected : (resumed ? Reason::NotSampledYet : Reason::ReadFailed);
    st.setF32(metric::kNetRxMbps, w.rxMbps, now, noRate);
    st.setF32(metric::kNetTxMbps, w.txMbps, now, noRate);
}

}  // namespace

void registerSystemSources(frame_perf_overlay::Scheduler& scheduler, Sensors& sensors,
                           frame_perf_overlay::MetricStore& store) {
    frame_perf_overlay::MetricStore& st = store;
    Sensors& se = sensors;

    scheduler.addSource(SourceId::CpuLoad, [&st, &se](double now, bool resumed) {
        // A delta: after a pause, rebuild the baseline instead of showing the average over the pause
        if (resumed) se.resetCpuBaseline();
        SensorSample s;
        se.readCpuUsage(s);
        const Reason nan = resumed ? Reason::NotSampledYet : Reason::ReadFailed;
        st.setF32(metric::kCpuUsagePct, s.cpuUsagePct, now, nan);
        st.setF32(metric::kCpuMaxCorePct, s.cpuMaxCorePct, now, nan);
        for (uint16_t i = 0; i < metric::kCpuCoreSlots; ++i) {
            const uint16_t id = metric::kCpuCore0Pct + i;
            if (i < s.corePct.size()) {
                st.setF32(id, s.corePct[i], now, nan);
            } else {
                // After the first delta, a slot past the last core is a core this SoC doesn't have
                st.setNa(id, s.corePct.empty() ? nan : Reason::SensorMissing, now);
            }
        }
    });
    scheduler.addSource(SourceId::CpuClock, [&st, &se](double now, bool) {
        SensorSample s;
        se.readCpuClocks(s);
        st.setF32(metric::kCpuFastestClockMhz, s.cpuFreqMHz, now);
        for (uint16_t i = 0; i < metric::kCpuClusterSlots; ++i) {
            if (i < s.clusters.size()) {
                st.setF32(metric::kCpuCluster0Mhz + i, s.clusters[i].curMHz, now);
                st.setF32(metric::kCpuCluster0MaxMhz + i, s.clusters[i].maxMHz, now);
                st.setF32(metric::kCpuCluster0HwMaxMhz + i, s.clusters[i].hwMaxMHz, now, Reason::SensorMissing);
            } else {
                st.setNa(metric::kCpuCluster0Mhz + i, Reason::SensorMissing, now);
                st.setNa(metric::kCpuCluster0MaxMhz + i, Reason::SensorMissing, now);
                st.setNa(metric::kCpuCluster0HwMaxMhz + i, Reason::SensorMissing, now);
            }
        }
    });
    scheduler.addSource(SourceId::GpuClock, [&st, &se](double now, bool) {
        SensorSample s;
        se.readGpuClock(s);
        st.setF32(metric::kGpuClockMhz, s.gpuFreqMHz, now);
        st.setF32(metric::kGpuMaxClockMhz, s.gpuMaxFreqMHz, now);
    });
    scheduler.addSource(SourceId::Ram, [&st, &se](double now, bool) {
        SensorSample s;
        se.readMemory(s);
        st.setF32(metric::kRamUsedGib, s.memUsedGiB, now);
        st.setF32(metric::kRamTotalGib, s.memTotalGiB, now);
    });
    scheduler.addSource(SourceId::Temps, [&st, &se](double now, bool) {
        SensorSample s;
        se.readThermal(s);
        st.setF32(metric::kTempCpuC, s.cpuTempC, now);
        st.setF32(metric::kTempGpuC, s.gpuTempC, now);
        st.setF32(metric::kTempDdrC, s.ddrTempC, now);
        st.setF32(metric::kTempNpuC, s.npuTempC, now);
        st.setF32(metric::kTempCameraC, s.cameraTempC, now);
        st.setF32(metric::kTempVideoC, s.videoTempC, now);
    });
    scheduler.addSource(SourceId::BoardTemps, [&st, &se](double now, bool) {
        SensorSample s;
        se.readIioTemps(s);
        st.setF32(metric::kTempDisplayC, s.displayTempC, now);
        st.setF32(metric::kTempExhaustC, s.exhaustTempC, now);
        st.setF32(metric::kTempHeatsinkC, s.heatsinkTempC, now);
        st.setF32(metric::kTempWifiC, s.wifiTempC, now);
        st.setF32(metric::kTempFlashC, s.flashTempC, now);
        st.setF32(metric::kTempNrfC, s.nrfTempC, now);
        st.setF32(metric::kTempConnectorC, s.connectorTempC, now);
    });
    scheduler.addSource(SourceId::Throttle, [&st, &se](double now, bool) {
        SensorSample s;
        se.readThrottle(s);
        st.setEnum(metric::kThrottleCpu, s.throttleCpu ? 1 : 0, now);
        st.setEnum(metric::kThrottleGpu, s.throttleGpu ? 1 : 0, now);
    });
    scheduler.addSource(SourceId::Fan, [&st, &se](double now, bool) {
        SensorSample s;
        se.readFan(s);
        st.setF32(metric::kFanRpm, s.fanRpm, now);
    });
    scheduler.addSource(SourceId::Power, [&st, &se](double now, bool) {
        SensorSample s;
        se.readPower(s);
        storePower(st, s, now);
    });
    scheduler.addSource(SourceId::Battery, [&st, &se](double now, bool) {
        SensorSample s;
        se.readBattery(s);
        storeBattery(st, s, now);
    });
    scheduler.addSource(SourceId::Network, [&st, &se](double now, bool resumed) {
        // Byte-counter rates: after a pause they'd be the average over the pause
        if (resumed) se.resetWifiCounters();
        SensorSample s;
        se.readWifi(s, now);
        storeWifi(st, s.wifi, resumed, now);
        storeAdapter(st, metric::kNetDongleBase, s.adapters.dongle, resumed, now);
        storeAdapter(st, metric::kNetHomeBase, s.adapters.home, resumed, now);
    });
    scheduler.addSource(SourceId::Uptime, [&st, &se](double now, bool) {
        SensorSample s;
        se.readUptime(s);
        if (std::isnan(s.uptimeS)) {
            st.setNa(metric::kUptimeS, Reason::ReadFailed, now);
        } else {
            st.setI32(metric::kUptimeS, static_cast<int32_t>(s.uptimeS), now);
        }
    });
    scheduler.addSource(SourceId::SelfCost, [&st, &se](double now, bool resumed) {
        if (resumed) se.resetSelfBaseline();
        SensorSample s;
        se.readSelfCost(s, now);
        const Reason nan = resumed ? Reason::NotSampledYet : Reason::ReadFailed;
        st.setF32(metric::kSelfCpuPct, s.selfCpuPct, now, nan);
        st.setF32(metric::kSelfRssMib, s.selfRssMiB, now);
        st.setF32(metric::kSelfPowerW, s.selfPowerW, now, nan);
    });
    scheduler.addSource(SourceId::Clock, [&st](double now, bool) {
        const std::time_t t = std::time(nullptr);
        std::tm tm {};
        char text[32];
        if (localtime_r(&t, &tm) != nullptr && std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm) > 0) {
            st.setText(metric::kClock, text, now);
        } else {
            st.setNa(metric::kClock, Reason::ReadFailed, now);
        }
    });
}
