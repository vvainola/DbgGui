#pragma once

#include "panel_window.h"

namespace panel_clipboard {

inline nlohmann::json copy(nlohmann::json const& panel, std::vector<uint64_t> const& selected) {
    auto widgets = nlohmann::json::array();
    for (auto const& widget : panel.at("widgets")) {
        if (contains(selected, widget.at("id").get<uint64_t>())) {
            widgets.push_back(widget);
        }
    }
    if (!widgets.empty()) {
        float x = widgets[0].value("x", 0.0f);
        float y = widgets[0].value("y", 0.0f);
        for (auto const& widget : widgets) {
            x = std::min(x, widget.value("x", 0.0f));
            y = std::min(y, widget.value("y", 0.0f));
        }
        for (auto& widget : widgets) {
            widget["x"] = widget.value("x", 0.0f) - x;
            widget["y"] = widget.value("y", 0.0f) - y;
        }
    }
    return widgets;
}

inline nlohmann::json place(nlohmann::json widgets, PanelWindow const& destination, ImVec2 position) {
    uint64_t id = 0;
    for (auto const& widget : destination.widgets) {
        id = std::max(id, widget.id);
    }
    for (auto& widget : widgets) {
        widget["id"] = ++id;
        widget["x"] = widget.value("x", 0.0f) + snapToPanelGrid(position.x);
        widget["y"] = widget.value("y", 0.0f) + snapToPanelGrid(position.y);
    }
    return widgets;
}

} // namespace panel_clipboard
