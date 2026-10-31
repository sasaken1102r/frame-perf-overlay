#include "layout.h"

#include "layout_edit.h"  // isRepeatable

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace frame_perf_overlay {

namespace {

struct KeyName {
    WidgetType type;
    const char* key;
};

// Stable: these go into saved layouts
const KeyName kKeys[] = {
    {WidgetType::Frame, "frame"},
    {WidgetType::Cpu, "cpu"},
    {WidgetType::GpuRam, "gpu_ram"},
    {WidgetType::Power, "power"},
    {WidgetType::Battery, "battery"},
    {WidgetType::Temp, "temp"},
    {WidgetType::CpuItems, "cpu_items"},
    {WidgetType::RamItems, "ram_items"},
    {WidgetType::Network, "network"},
    {WidgetType::General, "general"},
};

WidgetInstance instance(WidgetType type, bool compact = true) {
    WidgetInstance w;
    w.type = type;
    w.compact = compact;
    return w;
}

}  // namespace

bool compactWhenMissing(WidgetType type) {
    return type != WidgetType::Battery && type != WidgetType::Temp;
}

bool hasCompactView(WidgetType type) {
    switch (type) {
        case WidgetType::Frame:
        case WidgetType::Cpu:
        case WidgetType::Power:
        case WidgetType::Battery:
        case WidgetType::Temp:
        case WidgetType::Network:
        case WidgetType::General: return true;
        default: return false;
    }
}

bool isProcessList(WidgetType type) {
    return type == WidgetType::CpuItems || type == WidgetType::RamItems;
}

Layout defaultLayout() {
    using W = WidgetType;
    Layout layout;
    layout.version = 1;
    layout.columns.resize(2);
    // The fixed panel's readings in two columns of about the same height, so the panel is wide and low like the
    // fixed one instead of one tall column: its graph rows (frame, power, temperatures) and CPU on the left, its text
    // rows (GPU clock and memory, Wi-Fi, battery, controllers) and the clock (General's Time row) on the right
    layout.columns[0].widgets = {instance(W::Frame), instance(W::Power), instance(W::Temp, true), instance(W::Cpu)};
    layout.columns[1].widgets = {instance(W::GpuRam), instance(W::Network), instance(W::Battery, true),
                                 instance(W::General)};
    return layout;
}

std::vector<WidgetType> enabledWidgets(const Layout& layout) {
    std::vector<WidgetType> out;
    for (const LayoutColumn& column : layout.columns) {
        for (const WidgetInstance& w : column.widgets) out.push_back(w.type);
    }
    return out;
}

std::vector<bool> enabledCompact(const Layout& layout) {
    std::vector<bool> out;
    for (const LayoutColumn& column : layout.columns) {
        for (const WidgetInstance& w : column.widgets) out.push_back(hasCompactView(w.type) && w.compact);
    }
    return out;
}

const char* widgetKey(WidgetType type) {
    for (const KeyName& k : kKeys) {
        if (k.type == type) return k.key;
    }
    return nullptr;
}

bool parseWidgetKey(const std::string& key, WidgetType& out) {
    for (const KeyName& k : kKeys) {
        if (key == k.key) {
            out = k.type;
            return true;
        }
    }
    return false;
}

std::string layoutToJson(const Layout& layout, int indent) {
    const bool pretty = indent > 0;
    const std::string pad(static_cast<size_t>(std::max(0, indent)), ' ');
    const std::string nl = pretty ? "\n" : "";
    const std::string in1 = pretty ? pad + "  " : "";
    const std::string in2 = pretty ? pad + "    " : "";
    const std::string sp = pretty ? " " : "";
    std::string out = "{" + nl + in1 + "\"version\":" + sp + std::to_string(layout.version) + "," + nl + in1 +
                      "\"columns\":" + sp + "[";
    for (size_t c = 0; c < layout.columns.size(); ++c) {
        out += (c ? "," : "") + nl + in2 + "[";
        const std::vector<WidgetInstance>& widgets = layout.columns[c].widgets;
        for (size_t i = 0; i < widgets.size(); ++i) {
            const WidgetInstance& w = widgets[i];
            const char* key = widgetKey(w.type);
            if (key == nullptr) continue;
            out += std::string(i ? "," : "") + sp + "{" + sp + "\"type\":" + sp + "\"" + key + "\"";
            // only when it differs from what a missing key means, so a layout without it reads the same as before
            if (hasCompactView(w.type) && w.compact != compactWhenMissing(w.type)) {
                out += "," + sp + "\"compact\":" + sp + (w.compact ? "true" : "false");
            }
            if (isProcessList(w.type)) {
                out += "," + sp + "\"max_items\":" + sp + std::to_string(w.maxItems);
                if (w.sumSameName) out += "," + sp + "\"sum_same_name\":" + sp + "true";
                if (w.hideSystem) out += "," + sp + "\"hide_system\":" + sp + "true";
            }
            out += sp + "}";
        }
        out += (widgets.empty() ? "" : sp) + "]";
    }
    out += nl + in1 + "]" + nl + pad + "}";
    return out;
}

bool layoutFromJson(const JsonValue& value, Layout& out, std::vector<std::string>& warnings, std::string& error) {
    if (!value.isObject()) {
        error = "layout must be { \"version\": N, \"columns\": [...] }";
        return false;
    }
    const JsonValue* columns = value.get("columns");
    if (columns == nullptr || columns->type != JsonValue::Type::Array) {
        error = "layout.columns must be an array of columns";
        return false;
    }
    Layout layout;
    layout.version = 1;
    if (const JsonValue* version = value.get("version")) {
        if (version->isNumber() && version->number >= 1 && version->number <= 4294967295.0) {
            layout.version = static_cast<uint32_t>(version->number);
        } else {
            warnings.push_back("layout.version must be a whole number >= 1, using 1");
        }
    }
    for (const auto& member : value.members) {
        if (member.first != "version" && member.first != "columns") {
            warnings.push_back("unknown key: layout." + member.first);
        }
    }
    std::vector<WidgetType> seen;  // a widget that can't repeat is kept the first time only, like the editor
    bool listed = false;           // the file named at least one widget (an emptied layout names none)
    for (size_t c = 0; c < columns->items.size(); ++c) {
        const JsonValue& column = columns->items[c];
        const std::string where = "layout.columns[" + std::to_string(c) + "]";
        if (layout.columns.size() >= kMaxColumns) {
            warnings.push_back(where + ": more than " + std::to_string(kMaxColumns) + " columns, the rest are skipped");
            break;
        }
        if (column.type != JsonValue::Type::Array) {
            warnings.push_back(where + " must be an array of widgets, skipped");
            continue;
        }
        LayoutColumn out1;
        if (!column.items.empty()) listed = true;
        for (size_t i = 0; i < column.items.size(); ++i) {
            const JsonValue& item = column.items[i];
            const std::string at = where + "[" + std::to_string(i) + "]";
            if (out1.widgets.size() >= kMaxWidgetsPerColumn) {
                warnings.push_back(at + ": more than " + std::to_string(kMaxWidgetsPerColumn) +
                                   " widgets in one column, the rest are skipped");
                break;
            }
            const JsonValue* type = item.isObject() ? item.get("type") : nullptr;
            WidgetInstance w;
            if (type == nullptr || !type->isString() || !parseWidgetKey(type->text, w.type)) {
                warnings.push_back(at + ": unknown widget" +
                                   (type != nullptr && type->isString() ? " \"" + type->text + "\"" : "") + ", skipped");
                continue;
            }
            if (!isRepeatable(w.type) && std::find(seen.begin(), seen.end(), w.type) != seen.end()) {
                warnings.push_back(at + ": a second \"" + type->text + "\", skipped");
                continue;
            }
            seen.push_back(w.type);
            for (const auto& member : item.members) {
                const std::string& k = member.first;
                const bool known = k == "type" || (k == "compact" && hasCompactView(w.type)) ||
                                   ((k == "max_items" || k == "sum_same_name" || k == "hide_system") &&
                                    isProcessList(w.type));
                if (!known) warnings.push_back(at + ": unknown key " + k + " for " + type->text);
            }
            const auto readBool = [&](const char* key, bool& field) {
                if (const JsonValue* v = item.get(key)) {
                    if (v->isBool()) field = v->boolean;
                    else warnings.push_back(at + "." + key + " must be true or false");
                }
            };
            w.compact = compactWhenMissing(w.type);
            if (hasCompactView(w.type)) readBool("compact", w.compact);
            if (isProcessList(w.type)) {
                if (const JsonValue* max = item.get("max_items")) {
                    if (max->isNumber() && std::isfinite(max->number)) {
                        const double clamped = std::clamp(std::round(max->number), 1.0, double(kMaxListItems));
                        if (clamped != max->number) {
                            warnings.push_back(at + ".max_items clamped to " + std::to_string(int(clamped)));
                        }
                        w.maxItems = static_cast<uint32_t>(clamped);
                    } else {
                        warnings.push_back(at + ".max_items must be a number");
                    }
                }
                readBool("sum_same_name", w.sumSameName);
                readBool("hide_system", w.hideSystem);
            }
            out1.widgets.push_back(w);
        }
        if (out1.widgets.empty()) {  // it would be a blank column on the panel
            warnings.push_back(where + " has no widgets, dropped");
            continue;
        }
        layout.columns.push_back(std::move(out1));
    }
    // Every listed widget was unusable: a blank panel would look broken. Removing every card in the editor saves
    // no widgets at all, and that stays empty
    if (layout.columns.empty() && listed) {
        warnings.push_back("layout has no usable widgets: using the default layout");
        const uint32_t version = layout.version;
        layout = defaultLayout();
        layout.version = version;
    }
    out = std::move(layout);
    return true;
}

bool parseLayoutText(const std::string& text, Layout& out, std::vector<std::string>& warnings, std::string& error) {
    JsonValue root;
    if (!parseJson(text, root, error)) return false;
    return layoutFromJson(root, out, warnings, error);
}

}  // namespace frame_perf_overlay
