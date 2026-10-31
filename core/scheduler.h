// Demand-driven reading. A source is read only when it's needed (an enabled widget owns a metric from it)
// AND someone is looking. Nothing else ever starts a read.
#pragma once

#include "metric_registry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace frame_perf_overlay {

/** The signals that decide whether anyone is looking. */
struct DemandInputs {
    bool hudShown = false;    ///< the HUD overlay is switched on
    bool hmdInUse = false;    ///< HMD activity level isn't standby (off-head reads standby)
    bool editorOpen = false;  ///< the dashboard tab (editor) is visible

    bool operator==(const DemandInputs& o) const {
        return hudShown == o.hudShown && hmdInUse == o.hmdInUse && editorOpen == o.editorOpen;
    }
    bool operator!=(const DemandInputs& o) const { return !(*this == o); }
};

/**
 * "Someone looking" = HUD shown and the headset in use, or the editor open.
 * @param d the signals
 * @return true if sources may be read
 */
inline bool someoneLooking(const DemandInputs& d) {
    return (d.hudShown && d.hmdInUse) || d.editorOpen;
}

/**
 * Keeps a reference count per source and runs the sources that are due.
 * Single-threaded: call everything from the main loop.
 */
class Scheduler {
public:
    /**
     * Reads one source and stores its metrics.
     * @param now monotonic seconds
     * @param resumed true on the first read after the source was inactive (or ever), so delta-based sources
     *                can rebuild their baseline instead of reporting an average over the pause
     */
    using ReadFn = std::function<void(double now, bool resumed)>;

    /** Uses the compiled-in registry. */
    Scheduler();
    /** @param defs registry table to use instead (tests) */
    explicit Scheduler(const std::vector<MetricDef>& defs);

    /**
     * Registers the reader for a source. A source with metrics but no reader is counted, never read.
     * @param source the source
     * @param read the reader
     */
    void addSource(SourceId source, ReadFn read);

    /**
     * Sets the enabled widget instances (a type may appear more than once, e.g. two Network widgets)
     * and recomputes the reference counts.
     */
    void setEnabledWidgets(const std::vector<WidgetType>& widgets);

    /**
     * Same, with each instance's compact flag (enabledCompact(layout)): a compact General doesn't count for the
     * sources only its full card shows (Uptime, SelfCost, SceneApp). The one-argument form treats every card as full.
     */
    void setEnabledWidgets(const std::vector<WidgetType>& widgets, const std::vector<bool>& compact);

    /**
     * Sets the interval of the sources read every update (those with a kPerUpdateMs metric) to update_interval_ms.
     * The others keep their registry interval.
     * @param seconds the interval (update_interval_ms / 1000)
     */
    void setUpdateInterval(double seconds);

    /** Sets the "someone looking" signals; recomputes the reference counts if anything changed. */
    void setDemand(const DemandInputs& demand);

    /**
     * Runs every active source that is due.
     * @param now monotonic seconds
     * @return number of sources read
     */
    int runDue(double now);

    /**
     * @param now monotonic seconds
     * @return when the next active source is due (monotonic seconds); +infinity if none is active
     */
    double nextDue(double now) const;

    /**
     * Makes the source due now, so the next runDue reads it, if it is wanted (a widget on the panel uses it and
     * someone is looking). Does nothing otherwise: the read then happens when it becomes wanted, as usual.
     * For events that say a value changed (the scene app).
     */
    void requestRead(SourceId source);

    /** @return the source's reference count (0 when nobody is looking) */
    uint32_t refCount(SourceId source) const;
    /** @return how many times the source has been read since start */
    uint64_t readCount(SourceId source) const;
    /** @return the source's read interval in seconds (shortest interval among its metrics); 0 if it has none */
    double interval(SourceId source) const;
    /** @return how many times the reference counts were recomputed */
    uint64_t recomputeCount() const { return recomputes_; }
    /** @return the current signals */
    const DemandInputs& demand() const { return demand_; }

    /**
     * Human-readable state: signals, enabled widgets, and per source: interval, refs, reads, last read.
     * @param now monotonic seconds
     */
    std::string dump(double now) const;

private:
    struct SourceState {
        double interval = 0.0;  ///< seconds; 0 = no metric uses this source
        uint32_t refs = 0;
        uint64_t reads = 0;
        double lastRead = -1.0;
        double nextDue = 0.0;
        bool pendingResume = true;  ///< next read is the first since (re)activation
        double afterRefill = -1.0;  ///< >= 0: the next read is the resume refill, and the one after is due then
        bool perUpdate = false;     ///< a metric of it is read every update (kPerUpdateMs): setUpdateInterval moves it
        ReadFn read;
        /** Widgets owning at least one metric from this source (bit per WidgetType). */
        uint32_t ownerMask = 0;
    };

    std::array<SourceState, kSourceCount> sources_ {};
    std::vector<WidgetType> widgets_;
    std::vector<bool> compact_;  ///< per widgets_ entry: the card is compact
    DemandInputs demand_;
    uint64_t recomputes_ = 0;
    double now_ = 0.0;  ///< last time passed to runDue (activation schedules the read for then)

    void recompute();
};

}  // namespace frame_perf_overlay
