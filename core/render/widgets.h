// The widgets, drawn in the fixed panel's look. Each widget reads only the metrics it owns
// (core/metric_registry.h) from the MetricStore.
#pragma once

#include "draw.h"
#include "layout.h"
#include "metric_store.h"
#include "series.h"
#include "thresholds.h"
#include "widget_text.h"

#include <string>
#include <vector>

namespace frame_perf_overlay {

/** Width of one grid column (px): the fixed panel's width. */
constexpr int kColumnWidth = 512;

/** One process-list row (a process, or every process of one name). */
struct ProcessItem {
    std::string name;         ///< what to show: comm, or the exe basename when comm is cut off (processDisplayName)
    int pid = 0;              ///< 0 for a summed row
    int count = 1;            ///< processes in this row
    double cpuCorePct = 0.0;  ///< % of one core
    double rssMiB = 0.0;
};

/** Both process lists, as of the last scan (the rows aren't metrics). */
struct ProcessListsView {
    std::vector<ProcessItem> byCpu;
    std::vector<ProcessItem> byRam;
    bool cpuValid = false;        ///< byCpu has a real interval behind it
    bool hideRulesError = false;  ///< hide_rules.json's last load failed: the cards with Hide system say so
    uint64_t generation = 0;      ///< bumped on every scan (the list widgets redraw only when it moves)
};

/** What every widget gets to draw with. */
struct WidgetContext {
    const MetricStore* store = nullptr;
    const ProcessListsView* processes = nullptr;  ///< nullptr = no lists (drawn as N/A)
    Thresholds thresholds;
    double now = 0.0;        ///< monotonic seconds (graph right edge)
    int graphSeconds = 30;   ///< graph window
    int clockFormat = 24;    ///< 12 or 24; 0 = the dashboard's clock "Off": General hides its Time row
    WidgetLanguage language = WidgetLanguage::En;
};

/** Graph history one widget instance keeps between redraws. */
struct WidgetHistory {
    History a;  ///< main series (fps, vph W, CPU °C, GPU load %)
    History b;  ///< second series (GPU °C)
    History c;  ///< Frame: worst CPU frame time per read (ms), for its graphs with Compact off
    History d;  ///< Frame: worst GPU frame time per read (ms)
};

/** A throughput as text, value and unit apart so they can be styled differently. */
struct RateText {
    std::string value;  ///< "24", "4.6", "123", "--"
    std::string unit;   ///< " kbps" / " Mbps" (leading space)
};

/**
 * Network ↓/↑ text: below 1 Mbps whole kbps ("24 kbps", "0 kbps"), else Mbps with one decimal ("1.0", "4.6"),
 * whole from 100 Mbps. NaN (N/A) gives "--". Anything that rounds to 1000 kbps already reads "1.0 Mbps".
 * @param mbps megabits per second
 */
RateText formatRate(double mbps);

/**
 * Cuts text to fit maxWidth, ending in "..". Removes one whole UTF-8 character per step, so it never splits one.
 * @return s itself if it fits, else the longest cut + ".." that fits ("..": nothing fits)
 */
std::string ellipsize(const Pen& pen, const std::string& s, double size, double maxWidth, bool bold = false);

/**
 * Natural height of a widget's card (px). Network and General follow their visible rows when a context is
 * supplied, and so do the non-compact CPU and Battery cards. A Compact instance is shorter, a non-compact one taller.
 * @param w the instance
 * @return height
 */
int widgetHeight(const WidgetInstance& w, const WidgetContext* ctx = nullptr);

/**
 * The card's title (what the editor labels it with).
 * @param w the instance
 * @param language the language
 * @return the title
 */
const char* widgetTitle(const WidgetInstance& w, WidgetLanguage language);

/**
 * Draws the parts that only change with the layout or the theme: the card, the graph beds, the title.
 * @param pen drawing tools
 * @param x card left
 * @param y card top
 * @param w the instance
 * @param language the title's language
 * @param heightOverride the card's height (0 = widgetHeight)
 */
void drawWidgetStatic(const Pen& pen, double x, double y, const WidgetInstance& w, WidgetLanguage language,
                      int heightOverride = 0);

/**
 * What a widget instance's picture depends on: when each metric it owns was last set, the process scan, and the
 * settings it draws with. Equal signatures = identical pixels, so the grid skips that widget.
 * @param w the instance
 * @param ctx values
 * @return a hash
 */
uint64_t widgetSignature(const WidgetInstance& w, const WidgetContext& ctx);

/**
 * Appends this redraw's samples to the instance's graphs. Call once per redraw, before drawWidgetDynamic.
 * @param w the instance
 * @param ctx values
 * @param history its graphs
 */
void pushWidgetHistory(const WidgetInstance& w, const WidgetContext& ctx, WidgetHistory& history);

/**
 * Draws the values on top of the static layer.
 * @param pen drawing tools
 * @param x card left
 * @param y card top
 * @param w the instance
 * @param ctx values
 * @param history its graphs
 */
void drawWidgetDynamic(const Pen& pen, double x, double y, const WidgetInstance& w, const WidgetContext& ctx,
                       const WidgetHistory& history);

}  // namespace frame_perf_overlay
