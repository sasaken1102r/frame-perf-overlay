// Grid geometry, texture cap, draw rules and change detection of the widget grid renderer (links frame-perf-render).
// Renders into memory; a few PNGs go to $FRAME_PERF_PNG_DIR (default /tmp/frame-perf-render-tests/png) for a look by eye.
#include "atomic_file.h"
#include "grid_renderer.h"

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
using Pixels = std::vector<uint8_t>;

constexpr double kNow = 100.0;

// The same default fonts the headset asks for; where they're missing FontSet falls back to fontconfig
FontSet& fonts() {
    static FontSet f;
    static bool loaded = false;
    if (!loaded) {
        f.load("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc");
        loaded = true;
    }
    return f;
}

std::string pngDir() {
    const char* env = std::getenv("FRAME_PERF_PNG_DIR");
    const std::string dir = env != nullptr && env[0] != '\0' ? env : "/tmp/frame-perf-render-tests/png";
    makeDirectories(dir);
    return dir;
}

WidgetInstance inst(W type, bool compact = true) {
    WidgetInstance w;
    w.type = type;
    w.compact = compact;
    return w;
}

Layout oneWidget(const WidgetInstance& w) {
    Layout layout;
    layout.columns.resize(1);
    layout.columns[0].widgets = {w};
    return layout;
}

/** Renders a layout with a fresh renderer (no graph history carried over). */
Pixels renderPx(const Layout& layout, const MetricStore& store, const char* pngName = nullptr) {
    GridRenderer renderer(fonts());
    renderer.setLayout(layout);
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    renderer.render(ctx);
    if (pngName != nullptr) renderer.writePng(pngDir() + "/" + pngName);
    return renderer.toRgba();
}

// ---- Texture sizing ----

void testGeometry() {
    const GridGeometry def = layoutGrid(defaultLayout());
    CHECK(def.width == 1024 && def.columns == 2);  // two columns: wide and low like the fixed panel
    CHECK(def.scale == 1.0);
    CHECK(def.textureWidth == 1024 && def.height < 512);
    CHECK(def.textureHeight == def.height && def.height <= 2048);
    CHECK(def.slots.size() == 8);

    // 5 columns = 2560 px: scaled to fit 2048
    Layout wide;
    wide.columns.resize(5);
    for (LayoutColumn& c : wide.columns) c.widgets = {inst(W::Frame)};
    const GridGeometry gw = layoutGrid(wide);
    CHECK(gw.width == 2560);
    CHECK(gw.scale < 1.0);
    CHECK(gw.textureWidth <= 2048 && gw.textureHeight <= 2048);
    CHECK(std::abs(gw.textureWidth - std::ceil(gw.width * gw.scale)) <= 1);

    // one very tall column (more cards than a real layout allows, just to pass the cap)
    Layout tall;
    tall.columns.resize(1);
    for (int i = 0; i < 20; ++i) tall.columns[0].widgets.push_back(inst(W::Power));
    const GridGeometry gt = layoutGrid(tall);
    CHECK(gt.height > 2048);
    CHECK(gt.scale < 1.0);
    CHECK(gt.textureWidth <= 2048 && gt.textureHeight <= 2048);
    CHECK(std::abs(gt.textureHeight - std::ceil(gt.height * gt.scale)) <= 1);
    CHECK(std::abs(gt.textureWidth - std::ceil(gt.width * gt.scale)) <= 1);

    // the capped panel really renders at the capped size
    MetricStore store;
    GridRenderer renderer(fonts());
    renderer.setLayout(tall);
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    renderer.render(ctx);
    CHECK(renderer.toRgba().size() == size_t(gt.textureWidth) * size_t(gt.textureHeight) * 4);
    renderer.writePng(pngDir() + "/tall_scaled.png");

    // empty layout: minimal one-column panel, renders without a crash
    const GridGeometry ge = layoutGrid(Layout());
    CHECK(ge.columns == 1 && ge.slots.empty());
    CHECK(ge.textureWidth == 512 && ge.textureHeight > 0);
    CHECK(!renderPx(Layout(), store).empty());
}

// ---- Heights that follow the data ----

void testGeneralClockOffShrinks() {
    MetricStore store;
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    const WidgetInstance general = inst(W::General);

    ctx.clockFormat = 24;
    const int withClock = widgetHeight(general, &ctx);
    ctx.clockFormat = 0;
    const int withoutClock = widgetHeight(general, &ctx);
    CHECK(withClock - withoutClock == 22);  // the Time row, exactly one text line

    ctx.clockFormat = 12;
    CHECK(widgetHeight(general, &ctx) == withClock);  // 12 h / 24 h both show the row; only "Off" hides it

    // no context at all (the layout editor doesn't know the dashboard's clock setting): the fixed fallback
    // reserves the room the clock needs
    CHECK(widgetHeight(general) == withClock);
}

void testNetworkShrinksWhenNotExpanded() {
    const WidgetInstance network = inst(W::Network);

    MetricStore absent;  // interface state N/A: not expanded
    WidgetContext absentCtx;
    absentCtx.store = &absent;
    absentCtx.now = kNow;
    CHECK(widgetHeight(network, &absentCtx) == 64);

    MetricStore disconnected;
    disconnected.setEnum(metric::kNetInterfaceUp, 1, kNow);
    disconnected.setEnum(metric::kNetConnected, 0, kNow);
    WidgetContext disconnectedCtx;
    disconnectedCtx.store = &disconnected;
    disconnectedCtx.now = kNow;
    CHECK(widgetHeight(network, &disconnectedCtx) == 64);

    MetricStore connected;
    connected.setEnum(metric::kNetInterfaceUp, 1, kNow);
    connected.setEnum(metric::kNetConnected, 1, kNow);
    WidgetContext connectedCtx;
    connectedCtx.store = &connected;
    connectedCtx.now = kNow;
    CHECK(widgetHeight(network, &connectedCtx) == 88);

    CHECK(widgetHeight(network) == 88);  // no ctx: fixed fallback keeps the full reservation

    CHECK(renderPx(oneWidget(network), disconnected, "network_collapsed.png") !=
          renderPx(oneWidget(network), connected, "network_expanded.png"));
}

void testCompactHeights() {
    CHECK(widgetHeight(inst(W::Temp, false)) == 198);  // the card with its extra rows
    CHECK(widgetHeight(inst(W::Temp, true)) == 102);
    CHECK(widgetHeight(inst(W::Battery, false)) > 142);
    CHECK(widgetHeight(inst(W::Battery, true)) == 88);
    // GPU & RAM has no switch
    CHECK(widgetHeight(inst(W::GpuRam, true)) == widgetHeight(inst(W::GpuRam, false)));

    // no jitter: values moving doesn't move a height or the grid
    MetricStore low;
    low.setF32(metric::kTempCpuC, 50, kNow);
    low.setF32(metric::kBatteryPct, 40, kNow);
    MetricStore high;
    high.setF32(metric::kTempCpuC, 80, kNow);
    high.setF32(metric::kBatteryPct, 90, kNow);
    WidgetContext lowCtx, highCtx;
    lowCtx.store = &low;
    lowCtx.now = kNow;
    highCtx.store = &high;
    highCtx.now = kNow;
    Layout layout;
    layout.columns.resize(2);
    layout.columns[0].widgets = {inst(W::Frame), inst(W::Temp, false)};
    layout.columns[1].widgets = {inst(W::Power), inst(W::Battery, true)};
    CHECK(sameGridGeometry(layoutGrid(layout, &lowCtx), layoutGrid(layout, &highCtx)));

    // toggling compact changes the geometry
    Layout toggled = layout;
    toggled.columns[0].widgets[1].compact = true;
    CHECK(!sameGridGeometry(layoutGrid(layout, &lowCtx), layoutGrid(toggled, &lowCtx)));
    CHECK(layoutGrid(toggled, &lowCtx).slots[1].h == 102);
    CHECK(layoutGrid(layout, &lowCtx).slots[1].h == 198);

    // Compact Temp keeps the graph: its graph bed (the panel's background colour, sunk into the card) is where the
    // full card's is. The card sits at (8, 8); the bed starts 202 px in and 10 px down, 84 px tall
    MetricStore none;
    const Pixels compact = renderPx(oneWidget(inst(W::Temp, true)), none, "temp-compact.png");
    const Pixels full = renderPx(oneWidget(inst(W::Temp, false)), none);
    const auto rgbAt = [](const Pixels& px, int width, int x, int y) {
        const size_t i = (static_cast<size_t>(y) * width + x) * 4;
        return (unsigned(px[i]) << 16) | (unsigned(px[i + 1]) << 8) | px[i + 2];
    };
    const int bedX = 8 + 202 + 20, bedY = 8 + 10 + 76;  // low in the bed, clear of the guides and the label
    CHECK(rgbAt(compact, 512, bedX, bedY) == rgbAt(full, 512, bedX, bedY));
    CHECK(rgbAt(compact, 512, bedX, bedY) != rgbAt(compact, 512, 8 + 190, bedY));  // the card, left of the bed
}

void testTitles() {
    for (const W type : {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::Network, W::General}) {
        for (const WidgetLanguage language : {WidgetLanguage::En, WidgetLanguage::Ja}) {
            const char* title = widgetTitle(inst(type), language);
            CHECK(title != nullptr && title[0] != '\0');
        }
    }
}

// ---- Draw rules ----

void testEmptyStoreAllTypes() {
    MetricStore store;  // everything N/A NotSampledYet
    for (const W type : {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::Network, W::General}) {
        CHECK(!renderPx(oneWidget(inst(type)), store).empty());
        if (hasCompactView(type)) CHECK(!renderPx(oneWidget(inst(type, true)), store).empty());
    }
    // no store at all
    GridRenderer renderer(fonts());
    renderer.setLayout(defaultLayout());
    WidgetContext ctx;
    ctx.now = kNow;
    CHECK(renderer.render(ctx));
    renderer.writePng(pngDir() + "/default_empty_store.png");
}

void testDefaultFilled() {
    MetricStore store;
    store.setF32(metric::kFrameAppFps, 90, 1.0);
    store.setF32(metric::kFrameDisplayHz, 90, 1.0);
    store.setF32(metric::kCpuUsagePct, 35, 1.0);
    store.setF32(metric::kGpuBusyPct, 60, 1.0);
    store.setF32(metric::kPowerVphW, 7.5, 1.0);
    store.setF32(metric::kBatteryPct, 80, 1.0);
    store.setF32(metric::kTempCpuC, 55, 1.0);
    CHECK(!renderPx(defaultLayout(), store, "default_filled.png").empty());
}

/** Renders one General widget with a given clock format. */
Pixels renderGeneral(const MetricStore& store, int clockFormat) {
    GridRenderer renderer(fonts());
    renderer.setLayout(oneWidget(inst(W::General)));
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    ctx.clockFormat = clockFormat;
    renderer.render(ctx);
    return renderer.toRgba();
}

void testClockOffHidesTime() {
    MetricStore a;
    a.setText(metric::kClock, "2026-10-11 23:05:00", 1.0);
    MetricStore b;
    b.setText(metric::kClock, "2026-10-12 07:41:00", 1.0);
    // Shown (12 / 24 h): the time is drawn, so a different time changes pixels
    CHECK(renderGeneral(a, 24) != renderGeneral(b, 24));
    CHECK(renderGeneral(a, 12) != renderGeneral(a, 24));
    // Off (0): the Time row is gone, so the time no longer matters
    CHECK(renderGeneral(a, 0) == renderGeneral(b, 0));
    CHECK(renderGeneral(a, 0) != renderGeneral(a, 24));
}

void testLanguageChangesTitles() {
    MetricStore store;
    const Layout layout = defaultLayout();
    GridRenderer renderer(fonts());
    renderer.setLayout(layout);
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    ctx.language = WidgetLanguage::En;
    CHECK(renderer.render(ctx));
    const uint64_t builds = renderer.staticBuilds();
    CHECK(!renderer.render(ctx));
    // a new language rebuilds the static layer (the titles) and repaints
    ctx.language = WidgetLanguage::Ja;
    CHECK(renderer.render(ctx));
    CHECK(renderer.staticBuilds() == builds + 1);
}

// ---- Change detection ----

void testChangeDetection() {
    MetricStore store;
    GridRenderer renderer(fonts());
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;

    renderer.setLayout(defaultLayout());
    CHECK(renderer.render(ctx));          // first render: always new
    CHECK(!renderer.render(ctx));         // same store, same now
    CHECK(!renderer.render(ctx));
    CHECK(renderer.staticBuilds() == 1);

    store.setF32(metric::kCpuUsagePct, 42, 1.0);
    CHECK(renderer.render(ctx));          // a value changed
    CHECK(!renderer.render(ctx));
    CHECK(renderer.staticBuilds() == 1);

    // Same widgets, new version: no rebuild
    Layout same = defaultLayout();
    same.version = 9;
    CHECK(!renderer.setLayout(same));
    renderer.render(ctx);
    CHECK(renderer.staticBuilds() == 1);

    // Different widgets: one more static build
    Layout changed = defaultLayout();
    changed.columns[0].widgets.pop_back();
    CHECK(renderer.setLayout(changed));
    CHECK(renderer.render(ctx));
    CHECK(renderer.staticBuilds() == 2);
    CHECK(!renderer.render(ctx));
    CHECK(renderer.staticBuilds() == 2);

    // A setting tweak (Compact) is a different widget too
    Layout tweaked = changed;
    tweaked.columns[0].widgets[2].compact = !tweaked.columns[0].widgets[2].compact;  // Temp
    CHECK(renderer.setLayout(tweaked));
    renderer.render(ctx);
    CHECK(renderer.staticBuilds() == 3);

    // Every widget removed: the empty panel must still go up once (else the old widgets stay frozen on
    // screen), and an unchanged empty frame doesn't
    CHECK(renderer.setLayout(Layout()));
    CHECK(renderer.render(ctx));
    CHECK(renderer.lastRedrawn() == 0);
    CHECK(!renderer.render(ctx));
}

// ---- Per-widget redraw: partial == full ----

Pixels pixelsOf(const GridRenderer& r) {
    const GridGeometry& g = r.geometry();
    const uint8_t* data = r.pixels();
    return Pixels(data, data + size_t(g.textureWidth) * size_t(g.textureHeight) * 4);
}

void testPartialRedraw() {
    // No graph widgets, so history can't make a partial and a fresh render differ
    Layout layout;
    layout.columns.resize(2);
    layout.columns[0].widgets = {inst(W::Battery), inst(W::General), inst(W::Network)};
    layout.columns[1].widgets = {inst(W::Cpu)};

    MetricStore store;
    store.setF32(metric::kBatteryPct, 80, 1.0);

    GridRenderer renderer(fonts());
    renderer.setLayout(layout);
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    CHECK(renderer.render(ctx));
    CHECK(renderer.lastRedrawn() == 4);  // first render draws everything

    // Nothing changed: no redraw at all
    CHECK(!renderer.render(ctx));
    CHECK(renderer.lastRedrawn() == 0);

    // One battery metric changes: only the Battery widget redraws, and the picture equals a from-scratch one
    store.setF32(metric::kBatteryPct, 79, 2.0);
    CHECK(renderer.render(ctx));
    CHECK(renderer.lastRedrawn() == 1);
    GridRenderer fresh(fonts());
    fresh.setLayout(layout);
    CHECK(fresh.render(ctx));
    CHECK(pixelsOf(renderer) == pixelsOf(fresh));

    // Same value sampled again later: the signature moves (sampledAt), still the same picture
    store.setF32(metric::kBatteryPct, 79, 3.0);
    renderer.render(ctx);
    CHECK(pixelsOf(renderer) == pixelsOf(fresh));

    // A CPU value changes: only the CPU widget redraws, and the picture follows
    store.setF32(metric::kCpuUsagePct, 63, 4.0);
    CHECK(renderer.render(ctx));
    CHECK(renderer.lastRedrawn() == 1);
    GridRenderer fresh2(fonts());
    fresh2.setLayout(layout);
    fresh2.render(ctx);
    CHECK(pixelsOf(renderer) == pixelsOf(fresh2));
    CHECK(pixelsOf(renderer) != pixelsOf(fresh));
    CHECK(!renderer.render(ctx));
    CHECK(renderer.lastRedrawn() == 0);

    // markAllDirty redraws everything, same picture
    renderer.markAllDirty();
    renderer.render(ctx);
    CHECK(renderer.lastRedrawn() == 4);
    CHECK(pixelsOf(renderer) == pixelsOf(fresh2));
}

/** One card of the last render, cut out of the panel (RGBA). */
Pixels slotPixels(GridRenderer& renderer, size_t index) {
    const WidgetSlot& slot = renderer.geometry().slots[index];
    const int stride = renderer.geometry().textureWidth * 4;
    const std::vector<uint8_t>& rgba = renderer.toRgba();
    Pixels out;
    for (int row = slot.y; row < slot.y + slot.h; ++row) {
        out.insert(out.end(), rgba.begin() + row * stride + slot.x * 4,
                   rgba.begin() + row * stride + (slot.x + slot.w) * 4);
    }
    return out;
}

// A graph keeps its history when another card's setting changes (Compact on the card below it)
void testCompactToggleKeepsGraphHistory() {
    Layout withFull;
    withFull.columns.resize(1);
    withFull.columns[0].widgets = {inst(W::Power), inst(W::Battery)};
    Layout withCompact = withFull;
    withCompact.columns[0].widgets[1].compact = true;

    GridRenderer toggled(fonts());
    GridRenderer steady(fonts());
    toggled.setLayout(withFull);
    steady.setLayout(withFull);
    MetricStore last;
    for (int i = 0; i < 12; ++i) {
        last.setF32(metric::kPowerVphW, 5.0 + 0.5 * (i % 4), kNow + i);
        WidgetContext ctx;
        ctx.store = &last;
        ctx.now = kNow + i;
        toggled.render(ctx);
        steady.render(ctx);
    }
    toggled.setLayout(withCompact);
    CHECK(toggled.geometry().slots[1].h == 88);
    WidgetContext ctx;
    ctx.store = &last;
    ctx.now = kNow + 11;
    toggled.render(ctx);
    steady.render(ctx);
    CHECK(slotPixels(toggled, 0) == slotPixels(steady, 0));
    // and the graph really holds a line: a renderer that never saw the history draws something else
    GridRenderer unseen(fonts());
    unseen.setLayout(withFull);
    unseen.render(ctx);
    CHECK(slotPixels(unseen, 0) != slotPixels(steady, 0));
}

// ---- Network: the interface being down is "Not connected", not an N/A ----

void testNetworkInterfaceDown() {
    const Layout layout = oneWidget(inst(W::Network));
    MetricStore unread;  // interface state never read: the N/A text
    MetricStore down;    // read, and down (no dongle AP and no home Wi-Fi)
    down.setEnum(metric::kNetInterfaceUp, 0, 1.0);
    MetricStore upNotConnected;
    upNotConnected.setEnum(metric::kNetInterfaceUp, 1, 1.0);
    upNotConnected.setEnum(metric::kNetConnected, 0, 1.0);
    MetricStore failed;  // the read failed: N/A with its reason
    failed.setNa(metric::kNetInterfaceUp, Reason::ReadFailed, 1.0);

    CHECK(renderPx(layout, down, "network_down.png") != renderPx(layout, unread));
    CHECK(renderPx(layout, down) != renderPx(layout, failed));
    CHECK(renderPx(layout, down) != renderPx(layout, upNotConnected));  // the link row differs
    // the same for the collapsed height
    WidgetContext ctx;
    ctx.store = &down;
    ctx.now = kNow;
    CHECK(widgetHeight(inst(W::Network), &ctx) == 64);

    // "Not connected" is in the card's language
    const auto render = [&](WidgetLanguage language) {
        GridRenderer renderer(fonts());
        renderer.setLayout(layout);
        WidgetContext c;
        c.store = &down;
        c.now = kNow;
        c.language = language;
        renderer.render(c);
        return renderer.toRgba();
    };
    CHECK(render(WidgetLanguage::En) != render(WidgetLanguage::Ja));
}

// ---- Readouts (PR 2): compact cards stay as they were, Compact off adds rows, the lists ----

unsigned pixelHash(cairo_surface_t* surface) {
    cairo_surface_flush(surface);
    const uint8_t* p = cairo_image_surface_get_data(surface);
    const size_t n = size_t(cairo_image_surface_get_stride(surface)) * size_t(cairo_image_surface_get_height(surface));
    unsigned h = 2166136261u;
    for (size_t k = 0; k < n; ++k) h = (h ^ p[k]) * 16777619u;
    return h;
}

/** The card with its extra rows (Compact switched off). */
WidgetInstance rowsCard(W type) {
    return inst(type, false);
}

WidgetContext ctxFor(const MetricStore& store, int clockFormat = 24) {
    WidgetContext ctx;
    ctx.store = &store;
    ctx.now = kNow;
    ctx.clockFormat = clockFormat;
    return ctx;
}

// The default 2-column layout and every card in it: the heights v0.4.0 drew, with and without a context
void testV040HeightsUnchanged() {
    const Layout def = defaultLayout();
    MetricStore empty;
    MetricStore connected;
    connected.setEnum(metric::kNetInterfaceUp, 1, kNow);
    connected.setEnum(metric::kNetConnected, 1, kNow);
    const WidgetContext emptyCtx = ctxFor(empty);
    const WidgetContext connectedCtx = ctxFor(connected);
    const GridGeometry plain = layoutGrid(def);
    const GridGeometry noLink = layoutGrid(def, &emptyCtx);
    const GridGeometry linked = layoutGrid(def, &connectedCtx);
    CHECK(plain.width == 1024 && plain.height == 422 && plain.columns == 2 && plain.slots.size() == 8);
    CHECK(noLink.width == 1024 && noLink.height == 422);
    CHECK(linked.width == 1024 && linked.height == 422);
    for (const GridGeometry* g : {&plain, &noLink, &linked}) {
        for (const WidgetSlot& s : g->slots) {
            int want = 0;
            switch (s.widget.type) {
                case W::Frame: case W::Power: case W::GpuRam: want = 102; break;
                case W::Temp: want = 102; break;     // compact in the default layout
                case W::Cpu: want = 64; break;
                case W::Battery: want = 88; break;   // compact
                case W::Network: want = g == &noLink ? 64 : 88; break;
                case W::General: want = 82; break;
                default: break;
            }
            CHECK(s.h == want);
        }
    }
    CHECK(widgetHeight(inst(W::Cpu), &emptyCtx) == 64);
    const WidgetContext noClock = ctxFor(empty, 0);
    CHECK(widgetHeight(inst(W::General), &emptyCtx) == 82 && widgetHeight(inst(W::General), &noClock) == 60);
    CHECK(widgetHeight(inst(W::Network), &connectedCtx) == 88 && widgetHeight(inst(W::Network), &emptyCtx) == 64);
    CHECK(widgetHeight(inst(W::Frame)) == 102 && widgetHeight(inst(W::Power)) == 102);
    CHECK(widgetHeight(inst(W::Temp)) == 102 && widgetHeight(inst(W::Battery)) == 88);
}

// A layout saved by v0.4.0 (a key only on compact Temp / Battery) loads clean and writes back the same bytes
void testV040SavedLayout() {
    const std::string saved =
        R"({"version":7,"columns":[[{"type":"frame"},{"type":"temp","compact":true},{"type":"battery","compact":true},)"
        R"({"type":"cpu"}],[{"type":"gpu_ram"},{"type":"power"},{"type":"network"},{"type":"general"}]]})";
    Layout layout;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(parseLayoutText(saved, layout, warnings, error) && warnings.empty());
    CHECK(layoutToJson(layout, 0) == saved);
    for (const LayoutColumn& c : layout.columns) {
        for (const WidgetInstance& w : c.widgets) CHECK(w.compact || !hasCompactView(w.type));
    }
    const GridGeometry g = layoutGrid(layout);
    CHECK(g.slots[0].h == 102 && g.slots[1].h == 102 && g.slots[2].h == 88 && g.slots[3].h == 64);

    // and the built-in default layout
    const std::string def = layoutToJson(defaultLayout(), 0);
    CHECK(def.find("details") == std::string::npos && def.find("adapter") == std::string::npos);
    CHECK(def.find("false") == std::string::npos);
    Layout back;
    warnings.clear();
    CHECK(parseLayoutText(def, back, warnings, error) && warnings.empty() && layoutToJson(back, 0) == def);
}

// A v0.4.0 file with a non-compact Battery / Temp (no key) now shows the card with its extra rows: the documented change
void testV040FullCardsGainRows() {
    Layout layout;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(parseLayoutText(R"({"columns":[[{"type":"temp"},{"type":"battery"}]]})", layout, warnings, error));
    CHECK(warnings.empty());
    const WidgetInstance& temp = layout.columns[0].widgets[0];
    const WidgetInstance& battery = layout.columns[0].widgets[1];
    CHECK(!temp.compact && !battery.compact);
    const GridGeometry g = layoutGrid(layout);
    CHECK(g.slots[0].h == 198);
    CHECK(g.slots[1].h > 142);
}

// Compact off: every type with the switch gets its extra rows
void testCompactOffHeights() {
    MetricStore connected;
    connected.setEnum(metric::kNetInterfaceUp, 1, kNow);
    connected.setEnum(metric::kNetConnected, 1, kNow);
    const WidgetContext ctx = ctxFor(connected);
    for (const W type : {W::Frame, W::Cpu, W::Power, W::Battery, W::Temp, W::Network, W::General}) {
        CHECK(hasCompactView(type));
        CHECK(widgetHeight(rowsCard(type), &ctx) > widgetHeight(inst(type), &ctx));
        CHECK(widgetHeight(rowsCard(type)) > widgetHeight(inst(type)));
    }
    CHECK(!hasCompactView(W::GpuRam) && widgetHeight(rowsCard(W::GpuRam)) == widgetHeight(inst(W::GpuRam)));
    CHECK(widgetHeight(rowsCard(W::Frame)) == 194 && widgetHeight(rowsCard(W::Temp)) == 198);
    CHECK(widgetHeight(rowsCard(W::General), &ctx) == 82 + 3 * 22);
    // the rows card draws and differs from the compact one
    MetricStore store;
    store.setF32(metric::kTempCpuC, 60, kNow);
    CHECK(renderPx(oneWidget(rowsCard(W::Temp)), store) != renderPx(oneWidget(inst(W::Temp)), store));
}

// Compact on is the v0.4.0 card: the new metrics change nothing
void testCompactOnIgnoresNewMetrics() {
    MetricStore base;
    base.setEnum(metric::kNetInterfaceUp, 1, kNow);
    base.setEnum(metric::kNetConnected, 1, kNow);
    base.setF32(metric::kCpuUsagePct, 30, kNow);
    base.setF32(metric::kTempCpuC, 60, kNow);
    base.setF32(metric::kBatteryPct, 50, kNow);
    MetricStore more = base;
    more.setF32(metric::kCpuCore0Pct, 40, kNow);
    more.setF32(metric::kCpuCluster0Mhz, 1800, kNow);
    more.setF32(metric::kTempNpuC, 50, kNow);
    more.setF32(metric::kPowerUnmeasuredW, 2, kNow);
    more.setI32(metric::kBatteryCycles, 100, kNow);
    more.setI32(metric::kUptimeS, 1000, kNow);
    more.setEnum(metric::kNetDongleBase + metric::kNetAdapterPresent, 1, kNow);
    more.setEnum(metric::kNetDongleBase + metric::kNetAdapterConnected, 1, kNow);
    for (const W type : {W::Frame, W::Cpu, W::Power, W::Battery, W::Temp, W::Network, W::General}) {
        const Layout layout = oneWidget(inst(type));
        CHECK(renderPx(layout, base) == renderPx(layout, more));
    }
}

/** Sets one link block (kNetDongleBase / kNetHomeBase): present, connected, and some values when connected. */
void setLinkBlock(MetricStore& s, uint16_t base, bool present, bool connected) {
    s.setEnum(base + metric::kNetAdapterPresent, present ? 1 : 0, kNow);
    s.setEnum(base + metric::kNetAdapterConnected, connected ? 1 : 0, kNow);
    if (present && connected) {
        s.setF32(base + metric::kNetAdapterSignalDbm, -50, kNow);
        s.setF32(base + metric::kNetAdapterRxMbps, 20, kNow);
        s.setF32(base + metric::kNetAdapterTxMbps, 2, kNow);
        s.setText(base + metric::kNetAdapterIp, "192.168.1.47", kNow);
        s.setText(base + metric::kNetAdapterIfName, "wlan0", kNow);
    }
}

// Network with Compact on is the 0.4.0 combined card and ignores the per-link blocks
void testNetworkDetailsOffIgnoresBlocks() {
    MetricStore combined;
    combined.setEnum(metric::kNetInterfaceUp, 1, kNow);
    combined.setEnum(metric::kNetConnected, 1, kNow);
    combined.setF32(metric::kNetSignalDbm, -48, kNow);
    MetricStore withBlocks = combined;
    setLinkBlock(withBlocks, metric::kNetDongleBase, true, true);
    setLinkBlock(withBlocks, metric::kNetHomeBase, true, false);
    for (const Layout& layout : {oneWidget(inst(W::Network)), defaultLayout()}) {
        CHECK(renderPx(layout, combined) == renderPx(layout, withBlocks));
    }
    const WidgetContext ctx = ctxFor(withBlocks);
    CHECK(widgetHeight(inst(W::Network), &ctx) == 88);
}

// Compact off: two stacked blocks, dongle then Wi-Fi; 92 px connected, 50 px otherwise
void testNetworkDetailsBlocks() {
    const WidgetInstance card = rowsCard(W::Network);
    const auto heightFor = [&](bool dongleUp, bool homeUp) {
        MetricStore s;
        setLinkBlock(s, metric::kNetDongleBase, true, dongleUp);
        setLinkBlock(s, metric::kNetHomeBase, true, homeUp);
        const WidgetContext ctx = ctxFor(s);
        return widgetHeight(card, &ctx);
    };
    CHECK(heightFor(true, true) == 6 + 24 + 92 + 92 + 6);  // 220
    CHECK(heightFor(true, false) == 178 && heightFor(false, true) == 178);
    CHECK(heightFor(false, false) == 136);
    CHECK(widgetHeight(card) == 220);

    // a block that was never read (or failed) shows its N/A reason, not "Not connected", at the same height
    MetricStore notThere;
    setLinkBlock(notThere, metric::kNetDongleBase, false, false);
    setLinkBlock(notThere, metric::kNetHomeBase, false, false);
    MetricStore unread;
    MetricStore failed;
    failed.setNa(metric::kNetDongleBase + metric::kNetAdapterPresent, Reason::ReadFailed, kNow);
    failed.setNa(metric::kNetHomeBase + metric::kNetAdapterPresent, Reason::ReadFailed, kNow);
    const WidgetContext c1 = ctxFor(notThere), c2 = ctxFor(unread), c3 = ctxFor(failed);
    CHECK(widgetHeight(card, &c1) == 136 && widgetHeight(card, &c2) == 136 && widgetHeight(card, &c3) == 136);
    const Layout layout = oneWidget(card);
    CHECK(renderPx(layout, notThere) != renderPx(layout, unread));
    CHECK(renderPx(layout, notThere) != renderPx(layout, failed));

    // Compact off draws differently from on, and cairo stays happy
    MetricStore both;
    both.setEnum(metric::kNetInterfaceUp, 1, kNow);
    both.setEnum(metric::kNetConnected, 1, kNow);
    setLinkBlock(both, metric::kNetDongleBase, true, true);
    setLinkBlock(both, metric::kNetHomeBase, true, true);
    CHECK(renderPx(oneWidget(card), both) != renderPx(oneWidget(inst(W::Network)), both));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 260);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts()};
    const WidgetContext ctx = ctxFor(both);
    drawWidgetDynamic(pen, 8, 8, card, ctx, WidgetHistory());
    CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

// The old adapter key is gone: an unknown key, one warning, not written back
void testNetworkJson() {
    Layout out;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(parseLayoutText(R"({"columns":[[{"type":"network","details":true}]]})", out, warnings, error));
    CHECK(warnings.size() == 1 && warnings[0].find("unknown key details") != std::string::npos);
    const std::string text = layoutToJson(out, 0);
    Layout back;
    warnings.clear();
    CHECK(parseLayoutText(text, back, warnings, error) && warnings.empty() && back.sameWidgets(out));
    CHECK(layoutToJson(back, 0) == text);

    warnings.clear();
    CHECK(parseLayoutText(R"({"columns":[[{"type":"network","adapter":"home"}]]})", out, warnings, error));
    CHECK(warnings.size() == 1 && warnings[0].find("unknown key adapter") != std::string::npos);
    CHECK(out.columns[0].widgets.size() == 1);
    CHECK(layoutToJson(out, 0).find("adapter") == std::string::npos);
}

// CPU / RAM items: 6 + 26 + 20 per row + 10, N/A without a scan, cairo stays happy
void testProcessListWidgets() {
    for (const W type : {W::CpuItems, W::RamItems}) {
        WidgetInstance w = inst(type);
        CHECK(widgetHeight(w) == 6 + 26 + 20 * 15 + 10);
        w.maxItems = 3;
        CHECK(widgetHeight(w) == 6 + 26 + 20 * 3 + 10);
        w.maxItems = kMaxListItems;
        CHECK(widgetHeight(w) == 6 + 26 + 20 * 50 + 10);
        CHECK(isProcessList(type));
    }
    MetricStore store;
    store.setNa(metric::kCpuItemsRows, Reason::NotSampledYet, kNow);
    ProcessListsView lists;
    lists.cpuValid = true;
    lists.generation = 1;
    lists.byCpu = {{"vrchat", 10, 1, 42.5, 800}, {"steamwebhelper", 0, 3, 12.0, 300}};
    lists.byRam = {{"vrchat", 10, 1, 42.5, 800}};
    store.setI32(metric::kCpuItemsRows, 2, kNow);
    store.setI32(metric::kRamItemsRows, 1, kNow);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 600);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts()};
    const WidgetHistory history;
    for (const W type : {W::CpuItems, W::RamItems}) {
        WidgetContext ctx = ctxFor(store);  // ctx.processes == nullptr: N/A text
        drawWidgetDynamic(pen, 8, 8, inst(type), ctx, history);
        CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
        const unsigned na = pixelHash(surface);
        ctx.processes = &lists;
        drawWidgetDynamic(pen, 8, 8, inst(type), ctx, history);
        CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
        CHECK(pixelHash(surface) != na);  // the rows drew something
    }
    // in the grid too, and a store that never saw the scan
    const Layout layout = oneWidget(inst(W::CpuItems));
    CHECK(renderPx(layout, MetricStore()) != renderPx(layout, store));
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

// The hide_rules.json hint takes the card's last row, the card keeps its height; without Hide system it isn't drawn
void testHideRulesErrorHint() {
    MetricStore store;
    store.setI32(metric::kCpuItemsRows, 2, kNow);
    ProcessListsView lists;
    lists.cpuValid = true;
    lists.generation = 1;
    lists.byCpu = {{"vrchat", 10, 1, 42.5, 800}, {"steamwebhelper", 0, 3, 12.0, 300}};
    const auto draw = [&](bool hideSystem, bool error) {
        lists.hideRulesError = error;
        WidgetInstance card = inst(W::CpuItems);
        card.hideSystem = hideSystem;
        card.maxItems = 4;
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 200);
        cairo_t* cr = cairo_create(surface);
        const Pen pen {cr, &fonts()};
        WidgetContext ctx = ctxFor(store);
        ctx.processes = &lists;
        drawWidgetDynamic(pen, 8, 8, card, ctx, WidgetHistory());
        CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
        const unsigned hash = pixelHash(surface);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return hash;
    };
    CHECK(draw(true, true) != draw(true, false));    // the hint is on the card
    CHECK(draw(false, true) == draw(false, false));  // Hide system off: nothing to say
    // same height either way
    WidgetInstance card = inst(W::CpuItems);
    card.hideSystem = true;
    card.maxItems = 4;
    WidgetContext ctx = ctxFor(store);
    ctx.processes = &lists;
    lists.hideRulesError = false;
    const int plain = widgetHeight(card, &ctx);
    lists.hideRulesError = true;
    CHECK(widgetHeight(card, &ctx) == plain && plain == 6 + 26 + 20 * 4 + 10);
}

// ellipsize cuts at whole characters: always valid UTF-8, ends in "..", and keeps the longest prefix that fits
bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += n;
    }
    return true;
}

void testEllipsizeMixedText() {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 40);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts()};
    const double size = 15;
    for (const bool bold : {false, true}) {
        for (const std::string& s : {std::string("ab日本語cd"),
                                    std::string("日本a語b"),
                                    std::string("plain ascii text")}) {
            const double full = pen.measure(s, size, bold);
            CHECK(ellipsize(pen, s, size, full, bold) == s);  // fits as it is
            CHECK(ellipsize(pen, s, size, full + 50, bold) == s);
            // whole-character prefixes, longest first
            std::vector<std::string> prefixes;
            for (size_t i = 0; i < s.size(); ++i) {
                if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) prefixes.push_back(s.substr(0, i));
            }
            std::reverse(prefixes.begin(), prefixes.end());  // longest proper prefix ... empty
            for (double w = 0; w < full; w += 1.5) {
                std::string want = "..";
                for (const std::string& p : prefixes) {
                    if (pen.measure(p + "..", size, bold) <= w) {
                        want = p + "..";
                        break;
                    }
                }
                const std::string got = ellipsize(pen, s, size, w, bold);
                CHECK(validUtf8(got));
                CHECK(got.size() >= 2 && got.compare(got.size() - 2, 2, "..") == 0);
                if (got != want) {
                    std::printf("  ellipsize(w=%.1f) got [%s] want [%s]\n", w, got.c_str(), want.c_str());
                    CHECK(got == want);
                }
            }
        }
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

// ---- Text with broken UTF-8 doesn't upset cairo ----

void testInvalidUtf8Text() {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 40);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts()};
    const Color white {1, 1, 1};
    const char* broken[] = {"a\xFF" "b", "\xC0\xAF", "end\xE3\x81", "\xED\xA0\x80", "\x80\x80"};
    for (const char* text : broken) {
        pen.text(10, 25, text, 16, white);
        CHECK(pen.measure(text, 16) > 0);
        CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
    }
    // measured as the replacement character, so layout and drawing agree
    CHECK(pen.measure("a\xFF" "b", 16) == pen.measure("a\xEF\xBF\xBD" "b", 16));
    CHECK(pen.measure(std::string("x\xE3\x81"), 16) == pen.measure(std::string("x\xEF\xBF\xBD\xEF\xBF\xBD"), 16));
    CHECK(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

// ---- The segmented pill ----

/** @return the pixel as 0xRRGGBB (cairo ARGB32 is B, G, R, A bytes on little-endian) */
unsigned pixelRgb(cairo_surface_t* surface, int x, int y) {
    cairo_surface_flush(surface);
    const uint8_t* p = cairo_image_surface_get_data(surface) + y * cairo_image_surface_get_stride(surface) + x * 4;
    return (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[1]) << 8) | p[0];
}

void testSegmentedPillColours() {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 240, 70);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    const Pen pen {cr, &fonts()};
    // 200 x 48 at (10, 10): parts are 95 wide from x = 15 and x = 110. Sample 12 px inside each part's outer
    // end and mid-height, clear of the label and the check.
    for (const int selected : {0, 1}) {
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);
        drawSegmentedPill(pen, 10, 10, 200, 48, {{"On"}, {"Off"}}, selected, true);
        const unsigned left = pixelRgb(surface, 15 + 12, 34);
        const unsigned right = pixelRgb(surface, 205 - 12, 34);
        CHECK((selected == 0 ? left : right) == 0xe27dfd);   // kAccent
        CHECK((selected == 0 ? right : left) == 0x21262d);   // kControl
    }
    // unusable: no accent at full strength
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    drawSegmentedPill(pen, 10, 10, 200, 48, {{"On"}, {"Off"}}, 0, false);
    CHECK(pixelRgb(surface, 27, 34) != 0xe27dfd);

    // three parts (the attach pill: Head / L / R): 210 wide from x = 15, so 66.7 px each; the middle one is selected
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    drawSegmentedPill(pen, 10, 10, 210, 48, {{"Head"}, {"L"}, {"R"}}, 1, true);
    CHECK(pixelRgb(surface, 82 + 6, 34) == 0xe27dfd);  // the middle part's fill
    CHECK(pixelRgb(surface, 15 + 8, 34) != 0xe27dfd);
    CHECK(pixelRgb(surface, 215 - 8, 34) != 0xe27dfd);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

}  // namespace

int main() {
    testGeometry();
    testGeneralClockOffShrinks();
    testNetworkShrinksWhenNotExpanded();
    testCompactHeights();
    testV040HeightsUnchanged();
    testV040SavedLayout();
    testV040FullCardsGainRows();
    testCompactOffHeights();
    testCompactOnIgnoresNewMetrics();
    testNetworkDetailsOffIgnoresBlocks();
    testNetworkDetailsBlocks();
    testNetworkJson();
    testProcessListWidgets();
    testHideRulesErrorHint();
    testEllipsizeMixedText();
    testTitles();
    testEmptyStoreAllTypes();
    testDefaultFilled();
    testClockOffHidesTime();
    testNetworkInterfaceDown();
    testInvalidUtf8Text();
    testLanguageChangesTitles();
    testChangeDetection();
    testPartialRedraw();
    testCompactToggleKeepsGraphHistory();
    testSegmentedPillColours();
    if (failures == 0) std::printf("render-grid-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
