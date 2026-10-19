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
#include <cfloat>
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

// Numeric values are stored in panel files and session settings.
enum class PanelWidgetType {
    Plot,
    ButtonGrid,
    ValueGrid,
    CheckboxGrid,
    SliderGrid,
    LedGrid,
    TextNote
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

struct PanelSlider : PanelValueCell {
    double minimum = 0;
    double maximum = 1;
};

struct PanelSliderGrid : PanelControlGrid<PanelSlider> {
    float text_to_value_ratio = 0.3f;
};

struct PanelLedCell {
    std::string label = "LED";
    bool editor_open = false;
    std::string condition = "return false";
    std::string draft;
    ImVec4 color = {0.2f, 0.9f, 0.3f, 1};
    std::shared_ptr<PanelLedState> state;

    void configure(LuaScriptHost host) {
        state = std::make_shared<PanelLedState>(condition, std::move(host));
    }
};

struct PanelGroup {
    // Canvas units; shared by the background bounds and its title area.
    static constexpr float TITLE_HEIGHT = 20;

    uint64_t id = 0;
    std::string title = "Group";
    ImVec4 color = {0.15f, 0.25f, 0.3f, 0.7f};
    float padding = 8;
};

struct PanelWidget {
    uint64_t id = 0;
    PanelWidgetType type = PanelWidgetType::ButtonGrid;
    std::string label = "Widget";
    ImVec2 position = {30, 30};
    ImVec2 size = {320, 180};
    std::shared_ptr<PanelGroup> group;
    // Stable addresses keep plot drag/drop payloads valid when adding widgets.
    std::shared_ptr<PlotWindow> plot;
    PanelButtonGrid buttons;
    PanelCheckboxGrid checkboxes;
    PanelValueGrid values;
    PanelSliderGrid sliders;
    PanelControlGrid<PanelLedCell> leds;
    struct {
        std::string text = "Text note";
        float font_size = 20;
        ImVec4 color = {1, 1, 1, 1};
    } note;
    ImVec2 minimumSize() const {
        switch (type) {
            case PanelWidgetType::TextNote: return {64, 32};
            case PanelWidgetType::ButtonGrid:
            case PanelWidgetType::CheckboxGrid: return {64, 64};
            case PanelWidgetType::SliderGrid: return {100, 96};
            default: return {160, 100};
        }
    }
    void setSize(ImVec2 requested) {
        ImVec2 const minimum = minimumSize();
        size = {std::max(minimum.x, requested.x), std::max(minimum.y, requested.y)};
    }
    void resizeOnGrid(ImVec2 original, ImVec2 delta) {
        ImVec2 const minimum = minimumSize();
        setSize({std::max(std::ceil(minimum.x / PANEL_GRID_SPACING) * PANEL_GRID_SPACING, snapToPanelGrid(original.x + delta.x)),
                 std::max(std::ceil(minimum.y / PANEL_GRID_SPACING) * PANEL_GRID_SPACING, snapToPanelGrid(original.y + delta.y))});
    }
};

struct PanelWindow : Window {
    using Window::Window;
    explicit PanelWindow(nlohmann::json const& j);
    void updateJson(nlohmann::json& j) const;
    void selectWidget(uint64_t id, bool additive) {
        auto it = std::ranges::find(widgets, id, &PanelWidget::id);
        if (it == widgets.end()) {
            return;
        }
        bool const selected = contains(selected_widgets, id);
        if (!additive) {
            if (!selected) {
                selected_widgets = {id};
            }
        } else if (selected) {
            std::erase(selected_widgets, id);
        } else {
            selected_widgets.push_back(id);
        }
    }
    void groupSelected() {
        if (selected_widgets.size() == 1) {
            auto const widget = std::ranges::find(widgets, selected_widgets.front(), &PanelWidget::id);
            if (widget != widgets.end()) {
                if (auto group = groupAt(widget->position)) {
                    widget->group = std::move(group);
                }
            }
            return;
        }
        if (selected_widgets.size() < 2) {
            return;
        }
        auto group = std::make_shared<PanelGroup>();
        for (auto const& widget : widgets) {
            if (widget.group) {
                group->id = std::max(group->id, widget.group->id);
            }
        }
        ++group->id;
        for (auto& widget : widgets) {
            if (contains(selected_widgets, widget.id)) {
                widget.group = group;
            }
        }
    }
    void ungroupSelected() {
        for (auto& widget : widgets) {
            if (contains(selected_widgets, widget.id)) {
                widget.group.reset();
            }
        }
    }
    ImVec4 groupBounds(PanelGroup const& group) const {
        ImVec4 bounds = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (auto const& widget : widgets) {
            if (widget.group.get() == &group) {
                bounds.x = std::min(bounds.x, widget.position.x - group.padding);
                bounds.y = std::min(bounds.y, widget.position.y - PanelGroup::TITLE_HEIGHT);
                bounds.z = std::max(bounds.z, widget.position.x + widget.size.x + group.padding);
                bounds.w = std::max(bounds.w, widget.position.y + widget.size.y + group.padding);
            }
        }
        return bounds;
    }
    std::shared_ptr<PanelGroup> groupAt(ImVec2 position) const {
        std::vector<std::shared_ptr<PanelGroup>> groups;
        for (auto const& widget : widgets) {
            if (widget.group && !contains(groups, widget.group)) {
                groups.push_back(widget.group);
            }
        }
        // Backgrounds are drawn in first-member order; the last one is on top.
        for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
            auto const bounds = groupBounds(**it);
            if (position.x >= bounds.x && position.x <= bounds.z
                && position.y >= bounds.y && position.y <= bounds.w) {
                return *it;
            }
        }
        return nullptr;
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
    void removeWidgets(uint64_t context_widget) {
        if (contains(selected_widgets, context_widget)) {
            std::erase_if(widgets, [&](PanelWidget const& widget) { return contains(selected_widgets, widget.id); });
            selected_widgets.clear();
        } else {
            std::erase_if(widgets, [&](PanelWidget const& widget) { return widget.id == context_widget; });
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
