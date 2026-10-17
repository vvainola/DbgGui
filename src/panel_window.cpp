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

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
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
                ImGui::PushID(std::to_string(widget.id).c_str());
                ImVec2 const position = {origin.x + panel.pan.x + widget.position.x * panel.zoom,
                                         origin.y + panel.pan.y + widget.position.y * panel.zoom};
                ImVec2 const size = {widget.size.x * panel.zoom, widget.size.y * panel.zoom};
                ImVec2 const corner = {position.x + size.x, position.y + size.y};
                ImGui::SetCursorScreenPos(position);
                ImGui::PushFont(ImGui::GetDefaultFont(), ImGui::GetFontSize() * panel.zoom);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                bool const visible = ImGui::BeginChild("widget", size, ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::PopStyleVar();
                if (visible) {
                    auto* widget_draw = ImGui::GetWindowDrawList();
                    float const grip = std::min(std::max(12.0f, 16.0f * panel.zoom), std::min(size.x, size.y) / 3.0f);
                    bool const resize_target = panel.resizing_widget == widget.id
                        || (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect({corner.x - grip, corner.y - grip}, corner));
                    if (resize_target) {
                        ImGui::Dummy(size);
                    } else {
                        ImGui::InvisibleButton("control", size);
                    }
                    bool const widget_hovered = ImGui::IsWindowHovered();
                    if (!resize_target && ImGui::IsItemClicked()) {
                        panel.selectWidget(widget.id, io.KeyCtrl);
                        panel.moving_widgets = !io.KeyCtrl;
                        panel.move_origin = widget.position;
                        panel.applied_move = {0, 0};
                    }
                    if (widget_hovered && !resize_target) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    }
                    if (ImGui::BeginPopupContextItem("widget_settings")) {
                        ImGui::InputText("Label", &widget.label);
                        if (ImGui::MenuItem("Delete widget")) {
                            remove_widget = widget.id;
                        }
                        ImGui::EndPopup();
                    }
                    if (widget.type == PanelWidgetType::Led) {
                        float const radius = std::max(1.0f, size.x * 0.5f - 2.0f);
                        ImVec2 const center = {position.x + size.x * 0.5f, position.y + size.y * 0.5f};
                        widget_draw->AddCircleFilled(center, radius, IM_COL32(60, 60, 60, 255));
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
                       {"Checkbox", PanelWidgetType::Checkbox},
                       {"Slider", PanelWidgetType::Slider},
                       {"LED", PanelWidgetType::Led}}) {
                    if (ImGui::MenuItem(label)) {
                        PanelWidget widget;
                        for (auto const& existing : panel.widgets) {
                            widget.id = std::max(widget.id, existing.id);
                        }
                        ++widget.id;
                        widget.type = type;
                        if (type == PanelWidgetType::Led) {
                            widget.setSize({40, 40});
                        } else if (type == PanelWidgetType::Checkbox) {
                            widget.size = {140, 30};
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
