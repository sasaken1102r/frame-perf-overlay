// core/ layout tests: JSON round trip, bad input, and "widget off -> source refs drop" (no platform code).
#include "layout.h"
#include "scheduler.h"

#include <cstdio>
#include <string>
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

WidgetInstance inst(W type, bool compact = true) {
    WidgetInstance w;
    w.type = type;
    w.compact = compact;
    return w;
}

bool sameTypes(const LayoutColumn& column, const std::vector<W>& types) {
    if (column.widgets.size() != types.size()) return false;
    for (size_t i = 0; i < types.size(); ++i) {
        if (column.widgets[i].type != types[i]) return false;
    }
    return true;
}

void testDefaultLayout() {
    const Layout layout = defaultLayout();
    CHECK(layout.version == 1);
    CHECK(layout.columns.size() == 2);
    CHECK(sameTypes(layout.columns[0], {W::Frame, W::Power, W::Temp, W::Cpu}));
    CHECK(sameTypes(layout.columns[1], {W::GpuRam, W::Network, W::Battery, W::General}));
    // every card is the short one
    for (const LayoutColumn& column : layout.columns) {
        for (const WidgetInstance& w : column.widgets) CHECK(w.compact);
    }
    CHECK(hasCompactView(W::Battery) && hasCompactView(W::Temp));
    CHECK(hasCompactView(W::Frame) && hasCompactView(W::Network) && hasCompactView(W::General) && !hasCompactView(W::GpuRam));
}

void testRoundTrip() {
    // default, compact and indented
    const Layout def = defaultLayout();
    for (const int indent : {0, 2}) {
        Layout back;
        std::vector<std::string> warnings;
        std::string error;
        CHECK(parseLayoutText(layoutToJson(def, indent), back, warnings, error));
        CHECK(back.sameWidgets(def));
        CHECK(back.version == def.version);
        CHECK(warnings.empty());
    }

    // custom: several columns, the version kept
    Layout custom;
    custom.version = 42;
    custom.columns.resize(2);
    custom.columns[0].widgets = {inst(W::Cpu), inst(W::GpuRam), inst(W::Frame)};
    custom.columns[1].widgets = {inst(W::Network), inst(W::Temp, true), inst(W::General)};
    Layout back;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(parseLayoutText(layoutToJson(custom, 0), back, warnings, error));
    CHECK(back.version == 42);
    CHECK(back.columns.size() == 2);
    CHECK(back.sameWidgets(custom));
    CHECK(warnings.empty());
}

void testBadInput() {
    Layout out;
    std::vector<std::string> warnings;
    std::string error;

    // unknown widget type: skipped with a warning, the rest stays
    CHECK(parseLayoutText(R"({"version":3,"columns":[[{"type":"frame"},{"type":"bogus"},{"type":"cpu"}]]})", out,
                          warnings, error));
    CHECK(out.version == 3);
    CHECK(out.columns.size() == 1 && sameTypes(out.columns[0], {W::Frame, W::Cpu}));
    CHECK(!warnings.empty());

    // widgets that only exist in other versions are skipped the same way
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"foo"},{"type":"streaming"},{"type":"temp"}]]})", out,
                          warnings, error));
    CHECK(out.columns[0].widgets.size() == 1 && out.columns[0].widgets[0].type == W::Temp);
    CHECK(warnings.size() == 2);
    CHECK(out.version == 1);  // missing version = 1

    // columns that isn't an array: an error, `out` untouched
    const Layout before = out;
    warnings.clear();
    error.clear();
    CHECK(!parseLayoutText(R"({"version":9,"columns":5})", out, warnings, error));
    CHECK(!error.empty());
    CHECK(out.version == before.version && out.sameWidgets(before));
    error.clear();
    CHECK(!parseLayoutText(R"({"version":9})", out, warnings, error));  // no columns at all
    CHECK(!error.empty());
    error.clear();
    CHECK(!parseLayoutText(R"([1,2,3])", out, warnings, error));  // not an object
    CHECK(!error.empty());
    error.clear();
    CHECK(!parseLayoutText("{ not json", out, warnings, error));
    CHECK(!error.empty());

    // one column that isn't an array: skipped with a warning, the others stay
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[{"type":"frame"},[{"type":"temp"}]]})", out, warnings, error));
    CHECK(out.columns.size() == 1 && sameTypes(out.columns[0], {W::Temp}));
    CHECK(!warnings.empty());
}

// A file that says more than the editor could make: blank columns go, a second copy of a widget is skipped
void testCleanedUpOnLoad() {
    Layout out;
    std::vector<std::string> warnings;
    std::string error;

    // an empty column
    CHECK(parseLayoutText(R"({"columns":[[{"type":"frame"}],[],[{"type":"cpu"}]]})", out, warnings, error));
    CHECK(out.columns.size() == 2 && sameTypes(out.columns[0], {W::Frame}) && sameTypes(out.columns[1], {W::Cpu}));
    CHECK(warnings.size() == 1);

    // a column that only held an unknown widget: one warning for the widget, one for the column
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"bogus"}],[{"type":"frame"}]]})", out, warnings, error));
    CHECK(out.columns.size() == 1 && sameTypes(out.columns[0], {W::Frame}));
    CHECK(warnings.size() == 2);

    // a second copy: the first is kept, even across columns
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"frame"},{"type":"cpu"},{"type":"frame"}],)"
                          R"([{"type":"frame"},{"type":"temp"}]]})", out, warnings, error));
    CHECK(out.columns.size() == 2 && sameTypes(out.columns[0], {W::Frame, W::Cpu}) && sameTypes(out.columns[1], {W::Temp}));
    CHECK(warnings.size() == 2);

    // a column of only copies goes too
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"frame"}],[{"type":"frame"}]]})", out, warnings, error));
    CHECK(out.columns.size() == 1 && warnings.size() == 2);

    // a file that lists no widgets at all is an empty layout (the user removed them all), not the default
    warnings.clear();
    CHECK(parseLayoutText(R"({"version":7,"columns":[[],[]]})", out, warnings, error));
    CHECK(out.columns.empty() && out.version == 7);
    CHECK(warnings.size() == 2);  // one per dropped column
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[]})", out, warnings, error));
    CHECK(out.columns.empty() && out.version == 1);
    CHECK(warnings.empty());

    // nothing usable left of what the file listed: the default layout, the file's version kept
    warnings.clear();
    CHECK(parseLayoutText(R"({"version":3,"columns":[[{"type":"bogus"}]]})", out, warnings, error));
    CHECK(out.sameWidgets(defaultLayout()) && out.version == 3 && warnings.size() == 3);
}

/** Reference counts the way the headset sets them: enabledWidgets(layout) + someone looking. */
struct Refs {
    Scheduler sched;
    explicit Refs(const Layout& layout) {
        DemandInputs d;
        d.editorOpen = true;
        sched.setDemand(d);
        sched.setEnabledWidgets(enabledWidgets(layout));
    }
};

void testWidgetOffDropsRefs() {
    Layout layout = defaultLayout();
    {
        Refs r(layout);
        CHECK(r.sched.refCount(SourceId::Network) > 0);
        CHECK(r.sched.refCount(SourceId::CpuLoad) > 0);
        CHECK(r.sched.refCount(SourceId::Clock) > 0);
    }

    // Network removed: its source loses the reference, the others keep theirs
    Layout noNet = layout;
    for (LayoutColumn& column : noNet.columns) {
        std::vector<WidgetInstance> keep;
        for (const WidgetInstance& w : column.widgets) {
            if (w.type != W::Network) keep.push_back(w);
        }
        column.widgets = keep;
    }
    {
        Refs r(noNet);
        CHECK(r.sched.refCount(SourceId::Network) == 0);
        CHECK(r.sched.refCount(SourceId::CpuLoad) > 0);
    }

    // Empty layout: everything off
    Layout empty;
    {
        Refs r(empty);
        CHECK(r.sched.refCount(SourceId::Network) == 0);
        CHECK(r.sched.refCount(SourceId::CpuLoad) == 0);
    }
}

// The compact key: every type with extra rows has the switch; Battery / Temp read a missing key as off (their
// v0.4.0 full card), the others as on (their v0.4.0 card)
void testCompact() {
    CHECK(!compactWhenMissing(W::Battery) && !compactWhenMissing(W::Temp));
    for (const W t : {W::Frame, W::Cpu, W::Power, W::Network, W::General}) CHECK(compactWhenMissing(t));
    for (const W t : {W::Frame, W::Cpu, W::Power, W::Battery, W::Temp, W::Network, W::General}) CHECK(hasCompactView(t));
    for (const W t : {W::GpuRam, W::CpuItems, W::RamItems}) CHECK(!hasCompactView(t));
    CHECK(WidgetInstance().compact);  // a new instance is compact

    // the key is written only when it differs from what a missing key means
    Layout l;
    l.columns.resize(1);
    l.columns[0].widgets = {inst(W::Battery, true), inst(W::Temp, false), inst(W::Power, true)};
    const std::string text = layoutToJson(l, 0);
    size_t count = 0;
    for (size_t at = text.find("compact"); at != std::string::npos; at = text.find("compact", at + 1)) ++count;
    CHECK(count == 1);
    CHECK(text.find("\"compact\":true") != std::string::npos);

    Layout back;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(parseLayoutText(text, back, warnings, error));
    CHECK(warnings.empty());
    CHECK(back.sameWidgets(l));
    CHECK(back.columns[0].widgets[0].compact && !back.columns[0].widgets[1].compact && back.columns[0].widgets[2].compact);

    // the other five write "compact": false when off, and read it back
    for (const W t : {W::Frame, W::Cpu, W::Power, W::Network, W::General}) {
        Layout off;
        off.columns.resize(1);
        off.columns[0].widgets = {inst(t, false)};
        const std::string offText = layoutToJson(off, 0);
        CHECK(offText.find("\"compact\":false") != std::string::npos);
        Layout offBack;
        warnings.clear();
        CHECK(parseLayoutText(offText, offBack, warnings, error) && warnings.empty() && offBack.sameWidgets(off));
        CHECK(!offBack.columns[0].widgets[0].compact);
        CHECK(layoutToJson(offBack, 0) == offText);
        // a missing key = compact
        Layout missing;
        CHECK(parseLayoutText(std::string(R"({"columns":[[{"type":")") + (t == W::Frame ? "frame" : t == W::Cpu ? "cpu" : t == W::Power ? "power" : t == W::Network ? "network" : "general") + R"("}]]})",
                              missing, warnings, error));
        CHECK(missing.columns[0].widgets[0].compact);
    }
    // Battery / Temp: a missing key = not compact, "compact": true = compact
    CHECK(parseLayoutText(R"({"columns":[[{"type":"battery"},{"type":"temp","compact":true}]]})", back, warnings, error));
    CHECK(!back.columns[0].widgets[0].compact && back.columns[0].widgets[1].compact);

    // a type without a compact switch never writes the key, and warns when it reads one
    Layout noCompact;
    noCompact.columns.resize(1);
    noCompact.columns[0].widgets = {inst(W::GpuRam, true), inst(W::GpuRam, false)};
    CHECK(layoutToJson(noCompact, 0).find("compact") == std::string::npos);
    Layout wrong;
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"gpu_ram","compact":true},{"type":"cpu_items","compact":false}]]})",
                          wrong, warnings, error));
    CHECK(warnings.size() == 2);
    CHECK(wrong.columns[0].widgets.size() == 2);

    // non-bool value: warns, keeps what a missing key means
    Layout bad;
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"temp","compact":"yes"},{"type":"frame","compact":"no"}]]})", bad,
                          warnings, error));
    CHECK(warnings.size() == 2);
    CHECK(!bad.columns[0].widgets[0].compact && bad.columns[0].widgets[1].compact);

    // "details" is gone: an unknown key
    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"network","details":true}]]})", bad, warnings, error));
    CHECK(warnings.size() == 1 && warnings[0].find("unknown key details") != std::string::npos);
    CHECK(layoutToJson(bad, 0).find("details") == std::string::npos);

    // operator== sees the field
    WidgetInstance a = inst(W::Battery, false), b = inst(W::Battery, false);
    CHECK(a == b);
    b.compact = true;
    CHECK(a != b);
}

}  // namespace

// The process lists' settings: written only when not the default, read back
void testReadoutSettings() {
    Layout out;
    std::vector<std::string> warnings;
    std::string error;
    const auto parse = [&](const char* json) {
        warnings.clear();
        return parseLayoutText(json, out, warnings, error);
    };
    const auto first = [&]() -> const WidgetInstance& { return out.columns[0].widgets[0]; };

    // the process lists: defaults, round trip, clamping
    CHECK(parse(R"({"columns":[[{"type":"cpu_items"},{"type":"ram_items"}]]})") && warnings.empty());
    CHECK(first().maxItems == 15 && !first().sumSameName && !first().hideSystem);
    CHECK(out.columns[0].widgets[1].type == W::RamItems);
    CHECK(layoutToJson(out).find("\"max_items\":15") != std::string::npos);
    CHECK(parse(R"({"columns":[[{"type":"cpu_items","max_items":7,"sum_same_name":true,"hide_system":true}]]})") &&
          warnings.empty());
    CHECK(first().maxItems == 7 && first().sumSameName && first().hideSystem);
    Layout back;
    CHECK(parseLayoutText(layoutToJson(out), back, warnings, error) && back.sameWidgets(out) && warnings.empty());
    CHECK(parse(R"({"columns":[[{"type":"cpu_items","max_items":99}]]})") && warnings.size() == 1 && first().maxItems == 50);
    CHECK(parse(R"({"columns":[[{"type":"ram_items","max_items":0}]]})") && warnings.size() == 1 && first().maxItems == 1);
    CHECK(parse(R"({"columns":[[{"type":"ram_items","max_items":-4}]]})") && warnings.size() == 1 && first().maxItems == 1);
    CHECK(parse(R"({"columns":[[{"type":"ram_items","max_items":"ten"}]]})") && warnings.size() == 1 && first().maxItems == 15);
    CHECK(parse(R"({"columns":[[{"type":"ram_items","sum_same_name":1}]]})") && warnings.size() == 1 && !first().sumSameName);
    // the list keys mean nothing elsewhere
    CHECK(parse(R"({"columns":[[{"type":"frame","max_items":3}]]})") && warnings.size() == 1);
    CHECK(isProcessList(W::CpuItems) && isProcessList(W::RamItems) && !isProcessList(W::Cpu));
}

int main() {
    testReadoutSettings();
    testDefaultLayout();
    testRoundTrip();
    testBadInput();
    testCleanedUpOnLoad();
    testWidgetOffDropsRefs();
    testCompact();
    if (failures == 0) std::printf("core-layout-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
