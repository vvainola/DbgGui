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
#include "panel_file.h"
#include "panel_clipboard.h"
#include <fstream>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void zoomPanelToSelection(PanelWindow& panel, ImVec2 canvas_size) {
    ImVec2 min = {FLT_MAX, FLT_MAX};
    ImVec2 max = {-FLT_MAX, -FLT_MAX};
    for (auto const& widget : panel.widgets) {
        if (!contains(panel.selected_widgets, widget.id)) {
            continue;
        }
        min = {std::min(min.x, widget.position.x), std::min(min.y, widget.position.y)};
        max = {std::max(max.x, widget.position.x + widget.size.x),
               std::max(max.y, widget.position.y + widget.size.y)};
    }
    if (min.x > max.x || min.y > max.y) {
        return;
    }
    // Keep a screen-space margin while centering the selection in the canvas.
    float const padding = 16;
    float const scale_x = std::max(1.0f, canvas_size.x - 2 * padding) / std::max(1.0f, max.x - min.x);
    float const scale_y = std::max(1.0f, canvas_size.y - 2 * padding) / std::max(1.0f, max.y - min.y);
    panel.zoom = std::clamp(std::min(scale_x, scale_y), 0.25f, 3.0f);
    panel.pan = {canvas_size.x * 0.5f - (min.x + max.x) * 0.5f * panel.zoom,
                 canvas_size.y * 0.5f - (min.y + max.y) * 0.5f * panel.zoom};
}

bool drawPanelGroups(PanelWindow& panel, ImVec2 origin) {
    std::vector<std::shared_ptr<PanelGroup>> groups;
    for (auto const& widget : panel.widgets) {
        if (widget.group && !contains(groups, widget.group)) {
            groups.push_back(widget.group);
        }
    }
    bool header_hovered = false;
    for (auto const& group : groups) {
        auto const bounds = panel.groupBounds(*group);
        ImVec2 const min = {origin.x + panel.pan.x + bounds.x * panel.zoom, origin.y + panel.pan.y + bounds.y * panel.zoom};
        ImVec2 const max = {origin.x + panel.pan.x + bounds.z * panel.zoom, origin.y + panel.pan.y + bounds.w * panel.zoom};
        ImVec2 const header_end = {max.x, min.y + PanelGroup::TITLE_HEIGHT * panel.zoom};
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, ImGui::GetColorU32(group->color), 4 * panel.zoom);
        draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), 4 * panel.zoom);
        ImGui::PushFont(ImGui::GetDefaultFont(), 16 * panel.zoom);
        draw->PushClipRect(min, header_end, true);
        draw->AddText({min.x + 4 * panel.zoom, min.y + 2 * panel.zoom}, ImGui::GetColorU32(ImGuiCol_Text), group->title.c_str(), nullptr);
        draw->PopClipRect();
        ImGui::PopFont();
        ImGui::PushID(std::to_string(group->id).c_str());
        bool const hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, header_end);
        if (hovered) {
            header_hovered = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                panel.selected_widgets.clear();
                for (auto const& widget : panel.widgets) {
                    if (widget.group == group) {
                        panel.selected_widgets.push_back(widget.id);
                    }
                }
                auto const member = std::ranges::find(panel.widgets, group, &PanelWidget::group);
                panel.moving_widgets = true;
                panel.move_origin = member->position;
                panel.applied_move = {0, 0};
                panel.box_selecting = false;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("group_settings");
            }
        }
        if (ImGui::BeginPopup("group_settings")) {
            ImGui::InputText("Title", &group->title);
            ImGui::ColorEdit4("Background", &group->color.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("Padding", &group->padding, 0, 64, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Ungroup")) {
                for (auto& widget : panel.widgets) {
                    if (widget.group == group) {
                        widget.group.reset();
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    return header_hovered;
}

void drawPanelCanvas(PanelWindow& panel, ImVec2 origin, ImVec2 size) {
    auto const& io = ImGui::GetIO();
    bool const hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    // Ctrl gives canvas panning priority even when a plot owns the active item.
    bool const pan_hovered = io.KeyCtrl
        ? ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
        : hovered;
    if (pan_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
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

nlohmann::json DbgGui::panelSettings(PanelWindow& panel) {
    auto data = nlohmann::json::object();
    for (auto const& saved : m_settings["panels"]) {
        if (saved.value("id", uint64_t{0}) == panel.id) {
            data = saved;
            break;
        }
    }
    panel.updateJson(data);
    for (size_t index = 0; index < panel.widgets.size(); ++index) {
        if (panel.widgets[index].plot) {
            reconcilePlotSettings(*panel.widgets[index].plot, data["widgets"][index]["plot"]);
        }
    }
    return data;
}

void DbgGui::copyPanelWidgets(PanelWindow& panel) {
    if (!panel.selected_widgets.empty()) {
        m_panel_widget_clipboard = panel_clipboard::copy(panelSettings(panel), panel.selected_widgets);
    }
}

void DbgGui::pastePanelWidgets(PanelWindow& panel, ImVec2 position) {
    if (m_panel_widget_clipboard.empty()) {
        return;
    }
    auto widgets = panel_clipboard::place(m_panel_widget_clipboard, panel, position);
    PanelWindow pasted(nlohmann::json{{"widgets", widgets}});
    auto const group = panel.groupAt({snapToPanelGrid(position.x), snapToPanelGrid(position.y)});
    auto saved = panelSettings(panel);
    panel.selected_widgets.clear();
    for (auto& widget : pasted.widgets) {
        if (!widget.group) {
            widget.group = group;
        }
        panel.selected_widgets.push_back(widget.id);
        panel.widgets.push_back(std::move(widget));
    }
    for (auto& widget : widgets) {
        saved["widgets"].push_back(std::move(widget));
    }
    panel.updateJson(saved);
    auto& panels = m_settings["panels"];
    auto it = std::ranges::find(panels, panel.id, [](auto const& value) { return value.value("id", uint64_t{0}); });
    if (it != panels.end()) {
        *it = std::move(saved);
    } else {
        panels.push_back(std::move(saved));
    }
    std::scoped_lock lock(m_sampling_mutex);
    restorePanelPlotSettings();
}

void DbgGui::exportPanel(PanelWindow& panel) {
    auto const path = getFilenameToSave("json");
    if (path.empty()) {
        return;
    }
    try {
        auto data = panelSettings(panel);
        auto const text = panel_file::encode(std::move(data)).dump(2);
        std::ofstream out;
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out.open(path);
        out << text << '\n';
        out.close();
    } catch (std::exception const& error) {
        logMessage(std::format("Cannot export panel: {}", error.what()));
    }
}

bool DbgGui::importPanel() {
    auto const path = getFilenameToOpen("json");
    if (path.empty()) {
        return false;
    }
    try {
        std::ifstream in(path);
        if (!in) {
            throw std::runtime_error("Cannot open panel file");
        }
        auto const file = nlohmann::json::parse(in);
        uint64_t id = hashWithTime(path);
        while (std::ranges::any_of(m_panels, [id](auto const& panel) { return panel.id == id; })) {
            ++id;
        }
        auto data = panel_file::decode(file, id);
        PanelWindow panel(data);
        m_settings["panels"].push_back(std::move(data));
        m_panels.push_back(std::move(panel));
        std::scoped_lock lock(m_sampling_mutex);
        restorePanelPlotSettings();
        return true;
    } catch (std::exception const& error) {
        logMessage(std::format("Cannot import panel: {}", error.what()));
    }
    return false;
}

void DbgGui::showPanels() {
    bool import_requested = false;
    for (PanelWindow& panel : m_panels) {
        if (!panel.open) {
            continue;
        }
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        // Keep title text outside the top border's resize hit area in compact themes.
        auto const& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x, std::max(style.FramePadding.y, style.WindowBorderHoverPadding)));
        panel.focus.focused = ImGui::Begin(panel.title().c_str(), nullptr);
        ImGui::PopStyleVar();
        panel.closeOnMiddleClick();
        if (ImGui::BeginPopupContextItem("panel_context_menu")) {
            ImGui::InputText("Name", &panel.name);
            ImGui::Separator();
            if (ImGui::MenuItem("Import panel...")) {
                import_requested = true;
            }
            if (ImGui::MenuItem("Export panel...")) {
                exportPanel(panel);
            }
            ImGui::EndPopup();
        }
        if (!panel.focus.focused) {
            ImGui::End();
            continue;
        }
        std::optional<ImVec2> paste_position;
        if (ImGui::BeginChild("canvas",
                              ImVec2(0, 0),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            auto const& io = ImGui::GetIO();
            ImVec2 const origin = ImGui::GetCursorScreenPos();
            ImVec2 const available = ImGui::GetContentRegionAvail();
            drawPanelCanvas(panel, origin, available);
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()
                && !io.WantTextInput && !panel.moving_widgets && !panel.resizing_widget) {
                if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
                    && ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal)) {
                    panel.selected_widgets.clear();
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, ImGuiInputFlags_RouteGlobal)) {
                    panel.groupSelected();
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_G, ImGuiInputFlags_RouteGlobal)) {
                    panel.ungroupSelected();
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) {
                    copyPanelWidgets(panel);
                }
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, ImGuiInputFlags_RouteGlobal) && !m_panel_widget_clipboard.empty()) {
                    ImVec2 const anchor = ImGui::IsMouseHoveringRect(origin,
                                                                     {origin.x + available.x, origin.y + available.y}) ?
                                            io.MousePos :
                                            ImVec2(origin.x + available.x * 0.5f, origin.y + available.y * 0.5f);
                    paste_position = {(anchor.x - origin.x - panel.pan.x) / panel.zoom,
                                      (anchor.y - origin.y - panel.pan.y) / panel.zoom};
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
            bool const group_header_hovered = drawPanelGroups(panel, origin);
            // Capture empty-canvas clicks so dragging selects instead of moving the window.
            ImGui::InvisibleButton("background", available);
            if (ImGui::IsItemClicked() && !group_header_hovered) {
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
            std::optional<uint64_t> remove_widget;
            for (auto& widget : panel.widgets) {
                ImGui::PushID(std::to_string(widget.id).c_str());
                ImVec2 const position = {origin.x + panel.pan.x + widget.position.x * panel.zoom,
                                         origin.y + panel.pan.y + widget.position.y * panel.zoom};
                ImVec2 const size = {widget.size.x * panel.zoom, widget.size.y * panel.zoom};
                ImVec2 const corner = {position.x + size.x, position.y + size.y};
                ImGui::SetCursorScreenPos(position);
                ImGui::PushFont(ImGui::GetDefaultFont(), ImGui::GetFontSize() * panel.zoom);
                bool const is_plot = widget.type == PanelWidgetType::Plot;
                bool const has_controls = widget.type != PanelWidgetType::TextNote;
                float const content_padding = has_controls ? 12.0f * panel.zoom : 0.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(content_padding, 0));
                bool const visible = ImGui::BeginChild("widget", size, has_controls ? ImGuiChildFlags_AlwaysUseWindowPadding : ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::PopStyleVar();
                if (visible) {
                    auto* widget_draw = ImGui::GetWindowDrawList();
                    float const grip = std::min(std::max(12.0f, 16.0f * panel.zoom), std::min(size.x, size.y) / 3.0f);
                    bool const resize_target = panel.resizing_widget == widget.id
                                            || (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect({corner.x - grip, corner.y - grip}, corner));
                    ImVec2 const control_position = ImGui::GetCursorScreenPos();
                    ImVec2 const control_size = has_controls ?
                                                  ImVec2(ImGui::GetContentRegionAvail().x,
                                                         ImGui::GetFrameHeight()) :
                                                  size;
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
                    bool const settings_open = ImGui::BeginPopupContextItem("widget_settings");
                    if (settings_open) {
                        if (widget.type == PanelWidgetType::TextNote) {
                            ImGui::InputTextMultiline("##text", &widget.note.text, {420, 160});
                            ImGui::SliderFloat("Font size", &widget.note.font_size, 8, 100, "%.0f", ImGuiSliderFlags_AlwaysClamp);
                            ImGui::SameLine();
                            ImGui::ColorEdit4("##text_color", &widget.note.color.x, ImGuiColorEditFlags_NoInputs);
                        } else {
                            ImGui::InputText("Label", &widget.label);
                        }
                        if (widget.grid) {
                            editGridSettings(*widget.grid);
                        }
                        if (ImGui::Button("Delete widget")) {
                            ImGui::CloseCurrentPopup();
                            remove_widget = widget.id;
                        }
                        ImGui::EndPopup();
                    }
                    if (has_controls) {
                        widget_draw->AddText({control_position.x, control_position.y + ImGui::GetStyle().FramePadding.y},
                                             ImGui::GetColorU32(ImGuiCol_Text),
                                             widget.label.c_str());
                        if (is_plot) {
                            renderPlotContents(*widget.plot);
                        } else if (widget.grid) {
                            if (ImGui::Button("Menu")) {
                                ImGui::OpenPopup("grid_settings");
                            }
                            if (ImGui::BeginPopup("grid_settings")) {
                                editGridSettings(*widget.grid);
                                ImGui::EndPopup();
                            }
                            ImVec2 const grid_start = ImGui::GetCursorScreenPos();
                            bool const selecting = io.KeyCtrl && !io.WantTextInput;
                            if (selecting && !resize_target && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                                && ImGui::IsMouseHoveringRect(grid_start, corner)
                                && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                                panel.selectWidget(widget.id, true);
                                ImGui::ClearActiveID();
                            }
                            ImGui::BeginDisabled(selecting);
                            renderGrid(*widget.grid);
                            ImGui::EndDisabled();
                        }
                    } else {
                        ImGui::PushFont(ImGui::GetDefaultFont(), widget.note.font_size * panel.zoom);
                        widget_draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, ImGui::GetColorU32(widget.note.color), widget.note.text.c_str(), nullptr, size.x);
                        ImGui::PopFont();
                    }
                    bool const selected = contains(panel.selected_widgets, widget.id);
                    // Keep the right and bottom strokes inside the child window's clipping rectangle.
                    widget_draw->AddRect(position, {corner.x - 1, corner.y - 1},
                                         ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_Border));
                    if (widget_hovered || selected || panel.resizing_widget == widget.id) {
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
                        widget_draw->AddTriangleFilled({corner.x - grip * 0.5f, corner.y}, corner, {corner.x, corner.y - grip * 0.5f}, ImGui::GetColorU32(ImGuiCol_ResizeGripHovered));
                    }
                }
                ImGui::EndChild();
                ImGui::PopFont();
                ImGui::PopID();
            }
            if (remove_widget) {
                panel.removeWidgets(*remove_widget);
            }
            if (panel.box_selecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 const start = {origin.x + panel.pan.x + panel.box_start.x * panel.zoom,
                                      origin.y + panel.pan.y + panel.box_start.y * panel.zoom};
                // Draw after widget children so the selection rectangle stays visible.
                ImGui::SetCursorScreenPos(origin);
                ImGui::BeginChild("selection",
                                  available,
                                  ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
                auto* overlay = ImGui::GetWindowDrawList();
                ImVec2 const min = {std::min(start.x, io.MousePos.x), std::min(start.y, io.MousePos.y)};
                ImVec2 const max = {std::max(start.x, io.MousePos.x), std::max(start.y, io.MousePos.y)};
                overlay->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark, 0.15f));
                overlay->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark));
                ImGui::EndChild();
            }
            if (ImGui::BeginPopupContextWindow("canvas_menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup)) {
                ImVec2 const mouse = ImGui::GetMousePosOnOpeningCurrentPopup();
                for (auto const& [label, type] : {
                       std::pair{"Plot", PanelWidgetType::Plot},
                       {"Control grid", PanelWidgetType::ControlGrid},
                       {"Text note", PanelWidgetType::TextNote}}) {
                    if (ImGui::MenuItem(label)) {
                        uint64_t id = 0;
                        for (auto const& existing : panel.widgets) {
                            id = std::max(id, existing.id);
                        }
                        PanelWidget widget(id + 1, type, label);
                        switch (type) {
                            case PanelWidgetType::Plot:
                                widget.size = {640, 384};
                                break;
                            case PanelWidgetType::TextNote:
                                widget.size = {320, 96};
                                break;
                            case PanelWidgetType::ControlGrid:
                                break;
                            default:
                                break;
                        }
                        widget.position = {snapToPanelGrid((mouse.x - origin.x - panel.pan.x) / panel.zoom),
                                           snapToPanelGrid((mouse.y - origin.y - panel.pan.y) / panel.zoom)};
                        widget.group = panel.groupAt(widget.position);
                        panel.widgets.push_back(std::move(widget));
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Group selection", "Ctrl+G", false, !panel.selected_widgets.empty())) {
                    panel.groupSelected();
                }
                bool const grouped_selection = std::ranges::any_of(panel.widgets, [&](auto const& widget) {
                    return widget.group && contains(panel.selected_widgets, widget.id);
                });
                if (ImGui::MenuItem("Ungroup selection", "Ctrl+Shift+G", false, grouped_selection)) {
                    panel.ungroupSelected();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Zoom to selection", nullptr, false, !panel.selected_widgets.empty())) {
                    zoomPanelToSelection(panel, available);
                }
                if (ImGui::MenuItem("Reset view")) {
                    panel.pan = {0, 0};
                    panel.zoom = 1;
                }
                ImGui::TextDisabled("Drag widget: move | Ctrl-click: select | Drag canvas: box select");
                ImGui::TextDisabled("Middle drag: pan | Wheel on canvas: zoom");
                ImGui::TextDisabled("Ctrl + middle drag: pan over widgets");
                ImGui::TextDisabled("Ctrl + wheel: zoom over widgets");
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        if (paste_position) {
            pastePanelWidgets(panel, *paste_position);
        }
        ImGui::End();
    }
    if (import_requested) {
        importPanel();
    }
}
