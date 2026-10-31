#include "scheduler.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace frame_perf_overlay {

namespace {

/** A source counts as due this much early, so sources that share the 2 Hz tick run together. */
constexpr double kDueSlackSec = 0.05;

/** After a resumed (baseline) read of a delta source, the next read comes this soon (or after the interval). */
constexpr double kResumeRefillSec = 0.25;

size_t indexOf(SourceId source) {
    return static_cast<size_t>(source);
}

/** @return true if the source's values are deltas between two reads, so its first read after a pause shows nothing */
bool readsDeltas(size_t index) {
    return index == indexOf(SourceId::CpuLoad) || index == indexOf(SourceId::Network) ||
           index == indexOf(SourceId::Processes) || index == indexOf(SourceId::SelfCost);
}

/** @return true if only a full (non-compact) card shows the source's metrics: General's Uptime, App and HUD cost rows */
bool fullCardOnly(size_t index) {
    return index == indexOf(SourceId::Uptime) || index == indexOf(SourceId::SelfCost) ||
           index == indexOf(SourceId::SceneApp);
}

bool validSource(SourceId source) {
    return source != SourceId::None && indexOf(source) < kSourceCount;
}

}  // namespace

Scheduler::Scheduler() : Scheduler(registry()) {}

Scheduler::Scheduler(const std::vector<MetricDef>& defs) {
    for (const MetricDef& d : defs) {
        if (!validSource(d.source) || d.intervalMs == 0) continue;
        SourceState& s = sources_[indexOf(d.source)];
        const double sec = d.intervalMs / 1000.0;
        if (s.interval == 0.0 || sec < s.interval) s.interval = sec;
        if (d.intervalMs == kPerUpdateMs) s.perUpdate = true;
        if (static_cast<size_t>(d.owner) < kWidgetTypeCount) s.ownerMask |= 1u << static_cast<unsigned>(d.owner);
    }
}

void Scheduler::addSource(SourceId source, ReadFn read) {
    if (!validSource(source)) return;
    sources_[indexOf(source)].read = std::move(read);
}

void Scheduler::setEnabledWidgets(const std::vector<WidgetType>& widgets) {
    setEnabledWidgets(widgets, std::vector<bool>(widgets.size(), false));
}

void Scheduler::setEnabledWidgets(const std::vector<WidgetType>& widgets, const std::vector<bool>& compact) {
    widgets_ = widgets;
    compact_ = compact;
    compact_.resize(widgets_.size(), false);
    recompute();
}

void Scheduler::setUpdateInterval(double seconds) {
    if (!(seconds > 0.0)) return;
    for (SourceState& s : sources_) {
        if (!s.perUpdate || s.interval == seconds) continue;
        s.interval = seconds;
        // Shorter: the next read comes at the new interval, not the old one's
        if (s.lastRead >= 0.0) s.nextDue = std::min(s.nextDue, s.lastRead + seconds);
    }
}

void Scheduler::setDemand(const DemandInputs& demand) {
    if (demand == demand_) return;
    demand_ = demand;
    recompute();
}

void Scheduler::recompute() {
    ++recomputes_;
    const bool looking = someoneLooking(demand_);
    for (size_t i = 0; i < kSourceCount; ++i) {
        SourceState& s = sources_[i];
        uint32_t refs = 0;
        if (looking && s.interval > 0.0) {
            for (size_t k = 0; k < widgets_.size(); ++k) {
                const WidgetType w = widgets_[k];
                if (compact_[k] && fullCardOnly(i)) continue;
                if (static_cast<size_t>(w) < kWidgetTypeCount && (s.ownerMask & (1u << static_cast<unsigned>(w)))) {
                    ++refs;
                }
            }
        }
        if (s.refs == 0 && refs > 0) {
            // Just became active: read on the next runDue, and tell the reader its baseline is stale
            s.nextDue = now_;
            s.pendingResume = true;
        }
        s.refs = refs;
    }
}

int Scheduler::runDue(double now) {
    now_ = now;
    int ran = 0;
    for (size_t i = 0; i < kSourceCount; ++i) {
        SourceState& s = sources_[i];
        if (s.refs == 0 || !s.read) continue;
        // At most a quarter of the interval early, so a fast caller (the 50 ms dashboard poll) can't read a 100 ms
        // source twice as often
        if (now < s.nextDue - std::min(kDueSlackSec, s.interval / 4)) continue;
        const bool resumed = s.pendingResume;
        s.pendingResume = false;
        s.read(now, resumed);
        ++s.reads;
        s.lastRead = now;
        if (resumed && readsDeltas(i) && s.interval > kResumeRefillSec) {
            // The first read after a pause only rebuilds a delta's baseline (CPU %, network rates), so one extra
            // read comes soon; the one after it is back on this read's grid, in step with the other sources
            s.nextDue = now + kResumeRefillSec;
            s.afterRefill = now + s.interval;
        } else if (s.afterRefill >= 0.0 && !resumed) {
            s.nextDue = s.afterRefill > now ? s.afterRefill : now + s.interval;  // a late refill doesn't catch up
            s.afterRefill = -1.0;
        } else {
            // From the due time when read a little early (keeps the cadence), from now when late (no catching up)
            s.nextDue = std::max(now, s.nextDue) + s.interval;
            s.afterRefill = -1.0;
        }
        ++ran;
    }
    return ran;
}

double Scheduler::nextDue(double now) const {
    double best = std::numeric_limits<double>::infinity();
    for (const SourceState& s : sources_) {
        if (s.refs == 0 || !s.read) continue;
        const double due = s.nextDue < now ? now : s.nextDue;
        if (due < best) best = due;
    }
    return best;
}

void Scheduler::requestRead(SourceId source) {
    if (!validSource(source)) return;
    SourceState& s = sources_[indexOf(source)];
    if (s.refs > 0 && s.read) s.nextDue = std::min(s.nextDue, now_);
}

uint32_t Scheduler::refCount(SourceId source) const {
    return validSource(source) ? sources_[indexOf(source)].refs : 0;
}

uint64_t Scheduler::readCount(SourceId source) const {
    return validSource(source) ? sources_[indexOf(source)].reads : 0;
}

double Scheduler::interval(SourceId source) const {
    return validSource(source) ? sources_[indexOf(source)].interval : 0.0;
}

std::string Scheduler::dump(double now) const {
    std::string out;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "demand: hud_shown=%d hmd_in_use=%d editor_open=%d -> looking=%d "
                  "(recomputed %llu times)\n",
                  demand_.hudShown, demand_.hmdInUse, demand_.editorOpen,
                  someoneLooking(demand_), static_cast<unsigned long long>(recomputes_));
    out += line;
    out += "enabled widgets:";
    for (WidgetType w : widgets_) {
        out += " ";
        out += widgetName(w);
        out += ",";
    }
    if (!widgets_.empty()) out.pop_back();
    out += "\n";
    std::snprintf(line, sizeof(line), "%-14s %9s %5s %8s %10s\n", "source", "interval", "refs", "reads",
                  "last read");
    out += line;
    for (size_t i = 1; i < kSourceCount; ++i) {
        const SourceState& s = sources_[i];
        if (s.interval == 0.0) continue;
        char last[32];
        if (s.lastRead < 0) {
            std::snprintf(last, sizeof(last), "never");
        } else {
            std::snprintf(last, sizeof(last), "%.1fs ago", now - s.lastRead);
        }
        std::snprintf(line, sizeof(line), "%-14s %8.1fs %5u %8llu %10s%s\n", sourceName(static_cast<SourceId>(i)),
                      s.interval, s.refs, static_cast<unsigned long long>(s.reads), last, s.read ? "" : "  (no reader)");
        out += line;
    }
    return out;
}

}  // namespace frame_perf_overlay
