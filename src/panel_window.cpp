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

#include "dbg_gui_internal.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "lua_script_editor.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr ImVec2 PANEL_SCRIPT_EDITOR_SIZE = {420, 180};
constexpr ImVec4 RUNNING_BUTTON_GREEN = {0.15f, 0.5f, 0.2f, 1.0f};
constexpr ImVec4 RUNNING_BUTTON_HOVERED_LIGHT_GREEN = {0.2f, 0.6f, 0.25f, 1.0f};
constexpr ImVec4 RUNNING_BUTTON_PRESSED_DARK_GREEN = {0.1f, 0.4f, 0.15f, 1.0f};
constexpr ImVec4 FAILED_BUTTON_RED = {0.65f, 0.15f, 0.15f, 1.0f};
constexpr ImVec4 FAILED_BUTTON_HOVERED_LIGHT_RED = {0.75f, 0.2f, 0.2f, 1.0f};
constexpr ImVec4 FAILED_BUTTON_PRESSED_DARK_RED = {0.5f, 0.1f, 0.1f, 1.0f};

void showPanelScriptError(std::string_view error) {
    if (error.empty()) {
        return;
    }
    auto const& style = ImGui::GetStyle();
    float const wrap_width = PANEL_SCRIPT_EDITOR_SIZE.x - 2 * style.WindowPadding.x - style.ScrollbarSize;
    float const text_height = ImGui::CalcTextSize(error.data(), error.data() + error.size(), false, wrap_width).y;
    float const height = std::min(text_height, 4 * ImGui::GetTextLineHeight()) + 2 * style.WindowPadding.y;
    if (ImGui::BeginChild("script_error", {PANEL_SCRIPT_EDITOR_SIZE.x, height}, ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoBackground)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
        ImGui::TextUnformatted(error.data(), error.data() + error.size());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void showPanelActionEditor(PanelActionScript& script) {
    inputScriptTextWithLineNumbers(script.draft, PANEL_SCRIPT_EDITOR_SIZE, true);
    if (ImGui::Button("Apply script")) {
        script.applyScript();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    std::string error = script.syntax_error;
    if (script.action) {
        if (script.action->running.load()) {
            ImGui::TextDisabled("Running...");
        } else if (error.empty()) {
            error = script.action->error;
        }
    }
    showPanelScriptError(error);
}

void drawPanelCanvas(PanelWindow& panel, ImVec2 origin, ImVec2 size) {
    auto const& io = ImGui::GetIO();
    bool const hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        panel.pan.x += io.MouseDelta.x;
        panel.pan.y += io.MouseDelta.y;
    }
    if (hovered && (io.KeyCtrl || ImGui::IsWindowHovered()) && io.MouseWheel != 0) {
        float const old_zoom = panel.zoom;
        panel.zoom = std::clamp(old_zoom * std::pow(1.15f, io.MouseWheel), 0.25f, 3.0f);
        // Keep the canvas point under the mouse stationary while zooming.
        panel.pan.x = io.MousePos.x - origin.x - (io.MousePos.x - origin.x - panel.pan.x) * panel.zoom / old_zoom;
        panel.pan.y = io.MousePos.y - origin.y - (io.MousePos.y - origin.y - panel.pan.y) * panel.zoom / old_zoom;
    }
    auto* draw = ImGui::GetWindowDrawList();
    float const spacing = PANEL_GRID_SPACING * panel.zoom;
    for (float x = std::fmod(panel.pan.x, spacing); x < size.x; x += spacing) {
        draw->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + size.y}, IM_COL32(100, 100, 100, 40));
    }
    for (float y = std::fmod(panel.pan.y, spacing); y < size.y; y += spacing) {
        draw->AddLine({origin.x, origin.y + y}, {origin.x + size.x, origin.y + y}, IM_COL32(100, 100, 100, 40));
    }
}
} // namespace

void DbgGui::processPanelScripts(double timestamp) {
    // Called with m_sampling_mutex held. Keep active actions alive while Lua
    // callbacks may release the mutex to register scalars on the GUI thread.
    std::erase_if(m_panel.actions, [](auto const& action) {
        auto state = action.lock();
        return !state || !state->running.load();
    });
    std::vector<std::shared_ptr<PanelActionState>> actions;
    for (auto const& action : m_panel.actions) {
        if (auto state = action.lock()) {
            actions.push_back(std::move(state));
        }
    }
    for (auto const& action : actions) {
        action->process(timestamp);
    }
    // Multiple samples consume one LED frame request.
    if (!m_panel.scripts_requested.exchange(false)) {
        return;
    }
    std::erase_if(m_panel.led_scripts, [](auto const& script) { return script.expired(); });
    std::vector<std::shared_ptr<PanelLedState>> scripts;
    for (auto const& script : m_panel.led_scripts) {
        if (auto state = script.lock()) {
            scripts.push_back(std::move(state));
        }
    }
    // Host callbacks can release the sampling mutex to register symbols on the GUI
    // thread. Keep runtimes alive without iterating the mutable registry during Lua.
    for (auto const& state : scripts) {
        if (!state->requested.exchange(false) || !state->result.load()->error.empty()) {
            continue;
        }
        auto result = state->runner.evaluateBoolean(timestamp);
        auto published = std::make_shared<PanelLedResult>();
        published->lit = result.value_or(false);
        if (!result) {
            published->error = result.error();
        }
        state->result.store(std::move(published));
    }
}

void DbgGui::renderPanelButtonGrid(PanelWidget& widget, PanelWindow& panel) {
    auto& grid = widget.buttons;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y / grid.rows
        - 2 * ImGui::GetStyle().CellPadding.y);
    if (!ImGui::BeginTable("buttons", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    for (size_t index = 0; index < grid.cells.size(); ++index) {
        auto& cell = grid.cells[index];
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        bool const running = cell.action && cell.action->running.load();
        bool const failed = !running && cell.action && !cell.action->error.empty();
        if (running || failed) {
            ImGui::PushStyleColor(ImGuiCol_Button, failed ? FAILED_BUTTON_RED : RUNNING_BUTTON_GREEN);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, failed ? FAILED_BUTTON_HOVERED_LIGHT_RED : RUNNING_BUTTON_HOVERED_LIGHT_GREEN);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, failed ? FAILED_BUTTON_PRESSED_DARK_RED : RUNNING_BUTTON_PRESSED_DARK_GREEN);
        }
        if (ImGui::Button((cell.label + "###action").c_str(), {-1, cell_height}) && !ImGui::GetIO().KeyCtrl) {
            if (running) {
                cell.action->stop_requested.store(true);
            } else {
                cell.action = std::make_shared<PanelActionState>(cell.script, createLuaScriptHost());
                std::scoped_lock lock(m_sampling_mutex);
                m_panel.actions.push_back(cell.action);
            }
        }
        if (running || failed) {
            ImGui::PopStyleColor(3);
        }
        if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
            panel.selectWidget(widget.id, true);
            // A selection click must not run the action if Ctrl is released first.
            ImGui::ClearActiveID();
        }
        if (ImGui::BeginPopupContextItem("button_settings")) {
            if (ImGui::IsWindowAppearing()) {
                cell.draft = cell.script;
                cell.syntax_error.clear();
            }
            ImGui::InputText("Label", &cell.label);
            ImGui::SameLine();
            HelpMarker("Runs once when clicked. Clicking again stops the action. Lua waits use simulation time.");
            showPanelActionEditor(cell);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::renderPanelValueGrid(PanelWidget& widget) {
    auto& grid = widget.values;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y / grid.rows
        - 2 * ImGui::GetStyle().CellPadding.y);
    if (!ImGui::BeginTable("values", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    for (size_t index = 0; index < grid.cells.size(); ++index) {
        auto& cell = grid.cells[index];
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        ImGui::BeginGroup();
        ImVec2 const start = ImGui::GetCursorScreenPos();
        Scalar* scalar = cell.scalar_id ? findScalar(m_scalars, *cell.scalar_id) : nullptr;
        bool const available = scalar && !scalar->deleted;
        char const* label = available ? scalar->alias_and_group.c_str()
            : cell.scalar_id ? "Unavailable scalar" : "Drop a scalar here";
        auto const& style = ImGui::GetStyle();
        float const font_height = std::max(1.0f, cell_height - 2 * style.FramePadding.y - 2 * style.ItemSpacing.y);
        float const cell_width = ImGui::GetContentRegionAvail().x;
        // Split the cell height between its label and value.
        ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, font_height * grid.text_to_value_ratio));
        float const label_width = ImGui::CalcTextSize(label).x;
        if (label_width > cell_width) {
            ImGui::PopFont();
            ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, font_height * grid.text_to_value_ratio * cell_width / label_width));
        }
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, font_height * (1.0f - grid.text_to_value_ratio)));
        if (available) {
            std::string const value = numberAsStr(scalar->getScaledValue());
            float const value_width = ImGui::CalcTextSize(value.c_str()).x;
            float const input_width = std::max(1.0f, cell_width - 2 * style.FramePadding.x);
            if (value_width > input_width) {
                ImGui::PopFont();
                ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, font_height * (1.0f - grid.text_to_value_ratio) * input_width / value_width));
            }
            addInputScalar(scalar->src, "##value", scalar->getScale(), scalar->getOffset(), scalar->read_only);
        } else {
            ImGui::TextDisabled("-");
        }
        ImGui::PopFont();
        float const remaining = cell_height - (ImGui::GetCursorScreenPos().y - start.y);
        ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(0.0f, remaining)});
        ImGui::EndGroup();
        if (ImGui::BeginDragDropTarget()) {
            if (auto const* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
                if (payload->DataSize >= static_cast<int>(sizeof(uint64_t))) {
                    cell.scalar_id = *static_cast<uint64_t const*>(payload->Data);
                }
            }
            if (auto const* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL_MULTI")) {
                if (payload->DataSize >= static_cast<int>(sizeof(VariantSymbol*))) {
                    auto* symbol = *static_cast<VariantSymbol* const*>(payload->Data);
                    if (auto* added = addScalarSymbol(symbol, m_group_to_add_symbols)) {
                        cell.scalar_id = added->id;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("value_settings")) {
            if (ImGui::MenuItem("Unbind", nullptr, false, cell.scalar_id.has_value())) {
                cell.scalar_id.reset();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::renderPanelCheckboxGrid(PanelWidget& widget, PanelWindow& panel) {
    auto& grid = widget.checkboxes;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y / grid.rows
        - 2 * ImGui::GetStyle().CellPadding.y);
    if (!ImGui::BeginTable("checkboxes", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    for (size_t index = 0; index < grid.cells.size(); ++index) {
        auto& cell = grid.cells[index];
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        bool const checked = cell.checked;
        if (checked) {
            ImGui::PushStyleColor(ImGuiCol_Button, RUNNING_BUTTON_GREEN);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RUNNING_BUTTON_HOVERED_LIGHT_GREEN);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, RUNNING_BUTTON_PRESSED_DARK_GREEN);
        }
        if (ImGui::Button((cell.label + "###toggle").c_str(), {-1, cell_height}) && !ImGui::GetIO().KeyCtrl) {
            auto action = cell.setChecked(!cell.checked, createLuaScriptHost());
            std::scoped_lock lock(m_sampling_mutex);
            m_panel.actions.push_back(action);
        }
        if (checked) {
            ImGui::PopStyleColor(3);
        }
        if (cell.checked) {
            ImVec2 const min = ImGui::GetItemRectMin();
            ImVec2 const max = ImGui::GetItemRectMax();
            float const mark_size = std::min(ImGui::GetFontSize(), cell_height);
            ImGui::RenderCheckMark(ImGui::GetWindowDrawList(),
                {min.x + ImGui::GetStyle().FramePadding.x, (min.y + max.y - mark_size) * 0.5f},
                ImGui::GetColorU32(ImGuiCol_Text), mark_size);
        }
        if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
            panel.selectWidget(widget.id, true);
            ImGui::ClearActiveID();
        }
        if (ImGui::BeginPopupContextItem("checkbox_settings")) {
            if (ImGui::IsWindowAppearing()) {
                for (auto* script : {&cell.on_enable, &cell.on_disable}) {
                    script->draft = script->script;
                    script->syntax_error.clear();
                }
            }
            ImGui::InputText("Label", &cell.label);
            ImGui::SameLine();
            HelpMarker("Runs the selected transition script when checked or unchecked. Lua waits use simulation time.");
            if (ImGui::BeginTabBar("checkbox_scripts")) {
                if (ImGui::BeginTabItem("On enable")) {
                    showPanelActionEditor(cell.on_enable);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("On disable")) {
                    showPanelActionEditor(cell.on_disable);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::showPanels() {
    for (PanelWindow& panel : m_panels) {
        if (!panel.open) {
            continue;
        }
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        // Keep title text outside the top border's resize hit area in compact themes.
        auto const& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x, std::max(style.FramePadding.y, style.WindowBorderHoverPadding)));
        panel.focus.focused = ImGui::Begin(panel.title().c_str(), &panel.open);
        ImGui::PopStyleVar();
        panel.closeOnMiddleClick();
        panel.contextMenu();
        if (!panel.focus.focused) {
            ImGui::End();
            continue;
        }
        if (ImGui::BeginChild("canvas", ImVec2(0, 0), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            auto const& io = ImGui::GetIO();
            ImVec2 const origin = ImGui::GetCursorScreenPos();
            ImVec2 const available = ImGui::GetContentRegionAvail();
            drawPanelCanvas(panel, origin, available);
            // Capture empty-canvas clicks so dragging selects instead of moving the window.
            ImGui::InvisibleButton("background", available);
            if (ImGui::IsItemClicked()) {
                panel.box_selecting = true;
                panel.box_start = {(io.MousePos.x - origin.x - panel.pan.x) / panel.zoom,
                                   (io.MousePos.y - origin.y - panel.pan.y) / panel.zoom};
                panel.selection_before_box = io.KeyCtrl ? panel.selected_widgets : std::vector<uint64_t>{};
            }
            if (panel.box_selecting) {
                ImVec2 const end = {(io.MousePos.x - origin.x - panel.pan.x) / panel.zoom,
                                    (io.MousePos.y - origin.y - panel.pan.y) / panel.zoom};
                panel.selected_widgets = panel.selection_before_box;
                ImVec2 const delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                if (delta.x != 0 || delta.y != 0) {
                    panel.selectWidgetsInBox(panel.box_start, end, true);
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    panel.box_selecting = false;
                }
            }
            if (panel.moving_widgets && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 const drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                ImVec2 const movement = {snapToPanelGrid(panel.move_origin.x + drag.x / panel.zoom) - panel.move_origin.x,
                                         snapToPanelGrid(panel.move_origin.y + drag.y / panel.zoom) - panel.move_origin.y};
                panel.moveSelectedWidgets({movement.x - panel.applied_move.x, movement.y - panel.applied_move.y});
                panel.applied_move = movement;
            }
            if (!io.MouseDown[ImGuiMouseButton_Left]) {
                panel.moving_widgets = false;
                panel.resizing_widget.reset();
            }
            std::optional<uint64_t> remove_widget;
            for (auto& widget : panel.widgets) {
                if (widget.type == PanelWidgetType::Led && !widget.led.state) {
                    widget.configureLed(createLuaScriptHost());
                    {
                        std::scoped_lock lock(m_sampling_mutex);
                        m_panel.led_scripts.push_back(widget.led.state);
                    }
                }
                ImGui::PushID(std::to_string(widget.id).c_str());
                ImVec2 const position = {origin.x + panel.pan.x + widget.position.x * panel.zoom,
                                         origin.y + panel.pan.y + widget.position.y * panel.zoom};
                ImVec2 const size = {widget.size.x * panel.zoom, widget.size.y * panel.zoom};
                ImVec2 const corner = {position.x + size.x, position.y + size.y};
                ImGui::SetCursorScreenPos(position);
                ImGui::PushFont(ImGui::GetDefaultFont(), ImGui::GetFontSize() * panel.zoom);
                bool const is_plot = widget.type == PanelWidgetType::Plot;
                bool const is_buttons = widget.type == PanelWidgetType::ButtonGrid;
                bool const is_checkbox = widget.type == PanelWidgetType::CheckboxGrid;
                bool const is_values = widget.type == PanelWidgetType::ValueGrid;
                bool const has_controls = is_plot || is_buttons || is_checkbox || is_values;
                float const content_padding = has_controls ? 12.0f * panel.zoom : 0.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(content_padding, 0));
                bool const visible = ImGui::BeginChild("widget", size,
                    has_controls ? ImGuiChildFlags_AlwaysUseWindowPadding : ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::PopStyleVar();
                if (visible) {
                    auto* widget_draw = ImGui::GetWindowDrawList();
                    float const grip = std::min(std::max(12.0f, 16.0f * panel.zoom), std::min(size.x, size.y) / 3.0f);
                    bool const resize_target = panel.resizing_widget == widget.id
                        || (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect({corner.x - grip, corner.y - grip}, corner));
                    ImVec2 const control_position = ImGui::GetCursorScreenPos();
                    ImVec2 const control_size = has_controls ? ImVec2(ImGui::CalcTextSize(widget.label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2,
                                                                 ImGui::GetFrameHeight()) : size;
                    if (resize_target) {
                        ImGui::Dummy(control_size);
                    } else {
                        ImGui::InvisibleButton("control", control_size);
                    }
                    bool const widget_hovered = ImGui::IsWindowHovered();
                    if (!resize_target && ImGui::IsItemClicked()) {
                        panel.selectWidget(widget.id, io.KeyCtrl);
                        panel.moving_widgets = !io.KeyCtrl;
                        panel.move_origin = widget.position;
                        panel.applied_move = {0, 0};
                    }
                    if (widget_hovered && !resize_target && (!has_controls || ImGui::IsItemHovered())) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    }
                    if (ImGui::BeginPopupContextItem("widget_settings")) {
                        ImGui::InputText("Label", &widget.label);
                        if (is_buttons || is_checkbox || is_values) {
                            int rows = is_buttons ? widget.buttons.rows : is_checkbox ? widget.checkboxes.rows : widget.values.rows;
                            int columns = is_buttons ? widget.buttons.columns : is_checkbox ? widget.checkboxes.columns : widget.values.columns;
                            bool const rows_changed = ImGui::InputInt("Rows", &rows);
                            bool const columns_changed = ImGui::InputInt("Columns", &columns);
                            if (rows_changed || columns_changed) {
                                if (is_buttons) {
                                    widget.buttons.resize(rows, columns);
                                } else if (is_checkbox) {
                                    widget.checkboxes.resize(rows, columns);
                                } else {
                                    widget.values.resize(rows, columns);
                                }
                            }
                        }
                        if (is_values) {
                            ImGui::SliderFloat("Text to value ratio", &widget.values.text_to_value_ratio, 0.05f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                        }
                        if (widget.type == PanelWidgetType::Led) {
                            if (ImGui::IsWindowAppearing()) {
                                widget.led.draft = widget.led.condition;
                            }
                            ImGui::SameLine();
                            ImGui::ColorEdit4("##lit_color", &widget.led.color.x, ImGuiColorEditFlags_NoInputs);
                            ImGui::SameLine();
                            HelpMarker("Return true to light the LED, false to turn it off");
                            inputScriptTextWithLineNumbers(widget.led.draft, PANEL_SCRIPT_EDITOR_SIZE, true);
                            if (ImGui::Button("Apply condition")) {
                                widget.led.condition = widget.led.draft;
                                widget.configureLed(createLuaScriptHost());
                                {
                                    std::scoped_lock lock(m_sampling_mutex);
                                    m_panel.led_scripts.push_back(widget.led.state);
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Close")) {
                                ImGui::CloseCurrentPopup();
                            }
                            auto result = widget.led.state->result.load();
                            showPanelScriptError(result->error);
                        }
                        if (ImGui::MenuItem("Delete widget")) {
                            remove_widget = widget.id;
                        }
                        ImGui::EndPopup();
                    }
                    if (widget.type == PanelWidgetType::Led) {
                        float const radius = std::max(1.0f, size.x * 0.5f - 2.0f);
                        ImVec2 const center = {position.x + size.x * 0.5f, position.y + size.y * 0.5f};
                        auto& led = *widget.led.state;
                        led.requested.store(true);
                        m_panel.scripts_requested.store(true);
                        auto result = led.result.load();
                        widget_draw->AddCircleFilled(center, radius,
                            result->lit ? ImGui::GetColorU32(widget.led.color) : IM_COL32(60, 60, 60, 255));
                    } else if (has_controls) {
                        widget_draw->AddText({control_position.x, control_position.y + ImGui::GetStyle().FramePadding.y},
                                            ImGui::GetColorU32(ImGuiCol_Text), widget.label.c_str());
                        if (is_plot) {
                            if (!widget.plot) {
                                widget.plot = std::make_shared<PlotWindow>(widget.label, widget.id);
                            }
                            ImGui::SameLine();
                            renderPlotContents(*widget.plot);
                        } else if (is_buttons) {
                            renderPanelButtonGrid(widget, panel);
                        } else if (is_checkbox) {
                            renderPanelCheckboxGrid(widget, panel);
                        } else {
                            renderPanelValueGrid(widget);
                        }
                    } else {
                        widget_draw->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), widget.label.c_str());
                    }
                    bool const selected = contains(panel.selected_widgets, widget.id);
                    if (widget_hovered || selected || panel.resizing_widget == widget.id) {
                        widget_draw->AddRect(position, corner, ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_Border));
                        ImGui::SetCursorScreenPos({corner.x - grip, corner.y - grip});
                        ImGui::InvisibleButton("resize", {grip, grip});
                        if (ImGui::IsItemClicked()) {
                            panel.resizing_widget = widget.id;
                            panel.resize_origin = widget.size;
                            panel.moving_widgets = false;
                        }
                        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                        }
                        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                            ImVec2 const drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                            widget.resizeOnGrid(panel.resize_origin, {drag.x / panel.zoom, drag.y / panel.zoom});
                        }
                        widget_draw->AddTriangleFilled({corner.x - grip * 0.5f, corner.y}, corner,
                          {corner.x, corner.y - grip * 0.5f}, ImGui::GetColorU32(ImGuiCol_ResizeGripHovered));
                    }
                }
                ImGui::EndChild();
                if (visible && widget.type == PanelWidgetType::Led) {
                    ImVec2 const text_size = ImGui::CalcTextSize(widget.label.c_str());
                    ImVec2 const label_position = {corner.x + ImGui::GetStyle().ItemInnerSpacing.x * panel.zoom,
                                                  position.y + (size.y - text_size.y) * 0.5f};
                    ImGui::GetWindowDrawList()->AddText(label_position, ImGui::GetColorU32(ImGuiCol_Text), widget.label.c_str());
                }
                ImGui::PopFont();
                ImGui::PopID();
            }
            if (remove_widget) {
                std::erase_if(panel.widgets, [&](PanelWidget const& widget) { return widget.id == *remove_widget; });
                remove(panel.selected_widgets, *remove_widget);
            }
            if (panel.box_selecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 const start = {origin.x + panel.pan.x + panel.box_start.x * panel.zoom,
                                      origin.y + panel.pan.y + panel.box_start.y * panel.zoom};
                // Draw after widget children so the selection rectangle stays visible.
                ImGui::SetCursorScreenPos(origin);
                ImGui::BeginChild("selection", available, ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
                auto* overlay = ImGui::GetWindowDrawList();
                ImVec2 const min = {std::min(start.x, io.MousePos.x), std::min(start.y, io.MousePos.y)};
                ImVec2 const max = {std::max(start.x, io.MousePos.x), std::max(start.y, io.MousePos.y)};
                overlay->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark, 0.15f));
                overlay->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark));
                ImGui::EndChild();
            }
            if (ImGui::BeginPopupContextWindow("canvas_menu", ImGuiPopupFlags_MouseButtonRight)) {
                ImVec2 const mouse = ImGui::GetMousePosOnOpeningCurrentPopup();
                for (auto const& [label, type] : {
                       std::pair{"Plot", PanelWidgetType::Plot},
                       {"Button grid", PanelWidgetType::ButtonGrid},
                       {"Value grid", PanelWidgetType::ValueGrid},
                       {"Checkbox grid", PanelWidgetType::CheckboxGrid},
                       {"Slider", PanelWidgetType::Slider},
                       {"LED", PanelWidgetType::Led}}) {
                    if (ImGui::MenuItem(label)) {
                        PanelWidget widget;
                        for (auto const& existing : panel.widgets) {
                            widget.id = std::max(widget.id, existing.id);
                        }
                        ++widget.id;
                        widget.type = type;
                        if (type == PanelWidgetType::Plot) {
                            widget.size = {640, 384};
                            widget.plot = std::make_shared<PlotWindow>(label, widget.id);
                        } else if (type == PanelWidgetType::Led) {
                            widget.setSize({PANEL_GRID_SPACING, PANEL_GRID_SPACING});
                        } else if (type == PanelWidgetType::CheckboxGrid) {
                            widget.size = {320, 160};
                        } else if (type == PanelWidgetType::Slider) {
                            widget.size = {240, 30};
                        }
                        widget.label = label;
                        widget.position = {snapToPanelGrid((mouse.x - origin.x - panel.pan.x) / panel.zoom),
                                           snapToPanelGrid((mouse.y - origin.y - panel.pan.y) / panel.zoom)};
                        panel.widgets.push_back(std::move(widget));
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset view")) {
                    panel.pan = {0, 0};
                    panel.zoom = 1;
                }
                ImGui::TextDisabled("Drag widget: move | Ctrl-click: select | Drag canvas: box select");
                ImGui::TextDisabled("Middle drag: pan | Wheel on canvas: zoom");
                ImGui::TextDisabled("Ctrl + wheel: zoom over widgets");
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
