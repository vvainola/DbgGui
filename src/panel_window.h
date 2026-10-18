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

#pragma once

#include "data_structures.h"
#include "lua_script.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

inline constexpr float PANEL_GRID_SPACING = 32;
inline float snapToPanelGrid(float value) {
    return std::round(value / PANEL_GRID_SPACING) * PANEL_GRID_SPACING;
}

enum class PanelWidgetType {
    Plot,
    ButtonGrid,
    ValueGrid,
    CheckboxGrid,
    Slider,
    Led
};

struct PanelLedResult {
    bool lit = false;
    std::string error;
};

struct PanelLedState {
    PanelLedState(std::string condition, LuaScriptHost host)
        : runner(std::move(condition), std::move(host), "LED condition") {
    }
    // The runner is used only by the sampling thread. GUI reads published results.
    LuaScriptRunner runner;
    std::atomic<bool> requested = false;
    std::atomic<std::shared_ptr<PanelLedResult const>> result{std::make_shared<PanelLedResult>()};
};

// One invocation of a panel action. Execution belongs to the sampling thread;
// the GUI only reads the published status and error.
struct PanelActionState {
    PanelActionState(std::string source, LuaScriptHost host)
        : runner(std::move(source), std::move(host), "Panel action") {
    }
    void process(double timestamp) {
        if (!running.load()) {
            return;
        }
        if (stop_requested.load()) {
            runner.stop();
            running.store(false);
            return;
        }
        std::expected<void, std::string> result;
        if (!started) {
            started = true;
            result = runner.start(timestamp, 1);
        }
        if (result) {
            result = runner.process(timestamp);
        }
        if (!result) {
            error = result.error();
        }
        running.store(runner.running());
    }
    LuaScriptRunner runner;
    bool started = false;
    std::atomic<bool> running = true;
    std::atomic<bool> stop_requested = false;
    // Written before running becomes false; GUI reads it only after completion.
    std::string error;
};

struct PanelActionScript {
    std::string script;
    std::string draft;
    std::shared_ptr<PanelActionState> action;
    std::string syntax_error;

    bool hasError() const {
        return !syntax_error.empty() || (action && !action->running.load() && !action->error.empty());
    }

    void applyScript() {
        if (auto result = validateLuaSyntax(draft, "Panel action"); !result) {
            syntax_error = result.error();
            return;
        }
        syntax_error.clear();
        script = draft;
        if (action && !action->running.load()) {
            action.reset();
        }
    }
};

struct PanelButtonCell : PanelActionScript {
    bool editor_open = false;
    std::string label = "Button";
};

struct PanelCheckbox {
    bool editor_open = false;
    std::string label = "Checkbox";
    bool checked = false;
    PanelActionScript on_enable;
    PanelActionScript on_disable;

    std::shared_ptr<PanelActionState> setChecked(bool value, LuaScriptHost host) {
        if (checked == value) {
            return nullptr;
        }
        checked = value;
        // A delayed action from the previous state must not undo this transition.
        for (auto* script : {&on_enable, &on_disable}) {
            if (script->action && script->action->running.load()) {
                script->action->stop_requested.store(true);
            }
        }
        auto& script = checked ? on_enable : on_disable;
        script.action = std::make_shared<PanelActionState>(script.script, std::move(host));
        return script.action;
    }
};

template <typename Cell>
struct PanelControlGrid {
    int rows = 1;
    int columns = 1;
    std::vector<Cell> cells{1};

    void resize(int new_rows, int new_columns) {
        new_rows = std::clamp(new_rows, 1, 20);
        new_columns = std::clamp(new_columns, 1, 10);
        std::vector<Cell> resized(new_rows * new_columns);
        for (int row = 0; row < std::min(rows, new_rows); ++row) {
            for (int col = 0; col < std::min(columns, new_columns); ++col) {
                resized[row * new_columns + col] = std::move(cells[row * columns + col]);
            }
        }
        rows = new_rows;
        columns = new_columns;
        cells = std::move(resized);
    }
};

using PanelButtonGrid = PanelControlGrid<PanelButtonCell>;
using PanelCheckboxGrid = PanelControlGrid<PanelCheckbox>;

struct PanelValueCell {
    // Keep the ID even when the scalar is unavailable so it can resolve later.
    std::optional<uint64_t> scalar_id;
};

struct PanelValueGrid : PanelControlGrid<PanelValueCell> {
    float text_to_value_ratio = 0.3f;
};

struct PanelWidget {
    uint64_t id = 0;
    PanelWidgetType type = PanelWidgetType::ButtonGrid;
    std::string label = "Widget";
    ImVec2 position = {30, 30};
    ImVec2 size = {320, 180};
    struct {
        bool editor_open = false;
        std::string condition = "return false";
        std::string draft;
        ImVec4 color = {0.2f, 0.9f, 0.3f, 1};
        std::shared_ptr<PanelLedState> state;
    } led;
    // Stable addresses keep plot drag/drop payloads valid when adding widgets.
    std::shared_ptr<PlotWindow> plot;
    PanelButtonGrid buttons;
    PanelCheckboxGrid checkboxes;
    PanelValueGrid values;
    void configureLed(LuaScriptHost host) {
        led.state = std::make_shared<PanelLedState>(led.condition, std::move(host));
    }
    ImVec2 minimumSize() const {
        switch (type) {
            case PanelWidgetType::Led: return {20, 20};
            case PanelWidgetType::Slider: return {100, 20};
            default: return {160, 100};
        }
    }
    void setSize(ImVec2 requested) {
        ImVec2 const minimum = minimumSize();
        size = {std::max(minimum.x, requested.x), std::max(minimum.y, requested.y)};
        if (type == PanelWidgetType::Led) {
            size.x = size.y = std::min(size.x, size.y);
        }
    }
    void resizeOnGrid(ImVec2 original, ImVec2 delta) {
        if (type == PanelWidgetType::Led) {
            delta.x = delta.y = std::abs(delta.x) > std::abs(delta.y) ? delta.x : delta.y;
        }
        ImVec2 const minimum = minimumSize();
        setSize({std::max(std::ceil(minimum.x / PANEL_GRID_SPACING) * PANEL_GRID_SPACING, snapToPanelGrid(original.x + delta.x)),
                 std::max(std::ceil(minimum.y / PANEL_GRID_SPACING) * PANEL_GRID_SPACING, snapToPanelGrid(original.y + delta.y))});
    }
};

struct PanelWindow : Window {
    using Window::Window;
    explicit PanelWindow(nlohmann::json const& j) : Window(j) {
        zoom = std::clamp(j.value("zoom", 1.0f), 0.25f, 3.0f);
        pan = {j.value("pan_x", 0.0f), j.value("pan_y", 0.0f)};
        for (auto const& data : j.value("widgets", nlohmann::json::array())) {
            int const type = data.value("type", static_cast<int>(PanelWidgetType::ButtonGrid));
            if (type < 0 || type > static_cast<int>(PanelWidgetType::Led)) {
                continue;
            }
            PanelWidget widget;
            widget.id = data.value("id", uint64_t(widgets.size() + 1));
            widget.type = static_cast<PanelWidgetType>(type);
            widget.label = data.value("label", widget.label);
            widget.position = {data.value("x", 30.0f), data.value("y", 30.0f)};
            widget.setSize({data.value("width", 320.0f), data.value("height", 180.0f)});
            if (widget.type == PanelWidgetType::Led) {
                auto const color = data.value("led_color", std::vector<float>{0.2f, 0.9f, 0.3f, 1});
                if (color.size() == 4) {
                    widget.led.color = {color[0], color[1], color[2], color[3]};
                }
                auto const cells = data.value("cells", nlohmann::json::array());
                if (!cells.empty()) {
                    widget.led.condition = cells[0].value("script", widget.led.condition);
                }
            }
            if (widget.type == PanelWidgetType::ValueGrid) {
                widget.values.resize(data.value("rows", 1), data.value("columns", 1));
                widget.values.text_to_value_ratio = std::clamp(data.value("text_to_value_ratio", 0.3f), 0.05f, 0.95f);
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.values.cells.size(); ++index) {
                    auto const scalar = cells[index].find("scalar_id");
                    if (scalar != cells[index].end() && !scalar->is_null()) {
                        widget.values.cells[index].scalar_id = scalar->get<uint64_t>();
                    }
                }
            }
            if (widget.type == PanelWidgetType::CheckboxGrid) {
                widget.checkboxes.resize(data.value("rows", 1), data.value("columns", 1));
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.checkboxes.cells.size(); ++index) {
                    auto& cell = widget.checkboxes.cells[index];
                    cell.label = cells[index].value("label", cell.label);
                    cell.on_enable.script = cells[index].value("on_enable", std::string{});
                    cell.on_disable.script = cells[index].value("on_disable", std::string{});
                }
            }
            if (widget.type == PanelWidgetType::ButtonGrid) {
                widget.buttons.resize(data.value("rows", 1), data.value("columns", 1));
                auto const cells = data.value("cells", nlohmann::json::array());
                for (size_t index = 0; index < cells.size() && index < widget.buttons.cells.size(); ++index) {
                    auto& cell = widget.buttons.cells[index];
                    cell.label = cells[index].value("label", cell.label);
                    cell.script = cells[index].value("script", cell.script);
                }
            }
            if (widget.type == PanelWidgetType::Plot) {
                auto plot_data = data.value("plot", nlohmann::json::object());
                plot_data["name"] = widget.label;
                plot_data["id"] = widget.id;
                widget.plot = std::make_shared<PlotWindow>(plot_data);
            }
            widgets.push_back(std::move(widget));
        }
    }
    void updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["zoom"] = zoom;
        j["pan_x"] = pan.x;
        j["pan_y"] = pan.y;
        auto const previous_widgets = j.value("widgets", nlohmann::json::array());
        auto saved_widgets = nlohmann::json::array();
        for (auto const& widget : widgets) {
            // Keep control settings while restoring the widget types in stages.
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
            if (widget.type == PanelWidgetType::Led) {
                data["led_color"] = {widget.led.color.x, widget.led.color.y, widget.led.color.z, widget.led.color.w};
                data["cells"][0]["script"] = widget.led.condition;
            }
            if (widget.type == PanelWidgetType::ValueGrid) {
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
            }
            if (widget.type == PanelWidgetType::CheckboxGrid) {
                data["rows"] = widget.checkboxes.rows;
                data["columns"] = widget.checkboxes.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.checkboxes.cells) {
                    data["cells"].push_back({{"label", cell.label}, {"on_enable", cell.on_enable.script}, {"on_disable", cell.on_disable.script}});
                }
            }
            if (widget.type == PanelWidgetType::ButtonGrid) {
                data["rows"] = widget.buttons.rows;
                data["columns"] = widget.buttons.columns;
                data["cells"] = nlohmann::json::array();
                for (auto const& cell : widget.buttons.cells) {
                    data["cells"].push_back({{"label", cell.label}, {"script", cell.script}});
                }
            }
            if (widget.plot) {
                widget.plot->updateJson(data["plot"]);
            }
            saved_widgets.push_back(std::move(data));
        }
        j["widgets"] = std::move(saved_widgets);
    }
    void selectWidget(uint64_t id, bool additive) {
        if (additive) {
            if (contains(selected_widgets, id)) {
                remove(selected_widgets, id);
            } else {
                selected_widgets.push_back(id);
            }
        } else if (!contains(selected_widgets, id)) {
            selected_widgets = {id};
        }
    }
    void selectWidgetsInBox(ImVec2 start, ImVec2 end, bool additive) {
        if (!additive) {
            selected_widgets.clear();
        }
        ImVec2 const min = {std::min(start.x, end.x), std::min(start.y, end.y)};
        ImVec2 const max = {std::max(start.x, end.x), std::max(start.y, end.y)};
        for (auto const& widget : widgets) {
            if (widget.position.x <= max.x && widget.position.x + widget.size.x >= min.x
                && widget.position.y <= max.y && widget.position.y + widget.size.y >= min.y
                && !contains(selected_widgets, widget.id)) {
                selected_widgets.push_back(widget.id);
            }
        }
    }
    void moveSelectedWidgets(ImVec2 delta) {
        for (auto& widget : widgets) {
            if (contains(selected_widgets, widget.id)) {
                widget.position.x += delta.x;
                widget.position.y += delta.y;
            }
        }
    }
    std::vector<uint64_t> selected_widgets;
    std::vector<uint64_t> selection_before_box;
    bool box_selecting = false;
    ImVec2 box_start = {0, 0};
    bool moving_widgets = false;
    ImVec2 move_origin = {0, 0};
    ImVec2 applied_move = {0, 0};
    std::optional<uint64_t> resizing_widget;
    ImVec2 resize_origin = {0, 0};
    ImVec2 pan = {0, 0};
    float zoom = 1;
    std::vector<PanelWidget> widgets;
};
