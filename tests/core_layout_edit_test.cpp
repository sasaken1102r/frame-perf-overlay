// core/layout_edit tests: palette rules, insert / move / remove, limits, shortest column, drop slots (no platform code).
#include "layout_edit.h"

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

WidgetInstance inst(W type) {
    WidgetInstance w;
    w.type = type;
    return w;
}

Layout make(const std::vector<std::vector<W>>& columns) {
    Layout layout;
    layout.version = 3;
    for (const std::vector<W>& types : columns) {
        LayoutColumn column;
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

/** A layout with every column full of Network, built directly (a real layout holds one of each), so nothing else has room. */
Layout fullLayout(size_t columns, size_t perColumn) {
    Layout layout;
    for (size_t c = 0; c < columns; ++c) {
        LayoutColumn column;
        for (size_t i = 0; i < perColumn; ++i) column.widgets.push_back(inst(W::Network));
        layout.columns.push_back(column);
    }
    return layout;
}

void testPaletteRules() {
    // every widget is single-instance
    for (const W t : {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::Network, W::General, W::None}) {
        CHECK(!isRepeatable(t));
    }
    // Every real widget is offered; None and the reserved numbers are held back.
    for (const W t : {W::Frame, W::Cpu, W::GpuRam, W::Power, W::Battery, W::Temp, W::CpuItems, W::RamItems, W::Network,
                      W::General}) {
        CHECK(isOfferedInPalette(t));
    }
    CHECK(!isOfferedInPalette(W::None));
    CHECK(!isOfferedInPalette(static_cast<W>(10)));

    const Layout layout = make({{W::Frame, W::Network}, {W::Network, W::Cpu}});
    CHECK(countType(layout, W::Network) == 2);  // a hand-written layout may repeat; edits won't add more
    CHECK(countType(layout, W::Frame) == 1);
    CHECK(countType(layout, W::Power) == 0);

    CHECK(widgetAt(layout, {0, 1})->type == W::Network);
    CHECK(widgetAt(layout, {1, 1})->type == W::Cpu);
    CHECK(widgetAt(layout, {2, 0}) == nullptr);
    CHECK(widgetAt(layout, {0, 2}) == nullptr);
    CHECK(widgetAt(layout, {-1, 0}) == nullptr);
    CHECK(widgetAt(layout, {0, -1}) == nullptr);
    CHECK(!WidgetRef().valid());

    // canAdd: offered + not there (unless repeatable) + room
    CHECK(!canAdd(layout, W::Frame));    // already there
    CHECK(!canAdd(layout, W::Network));  // single-instance, already there
    CHECK(canAdd(layout, W::Power));     // free
    CHECK(!canAdd(layout, W::None));
    CHECK(canAdd(Layout(), W::Frame));   // empty layout: a new column can be made
}

void testNewInstance() {
    const Layout layout = make({{W::Frame}});
    const WidgetInstance cpu = newInstance(W::Cpu, layout);
    CHECK(cpu.type == W::Cpu && cpu.compact);  // a palette add starts compact, whatever the type
    for (const W t : {W::Frame, W::Power, W::Battery, W::Temp, W::Network, W::General, W::GpuRam}) {
        CHECK(newInstance(t, layout).type == t && newInstance(t, layout).compact);
    }
}

void testInsert() {
    Layout layout = make({{W::Frame, W::Cpu}, {W::Power}});
    CHECK(insertWidget(layout, inst(W::Battery), 0, 1));
    CHECK(columnIs(layout, 0, {W::Frame, W::Battery, W::Cpu}));
    CHECK(insertWidget(layout, inst(W::Temp), 1, 99));  // index clamps to the end
    CHECK(columnIs(layout, 1, {W::Power, W::Temp}));
    CHECK(insertWidget(layout, inst(W::General), 1, -5));  // ... and to the start
    CHECK(columnIs(layout, 1, {W::General, W::Power, W::Temp}));

    // New column on the right
    CHECK(insertWidget(layout, inst(W::Network), static_cast<int>(layout.columns.size()), 0));
    CHECK(layout.columns.size() == 3 && columnIs(layout, 2, {W::Network}));

    // Failures leave the layout alone
    const Layout before = layout;
    CHECK(!insertWidget(layout, inst(W::Frame), 0, 0));  // single-instance type already there
    CHECK(!insertWidget(layout, inst(W::None), 0, 0));
    CHECK(!insertWidget(layout, inst(W::GpuRam), 4, 0));  // out of range
    CHECK(!insertWidget(layout, inst(W::GpuRam), -1, 0));
    CHECK(layout.sameWidgets(before));
}

void testLimits() {
    // A full column refuses, and the layout is untouched
    Layout layout = fullLayout(2, kMaxWidgetsPerColumn);
    layout.columns[1].widgets.pop_back();  // column 1 has room for exactly one
    const Layout before = layout;
    CHECK(!insertWidget(layout, inst(W::Frame), 0, 0));
    CHECK(layout.sameWidgets(before));
    // ... including an already-empty column elsewhere: a failed insert doesn't prune it
    {
        Layout holey = layout;
        holey.columns.emplace_back();
        const Layout holeyBefore = holey;
        CHECK(!insertWidget(holey, inst(W::Frame), 0, 0));
        CHECK(holey.sameWidgets(holeyBefore) && holey.columns.size() == holeyBefore.columns.size());
    }
    CHECK(insertWidget(layout, inst(W::Frame), 1, 0));
    CHECK(layout.columns[1].widgets.size() == kMaxWidgetsPerColumn);
    CHECK(!insertWidget(layout, inst(W::Cpu), 1, 0));

    // Move into a full column: refused
    CHECK(!moveWidget(layout, {1, 0}, 0, 0));
    CHECK(layout.columns[0].widgets.size() == kMaxWidgetsPerColumn);

    // kMaxColumns: no new column past the limit
    Layout wide = fullLayout(kMaxColumns, 1);
    CHECK(wide.columns.size() == kMaxColumns);
    CHECK(!insertWidget(wide, inst(W::Frame), static_cast<int>(kMaxColumns), 0));
    CHECK(wide.columns.size() == kMaxColumns);
    CHECK(canAdd(wide, W::Frame));                 // the columns have room
    CHECK(insertWidget(wide, inst(W::Frame), 0, 0));
    // Two widgets in column 0, so moving one out into a "new column" is refused at 8 columns
    CHECK(!moveWidget(wide, {0, 0}, static_cast<int>(kMaxColumns), 0));

    // canAdd: every column full and 8 of them = nothing can be added
    const Layout packed = fullLayout(kMaxColumns, kMaxWidgetsPerColumn);
    CHECK(!canAdd(packed, W::Frame));
    CHECK(!canAdd(packed, W::Network));
    // Eight columns, only one with room: still addable
    Layout almost = packed;
    almost.columns[5].widgets.pop_back();
    CHECK(canAdd(almost, W::Frame));
    // Full columns but fewer than 8: a new column can be made
    CHECK(canAdd(fullLayout(2, kMaxWidgetsPerColumn), W::Frame));
}

void testMove() {
    Layout layout = make({{W::Frame, W::Cpu, W::GpuRam, W::General}, {W::Power, W::Battery}});

    // Same column: card 0 below card 2 → slot 3 → ends at index 2
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 0}, 0, 3));
        CHECK(columnIs(l, 0, {W::Cpu, W::GpuRam, W::Frame, W::General}));
    }
    // card 2 to the top
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 2}, 0, 0));
        CHECK(columnIs(l, 0, {W::GpuRam, W::Frame, W::Cpu, W::General}));
    }
    // To the very end
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 0}, 0, 4));
        CHECK(columnIs(l, 0, {W::Cpu, W::GpuRam, W::General, W::Frame}));
    }
    // Own slot and the slot just below itself: nothing changes
    for (int index = 0; index < 4; ++index) {
        Layout l = layout;
        CHECK(!moveWidget(l, {0, index}, 0, index));
        CHECK(!moveWidget(l, {0, index}, 0, index + 1));
        CHECK(l.sameWidgets(layout));
    }
    // Out of range
    {
        Layout l = layout;
        CHECK(!moveWidget(l, {0, 9}, 0, 0));
        CHECK(!moveWidget(l, {5, 0}, 0, 0));
        CHECK(!moveWidget(l, {0, 0}, 3, 0));
        CHECK(!moveWidget(l, {0, 0}, -1, 0));
        CHECK(l.sameWidgets(layout));
    }
    // Between columns, first / middle / last slot
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 1}, 1, 0));
        CHECK(columnIs(l, 0, {W::Frame, W::GpuRam, W::General}));
        CHECK(columnIs(l, 1, {W::Cpu, W::Power, W::Battery}));
    }
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 0}, 1, 1));
        CHECK(columnIs(l, 1, {W::Power, W::Frame, W::Battery}));
    }
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 3}, 1, 2));  // the last slot
        CHECK(columnIs(l, 1, {W::Power, W::Battery, W::General}));
        CHECK(columnIs(l, 0, {W::Frame, W::Cpu, W::GpuRam}));
    }
    // Into a new column: the widget ends up alone on the right
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 0}, 2, 0));
        CHECK(l.columns.size() == 3 && columnIs(l, 2, {W::Frame}));
        CHECK(columnIs(l, 0, {W::Cpu, W::GpuRam, W::General}));
    }
    // A column's only widget moved out: the column goes away
    {
        Layout l = make({{W::Frame}, {W::Power, W::Battery}});
        CHECK(moveWidget(l, {0, 0}, 1, 1));
        CHECK(l.columns.size() == 1 && columnIs(l, 0, {W::Power, W::Frame, W::Battery}));
    }
    // ... also when the target column sits before the emptied one
    {
        Layout l = make({{W::Power, W::Battery}, {W::Frame}});
        CHECK(moveWidget(l, {1, 0}, 0, 0));
        CHECK(l.columns.size() == 1 && columnIs(l, 0, {W::Frame, W::Power, W::Battery}));
    }
    // Alone in the LAST column, "new column": it'd be the same column again, so no change
    {
        Layout l = make({{W::Power, W::Battery}, {W::Frame}});
        CHECK(!moveWidget(l, {1, 0}, 2, 0));
        CHECK(l.sameWidgets(make({{W::Power, W::Battery}, {W::Frame}})));
    }
    // Alone in an earlier column: it moves to a new column on the right
    {
        Layout l = make({{W::Frame}, {W::Power, W::Battery}});
        CHECK(moveWidget(l, {0, 0}, 2, 0));
        CHECK(l.columns.size() == 2 && columnIs(l, 0, {W::Power, W::Battery}) && columnIs(l, 1, {W::Frame}));
    }
    // move never touches the version
    {
        Layout l = layout;
        CHECK(moveWidget(l, {0, 0}, 1, 0));
        CHECK(l.version == 3);
    }
}

void testRemoveAndPrune() {
    Layout layout = make({{W::Frame, W::Cpu}, {W::Power}});
    CHECK(removeWidget(layout, {0, 0}));
    CHECK(columnIs(layout, 0, {W::Cpu}));
    CHECK(!removeWidget(layout, {0, 5}));
    CHECK(!removeWidget(layout, {4, 0}));
    CHECK(!removeWidget(layout, {-1, -1}));
    CHECK(removeWidget(layout, {1, 0}));  // last one in its column: column gone
    CHECK(layout.columns.size() == 1);
    CHECK(removeWidget(layout, {0, 0}));
    CHECK(layout.columns.empty());
    CHECK(layout.version == 3);

    Layout holes;
    holes.columns.resize(4);
    holes.columns[1].widgets.push_back(inst(W::Frame));
    CHECK(pruneEmptyColumns(holes));
    CHECK(holes.columns.size() == 1 && columnIs(holes, 0, {W::Frame}));
    CHECK(!pruneEmptyColumns(holes));
}

void testShortestColumn() {
    const auto height = [](const WidgetInstance& w) { return w.type == W::Frame ? 100 : 10; };
    // Least total height wins
    CHECK(shortestColumn(make({{W::Frame}, {W::Cpu, W::Power}, {W::Battery}}), height) == 2);
    // Ties: leftmost
    CHECK(shortestColumn(make({{W::Cpu}, {W::Power}}), height) == 0);
    // Empty layout: a new column
    CHECK(shortestColumn(Layout(), height) == 0);
    // A full column is skipped even if it's the shortest
    {
        Layout l = make({{W::Frame}, {}});
        l.columns[1].widgets.assign(kMaxWidgetsPerColumn, inst(W::Network));  // 160 > 100
        CHECK(shortestColumn(l, height) == 0);
        l.columns[0].widgets.assign(kMaxWidgetsPerColumn, inst(W::Network));
        CHECK(shortestColumn(l, height) == 2);  // all full: a new column
    }
    // All full and no room for a new column: -1
    CHECK(shortestColumn(fullLayout(kMaxColumns, kMaxWidgetsPerColumn), height) == -1);
}

void testDropSlot() {
    const std::vector<double> centers = {50, 120, 190};
    CHECK(dropSlot(centers, 0) == 0);
    CHECK(dropSlot(centers, 50) == 0);   // on a centre: still before it
    CHECK(dropSlot(centers, 51) == 1);
    CHECK(dropSlot(centers, 119) == 1);
    CHECK(dropSlot(centers, 121) == 2);
    CHECK(dropSlot(centers, 190) == 2);
    CHECK(dropSlot(centers, 1000) == 3);
    CHECK(dropSlot({}, 123) == 0);
}

}  // namespace

int main() {
    testPaletteRules();
    testNewInstance();
    testInsert();
    testLimits();
    testMove();
    testRemoveAndPrune();
    testShortestColumn();
    testDropSlot();
    if (failures == 0) std::printf("core-layout-edit-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
