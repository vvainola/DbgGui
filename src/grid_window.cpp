#include "dbg_gui_internal.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "grid_slider.h"
#include "script_editor.h"
#include "magic_enum.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
void stopCellScripts(GridCell& cell) {
    // Lua runs on the sampling thread; retain its state and request cancellation.
    for (auto* script : {&cell.button.on_click,
                         &cell.checkbox.on_enable, &cell.checkbox.on_disable}) {
        if (script->action && script->action->running.load()) {
            script->action->stop_requested.store(true);
        }
    }
    if (cell.led.state) {
        cell.led.state->requested.store(false);
    }
}
} // namespace

GridCell GridCell::fromJson(nlohmann::json const& data) {
    GridCell cell;
    cell.type = data.value("type", GridCellType::Value);
    if (!magic_enum::enum_contains(cell.type)) {
        cell.type = GridCellType::Value;
    }
    switch (cell.type) {
        case GridCellType::Button:
            cell.button.label = data.value("label", cell.button.label);
            cell.button.on_click.script = data.value("script", cell.button.on_click.script);
            break;
        case GridCellType::Checkbox:
            cell.checkbox.label = data.value("label", cell.checkbox.label);
            cell.checkbox.on_enable.script = data.value("on_enable", std::string{});
            cell.checkbox.on_disable.script = data.value("on_disable", std::string{});
            break;
        case GridCellType::Value:
        case GridCellType::Slider: {
            if (data.contains("scalar_id") && !data["scalar_id"].is_null()) {
                cell.value.scalar_id = data["scalar_id"].get<uint64_t>();
            }
            cell.value.minimum = data.value("minimum", 0.0);
            cell.value.maximum = data.value("maximum", 1.0);
            break;
        }
        case GridCellType::LED: {
            cell.led.label = data.value("label", cell.led.label);
            cell.led.condition = data.value("script", cell.led.condition);
            auto const color = data.value("color", std::vector<float>{0.2f, 0.9f, 0.3f, 1});
            if (color.size() == 4) {
                cell.led.color = {color[0], color[1], color[2], color[3]};
            }
            break;
        }
    }
    return cell;
}

void GridCell::updateJson(nlohmann::json& data) const {
    data = {{"type", type}};
    switch (type) {
        case GridCellType::Button:
            data["label"] = button.label;
            data["script"] = button.on_click.script;
            break;
        case GridCellType::Checkbox:
            data["label"] = checkbox.label;
            data["on_enable"] = checkbox.on_enable.script;
            data["on_disable"] = checkbox.on_disable.script;
            break;
        case GridCellType::Value:
        case GridCellType::Slider: {
            if (value.scalar_id) {
                data["scalar_id"] = *value.scalar_id;
            }
            data["minimum"] = value.minimum;
            data["maximum"] = value.maximum;
            break;
        }
        case GridCellType::LED:
            data["label"] = led.label;
            data["script"] = led.condition;
            data["color"] = {led.color.x, led.color.y, led.color.z, led.color.w};
            break;
    }
}

GridWindow::GridWindow(std::string const& name, uint64_t id) : Window(name, id) {}

GridWindow::GridWindow(nlohmann::json const& data) : Window(data) {
    auto const& saved = data.at("grid");
    text_to_value_ratio = std::clamp(saved.value("text_to_value_ratio", 0.3f), 0.05f, 0.95f);
    resize(saved.value("rows", 1), saved.value("columns", 1));
    auto const saved_cells = saved.value("cells", nlohmann::json::array());
    for (size_t index = 0; index < saved_cells.size() && index < cells.size(); ++index) {
        cells[index] = GridCell::fromJson(saved_cells[index]);
    }
}

void GridWindow::updateJson(nlohmann::json& data) const {
    Window::updateJson(data);
    auto saved_cells = nlohmann::json::array();
    for (auto const& cell : cells) {
        nlohmann::json saved;
        cell.updateJson(saved);
        saved_cells.push_back(std::move(saved));
    }
    data["grid"] = {{"rows", rows}, {"columns", columns}, {"cells", std::move(saved_cells)},
                    {"text_to_value_ratio", text_to_value_ratio}};
}

GridWindow GridWindow::duplicate(uint64_t new_id) const {
    nlohmann::json data;
    updateJson(data);
    data["id"] = new_id;
    data["name"] = name + " copy";
    data["initial_focus"] = false;
    return GridWindow(data);
}

void GridWindow::resize(int new_rows, int new_columns) {
    new_rows = std::clamp(new_rows, 1, MAX_ROWS);
    new_columns = std::clamp(new_columns, 1, MAX_COLUMNS);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (row >= new_rows || column >= new_columns) {
                stopCellScripts(cells[row * MAX_COLUMNS + column]);
            }
        }
    }
    rows = new_rows;
    columns = new_columns;
    focus_cell.reset();
}

void GridWindow::setCellType(size_t index, GridCellType type) {
    if (!magic_enum::enum_contains(type)) {
        type = GridCellType::Value;
    }
    auto& cell = cells.at(index);
    if (cell.type == type) {
        return;
    }
    stopCellScripts(cell);
    cell.type = type;
    if (focus_cell == index && type != GridCellType::Value) {
        focus_cell.reset();
    }
}

std::optional<size_t> GridWindow::valueCellInDirection(size_t from, int row_delta, int column_delta) const {
    if (from >= cells.size() || (row_delta == 0 && column_delta == 0)) {
        return std::nullopt;
    }
    int row = static_cast<int>(from) / MAX_COLUMNS;
    int column = static_cast<int>(from) % MAX_COLUMNS;
    if (row >= rows || column >= columns) {
        return std::nullopt;
    }
    row += row_delta;
    column += column_delta;
    for (; row >= 0 && row < rows && column >= 0 && column < columns;
         row += row_delta, column += column_delta) {
        size_t const index = row * MAX_COLUMNS + column;
        if (cells[index].type == GridCellType::Value && cells[index].value.scalar_id) {
            return index;
        }
    }
    return std::nullopt;
}

void GridWindow::remapScalarId(uint64_t old_id, uint64_t new_id) {
    for (auto& cell : cells) {
        if (cell.value.scalar_id == old_id) {
            cell.value.scalar_id = new_id;
        }
    }
}

namespace {
constexpr ImVec2 GRID_SCRIPT_EDITOR_SIZE = {420, 180};
constexpr ImVec4 RUNNING_BUTTON_GREEN = {0.15f, 0.5f, 0.2f, 1.0f};
constexpr ImVec4 RUNNING_BUTTON_HOVERED_LIGHT_GREEN = {0.2f, 0.6f, 0.25f, 1.0f};
constexpr ImVec4 RUNNING_BUTTON_PRESSED_DARK_GREEN = {0.1f, 0.4f, 0.15f, 1.0f};
constexpr ImU32 SCRIPT_ERROR_RED = IM_COL32(255, 64, 64, 255);

bool editGridCellType(GridWindow& grid, size_t index, char const* label) {
    auto const type = grid.cells[index].type;
    bool changed = false;
    if (ImGui::BeginCombo(label, magic_enum::enum_name(type).data())) {
        for (auto const option : magic_enum::enum_values<GridCellType>()) {
            if (ImGui::Selectable(magic_enum::enum_name(option).data(), option == type) && option != type) {
                grid.setCellType(index, option);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void showCellTypeMenu(GridWindow& grid, size_t index) {
    if (editGridCellType(grid, index, "Type")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::Separator();
}

float gridCellFontHeight(float cell_height) {
    auto const& style = ImGui::GetStyle();
    return std::max(1.0f, cell_height - 2 * style.FramePadding.y - 2 * style.ItemSpacing.y);
}

void pushGridFontToFit(char const* text, float font_size, float width, float minimum_font_size = 1.0f) {
    font_size = std::max(minimum_font_size, font_size);
    ImGui::PushFont(ImGui::GetDefaultFont(), font_size);
    float const text_width = ImGui::CalcTextSize(text).x;
    if (text_width > width) {
        ImGui::PopFont();
        ImGui::PushFont(ImGui::GetDefaultFont(), std::max(minimum_font_size, font_size * width / text_width));
    }
}

char const* renderGridScalarLabel(Scalar const* scalar, bool bound, float font_size) {
    char const* label = scalar ? scalar->alias_and_group.c_str()
                     : bound ? "Unavailable scalar" : "Drop a scalar here";
    pushGridFontToFit(label, font_size, ImGui::GetContentRegionAvail().x, MIN_FONT_SIZE);
    if (!scalar) {
        ImGui::TextDisabled("%s", label);
    } else if (scalar->customScaleOrOffset()) {
        ImGui::TextColored(COLOR_LIGHT_BLUE, "%s", label);
    } else {
        ImGui::TextUnformatted(label);
    }
    ImGui::PopFont();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", label);
    }
    if (scalar && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &scalar->id, sizeof(scalar->id));
        ImGui::TextUnformatted(scalar->alias_and_group.c_str());
        ImGui::EndDragDropSource();
    }
    return label;
}

// Leave the button as the last item for the cell's context menu and check mark.
bool gridControlButton(char const* label, bool highlighted, bool has_error) {
    float const height = std::max(1.0f, ImGui::GetContentRegionAvail().y - 4);
    if (highlighted) {
        ImGui::PushStyleColor(ImGuiCol_Button, RUNNING_BUTTON_GREEN);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RUNNING_BUTTON_HOVERED_LIGHT_GREEN);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, RUNNING_BUTTON_PRESSED_DARK_GREEN);
    }
    bool const clicked = ImGui::Button(label, {-1, height});
    if (highlighted) {
        ImGui::PopStyleColor(3);
    }
    if (has_error) {
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                          ImGui::GetItemRectMax(),
                                          SCRIPT_ERROR_RED,
                                          ImGui::GetStyle().FrameRounding,
                                          ImDrawFlags_None,
                                          2.0f);
    }
    return clicked;
}

void showGridScriptError(std::string_view error, float width) {
    if (error.empty()) {
        return;
    }
    auto const& style = ImGui::GetStyle();
    float const wrap_width = width - 2 * style.WindowPadding.x - style.ScrollbarSize;
    float const text_height = ImGui::CalcTextSize(error.data(), error.data() + error.size(), false, wrap_width).y;
    float const height = std::min(text_height, 4 * ImGui::GetTextLineHeight()) + 2 * style.WindowPadding.y;
    if (ImGui::BeginChild("script_error",
                          {width, height},
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
    ImVec2 const editor_size = scriptEditorSizeToFit(script.editor.draft, GRID_SCRIPT_EDITOR_SIZE);
    inputScriptTextWithLineNumbers(script.editor.draft, editor_size, true);
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
    showGridScriptError(error, editor_size.x);
}

void editGridSettings(GridWindow& grid) {
    int rows = grid.rows;
    int columns = grid.columns;
    bool const rows_changed = ImGui::InputInt("Rows", &rows);
    bool const columns_changed = ImGui::InputInt("Columns", &columns);
    if (rows_changed || columns_changed) {
        grid.resize(rows, columns);
    }
    ImGui::SliderFloat("Text to value ratio", &grid.text_to_value_ratio,
                       0.05f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    float type_width = 0;
    for (auto const type : magic_enum::enum_values<GridCellType>()) {
        type_width = std::max(type_width, ImGui::CalcTextSize(magic_enum::enum_name(type).data()).x);
    }
    type_width += ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().FramePadding.x;
    if (ImGui::BeginTable("##GridCellTypes", grid.columns, ImGuiTableFlags_Borders)) {
        for (int row = 0; row < grid.rows; ++row) {
            ImGui::TableNextRow();
            for (int column = 0; column < grid.columns; ++column) {
                ImGui::TableSetColumnIndex(column);
                int const index = row * GridWindow::MAX_COLUMNS + column;
                auto const id = std::format("##Type{}", index);
                ImGui::SetNextItemWidth(type_width);
                editGridCellType(grid, index, id.c_str());
            }
        }
        ImGui::EndTable();
    }
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
        if (!state->requested.exchange(false) || !state->result.load()->has_value()) {
            continue;
        }
        state->result.store(std::make_shared<GridLedState::Result>(state->runner.evaluateLed(timestamp)));
    }
}

void DbgGui::renderButtonCell(GridWindow& grid, size_t index) {
    auto& cell = grid.cells[index].button;
    GridActionScript& on_click = cell.on_click;
    bool const running = on_click.action && on_click.action->running.load();
    if (gridControlButton((cell.label + "###action").c_str(), running, on_click.hasError())) {
        if (running) {
            on_click.action->stop_requested.store(true);
        } else {
            on_click.action = std::make_shared<GridActionState>(on_click.script, createLuaScriptHost());
            std::scoped_lock lock(m_sampling_mutex);
            m_grid_scripts.actions.push_back(on_click.action);
        }
    }
    bool const editor_open = ImGui::BeginPopupContextItem("button_settings");
    on_click.updateEditor(editor_open);
    if (editor_open) {
        showCellTypeMenu(grid, index);
        ImGui::InputText("Label", &cell.label);
        ImGui::SameLine();
        HelpMarker("Runs once when clicked. Clicking again stops the action. Lua waits use simulation time.");
        showGridActionEditor(on_click);
        ImGui::EndPopup();
    }
}

bool DbgGui::renderValueCell(GridWindow& grid, size_t index, bool focus_value) {
    auto& cell = grid.cells[index].value;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    ImGui::BeginGroup();
    ImVec2 const start = ImGui::GetCursorScreenPos();
    Scalar* scalar = cell.scalar_id ? findScalar(m_scalars, *cell.scalar_id) : nullptr;
    bool const available = scalar && !scalar->deleted;
    auto const& style = ImGui::GetStyle();
    float const font_height = gridCellFontHeight(cell_height);
    float const cell_width = ImGui::GetContentRegionAvail().x;
    renderGridScalarLabel(available ? scalar : nullptr, cell.scalar_id.has_value(), font_height * grid.text_to_value_ratio);
    float const value_font_size = std::max(float(MIN_FONT_SIZE), font_height * (1.0f - grid.text_to_value_ratio));
    bool input_focused = false;
    if (available) {
        std::string const value = numberAsStr(scalar->getScaledValue());
        float const input_width = std::max(1.0f, cell_width - 2 * style.FramePadding.x);
        pushGridFontToFit(value.c_str(), value_font_size, input_width, MIN_FONT_SIZE);
        if (focus_value) {
            ImGui::SetKeyboardFocusHere();
        }
        addInputScalar(scalar->src, "##value", scalar->getScale(), scalar->getOffset(), scalar->read_only);
        input_focused = ImGui::IsItemFocused();
    } else {
        ImGui::PushFont(ImGui::GetDefaultFont(), std::max(1.0f, value_font_size));
        ImGui::TextDisabled("-");
    }
    ImGui::PopFont();
    float const remaining = cell_height - (ImGui::GetCursorScreenPos().y - start.y);
    ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(0.0f, remaining)});
    ImGui::EndGroup();
    if (ImGui::IsItemHovered() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        cell.scalar_id.reset();
    }
    addGridScalarBinding(grid, index, cell.scalar_id);
    return input_focused;
}

void DbgGui::addGridScalarDropTarget(std::optional<uint64_t>& scalar_id) {
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

void DbgGui::addGridScalarBinding(GridWindow& grid, size_t index, std::optional<uint64_t>& scalar_id) {
    Scalar* scalar = scalar_id ? findScalar(m_scalars, *scalar_id) : nullptr;
    bool const available = scalar && !scalar->deleted;
    addGridScalarDropTarget(scalar_id);
    if (ImGui::BeginPopupContextItem("value_settings")) {
        showCellTypeMenu(grid, index);
        if (available) {
            if (scalarContextMenuContents(scalar)) {
                scalar_id.reset();
            }
        } else if (ImGui::MenuItem("Delete", nullptr, false, scalar_id.has_value())) {
            scalar_id.reset();
        }
        ImGui::EndPopup();
    }
}

void DbgGui::renderSliderCell(GridWindow& grid, size_t index) {
    auto& slider = grid.cells[index].value;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    ImVec2 const start = ImGui::GetCursorScreenPos();
    Scalar* scalar = slider.scalar_id ? findScalar(m_scalars, *slider.scalar_id) : nullptr;
    bool const available = scalar && !scalar->deleted;
    auto const& style = ImGui::GetStyle();
    float const font_height = gridCellFontHeight(cell_height);
    float const cell_width = ImGui::GetContentRegionAvail().x;
    char const* label = renderGridScalarLabel(available ? scalar : nullptr, slider.scalar_id.has_value(),
                                            font_height * grid.text_to_value_ratio);
    addGridScalarBinding(grid, index, slider.scalar_id);
    bool const valid_range = std::isfinite(slider.minimum) && std::isfinite(slider.maximum)
                          && slider.minimum < slider.maximum
                          && slider.minimum >= -std::numeric_limits<double>::max() / 2
                          && slider.maximum <= std::numeric_limits<double>::max() / 2;
    double const minimum = valid_range ? slider.minimum : 0;
    double const maximum = valid_range ? slider.maximum : 1;
    double value = available ? scalar->getScaledValue() : minimum;
    bool const writable = available && !scalar->read_only && scalar->getScale() != 0 && valid_range;
    float const input_width = std::max(1.0f, cell_width - 2 * style.FramePadding.x);
    pushGridFontToFit(numberAsStr(value).c_str(), font_height * (1.0f - grid.text_to_value_ratio), input_width, MIN_FONT_SIZE);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(!writable);
    ImGui::SetNextItemWidth(-1);
    if (gridSliderDouble("##slider", &value, minimum, maximum) && writable) {
        scalar->setScaledValue(value);
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::PopFont();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", label);
    }
    addGridScalarDropTarget(slider.scalar_id);
    if (ImGui::BeginPopupContextItem("slider_settings")) {
        showCellTypeMenu(grid, index);
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
}

void DbgGui::renderCheckboxCell(GridWindow& grid, size_t index) {
    auto& cell = grid.cells[index].checkbox;
    if (gridControlButton((cell.label + "###toggle").c_str(), cell.checked,
                          cell.on_enable.hasError() || cell.on_disable.hasError())) {
        auto action = cell.setChecked(!cell.checked, createLuaScriptHost());
        std::scoped_lock lock(m_sampling_mutex);
        m_grid_scripts.actions.push_back(action);
    }
    if (cell.checked) {
        ImVec2 const min = ImGui::GetItemRectMin();
        ImVec2 const max = ImGui::GetItemRectMax();
        float const mark_size = std::min(ImGui::GetFontSize(), max.y - min.y);
        ImGui::RenderCheckMark(ImGui::GetWindowDrawList(),
                               {min.x + ImGui::GetStyle().FramePadding.x, (min.y + max.y - mark_size) * 0.5f},
                               ImGui::GetColorU32(ImGuiCol_Text),
                               mark_size);
    }

    bool const editor_open = ImGui::BeginPopupContextItem("checkbox_settings");
    for (auto* script : {&cell.on_enable, &cell.on_disable}) {
        script->updateEditor(editor_open);
    }
    if (editor_open) {
        showCellTypeMenu(grid, index);
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
}

void DbgGui::renderLedCell(GridWindow& grid, size_t index) {
    auto& cell = grid.cells[index].led;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    if (!cell.state) {
        cell.configure(createLuaScriptHost());
        std::scoped_lock lock(m_sampling_mutex);
        m_grid_scripts.led_scripts.push_back(cell.state);
    }
    ImVec2 const position = ImGui::GetCursorScreenPos();
    float const width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    float const diameter = std::max(1.0f, std::min(cell_height, width * 0.35f) - 4);
    ImVec2 const center = {position.x + diameter * 0.5f + 2, position.y + cell_height * 0.5f};
    ImGui::InvisibleButton("led", {width, cell_height});
    cell.state->requested.store(true);
    m_grid_scripts.scripts_requested.store(true);
    GridLedState::Result const result = *cell.state->result.load();
    ImVec4 color = cell.color;
    char const* label = cell.label.c_str();
    if (result && result->appearance) {
        LuaLedAppearance const& appearance = *result->appearance;
        std::array<float, 4> const& rgba = appearance.color;
        color = {rgba[0], rgba[1], rgba[2], rgba[3]};
        label = appearance.text.c_str();
    }
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(center,
                          diameter * 0.5f,
                          result && result->lit ? ImGui::GetColorU32(color) : IM_COL32(60, 60, 60, 255));
    if (!result) {
        draw->AddCircle(center, diameter * 0.5f, SCRIPT_ERROR_RED, 0, 2.0f);
    }
    float const label_x = position.x + diameter + 4 + ImGui::GetStyle().ItemInnerSpacing.x;
    float const label_width = std::max(1.0f, position.x + width - label_x);
    pushGridFontToFit(label, diameter * 0.5f, label_width);
    draw->AddText({label_x, center.y - ImGui::GetFontSize() * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::PopFont();

    bool const editor_open = ImGui::BeginPopupContextItem("led_settings");
    bool apply = cell.editor.update(editor_open, cell.condition);
    if (editor_open) {
        showCellTypeMenu(grid, index);
        ImGui::InputText("Label", &cell.label);
        ImGui::SameLine();
        ImGui::ColorEdit4("##lit_color", &cell.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        HelpMarker("Return true or a nonzero number to light the LED using the configured label and color.\n"
                   "Return false or zero to turn it off.\n"
                   "Return two strings to set the text and color: return \"Tripped\", \"red\"\n"
                   "Colors (case insensitive): red, green, yellow, orange, blue, cyan, magenta, white, gray, black.");
        ImVec2 const editor_size = scriptEditorSizeToFit(cell.editor.draft, GRID_SCRIPT_EDITOR_SIZE);
        inputScriptTextWithLineNumbers(cell.editor.draft, editor_size, true);
        apply = ImGui::Button("Apply condition");
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        if (!result) {
            showGridScriptError(result.error(), editor_size.x);
        }
        ImGui::EndPopup();
    }
    if (apply) {
        cell.condition = cell.editor.draft;
        cell.configure(createLuaScriptHost());
        std::scoped_lock lock(m_sampling_mutex);
        m_grid_scripts.led_scripts.push_back(cell.state);
    }
}

void DbgGui::renderGrid(GridWindow& grid) {
    bool navigation_handled = false;
    float const cell_height = std::max(1.0f, ImGui::GetContentRegionAvail().y / grid.rows - 2 * ImGui::GetStyle().CellPadding.y);
    if (ImGui::BeginTable("grid", grid.columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        for (int row = 0; row < grid.rows; ++row) {
            for (int column = 0; column < grid.columns; ++column) {
                size_t const index = row * GridWindow::MAX_COLUMNS + column;
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::BeginChild("cell", {0, cell_height}, ImGuiChildFlags_Borders,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                    bool const focus = grid.focus_cell == index;
                    if (focus) {
                        grid.focus_cell.reset();
                    }
                    bool focused = false;
                    switch (grid.cells[index].type) {
                        case GridCellType::Value: focused = renderValueCell(grid, index, focus); break;
                        case GridCellType::Button: renderButtonCell(grid, index); break;
                        case GridCellType::Checkbox: renderCheckboxCell(grid, index); break;
                        case GridCellType::Slider: renderSliderCell(grid, index); break;
                        case GridCellType::LED: renderLedCell(grid, index); break;
                        default: break;
                    }
                    if (focused && !navigation_handled) {
                        int const row_delta = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
                        int const column_delta = ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -1 : 0;
                        navigation_handled = row_delta != 0 || column_delta != 0;
                        auto next = grid.valueCellInDirection(index, row_delta, column_delta);
                        while (next) {
                            auto* scalar = findScalar(m_scalars, *grid.cells[*next].value.scalar_id);
                            if (scalar && !scalar->deleted) {
                                grid.focus_cell = next;
                                break;
                            }
                            next = grid.valueCellInDirection(*next, row_delta, column_delta);
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void DbgGui::showGridWindows() {
    std::optional<GridWindow> duplicate;
    for (auto& window : m_grid_windows) {
        if (!window.open) {
            continue;
        }
        ImGui::SetNextWindowSize({360, 220}, ImGuiCond_FirstUseEver);
        window.focus.focused = ImGui::Begin(window.title().c_str(), nullptr);
        window.closeOnMiddleClick();
        ImGui::OpenPopupOnItemClick("grid_window_settings", ImGuiPopupFlags_MouseButtonRight);
        if (window.focus.focused && ImGui::Button("Menu")) {
            ImGui::OpenPopup("grid_window_settings");
        }
        if (ImGui::BeginPopup("grid_window_settings")) {
            ImGui::InputText("Name", &window.name);
            editGridSettings(window);
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate window")) {
                duplicate = window.duplicate(hashWithTime(window.name));
            }
            if (ImGui::MenuItem("Close window")) {
                window.open = false;
            }
            ImGui::EndPopup();
        }
        if (window.focus.focused && window.open) {
            renderGrid(window);
        }
        ImGui::End();
    }
    std::erase_if(m_grid_windows, [](auto const& window) { return !window.open; });
    if (duplicate) {
        m_grid_windows.push_back(std::move(*duplicate));
    }
}
