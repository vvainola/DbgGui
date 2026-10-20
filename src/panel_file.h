#pragma once

#include "panel_window.h"
#include <stdexcept>

namespace panel_file {

inline nlohmann::json encode(nlohmann::json panel) {
    return {{"format", "DbgGui panel"}, {"version", 1}, {"panel", std::move(panel)}};
}

// IDs identify live windows, not reusable panel definitions. Give every imported
// widget a fresh local ID, while preserving scalar IDs used for signal bindings.
inline nlohmann::json decode(nlohmann::json const& file, uint64_t panel_id) {
    if (!file.is_object() || file.value("format", std::string{}) != "DbgGui panel"
        || file.value("version", 0) != 1) {
        throw std::runtime_error("Unsupported panel file format");
    }
    auto panel = file.at("panel");
    if (!panel.is_object() || !panel.contains("widgets") || !panel["widgets"].is_array()) {
        throw std::runtime_error("Panel file must contain a panel and widget array");
    }
    panel["id"] = panel_id;
    panel["initial_focus"] = true;
    uint64_t id = 0;
    for (auto& widget : panel["widgets"]) {
        if (!widget.is_object()) {
            throw std::runtime_error("Invalid panel widget");
        }
        int const type = widget.at("type").get<int>();
        if (type < 0 || type > static_cast<int>(PanelWidgetType::TextNote)) {
            throw std::runtime_error("Unsupported panel widget type");
        }
        widget["id"] = ++id;
    }
    // Validate before the caller changes live panels or saved settings.
    PanelWindow validated(panel);
    return panel;
}

} // namespace panel_file
