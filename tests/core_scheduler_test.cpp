// core/ scheduler tests: reference counting, visibility rules, intervals (CTest runs them; no platform code).
#include "layout.h"
#include "scheduler.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #expr);                  \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

using namespace frame_perf_overlay;
using W = WidgetType;
using S = SourceId;

// 3 sources, 3 widgets. CpuLoad is shared by Cpu and General; Ram has two metrics (shortest interval wins).
const std::vector<MetricDef> kDefs = {
    {1, "cpu.a", "", W::Cpu, S::CpuLoad, 500, WireType::F32},
    {2, "cpu.shared", "", W::General, S::CpuLoad, 1000, WireType::F32},
    {3, "gpu.busy", "", W::GpuRam, S::GpuClock, 2000, WireType::F32},
    {4, "ram.used", "", W::GpuRam, S::Ram, 2000, WireType::F32},
    {5, "ram.total", "", W::GpuRam, S::Ram, 1000, WireType::F32},
};

struct Counts {
    int cpu = 0, gpu = 0, ram = 0;
    bool lastCpuResumed = false;
    std::vector<bool> cpuResumed;
};

struct Rig {
    Scheduler sched {kDefs};
    Counts c;
    double now = 0.0;
    Rig() {
        sched.addSource(S::CpuLoad, [this](double, bool resumed) {
            ++c.cpu;
            c.lastCpuResumed = resumed;
            c.cpuResumed.push_back(resumed);
        });
        sched.addSource(S::GpuClock, [this](double, bool) { ++c.gpu; });
        sched.addSource(S::Ram, [this](double, bool) { ++c.ram; });
    }
    /** Runs the scheduler on a 500 ms tick for the given number of ticks. */
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            sched.runDue(now);
            now += 0.5;
        }
    }
    void look() {
        DemandInputs d;
        d.editorOpen = true;
        sched.setDemand(d);
    }
};

void testNothingWithoutWidgets() {
    Rig r;
    r.look();
    r.run(10);
    CHECK(r.c.cpu == 0 && r.c.gpu == 0 && r.c.ram == 0);
    CHECK(r.sched.refCount(S::CpuLoad) == 0);
}

void testEnableDisable() {
    Rig r;
    r.look();
    r.sched.setEnabledWidgets({W::Cpu});
    CHECK(r.sched.refCount(S::CpuLoad) == 1);
    CHECK(r.sched.refCount(S::GpuClock) == 0);
    r.run(4);
    CHECK(r.c.cpu == 4);  // 500 ms source, 500 ms ticks
    CHECK(r.sched.readCount(S::CpuLoad) == 4);
    CHECK(r.c.gpu == 0);

    r.sched.setEnabledWidgets({});
    CHECK(r.sched.refCount(S::CpuLoad) == 0);
    const int frozen = r.c.cpu;
    r.run(10);
    CHECK(r.c.cpu == frozen);
}

void testSharedSource() {
    Rig r;
    r.look();
    r.sched.setEnabledWidgets({W::Cpu, W::General});
    CHECK(r.sched.refCount(S::CpuLoad) == 2);
    r.sched.setEnabledWidgets({W::General});
    CHECK(r.sched.refCount(S::CpuLoad) == 1);
    r.run(4);
    CHECK(r.c.cpu > 0);
}

void testTwoInstances() {
    Rig r;
    r.look();
    r.sched.setEnabledWidgets({W::Cpu, W::Cpu});
    CHECK(r.sched.refCount(S::CpuLoad) == 2);
}

void testVisibility() {
    Rig r;
    r.sched.setEnabledWidgets({W::Cpu});

    DemandInputs d;
    d.hudShown = true;
    d.hmdInUse = false;
    r.sched.setDemand(d);
    r.run(4);
    CHECK(r.c.cpu == 0);  // shown but headset in standby

    d.hmdInUse = true;
    r.sched.setDemand(d);
    r.run(4);
    CHECK(r.c.cpu == 4);

    d.hudShown = false;
    r.sched.setDemand(d);
    const int frozen = r.c.cpu;
    r.run(6);
    CHECK(r.c.cpu == frozen);
}

void testSingleSignals() {
    // the editor alone is enough; the HUD alone (headset in standby) is not
    {
        Rig r;
        r.sched.setEnabledWidgets({W::Cpu});
        DemandInputs d;
        d.editorOpen = true;
        r.sched.setDemand(d);
        r.run(4);
        CHECK(r.c.cpu == 4);

        r.sched.setDemand(DemandInputs {});
        const int frozen = r.c.cpu;
        r.run(10);
        CHECK(r.c.cpu == frozen);
    }
    {
        Rig r;
        r.sched.setEnabledWidgets({W::Cpu});
        DemandInputs d;
        d.hmdInUse = true;  // on the head, but the HUD is off
        r.sched.setDemand(d);
        r.run(4);
        CHECK(r.c.cpu == 0);
    }
}

void testResumed() {
    Rig r;
    r.look();
    r.sched.setEnabledWidgets({W::Cpu});
    r.run(3);
    CHECK(r.c.cpuResumed.size() == 3);
    CHECK(r.c.cpuResumed.size() == 3 && r.c.cpuResumed[0] && !r.c.cpuResumed[1] && !r.c.cpuResumed[2]);

    // pause, then reactivate: first read is resumed again
    r.sched.setDemand(DemandInputs {});
    r.run(4);
    r.look();
    r.c.cpuResumed.clear();
    r.run(3);
    CHECK(r.c.cpuResumed.size() == 3);
    CHECK(r.c.cpuResumed.size() == 3 && r.c.cpuResumed[0] && !r.c.cpuResumed[1] && !r.c.cpuResumed[2]);
}

void testIntervals() {
    Rig r;
    CHECK(std::fabs(r.sched.interval(S::CpuLoad) - 0.5) < 1e-9);  // shortest of 500 / 1000
    CHECK(std::fabs(r.sched.interval(S::GpuClock) - 2.0) < 1e-9);
    CHECK(std::fabs(r.sched.interval(S::Ram) - 1.0) < 1e-9);       // shortest of 2000 / 1000
    CHECK(r.sched.interval(S::Fan) == 0.0);                        // no metric uses it

    CHECK(std::isinf(r.sched.nextDue(0.0)));  // nothing active

    r.look();
    r.sched.setEnabledWidgets({W::GpuRam});
    r.run(8);  // 4 seconds of 500 ms ticks
    CHECK(r.c.gpu == 2);  // reads at t=0 and t=2 only (t=4 not reached)
    CHECK(r.c.ram == 4);  // 1 s interval: t=0,1,2,3
    CHECK(!std::isinf(r.sched.nextDue(r.now)));

    r.sched.setEnabledWidgets({});
    CHECK(std::isinf(r.sched.nextDue(r.now)));
}

/** Runs the scheduler over `seconds` of fake time in `step` increments from `*now`. @return reads of `source` */
uint64_t readsOver(Rig& r, double* now, double seconds, double step, S source) {
    const uint64_t before = r.sched.readCount(source);
    const int n = static_cast<int>(std::lround(seconds / step));
    for (int i = 0; i < n; ++i) {
        r.sched.runDue(*now);
        *now += step;
    }
    return r.sched.readCount(source) - before;
}

// setUpdateInterval moves the sources with a 500 ms (per update) metric and leaves the fixed ones alone
void testUpdateInterval() {
    // kDefs: CpuLoad has a 500 ms metric (per update); GpuClock is a fixed 2 s; Ram is 1 s / 2 s
    {
        Rig r;
        r.look();
        r.sched.setEnabledWidgets({W::Cpu, W::GpuRam});
        r.sched.setUpdateInterval(0.1);
        CHECK(std::fabs(r.sched.interval(S::CpuLoad) - 0.1) < 1e-9);
        CHECK(std::fabs(r.sched.interval(S::GpuClock) - 2.0) < 1e-9);
        CHECK(std::fabs(r.sched.interval(S::Ram) - 1.0) < 1e-9);
        double now = 0.0;
        const uint64_t cpu = readsOver(r, &now, 2.1, 0.1, S::CpuLoad);  // ticks at 0.0 .. 2.0
        CHECK(cpu == 21);  // every 100 ms tick
        CHECK(r.sched.readCount(S::GpuClock) == 2);  // t = 0 and t = 2: still 2 s
    }
    {
        Rig r;
        r.look();
        r.sched.setEnabledWidgets({W::Cpu});
        r.sched.setUpdateInterval(1.0);
        double now = 0.0;
        const uint64_t cpu = readsOver(r, &now, 5.0, 0.1, S::CpuLoad);
        CHECK(cpu >= 5 && cpu <= 6);  // every second
    }

    // A caller polling faster than the interval (every 20 ms) still reads a 100 ms source about 10 times a second,
    // not 20: the early-read slack is at most a quarter of the interval
    {
        Rig r;
        r.look();
        r.sched.setEnabledWidgets({W::Cpu});
        r.sched.setUpdateInterval(0.1);
        double now = 0.0;
        const uint64_t cpu = readsOver(r, &now, 1.0, 0.02, S::CpuLoad);
        CHECK(cpu >= 9 && cpu <= 11);
    }

    // Shortening after a read takes effect at lastRead + the new interval, not after the old one
    {
        Rig r;
        r.look();
        r.sched.setEnabledWidgets({W::Cpu});
        r.sched.setUpdateInterval(5.0);
        r.sched.runDue(0.0);  // the baseline read, then the quick refill read
        r.sched.runDue(0.25);
        CHECK(r.sched.readCount(S::CpuLoad) == 2);
        r.sched.runDue(1.0);
        CHECK(r.sched.readCount(S::CpuLoad) == 2);  // 5 s: not due
        r.sched.setUpdateInterval(0.1);
        r.sched.runDue(1.05);
        CHECK(r.sched.readCount(S::CpuLoad) == 3);  // lastRead (0.25) + 0.1 has long passed
        r.sched.runDue(1.06);  // just read at 1.05: the next one is a tick away
        CHECK(r.sched.readCount(S::CpuLoad) == 3);
        r.sched.runDue(1.20);
        CHECK(r.sched.readCount(S::CpuLoad) == 4);
    }

    // Zero and negative are ignored
    {
        Rig r;
        r.sched.setUpdateInterval(0.2);
        r.sched.setUpdateInterval(0.0);
        r.sched.setUpdateInterval(-3.0);
        CHECK(std::fabs(r.sched.interval(S::CpuLoad) - 0.2) < 1e-9);
    }

    // Demand gating still applies: nobody looking, no reads at any interval
    {
        Rig r;
        r.sched.setEnabledWidgets({W::Cpu});
        r.sched.setUpdateInterval(0.1);
        double now = 0.0;
        CHECK(readsOver(r, &now, 2.0, 0.1, S::CpuLoad) == 0);
        DemandInputs d;
        d.hudShown = true;  // shown but the headset is off the head
        r.sched.setDemand(d);
        CHECK(readsOver(r, &now, 2.0, 0.1, S::CpuLoad) == 0);
        d.hmdInUse = true;
        r.sched.setDemand(d);
        CHECK(readsOver(r, &now, 1.0, 0.1, S::CpuLoad) >= 9);
        d.hudShown = false;  // hidden again
        r.sched.setDemand(d);
        CHECK(readsOver(r, &now, 2.0, 0.1, S::CpuLoad) == 0);
    }
}

// A delta source (CPU %) shows nothing on its first read after a pause, so the second one comes within 0.25 s
// instead of a whole interval later; the other sources keep the full interval
void testResumeRefill() {
    Rig r;
    r.look();
    r.sched.setEnabledWidgets({W::Cpu, W::GpuRam});
    r.sched.setUpdateInterval(2.0);  // CpuLoad: 2 s; GpuClock stays a fixed 2 s
    double now = 0.0;
    for (int i = 0; i < 4; ++i, now += 0.05) r.sched.runDue(now);  // polled every 50 ms
    CHECK(r.c.cpu == 1 && r.c.gpu == 1);                           // the first reads, both "resumed"
    CHECK(r.c.cpuResumed.size() == 1 && r.c.cpuResumed[0]);
    for (int i = 0; i < 4; ++i, now += 0.05) r.sched.runDue(now);  // now = 0.4: past 0.25
    CHECK(r.c.cpu == 2 && !r.c.lastCpuResumed);                    // the refill read, a real delta
    CHECK(r.c.gpu == 1);                                           // GpuClock waits for its 2 s
    for (; now < 1.9; now += 0.05) r.sched.runDue(now);
    CHECK(r.c.cpu == 2 && r.c.gpu == 1);                           // back to the normal interval
    for (; now < 2.4; now += 0.05) r.sched.runDue(now);
    CHECK(r.c.cpu == 3 && r.c.gpu == 2);

    // A pause and a resume does it again; an interval below 0.25 s is simply the interval
    r.sched.setDemand(DemandInputs {});
    r.look();
    r.c.cpuResumed.clear();
    const int before = r.c.cpu;
    const double start = now;
    for (; now < start + 0.4; now += 0.05) r.sched.runDue(now);
    CHECK(r.c.cpu == before + 2);
    CHECK(r.c.cpuResumed.size() == 2 && r.c.cpuResumed[0] && !r.c.cpuResumed[1]);
    r.sched.setUpdateInterval(0.1);
    r.sched.setDemand(DemandInputs {});
    r.look();
    r.c.cpuResumed.clear();
    const int before2 = r.c.cpu;
    const double start2 = now;
    for (; now < start2 + 0.12; now += 0.05) r.sched.runDue(now);
    CHECK(r.c.cpu - before2 >= 2);  // second read within one 0.1 s interval, not 0.25
}

// The process scan and the HUD's own cost are deltas too: a resumed read gets its refill ~0.25 s later, and
// Uptime (an absolute value) doesn't
void testResumeRefillProcessesAndSelfCost() {
    const std::vector<MetricDef> defs = {
        {1, "proc.rows", "", W::CpuItems, S::Processes, 2000, WireType::I32},
        {2, "self.cpu", "", W::General, S::SelfCost, 2000, WireType::F32},
        {3, "uptime", "", W::General, S::Uptime, 2000, WireType::I32},
    };
    Scheduler sched {defs};
    std::vector<bool> proc, self, up;
    sched.addSource(S::Processes, [&](double, bool resumed) { proc.push_back(resumed); });
    sched.addSource(S::SelfCost, [&](double, bool resumed) { self.push_back(resumed); });
    sched.addSource(S::Uptime, [&](double, bool resumed) { up.push_back(resumed); });
    sched.setEnabledWidgets({W::CpuItems, W::General});
    DemandInputs d;
    d.editorOpen = true;
    sched.setDemand(d);
    double now = 0.0;
    for (; now < 0.4; now += 0.05) sched.runDue(now);
    CHECK(proc.size() == 2 && proc[0] && !proc[1]);  // the resumed read, then the refill
    CHECK(self.size() == 2 && self[0] && !self[1]);
    CHECK(up.size() == 1);                           // Uptime waits for its interval
    for (; now < 1.9; now += 0.05) sched.runDue(now);
    CHECK(proc.size() == 2 && self.size() == 2 && up.size() == 1);
    for (; now < 2.4; now += 0.05) sched.runDue(now);
    CHECK(proc.size() == 3 && self.size() == 3 && up.size() == 2);
}

// requestRead (the scene-app event): an early read only when the source is wanted
void testRequestRead() {
    const std::vector<MetricDef> defs = {
        {1, "app.key", "", W::General, S::SceneApp, 5000, WireType::ShortString},
    };
    int reads = 0;
    const auto rig = [&](Scheduler& sched, std::vector<W> widgets, bool look) {
        sched.addSource(S::SceneApp, [&reads](double, bool) { ++reads; });
        sched.setEnabledWidgets(widgets);
        DemandInputs d;
        d.hudShown = look;
        d.hmdInUse = look;
        sched.setDemand(d);
    };
    {  // wanted: one more read at once, and the 5 s cadence carries on
        Scheduler sched {defs};
        rig(sched, {W::General}, true);
        sched.runDue(0.0);
        CHECK(reads == 1);
        sched.runDue(1.0);
        CHECK(reads == 1);  // not due yet
        sched.requestRead(S::SceneApp);
        sched.runDue(1.05);
        CHECK(reads == 2);
        sched.runDue(3.0);
        CHECK(reads == 2);
        sched.runDue(6.2);
        CHECK(reads == 3);
    }
    reads = 0;
    {  // in the layout but nobody looking
        Scheduler sched {defs};
        rig(sched, {W::General}, false);
        sched.requestRead(S::SceneApp);
        sched.runDue(1.0);
        CHECK(reads == 0);
    }
    {  // looking but its widget isn't in the layout
        Scheduler sched {defs};
        rig(sched, {W::Cpu}, true);
        sched.requestRead(S::SceneApp);
        sched.runDue(1.0);
        CHECK(reads == 0);
    }
    {  // an invalid source is a no-op
        Scheduler sched {defs};
        rig(sched, {W::General}, true);
        sched.requestRead(S::None);
        sched.requestRead(S::Count);
        sched.runDue(0.0);
        CHECK(reads == 1);
    }
}

// A compact General doesn't show Uptime, the HUD cost or the app rows, so it doesn't make the scheduler read them
void testCompactGeneralReadsNothingExtra() {
    const std::vector<MetricDef> defs = {
        {1, "uptime", "", W::General, S::Uptime, 1000, WireType::I32},
        {2, "self.cpu", "", W::General, S::SelfCost, 1000, WireType::F32},
        {3, "app.key", "", W::General, S::SceneApp, 1000, WireType::ShortString},
        {4, "clock", "", W::General, S::Clock, 1000, WireType::I32},
    };
    struct General {
        Scheduler sched;
        int uptime = 0, self = 0, app = 0, clock = 0;
        std::vector<bool> uptimeResumed;
        double now = 0.0;
        explicit General(const std::vector<MetricDef>& d) : sched(d) {
            sched.addSource(S::Uptime, [this](double, bool resumed) {
                ++uptime;
                uptimeResumed.push_back(resumed);
            });
            sched.addSource(S::SelfCost, [this](double, bool) { ++self; });
            sched.addSource(S::SceneApp, [this](double, bool) { ++app; });
            sched.addSource(S::Clock, [this](double, bool) { ++clock; });
            DemandInputs demand;
            demand.editorOpen = true;
            sched.setDemand(demand);
        }
        void run(double seconds) {
            for (const double end = now + seconds; now < end; now += 0.05) sched.runDue(now);
        }
    };

    {  // (a) compact: only the compact card's own source is read, and requestRead does nothing
        General g(defs);
        g.sched.setEnabledWidgets({W::General}, {true});
        CHECK(g.sched.refCount(S::Uptime) == 0 && g.sched.refCount(S::SelfCost) == 0 &&
              g.sched.refCount(S::SceneApp) == 0);
        CHECK(g.sched.refCount(S::Clock) == 1);
        g.run(5.0);
        g.sched.requestRead(S::SceneApp);
        g.run(1.0);
        CHECK(g.uptime == 0 && g.self == 0 && g.app == 0);
        CHECK(g.clock > 0);
    }
    {  // (b) not compact: all three read
        General g(defs);
        g.sched.setEnabledWidgets({W::General}, {false});
        g.run(3.0);
        CHECK(g.uptime > 0 && g.self > 0 && g.app > 0 && g.clock > 0);
    }
    {  // (c) compact -> full -> compact: reads start on the switch (resumed), and stop again
        General g(defs);
        g.sched.setEnabledWidgets({W::General}, {true});
        g.run(2.0);
        CHECK(g.uptime == 0);
        g.sched.setEnabledWidgets({W::General}, {false});
        g.run(0.5);
        CHECK(g.uptime >= 1 && g.uptimeResumed[0]);
        CHECK(g.self >= 1 && g.app >= 1);
        g.sched.setEnabledWidgets({W::General}, {true});
        g.run(0.5);  // anything already due has been read; after this nothing more comes
        const int uptime = g.uptime, self = g.self, app = g.app;
        g.run(5.0);
        CHECK(g.uptime == uptime && g.self == self && g.app == app);
        CHECK(g.sched.refCount(S::Uptime) == 0);
    }
    {  // (d) the one-argument form treats every card as full
        General g(defs);
        g.sched.setEnabledWidgets({W::General});
        CHECK(g.sched.refCount(S::Uptime) == 1 && g.sched.refCount(S::SelfCost) == 1 &&
              g.sched.refCount(S::SceneApp) == 1);
        g.run(3.0);
        CHECK(g.uptime > 0 && g.self > 0 && g.app > 0);
    }
    {  // a compact flag list shorter than the widgets: the rest count as full
        General g(defs);
        g.sched.setEnabledWidgets({W::Cpu, W::General}, {true});
        CHECK(g.sched.refCount(S::Uptime) == 1);
    }
}

// (e) the default layout's General is compact, so the real registry's full-card-only sources are idle
void testDefaultLayoutGeneralIsCompact() {
    const Layout layout = defaultLayout();
    const std::vector<W> widgets = enabledWidgets(layout);
    const std::vector<bool> compact = enabledCompact(layout);
    CHECK(compact.size() == widgets.size());
    bool sawGeneral = false;
    for (size_t i = 0; i < widgets.size() && i < compact.size(); ++i) {
        if (widgets[i] == W::General) {
            sawGeneral = true;
            CHECK(compact[i]);
        }
    }
    CHECK(sawGeneral);

    Scheduler sched;
    DemandInputs demand;
    demand.editorOpen = true;
    sched.setDemand(demand);
    sched.setEnabledWidgets(widgets, compact);
    CHECK(sched.refCount(S::Uptime) == 0);
    CHECK(sched.refCount(S::SelfCost) == 0);
    CHECK(sched.refCount(S::SceneApp) == 0);
    CHECK(sched.refCount(S::Clock) > 0);  // General's compact rows still read

    // the one-argument form (everything full) would have counted them
    sched.setEnabledWidgets(widgets);
    CHECK(sched.refCount(S::Uptime) > 0);
}

// The quick refill read doesn't move the cadence: the reads after it stay on the grid of the first one
void testResumeRefillKeepsGrid() {
    Rig r;
    r.look();
    r.sched.setEnabledWidgets({W::Cpu});
    r.sched.setUpdateInterval(1.0);
    std::vector<double> times;  // when CpuLoad was read, to the polling step (50 ms)
    int seen = 0;
    for (int i = 0; i <= 84; ++i) {  // 0 .. 4.2 s, polled every 50 ms
        const double t = i * 0.05;
        r.sched.runDue(t);
        if (r.c.cpu != seen) {
            seen = r.c.cpu;
            times.push_back(t);
        }
    }
    const double expected[] = {0.0, 0.25, 1.0, 2.0, 3.0, 4.0};  // first read, refill, then the 1 s grid
    CHECK(times.size() == 6);
    for (size_t k = 0; k < times.size() && k < 6; ++k) {
        if (std::fabs(times[k] - expected[k]) > 0.06) {
            std::printf("read %zu at %.2f, expected about %.2f\n", k, times[k], expected[k]);
            CHECK(false);
        }
    }
}

void testRecomputeCount() {
    Rig r;
    const uint64_t start = r.sched.recomputeCount();
    DemandInputs d;
    d.editorOpen = true;
    r.sched.setDemand(d);
    CHECK(r.sched.recomputeCount() == start + 1);
    r.sched.setDemand(d);  // identical inputs
    CHECK(r.sched.recomputeCount() == start + 1);
    d.hudShown = true;
    d.hmdInUse = true;
    r.sched.setDemand(d);
    CHECK(r.sched.recomputeCount() == start + 2);
}

}  // namespace

int main() {
    testNothingWithoutWidgets();
    testEnableDisable();
    testSharedSource();
    testTwoInstances();
    testVisibility();
    testSingleSignals();
    testResumed();
    testIntervals();
    testUpdateInterval();
    testResumeRefill();
    testResumeRefillProcessesAndSelfCost();
    testRequestRead();
    testCompactGeneralReadsNothingExtra();
    testDefaultLayoutGeneralIsCompact();
    testResumeRefillKeepsGrid();
    testRecomputeCount();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
