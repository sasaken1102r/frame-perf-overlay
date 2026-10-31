// The dashboard layout editor. Drives LayoutEditor (and, for the close path, SettingsPanel) with pointer events:
// drag / drop hit-testing and the drop marker, palette add / remove, the selected widget's settings buttons.
// No SteamVR; the only drawing is one render() to make sure the cards' hit boxes sit where they're painted.
#include "config.h"
#include "draw.h"
#include "i18n.h"
#include "layout_editor.h"
#include "layout_edit.h"
#include "settings_panel.h"
#include "widgets.h"

#include <cairo.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
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

using frame_perf_overlay::Layout;
using frame_perf_overlay::WidgetInstance;
using frame_perf_overlay::WidgetRef;
using W = frame_perf_overlay::WidgetType;
using Setting = LayoutEditor::Setting;
using Outcome = LayoutEditor::CommitOutcome;

// The rects SettingsPanel gives its editor (kPad 28, kCardY 124, kEditorCardW 1000, kCardH 468, palette to the right)
const EditorRect kLayoutCard {28, 124, 1000, 468};
const EditorRect kPaletteCard {1052, 124, 520, 468};

WidgetInstance inst(W type) {
    WidgetInstance w;
    w.type = type;
    w.compact = false;  // the layouts these tests build start with the Compact switch off (a palette add is on)
    return w;
}

Layout make(const std::vector<std::vector<W>>& columns, uint32_t version = 1) {
    Layout layout;
    layout.version = version;
    for (const std::vector<W>& types : columns) {
        frame_perf_overlay::LayoutColumn column;
        for (const W t : types) column.widgets.push_back(inst(t));
        layout.columns.push_back(column);
    }
    return layout;
}

bool columnIs(const Layout& layout, size_t column, const std::vector<W>& types) {
    if (column >= layout.columns.size()) return false;
    const std::vector<WidgetInstance>& widgets = layout.columns[column].widgets;
    if (widgets.size() != types.size()) return false;
    for (size_t i = 0; i < types.size(); ++i) {
        if (widgets[i].type != types[i]) return false;
    }
    return true;
}

struct Pt {
    double x, y;
};

/** An editor plus the "config.layout" the caller keeps for it. */
struct Rig {
    LayoutEditor ed {kLayoutCard, kPaletteCard};
    Layout layout;

    explicit Rig(Layout l) { set(std::move(l)); }

    void set(Layout l) {
        layout = std::move(l);
        ed.sync(layout);
    }

    EditorRect cardRect(WidgetRef ref) const {
        for (const auto& c : ed.cards()) {
            if (c.ref == ref) return c.rect;
        }
        std::printf("FAIL: no card (%d, %d)\n", ref.column, ref.index);
        ++failures;
        return {};
    }
    Pt centre(WidgetRef ref) const {
        const EditorRect r = cardRect(ref);
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
    /** A point in the card's column just above / below the card's centre (the drop slot before / after it). */
    Pt above(WidgetRef ref) const {
        const EditorRect r = cardRect(ref);
        return {r.x + r.w / 2, r.y + 3};
    }
    Pt below(WidgetRef ref) const {
        const EditorRect r = cardRect(ref);
        return {r.x + r.w / 2, r.y + r.h - 3};
    }
    /** The "new column" lane (the last one while there's room for another column). */
    Pt newLane() const {
        const EditorRect& lane = ed.lanes().back();
        return {lane.x + lane.w / 2, lane.y + 40};
    }
    Pt palette() const { return {kPaletteCard.x + kPaletteCard.w / 2, kPaletteCard.y + kPaletteCard.h / 2}; }
    EditorRect chipRect(W type) const {
        for (const auto& c : ed.chips()) {
            if (c.first == type) return c.second;
        }
        std::printf("FAIL: no chip\n");
        ++failures;
        return {};
    }
    Pt chip(W type) const {
        const EditorRect r = chipRect(type);
        return {r.x + r.w / 2, r.y + r.h / 2};
    }
    Pt setting(Setting s) const {
        for (const auto& b : ed.settingButtons()) {
            if (b.first == s) return {b.second.x + b.second.w / 2, b.second.y + b.second.h / 2};
        }
        std::printf("FAIL: no settings button\n");
        ++failures;
        return {};
    }
    bool hasSetting(Setting s) const {
        for (const auto& b : ed.settingButtons()) {
            if (b.first == s) return true;
        }
        return false;
    }

    /** Press at `from`, move well past the drag threshold, move to `to`, release. */
    void drag(Pt from, Pt to) {
        ed.pointerDown(from.x, from.y);
        ed.pointerMove(from.x + 30, from.y + 30);
        ed.pointerMove(to.x, to.y);
        ed.pointerUp(to.x, to.y);
    }
    void tap(Pt at) {
        ed.pointerDown(at.x, at.y);
        ed.pointerUp(at.x, at.y);
    }
    bool take(uint32_t& base, Layout& out) { return ed.takeCommit(base, out); }
    /** Takes a queued commit and plays the caller's part: trySetLayout accepted it. */
    bool apply() {
        uint32_t base = 0;
        Layout edited;
        if (!take(base, edited)) return false;
        layout = edited;
        layout.version = base + 1;
        ed.commitResult(Outcome::Applied);
        ed.sync(layout);
        return true;
    }
    bool nothingQueued() {
        uint32_t base = 0;
        Layout edited;
        return !take(base, edited);
    }
};

// 4 + 2 cards: small enough that nothing scrolls and the drag edges never trigger an auto-scroll
Layout baseLayout(uint32_t version = 1) {
    return make({{W::Frame, W::Cpu, W::Network, W::General}, {W::Power, W::Battery}}, version);
}

void checkFitsWithoutScrolling(const Rig& rig) {
    const EditorRect& lane = rig.ed.lanes()[0];
    for (const auto& c : rig.ed.cards()) {
        CHECK(c.rect.y >= lane.y && c.rect.y + c.rect.h <= lane.y + lane.h - 45);  // clear of the auto-scroll edge
    }
    CHECK(rig.ed.scrollY() == 0.0);
}

// ---- Geometry ----

void testGeometry() {
    Rig rig(baseLayout());
    checkFitsWithoutScrolling(rig);
    CHECK(rig.ed.lanes().size() == 3);  // two columns + the "new column" lane
    CHECK(rig.ed.cards().size() == 6);
    // Cards sit inside their lane and don't overlap
    for (const auto& c : rig.ed.cards()) {
        const EditorRect& lane = rig.ed.lanes()[static_cast<size_t>(c.ref.column)];
        CHECK(c.rect.x >= lane.x && c.rect.x + c.rect.w <= lane.x + lane.w);
    }
    for (int i = 0; i + 1 < 4; ++i) {
        const EditorRect a = rig.cardRect({0, i}), b = rig.cardRect({0, i + 1});
        CHECK(a.y + a.h <= b.y);
    }
    // One chip per type, all inside the palette card
    CHECK(rig.ed.chips().size() == 10);  // the ten widgets; the reserved numbers have no chip
    for (const auto& c : rig.ed.chips()) {
        CHECK(c.second.x >= kPaletteCard.x && c.second.x + c.second.w <= kPaletteCard.x + kPaletteCard.w);
        CHECK(c.second.y >= kPaletteCard.y && c.second.y + c.second.h <= kPaletteCard.y + kPaletteCard.h);
    }
    // 8 columns: no "new column" lane
    Rig wide(make({{W::Frame}, {W::Cpu}, {W::Power}, {W::Battery}, {W::Temp}, {W::General}, {W::GpuRam}, {W::Network}}));
    CHECK(wide.ed.lanes().size() == 8);
    CHECK(wide.ed.cards().size() == 8);
    // Empty layout: just the one lane to drop into
    Rig empty((Layout()));
    CHECK(empty.ed.lanes().size() == 1 && empty.ed.cards().empty());
}

// ---- Drag within a column ----

void testDragWithinColumn() {
    Rig rig(baseLayout(7));
    checkFitsWithoutScrolling(rig);

    // Card 0 dragged below card 2 → ends at index 2
    rig.ed.pointerDown(rig.centre({0, 0}).x, rig.centre({0, 0}).y);
    CHECK(rig.ed.busy());
    CHECK(rig.ed.dropTarget().first == -1);  // not a drag yet
    const Pt from = rig.centre({0, 0});
    CHECK(!rig.ed.pointerMove(from.x + 5, from.y + 5));  // under the threshold: still a press
    CHECK(rig.ed.dropTarget().first == -1);
    const Pt to = rig.below({0, 2});
    CHECK(rig.ed.pointerMove(to.x, to.y));
    CHECK(rig.ed.dropTarget() == std::make_pair(0, 3));  // the slot after card 2
    CHECK(rig.ed.pointerUp(to.x, to.y));
    CHECK(!rig.ed.busy());
    uint32_t base = 0;
    Layout edited;
    CHECK(rig.take(base, edited));
    CHECK(base == 7);
    CHECK(columnIs(edited, 0, {W::Cpu, W::Network, W::Frame, W::General}));
    CHECK(columnIs(edited, 1, {W::Power, W::Battery}));
    CHECK(!rig.take(base, edited));  // taken once

    // Card 3 dragged to the very top
    rig.set(baseLayout(7));
    rig.drag(rig.centre({0, 3}), rig.above({0, 0}));
    CHECK(rig.take(base, edited));
    CHECK(columnIs(edited, 0, {W::General, W::Frame, W::Cpu, W::Network}));

    // Dropping on its own slot, or just below itself: no marker, no commit
    for (int index = 0; index < 4; ++index) {
        for (const bool below : {false, true}) {
            rig.set(baseLayout(7));
            const WidgetRef ref {0, index};
            const Pt target = below ? rig.below(ref) : rig.above(ref);
            const Pt start = rig.centre(ref);
            rig.ed.pointerDown(start.x, start.y);
            rig.ed.pointerMove(start.x + 20, start.y + 20);
            rig.ed.pointerMove(target.x, target.y);
            CHECK(rig.ed.dropTarget().first == -1);
            rig.ed.pointerUp(target.x, target.y);
            CHECK(rig.nothingQueued());
        }
    }
}

// ---- Drag between columns ----

void testDragBetweenColumns() {
    uint32_t base = 0;
    Layout edited;
    Rig rig(baseLayout(2));

    // Into the middle of the other column
    rig.drag(rig.centre({0, 1}), rig.above({1, 1}));
    CHECK(rig.take(base, edited) && base == 2);
    CHECK(columnIs(edited, 0, {W::Frame, W::Network, W::General}));
    CHECK(columnIs(edited, 1, {W::Power, W::Cpu, W::Battery}));

    // Into the LAST slot of the other column
    rig.set(baseLayout(2));
    {
        const Pt start = rig.centre({0, 0});
        const Pt to = rig.below({1, 1});
        rig.ed.pointerDown(start.x, start.y);
        rig.ed.pointerMove(start.x + 20, start.y);
        rig.ed.pointerMove(to.x, to.y);
        CHECK(rig.ed.dropTarget() == std::make_pair(1, 2));
        rig.ed.pointerUp(to.x, to.y);
    }
    CHECK(rig.take(base, edited));
    CHECK(columnIs(edited, 0, {W::Cpu, W::Network, W::General}));
    CHECK(columnIs(edited, 1, {W::Power, W::Battery, W::Frame}));
    // The selection lands on the moved card (once the caller reports Applied)
    rig.layout = edited;
    rig.layout.version = base + 1;
    rig.ed.commitResult(Outcome::Applied);
    rig.ed.sync(rig.layout);
    CHECK(rig.ed.selected() == WidgetRef({1, 2}));
    CHECK(rig.layout.version == 3);

    // The next edit's base version is the new one
    rig.drag(rig.centre({1, 2}), rig.above({0, 0}));
    CHECK(rig.take(base, edited) && base == 3);

    // Into the first slot of an empty-ish spot: above the top card of the other column
    rig.set(baseLayout(2));
    rig.drag(rig.centre({0, 3}), rig.above({1, 0}));
    CHECK(rig.take(base, edited));
    CHECK(columnIs(edited, 1, {W::General, W::Power, W::Battery}));
}

void testNewColumnAndColumnRemoval() {
    uint32_t base = 0;
    Layout edited;

    // Into the "new column" lane
    Rig rig(baseLayout(5));
    {
        const Pt start = rig.centre({1, 0});
        const Pt to = rig.newLane();
        rig.ed.pointerDown(start.x, start.y);
        rig.ed.pointerMove(start.x + 20, start.y + 20);
        rig.ed.pointerMove(to.x, to.y);
        CHECK(rig.ed.dropTarget() == std::make_pair(2, 0));  // lane index 2 = the new column
        rig.ed.pointerUp(to.x, to.y);
    }
    CHECK(rig.take(base, edited));
    CHECK(edited.columns.size() == 3);
    CHECK(columnIs(edited, 2, {W::Power}) && columnIs(edited, 1, {W::Battery}));
    rig.layout = edited;
    rig.layout.version = base + 1;
    rig.ed.commitResult(Outcome::Applied);
    rig.ed.sync(rig.layout);
    CHECK(rig.ed.selected() == WidgetRef({2, 0}));
    CHECK(rig.ed.lanes().size() == 4);  // 3 columns + the new-column lane

    // A column's only widget moved out: the column goes, and the selection still lands on the moved card
    rig.set(make({{W::Frame}, {W::Power, W::Battery}}, 9));
    rig.drag(rig.centre({0, 0}), rig.below({1, 1}));
    CHECK(rig.take(base, edited) && base == 9);
    CHECK(edited.columns.size() == 1 && columnIs(edited, 0, {W::Power, W::Battery, W::Frame}));
    rig.layout = edited;
    rig.layout.version = 10;
    rig.ed.commitResult(Outcome::Applied);
    rig.ed.sync(rig.layout);
    CHECK(rig.ed.selected() == WidgetRef({0, 2}));
    CHECK(frame_perf_overlay::widgetAt(rig.layout, rig.ed.selected())->type == W::Frame);

    // Same, but the emptied column is the RIGHT one (the target column's index doesn't shift)
    rig.set(make({{W::Power, W::Battery}, {W::Frame}}, 11));
    rig.drag(rig.centre({1, 0}), rig.above({0, 0}));
    CHECK(rig.take(base, edited));
    CHECK(columnIs(edited, 0, {W::Frame, W::Power, W::Battery}) && edited.columns.size() == 1);
    rig.layout = edited;
    rig.layout.version = 12;
    rig.ed.commitResult(Outcome::Applied);
    rig.ed.sync(rig.layout);
    CHECK(rig.ed.selected() == WidgetRef({0, 0}));

    // Emptied column in the middle of three, moved to a new column: it lands in the last column
    rig.set(make({{W::Power, W::Battery}, {W::Frame}, {W::Cpu}}, 13));
    rig.drag(rig.centre({1, 0}), rig.newLane());
    CHECK(rig.take(base, edited));
    CHECK(edited.columns.size() == 3 && columnIs(edited, 1, {W::Cpu}) && columnIs(edited, 2, {W::Frame}));
    rig.layout = edited;
    rig.layout.version = 14;
    rig.ed.commitResult(Outcome::Applied);
    rig.ed.sync(rig.layout);
    CHECK(rig.ed.selected() == WidgetRef({2, 0}));

    // A widget alone in the last column dragged to "new column": nothing would change → no marker, no commit
    rig.set(make({{W::Power, W::Battery}, {W::Frame}}, 15));
    {
        const Pt start = rig.centre({1, 0});
        const Pt to = rig.newLane();
        rig.ed.pointerDown(start.x, start.y);
        rig.ed.pointerMove(start.x + 20, start.y + 20);
        rig.ed.pointerMove(to.x, to.y);
        CHECK(rig.ed.dropTarget().first == -1);
        rig.ed.pointerUp(to.x, to.y);
    }
    CHECK(rig.nothingQueued());
}

void testDragToPaletteRemoves() {
    uint32_t base = 0;
    Layout edited;
    Rig rig(baseLayout(4));
    {
        const Pt start = rig.centre({0, 1});
        const Pt to = rig.palette();
        rig.ed.pointerDown(start.x, start.y);
        rig.ed.pointerMove(start.x + 20, start.y + 20);
        rig.ed.pointerMove(to.x, to.y);
        CHECK(rig.ed.dropTarget().first == -1);  // over the palette there's no insert marker
        rig.ed.pointerUp(to.x, to.y);
    }
    CHECK(rig.take(base, edited) && base == 4);
    CHECK(columnIs(edited, 0, {W::Frame, W::Network, W::General}));
    CHECK(columnIs(edited, 1, {W::Power, W::Battery}));

    // The last widget of a column: the column goes too
    rig.set(make({{W::Frame}, {W::Power, W::Battery}}, 4));
    rig.drag(rig.centre({0, 0}), rig.palette());
    CHECK(rig.take(base, edited));
    CHECK(edited.columns.size() == 1 && columnIs(edited, 0, {W::Power, W::Battery}));
}

// ---- Taps, cancel, stale ----

void testTapSelects() {
    Rig rig(baseLayout());
    const Pt c = rig.centre({0, 1});
    CHECK(rig.ed.selected() == WidgetRef());
    rig.ed.pointerDown(c.x, c.y);
    rig.ed.pointerMove(c.x + 6, c.y + 6);  // a wobble under 14 px is still a tap
    rig.ed.pointerUp(c.x + 6, c.y + 6);
    CHECK(rig.ed.selected() == WidgetRef({0, 1}));
    CHECK(rig.nothingQueued());
    CHECK(rig.ed.settingButtons().size() == 3);  // Cpu: Remove and the Compact pair
    // Another card: the selection moves
    rig.tap(rig.centre({1, 0}));
    CHECK(rig.ed.selected() == WidgetRef({1, 0}));
    // Tapping the selected card again deselects
    rig.tap(rig.centre({1, 0}));
    CHECK(rig.ed.selected() == WidgetRef());
    CHECK(rig.ed.settingButtons().empty());
    CHECK(rig.nothingQueued());
    // Tap on empty lane space deselects
    rig.tap(rig.centre({0, 0}));
    CHECK(rig.ed.selected() == WidgetRef({0, 0}));
    const EditorRect& lane = rig.ed.lanes()[1];
    rig.tap({lane.x + lane.w / 2, lane.y + lane.h - 20});
    CHECK(rig.ed.selected() == WidgetRef());
    // A press outside everything does nothing
    CHECK(!rig.ed.pointerDown(5, 5));
    CHECK(!rig.ed.busy());
    // 14 px exactly counts as a drag
    const Pt s = rig.centre({0, 0});
    rig.ed.pointerDown(s.x, s.y);
    CHECK(rig.ed.pointerMove(s.x + LayoutEditor::kDragStartPx, s.y));
    rig.ed.pointerLeave();
}

void testPointerLeaveCancelsDrag() {
    Rig rig(baseLayout());
    const Pt start = rig.centre({0, 0});
    const Pt to = rig.below({0, 3});
    rig.ed.pointerDown(start.x, start.y);
    rig.ed.pointerMove(start.x + 20, start.y + 20);
    rig.ed.pointerMove(to.x, to.y);
    CHECK(rig.ed.busy());
    CHECK(rig.ed.dropTarget().first == 0);
    CHECK(rig.ed.pointerLeave());
    CHECK(!rig.ed.busy());
    CHECK(rig.ed.dropTarget().first == -1);
    CHECK(rig.nothingQueued());
    // The Up that may still arrive does nothing
    CHECK(!rig.ed.pointerUp(to.x, to.y));
    CHECK(rig.nothingQueued());
    CHECK(rig.ed.selected() == WidgetRef());
    // Leaving with nothing held: no redraw
    CHECK(!rig.ed.pointerLeave());
    // A palette-chip drag too
    const Pt chip = rig.chip(W::Power);
    Rig rig2(make({{W::Frame}, {W::Cpu}}));
    const Pt chip2 = rig2.chip(W::Power);
    rig2.ed.pointerDown(chip2.x, chip2.y);
    rig2.ed.pointerMove(chip2.x - 100, chip2.y + 200);
    const Pt lane = rig2.above({0, 0});
    rig2.ed.pointerMove(lane.x, lane.y);
    CHECK(rig2.ed.dropTarget().first == 0);
    rig2.ed.pointerLeave();
    rig2.ed.pointerUp(lane.x, lane.y);
    CHECK(rig2.nothingQueued());
    (void)chip;
}

void testSyncMidDragCancels() {
    Rig rig(baseLayout(3));
    const Pt start = rig.centre({0, 0});
    const Pt to = rig.below({0, 3});
    rig.ed.pointerDown(start.x, start.y);
    rig.ed.pointerMove(start.x + 20, start.y + 20);
    rig.ed.pointerMove(to.x, to.y);
    CHECK(rig.ed.busy());

    // Same version, same widgets: the drag goes on
    rig.ed.sync(baseLayout(3));
    CHECK(rig.ed.busy());

    // A PC changed the layout meanwhile (newer version): the indices are stale, the drag is cancelled
    Layout newer = make({{W::Cpu, W::Frame}, {W::Power}}, 4);
    rig.ed.sync(newer);
    CHECK(!rig.ed.busy());
    CHECK(rig.ed.dropTarget().first == -1);
    rig.ed.pointerMove(to.x, to.y);
    rig.ed.pointerUp(to.x, to.y);
    CHECK(rig.nothingQueued());
    // The editor shows the new layout
    CHECK(rig.ed.cards().size() == 3);

    // A newer version with the same widgets cancels too (the version is what the commit's base is checked on)
    rig.set(baseLayout(3));
    rig.ed.pointerDown(start.x, start.y);
    rig.ed.pointerMove(start.x + 20, start.y + 20);
    rig.ed.sync(baseLayout(4));
    CHECK(!rig.ed.busy());
    rig.ed.pointerUp(start.x + 20, start.y + 20);
    CHECK(rig.nothingQueued());

    // A tap's selection follows the layout: after a PC removes that widget the selection is dropped
    Rig sel(baseLayout(3));
    sel.tap(sel.centre({1, 1}));  // Battery
    CHECK(sel.ed.selected() == WidgetRef({1, 1}));
    sel.ed.sync(make({{W::Frame}, {W::Power}}, 4));
    CHECK(sel.ed.selected() == WidgetRef());
}

void testCommitOutcomes() {
    Rig rig(baseLayout(1));
    rig.tap(rig.centre({0, 0}));  // select Frame
    rig.drag(rig.centre({0, 0}), rig.below({1, 1}));
    uint32_t base = 0;
    Layout edited;
    CHECK(rig.take(base, edited));
    // Stale / Failed: the editor keeps what it showed and the selection stays
    rig.ed.commitResult(Outcome::Stale);
    CHECK(rig.ed.cards().size() == 6);
    CHECK(columnIs(rig.layout, 0, {W::Frame, W::Cpu, W::Network, W::General}));
    rig.ed.commitResult(Outcome::Failed);
    CHECK(rig.ed.cards().size() == 6);
}

// ---- Palette ----

void testPaletteTapAdds() {
    uint32_t base = 0;
    Layout edited;

    // With a card selected: right below it
    Rig rig(make({{W::Frame, W::Cpu}, {W::Battery}}, 6));
    rig.tap(rig.centre({0, 0}));
    CHECK(rig.ed.selected() == WidgetRef({0, 0}));
    rig.tap(rig.chip(W::Power));
    CHECK(rig.take(base, edited) && base == 6);
    CHECK(columnIs(edited, 0, {W::Frame, W::Power, W::Cpu}));
    rig.layout = edited;
    rig.layout.version = 7;
    rig.ed.commitResult(Outcome::Applied);
    rig.ed.sync(rig.layout);
    CHECK(rig.ed.selected() == WidgetRef({0, 1}));  // the new card is selected

    // Nothing selected: the end of the shortest column
    Rig rig2(make({{W::Frame, W::Cpu, W::GpuRam}, {W::Battery}}));
    const auto heightOf = [](const frame_perf_overlay::LayoutColumn& column) {
        int total = 0;
        for (const WidgetInstance& w : column.widgets) total += frame_perf_overlay::widgetHeight(w);
        return total;
    };
    const int h0 = heightOf(rig2.layout.columns[0]), h1 = heightOf(rig2.layout.columns[1]);
    CHECK(h0 != h1);
    const size_t shortest = h1 < h0 ? 1 : 0;
    rig2.tap(rig2.chip(W::Temp));
    CHECK(rig2.take(base, edited));
    CHECK(edited.columns[shortest].widgets.back().type == W::Temp);
    CHECK(edited.columns[1 - shortest].widgets.size() == rig2.layout.columns[1 - shortest].widgets.size());
    CHECK(frame_perf_overlay::countType(edited, W::Temp) == 1);

    // The selected card's column is full: it falls back to the shortest column
    Layout fullCol = make({{}, {W::Battery}});
    fullCol.columns[0].widgets.assign(frame_perf_overlay::kMaxWidgetsPerColumn, inst(W::Network));
    fullCol.columns[0].widgets[0] = inst(W::Frame);
    Rig rig3(fullCol);
    rig3.ed.sync(fullCol);
    // (the 16 cards don't fit the viewport; select through the editor's own scroll-free API by tapping the first)
    rig3.tap(rig3.centre({0, 0}));
    CHECK(rig3.ed.selected() == WidgetRef({0, 0}));
    rig3.tap(rig3.chip(W::Power));
    CHECK(rig3.take(base, edited));
    CHECK(edited.columns[0].widgets.size() == frame_perf_overlay::kMaxWidgetsPerColumn);
    CHECK(columnIs(edited, 1, {W::Battery, W::Power}));
}

void testTickedChipSelectsExisting() {
    Rig rig(baseLayout());
    CHECK(rig.ed.selected() == WidgetRef());
    rig.tap(rig.chip(W::Battery));
    CHECK(rig.nothingQueued());
    CHECK(rig.ed.selected() == WidgetRef({1, 1}));
    rig.tap(rig.chip(W::Frame));
    CHECK(rig.nothingQueued());
    CHECK(rig.ed.selected() == WidgetRef({0, 0}));
    CHECK(frame_perf_overlay::countType(rig.layout, W::Frame) == 1);  // never a second one
    // Dragging a ticked chip does not add a second one either (a tap-like release)
    const Pt c = rig.chip(W::Frame);
    const Pt to = rig.above({1, 0});
    rig.ed.pointerDown(c.x, c.y);
    rig.ed.pointerMove(to.x, to.y);
    CHECK(rig.ed.dropTarget().first == -1);
    rig.ed.pointerUp(to.x, to.y);
    CHECK(rig.nothingQueued());
}

// Every widget is single-instance: the chip adds it once, and ticked it selects the one that's there
void testChipAddsOnce() {
    Rig rig(make({{W::Frame}, {W::Cpu}}));
    rig.tap(rig.chip(W::Network));
    CHECK(rig.apply());
    CHECK(frame_perf_overlay::countType(rig.layout, W::Network) == 1);
    CHECK(rig.layout.version == 2);
    rig.tap(rig.chip(W::Network));
    CHECK(rig.nothingQueued());
    CHECK(frame_perf_overlay::countType(rig.layout, W::Network) == 1);
    const WidgetRef at = rig.ed.selected();
    CHECK(at.valid() && frame_perf_overlay::widgetAt(rig.layout, at)->type == W::Network);
}

void testUnofferedChips() {
    // Only the widgets this version has get a chip; None and the reserved numbers don't
    CHECK(!frame_perf_overlay::isOfferedInPalette(W::None));
    for (const int reserved : {10, 12, 13}) CHECK(!frame_perf_overlay::isOfferedInPalette(static_cast<W>(reserved)));
    Rig rig(baseLayout(2));
    for (const auto& chip : rig.ed.chips()) CHECK(frame_perf_overlay::isOfferedInPalette(chip.first));
    for (const W type : {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::CpuItems, W::RamItems, W::Network,
                         W::General}) {
        bool found = false;
        for (const auto& chip : rig.ed.chips()) found = found || chip.first == type;
        CHECK(found);
    }
}

void testChipDrag() {
    uint32_t base = 0;
    Layout edited;
    Rig rig(make({{W::Frame, W::Cpu}, {W::Battery, W::Temp}}, 8));
    // Into the middle of a column
    {
        const Pt c = rig.chip(W::Power);
        const Pt to = rig.above({1, 1});
        rig.ed.pointerDown(c.x, c.y);
        rig.ed.pointerMove(c.x - 10, c.y + 60);
        rig.ed.pointerMove(to.x, to.y);
        CHECK(rig.ed.dropTarget() == std::make_pair(1, 1));
        rig.ed.pointerUp(to.x, to.y);
    }
    CHECK(rig.take(base, edited) && base == 8);
    CHECK(columnIs(edited, 1, {W::Battery, W::Power, W::Temp}));
    CHECK(frame_perf_overlay::countType(edited, W::Power) == 1);
    // Into the new column
    rig.set(make({{W::Frame, W::Cpu}, {W::Battery, W::Temp}}, 8));
    rig.drag(rig.chip(W::Power), rig.newLane());
    CHECK(rig.take(base, edited));
    CHECK(edited.columns.size() == 3 && columnIs(edited, 2, {W::Power}));
    // Onto the palette itself: nothing (a chip isn't removable)
    rig.set(make({{W::Frame, W::Cpu}, {W::Battery, W::Temp}}, 8));
    rig.drag(rig.chip(W::Power), {kPaletteCard.x + 100, kPaletteCard.y + 400});
    CHECK(rig.nothingQueued());
}

void testLimitsInEditor() {
    // Every column full and 8 columns: nothing is addable, chips do nothing
    Layout packed = make({}, 3);
    for (size_t c = 0; c < frame_perf_overlay::kMaxColumns; ++c) {
        frame_perf_overlay::LayoutColumn column;
        column.widgets.assign(frame_perf_overlay::kMaxWidgetsPerColumn, inst(W::Network));
        packed.columns.push_back(column);
    }
    Rig rig(packed);
    CHECK(rig.ed.lanes().size() == frame_perf_overlay::kMaxColumns);
    rig.tap(rig.chip(W::Frame));
    CHECK(rig.nothingQueued());
    rig.tap(rig.chip(W::Network));
    CHECK(rig.nothingQueued());
    // Drag from a chip: refused up front
    const Pt c = rig.chip(W::Power);
    rig.ed.pointerDown(c.x, c.y);
    CHECK(!rig.ed.pointerMove(c.x + 50, c.y + 50));
    rig.ed.pointerUp(c.x + 50, c.y + 50);
    CHECK(rig.nothingQueued());

    // One column with room: addable again
    Layout almost = packed;
    almost.columns[7].widgets.pop_back();
    rig.set(almost);
    rig.tap(rig.chip(W::Frame));
    uint32_t base = 0;
    Layout edited;
    CHECK(rig.take(base, edited));
    CHECK(edited.columns[7].widgets.size() == frame_perf_overlay::kMaxWidgetsPerColumn);
    CHECK(frame_perf_overlay::countType(edited, W::Frame) == 1);
}

// ---- The selected widget's settings ----

/** Presses a settings button; @return the edited widget (the one at `ref`) if a commit was queued. */
bool pressSetting(Rig& rig, Setting s, WidgetRef ref, WidgetInstance& out) {
    const Pt p = rig.setting(s);
    rig.ed.pointerDown(p.x, p.y);
    rig.ed.pointerUp(p.x, p.y);
    uint32_t base = 0;
    Layout edited;
    if (!rig.take(base, edited)) return false;
    out = *frame_perf_overlay::widgetAt(edited, ref);
    return true;
}

void testSettingsButtons() {
    // Which buttons show for which widget: Remove always, Compact where the card has extra rows, items / sum / hide
    // for the process lists
    for (const W type : {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::CpuItems, W::RamItems, W::Network,
                         W::General}) {
        Rig rig(make({{type}}));
        rig.tap(rig.centre({0, 0}));
        const bool compact = frame_perf_overlay::hasCompactView(type);
        const bool list = frame_perf_overlay::isProcessList(type);
        CHECK(rig.hasSetting(Setting::Remove));
        CHECK(rig.hasSetting(Setting::CompactOn) == compact && rig.hasSetting(Setting::CompactOff) == compact);
        CHECK(rig.hasSetting(Setting::ItemsMore) == list && rig.hasSetting(Setting::SumOn) == list &&
              rig.hasSetting(Setting::HideOff) == list);
        const size_t expected = 1 + (compact ? 2 : 0) + (list ? 6 : 0);
        CHECK(rig.ed.settingButtons().size() == expected);
        // They all sit inside the palette card and don't overlap the chips
        for (const auto& b : rig.ed.settingButtons()) {
            CHECK(b.second.x >= kPaletteCard.x && b.second.x + b.second.w <= kPaletteCard.x + kPaletteCard.w);
            CHECK(b.second.y + b.second.h <= kPaletteCard.y + kPaletteCard.h);
            for (const auto& chip : rig.ed.chips()) CHECK(b.second.y >= chip.second.y + chip.second.h);
        }
        // pressing flips only that field; a no-op press queues nothing
        if (compact) {
            WidgetInstance out;
            WidgetInstance want = rig.layout.columns[0].widgets[0];
            CHECK(!pressSetting(rig, Setting::CompactOff, {0, 0}, out));
            CHECK(pressSetting(rig, Setting::CompactOn, {0, 0}, out));
            want.compact = true;
            CHECK(out == want);
        }
    }

    // A settings edit keeps the selection (once applied) and bases on the press-time version
    {
        Rig rig(make({{W::Frame, W::Battery}}, 12));
        rig.tap(rig.centre({0, 1}));
        const Pt p = rig.setting(Setting::CompactOn);
        rig.ed.pointerDown(p.x, p.y);
        rig.ed.pointerUp(p.x, p.y);
        uint32_t base = 0;
        Layout edited;
        CHECK(rig.take(base, edited) && base == 12);
        CHECK(edited.columns[0].widgets[1].compact);
        rig.layout = edited;
        rig.layout.version = 13;
        rig.ed.commitResult(Outcome::Applied);
        rig.ed.sync(rig.layout);
        CHECK(rig.ed.selected() == WidgetRef({0, 1}));
        // ... and with Compact on, Off queues the way back
        WidgetInstance out;
        CHECK(!pressSetting(rig, Setting::CompactOn, {0, 1}, out));
        CHECK(pressSetting(rig, Setting::CompactOff, {0, 1}, out) && !out.compact);
    }

    // Remove
    {
        Rig rig(make({{W::Frame, W::Cpu}, {W::Power}}, 5));
        rig.tap(rig.centre({0, 0}));
        const Pt p = rig.setting(Setting::Remove);
        rig.ed.pointerDown(p.x, p.y);
        rig.ed.pointerUp(p.x, p.y);
        uint32_t base = 0;
        Layout edited;
        CHECK(rig.take(base, edited) && base == 5);
        CHECK(columnIs(edited, 0, {W::Cpu}) && columnIs(edited, 1, {W::Power}));
        rig.layout = edited;
        rig.layout.version = 6;
        rig.ed.commitResult(Outcome::Applied);
        rig.ed.sync(rig.layout);
        CHECK(rig.ed.selected() == WidgetRef());
        CHECK(rig.ed.settingButtons().empty());

        // The only widget in its column: the column goes with it
        Rig rig2(make({{W::Frame}, {W::Power}}, 5));
        rig2.tap(rig2.centre({1, 0}));
        const Pt q = rig2.setting(Setting::Remove);
        rig2.ed.pointerDown(q.x, q.y);
        rig2.ed.pointerUp(q.x, q.y);
        CHECK(rig2.take(base, edited));
        CHECK(edited.columns.size() == 1 && columnIs(edited, 0, {W::Frame}));
    }
}

// ---- Drawing: the hit boxes are where the cards are painted ----

void testRenderMatchesHitBoxes() {
    Config cfg;
    cfg.language = Language::En;
    FontSet fonts;
    fonts.load(cfg.fontPath, cfg.boldFontPath);
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1600, 700);
    cairo_t* cr = cairo_create(surface);
    {
        Rig rig(baseLayout());
        rig.tap(rig.centre({0, 1}));
        const Pen pen {cr, &fonts};
        rig.ed.render(pen, uiText(Language::En));
        cairo_surface_flush(surface);
        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const auto pixelAt = [&](double x, double y) {
            const unsigned char* p = data + static_cast<int>(y) * stride + static_cast<int>(x) * 4;
            return (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[1]) << 8) | p[0];
        };
        // The card's body is painted differently from the lane behind it; the pixel just outside is the lane's colour
        for (const auto& c : rig.ed.cards()) {
            const unsigned inside = pixelAt(c.rect.x + 4, c.rect.y + c.rect.h / 2);
            const unsigned outside = pixelAt(c.rect.x - 4, c.rect.y + c.rect.h / 2);
            CHECK(inside != outside);
        }
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

// ---- SettingsPanel: the dashboard closing mid-drag ----

void testPanelLeaveCancelsDrag() {
    Config cfg;
    cfg.language = Language::En;
    FontSet fonts;
    fonts.load(cfg.fontPath, cfg.boldFontPath);
    SettingsPanel panel(fonts);
    CHECK(panel.page() == SettingsPage::Placement);  // Placement is the first page now
    panel.showPage(SettingsPage::Layout);           // this test drives the editor
    const AutostartStatus service {AutostartStatus::State::Enabled, false, false};
    cfg.layout = baseLayout(9);  // render() takes the layout from the config, like the agent's loop
    panel.syncLayout(cfg.layout);
    panel.render(cfg, service, {});

    const auto cardCentre = [&](WidgetRef ref) {
        for (const auto& c : panel.editor().cards()) {
            if (c.ref == ref) return Pt {c.rect.x + c.rect.w / 2, c.rect.y + c.rect.h / 2};
        }
        ++failures;
        std::printf("FAIL: panel has no card\n");
        return Pt {0, 0};
    };
    const Pt start = cardCentre({0, 0});
    const Pt to = cardCentre({1, 1});
    CHECK(panel.pointerDown(start.x, start.y, 1) == SettingsAction::None);
    CHECK(panel.editorBusy());
    panel.pointerMove(start.x + 30, start.y + 30);
    panel.pointerMove(to.x, to.y);
    CHECK(panel.editor().dropTarget().first == 1);
    CHECK(panel.pointerLeave());  // the dashboard closed / the laser left
    CHECK(!panel.editorBusy());
    CHECK(panel.editor().dropTarget().first == -1);
    uint32_t base = 0;
    Layout edited;
    CHECK(!panel.takeLayoutCommit(base, edited));
    panel.pointerUp(to.x, to.y);  // a late Up
    CHECK(!panel.takeLayoutCommit(base, edited));

    // The same drag, released normally, does commit (so the check above can fail)
    panel.pointerDown(start.x, start.y, 2);
    panel.pointerMove(start.x + 30, start.y + 30);
    panel.pointerMove(to.x, to.y);
    panel.pointerUp(to.x, to.y);
    const bool got = panel.takeLayoutCommit(base, edited);
    if (!got || base != 9) {
        std::printf("  (panel commit: got=%d base=%u busy=%d)\n", got, base, panel.editorBusy());
    }
    CHECK(got && base == 9);
    CHECK(frame_perf_overlay::countType(edited, W::Frame) == 1 && edited.columns[0].widgets.size() == 3);

    // Switching to the placement page also drops a press
    panel.pointerDown(start.x, start.y, 3);
    CHECK(panel.editorBusy());
    panel.pointerLeave();
    panel.showPage(SettingsPage::Placement);
    CHECK(panel.page() == SettingsPage::Placement);
    CHECK(!panel.editorBusy());
}

// ---- The Profiles page (two-tap Use / Delete, selection, disabled buttons) ----

// The two-tap rule uses "unsaved" as of the press (a PC's edit after the last draw), and any other
// press, even one that returns early (Quit, a page tab), cancels a pending second tap
void testProfilesReviewFixes() {
    Config cfg;
    cfg.language = Language::En;
    FontSet fonts;
    fonts.load(cfg.fontPath, cfg.boldFontPath);
    SettingsPanel panel(fonts);
    const AutostartStatus service {AutostartStatus::State::Enabled, false, false};
    panel.syncLayout(cfg.layout);
    ProfilesUiState ui;
    ui.items = {{"profile-1", "Gaming"}, {"profile-2", "Desk"}};
    ui.activeId = "profile-1";
    ui.unsaved = false;  // drawn clean...
    panel.showPage(SettingsPage::Profiles);
    panel.render(cfg, service, {}, ui);
    double ux = 0, uy = 0, rx = 0, ry = 0, qx = 0, qy = 0;
    CHECK(panel.buttonCenter(SettingsAction::ProfileUse, ux, uy));
    CHECK(panel.buttonCenter(SettingsAction::ProfileReset, rx, ry));
    CHECK(panel.buttonCenter(SettingsAction::Quit, qx, qy));

    // ...then a PC's edit lands before the next draw: the agent passes the current state before the press
    panel.setProfilesUnsaved(true);
    CHECK(panel.pointerDown(ux, uy, 10) == SettingsAction::None);  // asks first: the PC's edit isn't silently lost
    panel.pointerUp(ux, uy);
    CHECK(panel.pointerDown(ux, uy, 11) == SettingsAction::ProfileUse);
    panel.pointerUp(ux, uy);
    CHECK(panel.pointerDown(rx, ry, 12) == SettingsAction::None);  // Reset the same
    panel.pointerUp(rx, ry);
    CHECK(panel.pointerDown(rx, ry, 13) == SettingsAction::ProfileReset);
    panel.pointerUp(rx, ry);

    // Use → Quit → Use: Quit (an early return) cancels the armed Use, so the last tap only arms again
    CHECK(panel.pointerDown(ux, uy, 20) == SettingsAction::None);
    panel.pointerUp(ux, uy);
    CHECK(panel.pointerDown(qx, qy, 20.5) == SettingsAction::None);  // Quit's first tap
    panel.pointerUp(qx, qy);
    CHECK(panel.pointerDown(ux, uy, 21) == SettingsAction::None);
    panel.pointerUp(ux, uy);
    CHECK(panel.pointerDown(ux, uy, 21.5) == SettingsAction::ProfileUse);
    panel.pointerUp(ux, uy);

    // and back to clean: Use goes on the first tap
    panel.setProfilesUnsaved(false);
    CHECK(panel.pointerDown(ux, uy, 30) == SettingsAction::ProfileUse);
    panel.pointerUp(ux, uy);
}

void testProfilesPanel() {
    Config cfg;
    cfg.language = Language::En;
    FontSet fonts;
    fonts.load(cfg.fontPath, cfg.boldFontPath);
    SettingsPanel panel(fonts);
    const AutostartStatus service {AutostartStatus::State::Enabled, false, false};
    CHECK(panel.page() == SettingsPage::Placement);  // the tab opens on the first page
    panel.syncLayout(cfg.layout);

    ProfilesUiState ui;
    ui.items = {{"profile-1", "Gaming"}, {"profile-2", "Desk"}};
    ui.activeId = "profile-1";
    ui.unsaved = true;
    panel.showPage(SettingsPage::Profiles);
    panel.render(cfg, service, {}, ui);
    CHECK(panel.selectedProfileId() == "profile-1");  // the active one is selected first

    const auto centre = [&](SettingsAction a, double& x, double& y) {
        const bool ok = panel.buttonCenter(a, x, y);
        CHECK(ok);
        return ok;
    };
    double x = 0, y = 0;

    // tab bar order: Placement | Layout | Profiles
    double px, py, lx, ly, fx, fy;
    CHECK(centre(SettingsAction::PagePlacement, px, py) && centre(SettingsAction::PageLayout, lx, ly) &&
          centre(SettingsAction::PageProfiles, fx, fy));
    CHECK(px < lx && lx < fx);
    CHECK(py == ly && ly == fy);

    // Use with unsaved changes: two taps within 3 s
    CHECK(centre(SettingsAction::ProfileUse, x, y));
    CHECK(panel.pointerDown(x, y, 100) == SettingsAction::None);
    panel.pointerUp(x, y);
    CHECK(panel.pointerDown(x, y, 101.5) == SettingsAction::ProfileUse);
    panel.pointerUp(x, y);
    // ...and the arm is spent: the next tap arms again
    CHECK(panel.pointerDown(x, y, 102) == SettingsAction::None);
    panel.pointerUp(x, y);
    // an arm expires: tick(now + 4) drops it
    CHECK(panel.tick(106));
    CHECK(panel.pointerDown(x, y, 106.5) == SettingsAction::None);  // re-arms, not a confirm
    panel.pointerUp(x, y);
    CHECK(panel.pointerDown(x, y, 107) == SettingsAction::ProfileUse);
    panel.pointerUp(x, y);
    // even without a tick, a second tap after the 3 s is a fresh first tap
    CHECK(panel.pointerDown(x, y, 200) == SettingsAction::None);
    panel.pointerUp(x, y);
    CHECK(panel.pointerDown(x, y, 204) == SettingsAction::None);
    panel.pointerUp(x, y);
    // arming Use then tapping another button cancels it
    double rx, ry;
    CHECK(centre(SettingsAction::ProfileRename, rx, ry));
    CHECK(panel.pointerDown(rx, ry, 205) == SettingsAction::ProfileRename);
    panel.pointerUp(rx, ry);
    CHECK(panel.pointerDown(x, y, 205.5) == SettingsAction::None);  // Use was disarmed by Rename

    // Delete always needs two taps, unsaved or not
    double dx, dy;
    CHECK(centre(SettingsAction::ProfileDelete, dx, dy));
    CHECK(panel.pointerDown(dx, dy, 300) == SettingsAction::None);
    panel.pointerUp(dx, dy);
    CHECK(panel.pointerDown(dx, dy, 301) == SettingsAction::ProfileDelete);
    panel.pointerUp(dx, dy);
    CHECK(panel.pointerDown(dx, dy, 302) == SettingsAction::None);  // spent
    panel.pointerUp(dx, dy);
    CHECK(panel.tick(310));
    CHECK(panel.pointerDown(dx, dy, 311) == SettingsAction::None);
    panel.pointerUp(dx, dy);
    // Use then Delete: the armed action is Delete's, so Use is not confirmed by Delete's second tap
    CHECK(panel.pointerDown(x, y, 320) == SettingsAction::None);
    panel.pointerUp(x, y);
    CHECK(panel.pointerDown(dx, dy, 320.5) == SettingsAction::None);
    panel.pointerUp(dx, dy);

    // Reset with unsaved changes is two taps too
    double zx, zy;
    CHECK(centre(SettingsAction::ProfileReset, zx, zy));
    CHECK(panel.pointerDown(zx, zy, 400) == SettingsAction::None);
    panel.pointerUp(zx, zy);
    CHECK(panel.pointerDown(zx, zy, 401) == SettingsAction::ProfileReset);
    panel.pointerUp(zx, zy);

    // unsaved = false: Use and Reset go through on the first tap; Delete still needs two
    ui.unsaved = false;
    ui.revision++;
    panel.render(cfg, service, {}, ui);
    CHECK(centre(SettingsAction::ProfileUse, x, y));
    CHECK(panel.pointerDown(x, y, 500) == SettingsAction::ProfileUse);
    panel.pointerUp(x, y);
    CHECK(centre(SettingsAction::ProfileReset, zx, zy));
    CHECK(panel.pointerDown(zx, zy, 501) == SettingsAction::ProfileReset);
    panel.pointerUp(zx, zy);
    CHECK(centre(SettingsAction::ProfileDelete, dx, dy));
    CHECK(panel.pointerDown(dx, dy, 502) == SettingsAction::None);
    panel.pointerUp(dx, dy);

    // Tapping row 2 selects profile-2; tapping a row also cancels an armed Delete
    CHECK(centre(SettingsAction::ProfileRow1, x, y));
    CHECK(panel.pointerDown(x, y, 503) == SettingsAction::None);
    panel.pointerUp(x, y);
    CHECK(panel.selectedProfileId() == "profile-2");
    CHECK(panel.pointerDown(dx, dy, 504) == SettingsAction::None);  // Delete armed again (cancelled by the row tap)
    panel.pointerUp(dx, dy);
    CHECK(centre(SettingsAction::ProfileRow0, x, y));
    CHECK(panel.pointerDown(x, y, 504.5) == SettingsAction::None);
    panel.pointerUp(x, y);
    CHECK(panel.selectedProfileId() == "profile-1");
    CHECK(panel.pointerDown(dx, dy, 505) == SettingsAction::None);  // not confirmed: the row tap disarmed it
    panel.pointerUp(dx, dy);
    // selectProfile() is what the caller / tests use
    panel.selectProfile("profile-2");
    CHECK(panel.selectedProfileId() == "profile-2");
    // a row that doesn't exist (only 2 items) has no button to hit
    double ox, oy;
    CHECK(!panel.buttonCenter(SettingsAction::ProfileRow5, ox, oy) ||
          panel.pointerDown(ox, oy, 506) == SettingsAction::None);
    CHECK(panel.selectedProfileId() == "profile-2");

    // New: not hittable with 8 items
    CHECK(centre(SettingsAction::ProfileNew, x, y));
    CHECK(panel.pointerDown(x, y, 600) == SettingsAction::ProfileNew);  // 2 of 8: fine
    panel.pointerUp(x, y);
    ProfilesUiState full = ui;
    full.items.clear();
    for (int i = 1; i <= 8; ++i) full.items.push_back({"profile-" + std::to_string(i), "P" + std::to_string(i)});
    full.revision++;
    panel.render(cfg, service, {}, full);
    CHECK(centre(SettingsAction::ProfileNew, x, y));
    CHECK(panel.pointerDown(x, y, 601) == SettingsAction::None);
    panel.pointerUp(x, y);
    // the 8th row is selectable
    CHECK(centre(SettingsAction::ProfileRow7, x, y));
    CHECK(panel.pointerDown(x, y, 602) == SettingsAction::None);
    CHECK(panel.selectedProfileId() == "profile-8");
    panel.pointerUp(x, y);

    // Save: not hittable with no active profile, hittable with one
    ProfilesUiState none = ui;
    none.activeId = "";
    none.revision++;
    panel.render(cfg, service, {}, none);
    CHECK(centre(SettingsAction::ProfileSave, x, y));
    CHECK(panel.pointerDown(x, y, 700) == SettingsAction::None);
    panel.pointerUp(x, y);
    // an active id that isn't in the list counts as none
    ProfilesUiState gone = ui;
    gone.activeId = "profile-7";
    gone.revision++;
    panel.render(cfg, service, {}, gone);
    CHECK(centre(SettingsAction::ProfileSave, x, y));
    CHECK(panel.pointerDown(x, y, 701) == SettingsAction::None);
    panel.pointerUp(x, y);
    panel.render(cfg, service, {}, ui);  // active = profile-1
    CHECK(centre(SettingsAction::ProfileSave, x, y));
    CHECK(panel.pointerDown(x, y, 702) == SettingsAction::ProfileSave);
    panel.pointerUp(x, y);

    // nothing selected (no profiles): Use / Rename / Delete are not hittable
    ProfilesUiState empty;
    empty.revision = 99;
    SettingsPanel blank(fonts);
    blank.showPage(SettingsPage::Profiles);
    blank.syncLayout(cfg.layout);
    blank.render(cfg, service, {}, empty);
    CHECK(blank.selectedProfileId().empty());
    for (const SettingsAction a : {SettingsAction::ProfileUse, SettingsAction::ProfileRename, SettingsAction::ProfileDelete,
                                   SettingsAction::ProfileSave}) {
        if (blank.buttonCenter(a, x, y)) CHECK(blank.pointerDown(x, y, 800) == SettingsAction::None);
    }
    if (blank.buttonCenter(SettingsAction::ProfileNew, x, y)) CHECK(blank.pointerDown(x, y, 801) == SettingsAction::ProfileNew);

    // Page buttons switch pages and drop an armed second tap
    CHECK(centre(SettingsAction::PagePlacement, px, py));
    CHECK(panel.pointerDown(px, py, 900) == SettingsAction::None);
    CHECK(panel.page() == SettingsPage::Placement);
    panel.pointerUp(px, py);
    panel.render(cfg, service, {}, ui);
    CHECK(centre(SettingsAction::PageProfiles, fx, fy));
    CHECK(panel.pointerDown(fx, fy, 901) == SettingsAction::None);
    CHECK(panel.page() == SettingsPage::Profiles);
    panel.pointerUp(fx, fy);
}

// sync() says whether the tab must redraw, so a PC's edit (no pointer event) refreshes the open tab
void testSyncReportsChange() {
    Rig rig(baseLayout(1));
    CHECK(!rig.ed.sync(baseLayout(1)));  // same layout: no redraw
    CHECK(rig.ed.sync(baseLayout(2)));   // a newer version (a PC's LAYOUT_SET)
    CHECK(!rig.ed.sync(baseLayout(2)));
    CHECK(rig.ed.sync(make({{W::Frame}, {W::Power}}, 2)));  // same version, other widgets (a file reload)
    CHECK(rig.ed.cards().size() == 2);
}

// Scrolled down, then the layout gets shorter → the offset is clamped before the cards are placed
void testShrinkClampsScroll() {
    Layout tall = make({{}}, 1);
    tall.columns[0].widgets.assign(12, inst(W::Temp));  // more cards than a real layout has, to overflow the lane
    Rig rig(tall);
    const EditorRect lane = rig.ed.lanes().front();
    rig.ed.pointerMove(lane.x + lane.w / 2, lane.y + lane.h / 2);
    CHECK(rig.ed.scroll(-20));  // to the bottom
    CHECK(rig.ed.scrollY() > 100);
    rig.set(make({{W::Power, W::Battery}}, 2));  // fits now
    CHECK(rig.ed.scrollY() == 0.0);
    checkFitsWithoutScrolling(rig);
    CHECK(rig.cardRect({0, 0}).y == lane.y + 8);  // the first card sits at the top, not clipped above it
}

// Compact is one pill over two adjacent halves (the two Setting rects)
void testPills() {
    for (const W type : {W::Battery, W::Temp}) {
        Rig rig(make({{type}}));
        rig.tap(rig.centre({0, 0}));
        EditorRect a {}, b {};
        for (const auto& s : rig.ed.settingButtons()) {
            if (s.first == Setting::CompactOn) a = s.second;
            if (s.first == Setting::CompactOff) b = s.second;
        }
        CHECK(a.w > 0 && b.w > 0);
        CHECK(a.y == b.y && a.h == b.h && a.w == b.w);
        CHECK(a.x + a.w == b.x);
        for (const EditorRect& r : {a, b}) {
            CHECK(r.x >= kPaletteCard.x && r.x + r.w <= kPaletteCard.x + kPaletteCard.w);
            CHECK(r.y + r.h <= kPaletteCard.y + kPaletteCard.h);
            for (const auto& chip : rig.ed.chips()) CHECK(r.y >= chip.second.y + chip.second.h);
        }

        // each half queues the right edit; the half that's already the state queues nothing
        WidgetInstance out;
        const WidgetInstance before = rig.layout.columns[0].widgets[0];
        CHECK(!pressSetting(rig, Setting::CompactOff, {0, 0}, out));  // already off
        CHECK(pressSetting(rig, Setting::CompactOn, {0, 0}, out));
        WidgetInstance want = before;
        want.compact = true;
        CHECK(out == want);
        // flip it on in the layout, then the other half queues the way back
        rig.set([&] {
            Layout l = rig.layout;
            l.columns[0].widgets[0] = want;
            return l;
        }());
        CHECK(!pressSetting(rig, Setting::CompactOn, {0, 0}, out));
        CHECK(pressSetting(rig, Setting::CompactOff, {0, 0}, out));
        CHECK(out == before);
    }
}

// No palette chip label may shrink to fitSize's minimum (same formula as LayoutEditor::drawPalette)
void testChipLabelSizes() {
    Config cfg;
    FontSet fonts;
    fonts.load(cfg.fontPath, cfg.boldFontPath);
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts};
    const UiText& text = uiText(Language::En);
    Rig rig(make({{W::Frame}}));
    for (const auto& chip : rig.ed.chips()) {
        const W type = chip.first;
        std::string label;
        if (type == W::Network) {
            label = text.paletteNetwork;
        } else {
            WidgetInstance w;
            w.type = type;
            label = frame_perf_overlay::widgetTitle(w, frame_perf_overlay::WidgetLanguage::En);
        }
        // a present chip: check mark + bold
        CHECK(fitSize(pen, label, 17, 11, chip.second.w - 20 - 20, true) > 11);
        // idle
        CHECK(fitSize(pen, label, 17, 11, chip.second.w - 20, false) > 11);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

// A card added from the palette starts compact, whatever the type, and Battery / Temp then write "compact": true
void testPaletteAddIsCompact() {
    for (const W type : {W::Frame, W::Power, W::Battery, W::Temp, W::General}) {
        Layout l = make({{W::Cpu}});
        Rig rig(l);
        rig.tap(rig.chip(type));
        uint32_t base = 0;
        Layout edited;
        CHECK(rig.take(base, edited));
        CHECK(frame_perf_overlay::countType(edited, type) == 1);
        for (const auto& column : edited.columns) {
            for (const WidgetInstance& w : column.widgets) {
                if (w.type == type) {
                    CHECK(w.compact);
                    const std::string json = frame_perf_overlay::layoutToJson(edited, 0);
                    CHECK((json.find("\"compact\":true") != std::string::npos) == (type == W::Battery || type == W::Temp));
                }
            }
        }
    }
}

// The Compact switch is on every card with extra rows; the list settings change only their own field
void testCompactSwitch() {
    WidgetInstance out;
    for (const W type : {W::Frame, W::Cpu, W::Power, W::Battery, W::Temp, W::Network, W::General}) {
        Layout l = make({{type}});
        l.columns[0].widgets[0].compact = false;
        Rig rig(l);
        rig.tap(rig.centre({0, 0}));
        CHECK(!pressSetting(rig, Setting::CompactOff, {0, 0}, out));  // already off: nothing queued
        CHECK(pressSetting(rig, Setting::CompactOn, {0, 0}, out) && out.compact);
    }
    {
        Rig rig(make({{W::CpuItems}}));
        rig.tap(rig.centre({0, 0}));
        CHECK(!pressSetting(rig, Setting::SumOff, {0, 0}, out));
        CHECK(pressSetting(rig, Setting::ItemsMore, {0, 0}, out) && out.maxItems == frame_perf_overlay::kDefaultListItems + 1);
        CHECK(pressSetting(rig, Setting::HideOn, {0, 0}, out) && out.hideSystem && !out.sumSameName);
    }
}

}  // namespace

int main() {
    testCompactSwitch();
    testPaletteAddIsCompact();
    testSyncReportsChange();
    testShrinkClampsScroll();
    testGeometry();
    testDragWithinColumn();
    testDragBetweenColumns();
    testNewColumnAndColumnRemoval();
    testDragToPaletteRemoves();
    testTapSelects();
    testPointerLeaveCancelsDrag();
    testSyncMidDragCancels();
    testCommitOutcomes();
    testPaletteTapAdds();
    testTickedChipSelectsExisting();
    testChipAddsOnce();
    testUnofferedChips();
    testChipDrag();
    testLimitsInEditor();
    testSettingsButtons();
    testRenderMatchesHitBoxes();
    testPanelLeaveCancelsDrag();
    testProfilesPanel();
    testProfilesReviewFixes();
    testPills();
    testChipLabelSizes();
    if (failures == 0) std::printf("layout-editor-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
