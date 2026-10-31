// Bridge between the readers (Sensors, VrOverlay) and the metric registry. Each scheduler source calls one reader
// and stores its metrics in the MetricStore; the widgets read the store.
#pragma once

#include "history.h"
#include "layout.h"
#include "metric_store.h"
#include "process_scan.h"
#include "scheduler.h"
#include "sensors.h"
#include "widgets.h"

#include <vector>

class HideRules;
class VrOverlay;

class SensorBridge {
public:
    /**
     * @param sensors the sensors (discover() already called)
     * @param vr the overlay for frame timings and controllers, or nullptr when running without OpenVR
     * @param store where values go
     */
    SensorBridge(Sensors& sensors, VrOverlay* vr, frame_perf_overlay::MetricStore& store);

    /** Registers one reader per source with the scheduler. */
    void registerSources(frame_perf_overlay::Scheduler& scheduler);

    /** @return the sensor sample, rebuilt from the store (N/A → NaN / false / empty), for --print */
    SensorSample sample() const;

    /** @return every widget type, for --print (reads every system source) */
    static std::vector<frame_perf_overlay::WidgetType> allWidgets();

    /** Takes the CPU / RAM items widgets' settings (rows, one row per name, Hide system) from the layout. */
    void setProcessOptions(const frame_perf_overlay::Layout& layout);
    /** The rules Hide system uses (nullptr = nothing is hidden). Not owned; must outlive the bridge. */
    void setHideRules(HideRules* rules) { processes_.setHideRules(rules); }
    /** @return the process lists of the last scan, for WidgetContext::processes */
    const frame_perf_overlay::ProcessListsView& processLists() const { return processView_; }
private:
    /** The SceneApp source's reader (VREvent_SceneApplicationChanged makes it due: Scheduler::requestRead). */
    void refreshSceneApp(double now);
    Sensors& sensors_;
    VrOverlay* vr_;
    frame_perf_overlay::MetricStore& store_;
    ProcessScanner processes_;
    ProcessListOptions processOptions_;
    frame_perf_overlay::ProcessListsView processView_;

    void readFrameTiming(double now);
    void readControllers(double now);
    void readProcesses(double now, bool resumed);
};
