// MIT License
//
// Copyright (c) 2022 vvainola
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "panel_window.h"
#include "control_grid.h"

#include <map>

PanelWidget::PanelWidget(uint64_t id, PanelWidgetType type, std::string label, ImVec2 position, ImVec2 size)
    : id(id), type(type), label(std::move(label)), position(position), size(size) {
    if (type == PanelWidgetType::ControlGrid) {
        grid = std::make_shared<Grid>();
    } else if (type == PanelWidgetType::Plot) {
        plot = std::make_shared<PlotWindow>(this->label, id);
    }
}

void migratePanelWidgetSettings(nlohmann::json& data) {
    if (!data.is_object()) {
        return;
    }
    // Types 1–5 were the original homogeneous panel grids.
    int const type = data.value("type", static_cast<int>(PanelWidgetType::ControlGrid));
    ControlCellType cell_type;
    switch (type) {
        case 1: cell_type = ControlCellType::Button; break;
        case 2: cell_type = ControlCellType::Value; break;
        case 3: cell_type = ControlCellType::Checkbox; break;
        case 4: cell_type = ControlCellType::Slider; break;
        case 5: cell_type = ControlCellType::Led; break;
        default: return;
    }
    int const rows = std::clamp(data.value("rows", 1), 1, 20);
    int const columns = std::clamp(data.value("columns", 1), 1, 10);
    auto const previous_cells = data.value("cells", nlohmann::json::array());
    auto cells = nlohmann::json::array();
    for (int index = 0; index < rows * columns; ++index) {
        auto const previous = index < static_cast<int>(previous_cells.size()) ? previous_cells[index] : nlohmann::json::object();
        auto cell = previous;
        cell["type"] = cell_type;
        cells.push_back(std::move(cell));
    }
    data["type"] = PanelWidgetType::ControlGrid;
    data["grid"] = {{"rows", rows}, {"columns", columns}, {"cells", std::move(cells)},
                    {"text_to_value_ratio", data.value("text_to_value_ratio", 0.3f)}};
    for (auto key : {"rows", "columns", "cells", "text_to_value_ratio"}) {
        data.erase(key);
    }
}

PanelWidget PanelWidget::fromJson(nlohmann::json const& data) {
    int const type = data.value("type", static_cast<int>(PanelWidgetType::ControlGrid));
    if (type >= 1 && type <= 5) {
        auto migrated = data;
        migratePanelWidgetSettings(migrated);
        return fromJson(migrated);
    }
    PanelWidget widget(data.value("id", uint64_t{0}), data.value("type", PanelWidgetType::ControlGrid),
                       data.value("label", std::string{"Widget"}));
    if (data.contains("group") && data["group"].is_object()) {
        auto const& saved = data["group"];
        uint64_t const id = saved.at("id").get<uint64_t>();
        auto group = std::make_shared<PanelGroup>();
        group->id = id;
        group->title = saved.value("title", group->title);
        group->padding = std::clamp(saved.value("padding", group->padding), 0.0f, 64.0f);
        auto const color = saved.value("color", std::vector<float>{0.15f, 0.25f, 0.3f, 0.7f});
        if (color.size() == 4) {
            group->color = {color[0], color[1], color[2], color[3]};
        }
        widget.group = group;
    }
    widget.position = {data.value("x", 30.0f), data.value("y", 30.0f)};
    widget.setSize({data.value("width", 320.0f), data.value("height", 180.0f)});
    switch (widget.type) {
        case PanelWidgetType::ControlGrid:
            widget.grid = std::make_shared<Grid>(data.value("grid", nlohmann::json::object()));
            break;
        case PanelWidgetType::Plot: {
            auto plot_data = data.value("plot", nlohmann::json::object());
            plot_data["name"] = widget.label;
            plot_data["id"] = widget.id;
            widget.plot = std::make_shared<PlotWindow>(plot_data);
            break;
        }
        case PanelWidgetType::TextNote: {
            widget.note.text = data.value("text", widget.note.text);
            widget.note.font_size = std::clamp(data.value("font_size", 20.0f), 8.0f, 100.0f);
            auto const color = data.value("text_color", std::vector<float>{1, 1, 1, 1});
            if (color.size() == 4) {
                widget.note.color = {color[0], color[1], color[2], color[3]};
            }
            break;
        }
    }
    return widget;
}

void PanelWidget::updateJson(nlohmann::json& data) const {
    migratePanelWidgetSettings(data);
    data["id"] = id;
    data["type"] = static_cast<int>(type);
    data["label"] = label;
    data["x"] = position.x;
    data["y"] = position.y;
    data["width"] = size.x;
    data["height"] = size.y;
    data.erase("group");
    if (group) {
        auto const& group = *this->group;
        data["group"] = {{"id", group.id}, {"title", group.title}, {"padding", group.padding}, {"color", {group.color.x, group.color.y, group.color.z, group.color.w}}};
    }
    switch (type) {
        case PanelWidgetType::ControlGrid:
            grid->updateJson(data["grid"]);
            break;
        case PanelWidgetType::Plot:
            plot->updateJson(data["plot"]);
            break;
        case PanelWidgetType::TextNote: {
            data["text"] = note.text;
            data["font_size"] = note.font_size;
            data["text_color"] = {note.color.x, note.color.y, note.color.z, note.color.w};
            break;
        }
    }
}

PanelWindow::PanelWindow(nlohmann::json const& j) : Window(j) {
    zoom = std::clamp(j.value("zoom", 1.0f), 0.25f, 3.0f);
    pan = {j.value("pan_x", 0.0f), j.value("pan_y", 0.0f)};
    std::map<uint64_t, std::shared_ptr<PanelGroup>> groups;
    for (auto data : j.value("widgets", nlohmann::json::array())) {
        migratePanelWidgetSettings(data);
        int const type = data.value("type", static_cast<int>(PanelWidgetType::ControlGrid));
        if (!isPanelWidgetType(type)) {
            continue;
        }
        data["id"] = data.value("id", uint64_t(widgets.size() + 1));
        auto widget = PanelWidget::fromJson(data);
        if (widget.group) {
            widget.group = groups.try_emplace(widget.group->id, widget.group).first->second;
        }
        widgets.push_back(std::move(widget));
    }
}

void PanelWindow::updateJson(nlohmann::json& j) const {
    Window::updateJson(j);
    j["zoom"] = zoom;
    j["pan_x"] = pan.x;
    j["pan_y"] = pan.y;
    auto const previous_widgets = j.value("widgets", nlohmann::json::array());
    auto saved_widgets = nlohmann::json::array();
    for (auto const& widget : widgets) {
        // Preserve saved plot placements whose scalars are not registered yet.
        auto data = nlohmann::json::object();
        for (auto const& saved : previous_widgets) {
            if (saved.value("id", uint64_t{0}) == widget.id) {
                data = saved;
                break;
            }
        }
        widget.updateJson(data);
        saved_widgets.push_back(std::move(data));
    }
    j["widgets"] = std::move(saved_widgets);
}

void PanelWidget::remapScalarId(uint64_t old_id, uint64_t new_id) {
    if (grid) {
        grid->remapScalarId(old_id, new_id);
    }
}
