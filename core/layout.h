// The widget grid layout: columns, each an ordered list of widget instances, plus a version number.
// It lives in config.json's "layout" key.
// A widget that isn't in the layout is off: its sources lose their reference (Scheduler::setEnabledWidgets).
#pragma once

#include "json.h"
#include "metric_registry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace frame_perf_overlay {

/** Most columns a layout may hold (8 × 512 px is already twice the 2048 cap). */
constexpr size_t kMaxColumns = 8;
/** Most widgets one column may hold. */
constexpr size_t kMaxWidgetsPerColumn = 16;

/** Most process rows a CPU / RAM items widget may show. */
constexpr uint32_t kMaxListItems = 50;
/** The rows a new CPU / RAM items widget shows. */
constexpr uint32_t kDefaultListItems = 15;

/** One widget in the grid, with its own settings. Settings a type doesn't use keep their defaults. */
struct WidgetInstance {
    WidgetType type = WidgetType::None;
    /** The short card (hasCompactView types only). Off adds the extra rows. A new instance starts compact; a
     *  layout file without the key means compactWhenMissing(). */
    bool compact = true;
    uint32_t maxItems = kDefaultListItems;    ///< CPU / RAM items: rows shown
    bool sumSameName = false;                 ///< CPU / RAM items: one row per process name
    bool hideSystem = false;                  ///< CPU / RAM items: processes matching hide_rules.json left out

    bool operator==(const WidgetInstance& o) const {
        return type == o.type && compact == o.compact && maxItems == o.maxItems && sumSameName == o.sumSameName && hideSystem == o.hideSystem;
    }
    bool operator!=(const WidgetInstance& o) const { return !(*this == o); }
};

/** @return true if the type has a Compact switch (every widget with extra rows: all but GPU & RAM and the lists) */
bool hasCompactView(WidgetType type);

/**
 * @return what a layout file without "compact" means: off for Battery and Temp (their full card, as before the
 *         switch spread to the others), on for the rest (their card as it was before the extra rows)
 */
bool compactWhenMissing(WidgetType type);

/** @return true for CPU items and RAM items (the process lists) */
bool isProcessList(WidgetType type);

/** One column, top to bottom. */
struct LayoutColumn {
    std::vector<WidgetInstance> widgets;

    bool operator==(const LayoutColumn& o) const { return widgets == o.widgets; }
    bool operator!=(const LayoutColumn& o) const { return !(*this == o); }
};

/** The whole grid. */
struct Layout {
    /** Bumped on every applied edit; the editor's compare-and-set (LayoutAuthority) checks it. */
    uint32_t version = 1;
    std::vector<LayoutColumn> columns;

    /** Same widgets in the same places (the version isn't compared). */
    bool sameWidgets(const Layout& o) const { return columns == o.columns; }
};

/**
 * The first-run layout, also used for a config.json from before the grid (no "layout" key): two columns that
 * show what the fixed panel showed (Frame, Power, compact Temp, CPU | GPU & RAM, Network, compact Battery,
 * General), so an updated install keeps its readings in a wide, low panel like the fixed one. It is twice as wide
 * as the fixed panel at the same width_m (width_m is one column), so the text keeps its size.
 * @return the layout, version 1
 */
Layout defaultLayout();

/**
 * @param layout a layout
 * @return one entry per widget instance (a type may repeat), for Scheduler::setEnabledWidgets
 */
std::vector<WidgetType> enabledWidgets(const Layout& layout);

/**
 * @param layout a layout
 * @return per enabledWidgets() entry, true if that card is compact (hasCompactView and compact on)
 */
std::vector<bool> enabledCompact(const Layout& layout);

/**
 * @param type a widget type
 * @return its key in the layout JSON ("frame", "gpu_ram", ...), or nullptr for None / unknown
 */
const char* widgetKey(WidgetType type);

/**
 * @param key a layout JSON key
 * @param out the type
 * @return true if the key is known
 */
bool parseWidgetKey(const std::string& key, WidgetType& out);

/**
 * Writes the layout as a JSON object.
 * @param layout the layout
 * @param indent spaces before each line after the first (0 = compact, for the link)
 * @return `{ "version": N, "columns": [ [ {...}, ... ], ... ] }`
 */
std::string layoutToJson(const Layout& layout, int indent = 0);

/**
 * Reads a layout from the JSON object layoutToJson writes. Unknown widget types and keys are skipped with a
 * warning (a newer version may know more types); settings out of range are clamped with a warning. A missing
 * "compact" is compactWhenMissing(). A second copy of a widget that can't repeat is skipped too
 * (isRepeatable). A column left with no widgets is dropped. A layout that listed widgets but kept none
 * gets defaultLayout(); one that listed none (every card removed in the editor) stays empty.
 * @param value the object
 * @param out the layout (unchanged on failure)
 * @param warnings things noticed but tolerated
 * @param error why it failed
 * @return false if the value isn't a layout at all (not an object, "columns" missing or not an array)
 */
bool layoutFromJson(const JsonValue& value, Layout& out, std::vector<std::string>& warnings, std::string& error);

/**
 * parseJson + layoutFromJson.
 * @return false if the text isn't valid JSON or not a layout
 */
bool parseLayoutText(const std::string& text, Layout& out, std::vector<std::string>& warnings, std::string& error);

}  // namespace frame_perf_overlay
