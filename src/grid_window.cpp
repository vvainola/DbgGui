#include "dbg_gui_internal.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "lua_script_editor.h"
#include "panel_slider.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr ImVec2 GRID_SCRIPT_EDITOR_SIZE = {420, 180};
constexpr ImVec4 RUNNING_BUTTON_GREEN = {0.15f, 0.5f, 0.2f, 1.0f};
constexpr ImVec4 RUNNING_BUTTON_HOVERED_LIGHT_GREEN = {0.2f, 0.6f, 0.25f, 1.0f};
constexpr ImVec4 RUNNING_BUTTON_PRESSED_DARK_GREEN = {0.1f, 0.4f, 0.15f, 1.0f};
constexpr ImU32 SCRIPT_ERROR_RED = IM_COL32(255, 64, 64, 255);

float gridCellFontHeight(float cell_height) {
    auto const& style = ImGui::GetStyle();
    return std::max(1.0f, cell_height - 2 * style.FramePadding.y - 2 * style.ItemSpacing.y);
}

void pushGridFontToFit(char const* text, float font_size, float width) {
    font_size = std::max(1.0f, font_size);
    ImGui::PushFont(ImGui::GetDefaultFont(), font_size);
    float const text_width = ImGui::CalcTextSize(text).x;
    if (text_width > width) {
        ImGui::PopFont();
        ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, font_size * width / text_width));
    }
}

void drawGridScriptErrorBorder() {
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                        ImGui::GetItemRectMax(),
                                        SCRIPT_ERROR_RED,
                                        ImGui::GetStyle().FrameRounding,
                                        ImDrawFlags_None,
                                        2.0f);
}

void showGridScriptError(std::string_view error) {
    if (error.empty()) {
        return;
    }
    auto const& style = ImGui::GetStyle();
    float const wrap_width = GRID_SCRIPT_EDITOR_SIZE.x - 2 * style.WindowPadding.x - style.ScrollbarSize;
    float const text_height = ImGui::CalcTextSize(error.data(), error.data() + error.size(), false, wrap_width).y;
    float const height = std::min(text_height, 4 * ImGui::GetTextLineHeight()) + 2 * style.WindowPadding.y;
    if (ImGui::BeginChild("script_error",
                          {GRID_SCRIPT_EDITOR_SIZE.x, height},
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoBackground)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
        ImGui::TextUnformatted(error.data(), error.data() + error.size());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void showGridActionEditor(GridActionScript& script) {
    inputScriptTextWithLineNumbers(script.draft, GRID_SCRIPT_EDITOR_SIZE, true);
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
    showGridScriptError(error);
}

} // namespace

void DbgGui::processGridScripts(double timestamp) {
    // Called with m_sampling_mutex held. Keep active actions alive while Lua
    // callbacks may release the mutex to register scalars on the GUI thread.
    std::erase_if(m_grid_scripts.actions, [](auto const& action) {
        auto state = action.lock();
        return !state || !state->running.load();
    });
    std::vector<std::shared_ptr<GridActionState>> actions;
    for (auto const& action : m_grid_scripts.actions) {
        if (auto state = action.lock()) {
            actions.push_back(std::move(state));
        }
    }
    for (auto const& action : actions) {
        action->process(timestamp);
    }
    // Multiple samples consume one LED frame request.
    if (!m_grid_scripts.scripts_requested.exchange(false)) {
        return;
    }
    std::erase_if(m_grid_scripts.led_scripts, [](auto const& script) { return script.expired(); });
    std::vector<std::shared_ptr<GridLedState>> scripts;
    for (auto const& script : m_grid_scripts.led_scripts) {
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
        auto published = std::make_shared<GridLedResult>();
        published->lit = result.value_or(false);
        if (!result) {
            published->error = result.error();
        }
        state->result.store(std::move(published));
    }
}

void DbgGui::renderButtonGrid(PanelWidget& widget, PanelWindow& panel) {
    auto& grid = widget.buttons;
    float const bottom_padding = 4 * panel.zoom;
    float const cell_height = std::max(1.0f,
                                       (ImGui::GetContentRegionAvail().y - bottom_padding) / grid.rows - 2 * ImGui::GetStyle().CellPadding.y);
    if (!ImGui::BeginTable("buttons", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    for (size_t index = 0; index < grid.cells.size(); ++index) {
        auto& cell = grid.cells[index];
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        bool const running = cell.action && cell.action->running.load();
        if (running) {
            ImGui::PushStyleColor(ImGuiCol_Button, RUNNING_BUTTON_GREEN);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RUNNING_BUTTON_HOVERED_LIGHT_GREEN);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, RUNNING_BUTTON_PRESSED_DARK_GREEN);
        }
        if (ImGui::Button((cell.label + "###action").c_str(), {-1, cell_height}) && !ImGui::GetIO().KeyCtrl) {
            if (running) {
                cell.action->stop_requested.store(true);
            } else {
                cell.action = std::make_shared<GridActionState>(cell.script, createLuaScriptHost());
                std::scoped_lock lock(m_sampling_mutex);
                m_grid_scripts.actions.push_back(cell.action);
            }
        }
        if (running) {
            ImGui::PopStyleColor(3);
        }
        if (cell.hasError()) {
            drawGridScriptErrorBorder();
        }
        if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
            panel.selectWidget(widget.id, true);
            // A selection click must not run the action if Ctrl is released first.
            ImGui::ClearActiveID();
        }
        bool const editor_open = ImGui::BeginPopupContextItem("button_settings");
        if (editor_open) {
            if (ImGui::IsWindowAppearing() && cell.syntax_error.empty()) {
                cell.draft = cell.script;
            }
            ImGui::InputText("Label", &cell.label);
            ImGui::SameLine();
            HelpMarker("Runs once when clicked. Clicking again stops the action. Lua waits use simulation time.");
            showGridActionEditor(cell);
            ImGui::EndPopup();
        }
        if (cell.editor_open && !editor_open && (cell.draft != cell.script || !cell.syntax_error.empty())) {
            cell.applyScript();
        }
        cell.editor_open = editor_open;
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::renderValueGrid(PanelWidget& widget) {
    auto& grid = widget.values;
    float const cell_height = std::max(1.0f,
                                       ImGui::GetContentRegionAvail().y / grid.rows - 2 * ImGui::GetStyle().CellPadding.y);
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
        char const* label = available ?
                              scalar->alias_and_group.c_str() :
                            cell.scalar_id ? "Unavailable scalar" :
                                             "Drop a scalar here";
        auto const& style = ImGui::GetStyle();
        float const font_height = gridCellFontHeight(cell_height);
        float const cell_width = ImGui::GetContentRegionAvail().x;
        // Split the cell height between its label and value.
        pushGridFontToFit(label, font_height * grid.text_to_value_ratio, cell_width);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        if (available && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &scalar->id, sizeof(scalar->id));
            ImGui::TextUnformatted(scalar->alias_and_group.c_str());
            ImGui::EndDragDropSource();
        }
        float const value_font_size = font_height * (1.0f - grid.text_to_value_ratio);
        if (available) {
            std::string const value = numberAsStr(scalar->getScaledValue());
            float const input_width = std::max(1.0f, cell_width - 2 * style.FramePadding.x);
            pushGridFontToFit(value.c_str(), value_font_size, input_width);
            addInputScalar(scalar->src, "##value", scalar->getScale(), scalar->getOffset(), scalar->read_only);
        } else {
            ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, value_font_size));
            ImGui::TextDisabled("-");
        }
        ImGui::PopFont();
        float const remaining = cell_height - (ImGui::GetCursorScreenPos().y - start.y);
        ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(0.0f, remaining)});
        ImGui::EndGroup();
        addPanelScalarBinding(cell.scalar_id);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::addPanelScalarDropTarget(std::optional<uint64_t>& scalar_id) {
    if (ImGui::BeginDragDropTarget()) {
        if (auto const* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
            if (payload->DataSize >= static_cast<int>(sizeof(uint64_t))) {
                scalar_id = *static_cast<uint64_t const*>(payload->Data);
            }
        }
        if (auto const* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL_MULTI")) {
            if (payload->DataSize >= static_cast<int>(sizeof(VariantSymbol*))) {
                auto* symbol = *static_cast<VariantSymbol* const*>(payload->Data);
                if (auto* added = addScalarSymbol(symbol, m_group_to_add_symbols)) {
                    scalar_id = added->id;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void DbgGui::addPanelScalarBinding(std::optional<uint64_t>& scalar_id) {
    Scalar* scalar = scalar_id ? findScalar(m_scalars, *scalar_id) : nullptr;
    bool const available = scalar && !scalar->deleted;
    addPanelScalarDropTarget(scalar_id);
    if (available) {
        if (addScalarContextMenu(scalar)) {
            scalar_id.reset();
        }
    } else if (ImGui::BeginPopupContextItem("value_settings")) {
        if (ImGui::MenuItem("Delete", nullptr, false, scalar_id.has_value())) {
            scalar_id.reset();
        }
        ImGui::EndPopup();
    }
}

void DbgGui::renderSliderGrid(PanelWidget& widget) {
    auto& grid = widget.sliders;
    float const cell_height = std::max(1.0f,
                                       ImGui::GetContentRegionAvail().y / grid.rows - 2 * ImGui::GetStyle().CellPadding.y);
    if (!ImGui::BeginTable("sliders", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    for (size_t index = 0; index < grid.cells.size(); ++index) {
        auto& slider = grid.cells[index];
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        ImVec2 const start = ImGui::GetCursorScreenPos();
        Scalar* scalar = slider.scalar_id ? findScalar(m_scalars, *slider.scalar_id) : nullptr;
        bool const available = scalar && !scalar->deleted;
        auto const& style = ImGui::GetStyle();
        float const font_height = gridCellFontHeight(cell_height);
        float const cell_width = ImGui::GetContentRegionAvail().x;
        char const* label = available ?
                              scalar->alias_and_group.c_str() :
                            slider.scalar_id ? "Unavailable scalar" :
                                               "Drop a scalar here";
        pushGridFontToFit(label, font_height * grid.text_to_value_ratio, cell_width);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        if (available && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &scalar->id, sizeof(scalar->id));
            ImGui::TextUnformatted(scalar->alias_and_group.c_str());
            ImGui::EndDragDropSource();
        }
        addPanelScalarBinding(slider.scalar_id);
        bool const valid_range = std::isfinite(slider.minimum) && std::isfinite(slider.maximum)
                              && slider.minimum < slider.maximum
                              && slider.minimum >= -std::numeric_limits<double>::max() / 2
                              && slider.maximum <= std::numeric_limits<double>::max() / 2;
        double const minimum = valid_range ? slider.minimum : 0;
        double const maximum = valid_range ? slider.maximum : 1;
        double value = available ? scalar->getScaledValue() : minimum;
        bool const writable = available && !scalar->read_only && scalar->getScale() != 0 && valid_range;
        float const input_width = std::max(1.0f, cell_width - 2 * style.FramePadding.x);
        pushGridFontToFit(numberAsStr(value).c_str(), font_height * (1.0f - grid.text_to_value_ratio), input_width);
        ImGui::BeginGroup();
        ImGui::BeginDisabled(!writable);
        ImGui::SetNextItemWidth(-1);
        if (panelSliderDouble("##slider", &value, minimum, maximum) && writable) {
            scalar->setScaledValue(value);
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::PopFont();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", label);
        }
        addPanelScalarDropTarget(slider.scalar_id);
        if (ImGui::BeginPopupContextItem("slider_settings")) {
            if (available && scalarContextMenuContents(scalar)) {
                slider.scalar_id.reset();
            }
            if (!available && ImGui::Button("Delete")) {
                slider.scalar_id.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Minimum");
            addInputScalar(&slider.minimum, "##minimum", 1, 0, false);
            ImGui::TextUnformatted("Maximum");
            addInputScalar(&slider.maximum, "##maximum", 1, 0, false);
            ImGui::EndPopup();
        }
        if (!valid_range) {
            ImGui::TextDisabled("Set a valid minimum and maximum");
        }
        float const remaining = cell_height - (ImGui::GetCursorScreenPos().y - start.y);
        ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(0.0f, remaining)});
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::renderCheckboxGrid(PanelWidget& widget, PanelWindow& panel) {
    auto& grid = widget.checkboxes;
    float const bottom_padding = 4 * panel.zoom;
    float const cell_height = std::max(1.0f,
                                       (ImGui::GetContentRegionAvail().y - bottom_padding) / grid.rows - 2 * ImGui::GetStyle().CellPadding.y);
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
            m_grid_scripts.actions.push_back(action);
        }
        if (checked) {
            ImGui::PopStyleColor(3);
        }
        if (cell.on_enable.hasError() || cell.on_disable.hasError()) {
            drawGridScriptErrorBorder();
        }
        if (cell.checked) {
            ImVec2 const min = ImGui::GetItemRectMin();
            ImVec2 const max = ImGui::GetItemRectMax();
            float const mark_size = std::min(ImGui::GetFontSize(), cell_height);
            ImGui::RenderCheckMark(ImGui::GetWindowDrawList(),
                                   {min.x + ImGui::GetStyle().FramePadding.x, (min.y + max.y - mark_size) * 0.5f},
                                   ImGui::GetColorU32(ImGuiCol_Text),
                                   mark_size);
        }
        if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
            panel.selectWidget(widget.id, true);
            ImGui::ClearActiveID();
        }
        bool const editor_open = ImGui::BeginPopupContextItem("checkbox_settings");
        if (editor_open) {
            if (ImGui::IsWindowAppearing()) {
                for (auto* script : {&cell.on_enable, &cell.on_disable}) {
                    if (script->syntax_error.empty()) {
                        script->draft = script->script;
                    }
                }
            }
            ImGui::InputText("Label", &cell.label);
            ImGui::SameLine();
            HelpMarker("Runs the selected transition script when checked or unchecked. Lua waits use simulation time.");
            if (ImGui::BeginTabBar("checkbox_scripts")) {
                if (ImGui::BeginTabItem("On enable")) {
                    showGridActionEditor(cell.on_enable);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("On disable")) {
                    showGridActionEditor(cell.on_disable);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndPopup();
        }
        if (cell.editor_open && !editor_open) {
            for (auto* script : {&cell.on_enable, &cell.on_disable}) {
                if (script->draft != script->script || !script->syntax_error.empty()) {
                    script->applyScript();
                }
            }
        }
        cell.editor_open = editor_open;
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DbgGui::renderLedGrid(PanelWidget& widget) {
    auto& grid = widget.leds;
    float const cell_height = std::max(1.0f,
                                       ImGui::GetContentRegionAvail().y / grid.rows - 2 * ImGui::GetStyle().CellPadding.y);
    if (!ImGui::BeginTable("leds", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        return;
    }
    for (size_t index = 0; index < grid.cells.size(); ++index) {
        auto& cell = grid.cells[index];
        if (!cell.state) {
            cell.configure(createLuaScriptHost());
            std::scoped_lock lock(m_sampling_mutex);
            m_grid_scripts.led_scripts.push_back(cell.state);
        }
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(index));
        ImVec2 const position = ImGui::GetCursorScreenPos();
        float const width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        float const diameter = std::max(1.0f, std::min(cell_height, width * 0.35f) - 4);
        ImVec2 const center = {position.x + diameter * 0.5f + 2, position.y + cell_height * 0.5f};
        ImGui::InvisibleButton("led", {width, cell_height});
        cell.state->requested.store(true);
        m_grid_scripts.scripts_requested.store(true);
        auto result = cell.state->result.load();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(center,
                              diameter * 0.5f,
                              result->lit ? ImGui::GetColorU32(cell.color) : IM_COL32(60, 60, 60, 255));
        if (!result->error.empty()) {
            draw->AddCircle(center, diameter * 0.5f, SCRIPT_ERROR_RED, 0, 2.0f);
        }
        float const label_x = position.x + diameter + 4 + ImGui::GetStyle().ItemInnerSpacing.x;
        float const label_width = std::max(1.0f, position.x + width - label_x);
        pushGridFontToFit(cell.label.c_str(), diameter * 0.5f, label_width);
        draw->AddText({label_x, center.y - ImGui::GetFontSize() * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), cell.label.c_str());
        ImGui::PopFont();

        bool apply = false;
        bool const editor_open = ImGui::BeginPopupContextItem("led_settings");
        if (editor_open) {
            if (ImGui::IsWindowAppearing()) {
                cell.draft = cell.condition;
            }
            ImGui::InputText("Label", &cell.label);
            ImGui::SameLine();
            ImGui::ColorEdit4("##lit_color", &cell.color.x, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine();
            HelpMarker("Return true to light the LED, false to turn it off");
            inputScriptTextWithLineNumbers(cell.draft, GRID_SCRIPT_EDITOR_SIZE, true);
            apply = ImGui::Button("Apply condition");
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
            showGridScriptError(result->error);
            ImGui::EndPopup();
        }
        if (apply || (cell.editor_open && !editor_open && cell.draft != cell.condition)) {
            cell.condition = cell.draft;
            cell.configure(createLuaScriptHost());
            std::scoped_lock lock(m_sampling_mutex);
            m_grid_scripts.led_scripts.push_back(cell.state);
        }
        cell.editor_open = editor_open;
        ImGui::PopID();
    }
    ImGui::EndTable();
}
