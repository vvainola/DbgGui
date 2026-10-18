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

#include <map>

PanelWindow::PanelWindow(nlohmann::json const& j) : Window(j) {
    zoom = std::clamp(j.value("zoom", 1.0f), 0.25f, 3.0f);
    pan = {j.value("pan_x", 0.0f), j.value("pan_y", 0.0f)};
    // Each widget saves its group metadata. Reconnect matching IDs to one
    // shared group so edits and selection apply to all its members.
    std::map<uint64_t, std::shared_ptr<PanelGroup>> groups;
    for (auto const& data : j.value("widgets", nlohmann::json::array())) {
        int const type = data.value("type", static_cast<int>(PanelWidgetType::ButtonGrid));
        if (type < 0 || type > static_cast<int>(PanelWidgetType::TextNote)) {
            continue;
        }
        PanelWidget widget;
        widget.id = data.value("id", uint64_t(widgets.size() + 1));
        widget.type = static_cast<PanelWidgetType>(type);
        widget.label = data.value("label", widget.label);
        if (data.contains("group") && data["group"].is_object()) {
            auto const& saved = data["group"];
            uint64_t const id = saved.at("id").get<uint64_t>();
            auto& group = groups[id];
            if (!group) {
                group = std::make_shared<PanelGroup>();
                group->id = id;
                group->title = saved.value("title", group->title);
                group->padding = std::clamp(saved.value("padding", group->padding), 0.0f, 64.0f);
                auto const color = saved.value("color", std::vector<float>{0.15f, 0.25f, 0.3f, 0.7f});
                if (color.size() == 4) {
                    group->color = {color[0], color[1], color[2], color[3]};
                }
            }
            widget.group = group;
        }
        widget.position = {data.value("x", 30.0f), data.value("y", 30.0f)};
        widget.setSize({data.value("width", 320.0f), data.value("height", 180.0f)});
        switch (widget.type) {
            case PanelWidgetType::TextNote: {
                widget.note.text = data.value("text", widget.note.text);
                widget.note.font_size = std::clamp(data.value("font_size", 20.0f), 8.0f, 100.0f);
                auto const color = data.value("text_color", std::vector<float>{1, 1, 1, 1});
                if (color.size() == 4) {
                    widget.note.color = {color[0], color[1], color[2], color[3]};
                }
                break;
            }
            case PanelWidgetType::LedGrid: {
                widget.leds.resize(data.value("rows", 1), data.value("columns", 1));
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < widget.leds.cells.size(); ++index) {
                    auto& cell = widget.leds.cells[index];
                    auto const saved = index < cells.size() ? cells[index] : nlohmann::json::object();
                    cell.label = saved.value("label", cell.label);
                    cell.condition = saved.value("script", cell.condition);
                    auto const color = saved.value("color", std::vector<float>{0.2f, 0.9f, 0.3f, 1});
                    if (color.size() == 4) {
                        cell.color = {color[0], color[1], color[2], color[3]};
                    }
                }
                break;
            }
            case PanelWidgetType::SliderGrid: {
                widget.sliders.resize(data.value("rows", 1), data.value("columns", 1));
                widget.sliders.text_to_value_ratio = std::clamp(data.value("text_to_value_ratio", 0.3f), 0.05f, 0.95f);
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.sliders.cells.size(); ++index) {
                    auto& cell = widget.sliders.cells[index];
                    if (cells[index].contains("scalar_id") && !cells[index]["scalar_id"].is_null()) {
                        cell.scalar_id = cells[index]["scalar_id"].get<uint64_t>();
                    }
                    cell.minimum = cells[index].value("minimum", 0.0);
                    cell.maximum = cells[index].value("maximum", 1.0);
                }
                break;
            }
            case PanelWidgetType::ValueGrid: {
                widget.values.resize(data.value("rows", 1), data.value("columns", 1));
                widget.values.text_to_value_ratio = std::clamp(data.value("text_to_value_ratio", 0.3f), 0.05f, 0.95f);
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.values.cells.size(); ++index) {
                    auto const scalar = cells[index].find("scalar_id");
                    if (scalar != cells[index].end() && !scalar->is_null()) {
                        widget.values.cells[index].scalar_id = scalar->get<uint64_t>();
                    }
                }
                break;
            }
            case PanelWidgetType::CheckboxGrid: {
                widget.checkboxes.resize(data.value("rows", 1), data.value("columns", 1));
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.checkboxes.cells.size(); ++index) {
                    auto& cell = widget.checkboxes.cells[index];
                    cell.label = cells[index].value("label", cell.label);
                    cell.on_enable.script = cells[index].value("on_enable", std::string{});
                    cell.on_disable.script = cells[index].value("on_disable", std::string{});
                }
                break;
            }
            case PanelWidgetType::ButtonGrid: {
                widget.buttons.resize(data.value("rows", 1), data.value("columns", 1));
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.buttons.cells.size(); ++index) {
                    auto& cell = widget.buttons.cells[index];
                    cell.label = cells[index].value("label", cell.label);
                    cell.script = cells[index].value("script", cell.script);
                }
                break;
            }
            case PanelWidgetType::Plot: {
                auto plot_data = data.value("plot", nlohmann::json::object());
                plot_data["name"] = widget.label;
                plot_data["id"] = widget.id;
                widget.plot = std::make_shared<PlotWindow>(plot_data);
                break;
            }
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
        data["id"] = widget.id;
        data["type"] = static_cast<int>(widget.type);
        data["label"] = widget.label;
        data["x"] = widget.position.x;
        data["y"] = widget.position.y;
        data["width"] = widget.size.x;
        data["height"] = widget.size.y;
        data.erase("group");
        if (widget.group) {
            auto const& group = *widget.group;
            data["group"] = {{"id", group.id}, {"title", group.title}, {"padding", group.padding}, {"color", {group.color.x, group.color.y, group.color.z, group.color.w}}};
        }
        switch (widget.type) {
            case PanelWidgetType::TextNote: {
                data["text"] = widget.note.text;
                data["font_size"] = widget.note.font_size;
                data["text_color"] = {widget.note.color.x, widget.note.color.y, widget.note.color.z, widget.note.color.w};
                break;
            }
            case PanelWidgetType::LedGrid: {
                data["rows"] = widget.leds.rows;
                data["columns"] = widget.leds.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.leds.cells) {
                    data["cells"].push_back({{"label", cell.label}, {"script", cell.condition}, {"color", {cell.color.x, cell.color.y, cell.color.z, cell.color.w}}});
                }
                break;
            }
            case PanelWidgetType::SliderGrid: {
                data["text_to_value_ratio"] = widget.sliders.text_to_value_ratio;
                data["rows"] = widget.sliders.rows;
                data["columns"] = widget.sliders.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.sliders.cells) {
                    nlohmann::json saved = {{"minimum", cell.minimum}, {"maximum", cell.maximum}};
                    if (cell.scalar_id) {
                        saved["scalar_id"] = *cell.scalar_id;
                    }
                    data["cells"].push_back(std::move(saved));
                }
                break;
            }
            case PanelWidgetType::ValueGrid: {
                data["text_to_value_ratio"] = widget.values.text_to_value_ratio;
                data["rows"] = widget.values.rows;
                data["columns"] = widget.values.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.values.cells) {
                    auto saved = nlohmann::json::object();
                    if (cell.scalar_id) {
                        saved["scalar_id"] = *cell.scalar_id;
                    }
                    data["cells"].push_back(std::move(saved));
                }
                break;
            }
            case PanelWidgetType::CheckboxGrid: {
                data["rows"] = widget.checkboxes.rows;
                data["columns"] = widget.checkboxes.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.checkboxes.cells) {
                    data["cells"].push_back({{"label", cell.label}, {"on_enable", cell.on_enable.script}, {"on_disable", cell.on_disable.script}});
                }
                break;
            }
            case PanelWidgetType::ButtonGrid: {
                data["rows"] = widget.buttons.rows;
                data["columns"] = widget.buttons.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.buttons.cells) {
                    data["cells"].push_back({{"label", cell.label}, {"script", cell.script}});
                }
                break;
            }
            case PanelWidgetType::Plot:
                break;
        }
        if (widget.plot) {
            widget.plot->updateJson(data["plot"]);
        }
        saved_widgets.push_back(std::move(data));
    }
    j["widgets"] = std::move(saved_widgets);
}
