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
#include "imgui_stdlib.h"
#include "str_helpers.h"
#include "imgui_internal.h"
#include "themes.h"
#include "multi_select_helpers.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <fstream>
#include <string_view>
#include <span>

namespace {

int scriptLineCount(std::string_view text) {
    return static_cast<int>(std::ranges::count(text, '\n')) + 1;
}

float scriptLineNumberGutterWidth(int line_count) {
    ImGuiStyle const& style = ImGui::GetStyle();
    return ImGui::CalcTextSize(std::to_string(std::max(1, line_count)).c_str()).x + 2.0f * style.FramePadding.x;
}

void drawScriptLineNumberGutter(std::string const& text, ImVec2 min, ImVec2 max, float scroll_y) {
    ImGuiStyle const& style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 const text_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    float const line_height = ImGui::GetFontSize();
    int const line_count = scriptLineCount(text);

    draw_list->PushClipRect(min, max, true);
    for (int line = 1; line <= line_count; ++line) {
        // InputTextMultiline scrolls internally, so draw the gutter with the
        // same vertical scroll instead of making line numbers their own child.
        float const y = min.y + style.FramePadding.y + static_cast<float>(line - 1) * line_height - scroll_y;
        if (y + line_height < min.y) {
            continue;
        }
        if (y > max.y) {
            break;
        }
        std::string line_number = std::to_string(line);
        float const x = max.x - style.FramePadding.x - ImGui::CalcTextSize(line_number.c_str()).x;
        draw_list->AddText(ImVec2(x, y), text_color, line_number.c_str());
    }
    draw_list->PopClipRect();
}

ImGuiWindow* findChildWindowByChildId(ImGuiID child_id) {
    ImGuiContext& g = *GImGui;
    for (ImGuiWindow* window : g.Windows) {
        if (window->ChildId == child_id) {
            return window;
        }
    }
    return nullptr;
}

void inputScriptTextWithLineNumbers(std::string& text, ImVec2 size) {
    ImGuiID const input_id = ImGui::GetID("##source");
    float const gutter_width = scriptLineNumberGutterWidth(scriptLineCount(text));
    float const editor_width = std::max(1.0f, size.x - gutter_width);

    // Layout order is intentional:
    // 1. Reserve gutter space with Dummy().
    // 2. Submit InputTextMultiline() so ImGui updates its internal scroll state.
    // 3. Draw line numbers into the reserved gutter using that scroll offset.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
    ImVec2 const gutter_min = ImGui::GetCursorScreenPos();
    ImVec2 const gutter_size = ImVec2(gutter_width, size.y);
    ImGui::Dummy(gutter_size);
    ImVec2 const gutter_max = ImVec2(gutter_min.x + gutter_size.x, gutter_min.y + gutter_size.y);
    ImGui::SameLine();
    ImGui::InputTextMultiline("##source", &text, ImVec2(editor_width, size.y));
    // InputTextMultiline is implemented as a child window. Its ChildId is the
    // input ID, but its window ID is generated from the parent/window name.
    ImGuiWindow* input_window = findChildWindowByChildId(input_id);
    ImGuiInputTextState* input_state = ImGui::GetInputTextState(input_id);
    float const scroll_y = input_window ?
                             input_window->Scroll.y :
                           input_state ? input_state->Scroll.y :
                                         0.0f;
    drawScriptLineNumberGutter(text, gutter_min, gutter_max, scroll_y);
    ImGui::PopStyleVar();
}

void showRunningScriptWithLineNumbers(std::vector<std::string_view> const& lines, int current_line) {
    int const line_count = static_cast<int>(lines.size());
    float const gutter_width = scriptLineNumberGutterWidth(line_count);
    if (ImGui::BeginTable("##running_script_lines", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##line", ImGuiTableColumnFlags_WidthFixed, gutter_width);
        ImGui::TableSetupColumn("##source", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < line_count; ++i) {
            if (i == current_line) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::Separator();
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string line_number = std::to_string(i + 1);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gutter_width - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize(line_number.c_str()).x);
            ImGui::TextDisabled("%s", line_number.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(lines[i].data(), lines[i].data() + lines[i].size());
        }
        ImGui::EndTable();
    }
}

std::string getSymbolScaleStr(VariantSymbol& sym,
                              std::unordered_map<std::string, std::string> const& symbol_scale_settings) {
    auto it = symbol_scale_settings.find(sym.getFullName());
    return it == symbol_scale_settings.end() ? "1" : it->second;
}

std::optional<std::string> setSymbolScaleStr(VariantSymbol& sym,
                                             std::string const& scale,
                                             std::unordered_map<std::string, std::string>& symbol_scale_settings) {
    auto scale_value = str::evaluateExpression(scale);
    if (!scale_value.has_value()) {
        return scale_value.error();
    }

    if (scale_value.value() == 1) {
        symbol_scale_settings.erase(sym.getFullName());
    } else {
        symbol_scale_settings[sym.getFullName()] = scale;
    }
    return std::nullopt;
}

std::optional<std::string> addSymbolScaleInput(VariantSymbol& sym,
                                               std::vector<VariantSymbol*> const& selected_symbols,
                                               std::unordered_map<std::string, std::string>& symbol_scale_settings) {
    std::string scale = getSymbolScaleStr(sym, symbol_scale_settings);
    if (ImGui::InputText("Scale", &scale, ImGuiInputTextFlags_EnterReturnsTrue)) {
        // If the symbol is part of the selection, apply the scale to all selected symbols.
        if (contains(selected_symbols, &sym)) {
            for (VariantSymbol* selected : selected_symbols) {
                if (std::optional<std::string> error = setSymbolScaleStr(*selected, scale, symbol_scale_settings)) {
                    return error;
                }
            }
        } else if (std::optional<std::string> error = setSymbolScaleStr(sym, scale, symbol_scale_settings)) {
            return error;
        }
    }
    return std::nullopt;
}

std::vector<VariantSymbol*> buildSymbolSearchResults(DbgSymbols const& symbols,
                                                     std::string const& search_string,
                                                     int search_depth) {
    if (search_string.size() <= 2) {
        return {};
    }

    std::vector<VariantSymbol*> results = symbols.findMatchingSymbols(search_string, search_depth);
    std::vector<VariantSymbol*>::iterator begin_it = results.begin();
    // Don't sort first element if it is an exact match.
    if (!results.empty() && results[0]->getFullName() == search_string) {
        ++begin_it;
    }
    std::sort(begin_it, results.end(), [](VariantSymbol* l, VariantSymbol* r) {
        return l->getFullName() < r->getFullName();
    });
    return results;
}

} // namespace

std::optional<std::string> addScalarScaleInput(Scalar* scalar, std::vector<Scalar*> const& selected_scalars) {
    char buffer[1024];
    std::memcpy(buffer, scalar->getScaleStr().data(), scalar->getScaleStr().size());
    buffer[scalar->getScaleStr().size()] = '\0';
    if (ImGui::InputText("Scale", buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::expected<double, std::string> scale = str::evaluateExpression(buffer);
        if (scale.has_value()) {
            if (contains(selected_scalars, scalar)) {
                for (Scalar* s : selected_scalars) {
                    s->setScaleStr(buffer);
                }
            } else {
                scalar->setScaleStr(buffer);
            }
        } else {
            return scale.error();
        }
    }
    return std::nullopt;
}

std::optional<std::string> addScalarOffsetInput(Scalar* scalar, std::vector<Scalar*> const& selected_scalars) {
    char buffer[1024];
    std::memcpy(buffer, scalar->getOffsetStr().data(), scalar->getOffsetStr().size());
    buffer[scalar->getOffsetStr().size()] = '\0';
    if (ImGui::InputText("Offset", buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::expected<double, std::string> offset = str::evaluateExpression(buffer);
        if (offset.has_value()) {
            if (contains(selected_scalars, scalar)) {
                for (Scalar* s : selected_scalars) {
                    s->setOffsetStr(buffer);
                }
            } else {
                scalar->setOffsetStr(buffer);
            }
        } else {
            return offset.error();
        }
    }
    return std::nullopt;
}

std::string getSourceValueStr(ValueSource src) {
    return std::visit(
      [=](auto&& src) {
          using T = std::decay_t<decltype(src)>;
          if constexpr (std::is_same_v<T, ReadWriteFn>) {
              return numberAsStr(src(std::nullopt));
          } else if constexpr (std::is_same_v<T, ReadWriteFnCustomStr>) {
              return src(std::nullopt).first;
          } else {
              return numberAsStr(*src);
          }
      },
      src);
}

void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

int setCursorOnFirstNumberPress(ImGuiInputTextCallbackData* data) {
    ImGuiKey* pressed_key = (ImGuiKey*)data->UserData;
    if (*pressed_key == ImGuiKey_None) {
        return 0;
    }
    // Minus is handled separately because its name is "Minus" instead of "-"
    std::string key_name;
    if (*pressed_key == ImGuiKey_Minus) {
        key_name = "-";
    } else {
        key_name = ImGui::GetKeyName(*pressed_key);
    }
    if (key_name.starts_with("Keypad")) {
        key_name = key_name[6];
    }
    // Clear text edit and set cursor after first character
    strncpy(data->Buf, key_name.c_str(), 20);
    data->Buf[19] = '\0';
    data->BufTextLen = 1;
    data->BufDirty = 1;
    data->CursorPos = 1;
    data->SelectionStart = 1;
    data->SelectionEnd = 1;
    *pressed_key = ImGuiKey_None;
    return 0;
}

std::optional<ImGuiKey> pressedNumber() {
    for (int key = ImGuiKey_0; key <= ImGuiKey_9; key++) {
        if (ImGui::IsKeyPressed(ImGuiKey(key))) {
            return ImGuiKey(key);
        }
    }
    for (int key = ImGuiKey_Keypad0; key <= ImGuiKey_Keypad9; key++) {
        if (ImGui::IsKeyPressed(ImGuiKey(key))) {
            return ImGuiKey(key);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus)) {
        return ImGuiKey_Minus;
    }
    return std::nullopt;
}

void addReadonlyScalar(ValueSource const& value_src, double scale = 1, double offset = 0) {
    std::string value_str;
    if (std::get_if<ReadWriteFnCustomStr>(&value_src)) {
        value_str = getSourceValueStr(value_src);
    } else {
        value_str = numberAsStr(getSourceValue(value_src) * scale + offset);
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 text_size = ImGui::CalcTextSize(value_str.c_str());
    if (available.x < text_size.x) {
        float current_font_size = ImGui::GetFontSize();
        float font_size = std::max(current_font_size * (available.x / text_size.x) - 1, 1.0f);
        ImGui::PushFont(ImGui::GetDefaultFont(), font_size);
        ImGui::Text(value_str.c_str());
        ImGui::PopFont();
    } else {
        ImGui::Text(value_str.c_str());
    }
}

void addInputScalar(ValueSource const& value_src,
                    std::string const& label,
                    double scale = 1,
                    double offset = 0,
                    bool read_only = false) {
    if (read_only) {
        addReadonlyScalar(value_src, scale, offset);
        return;
    }

    if (std::get_if<ReadWriteFnCustomStr>(&value_src)) {
        // Scale text to fit
        ImVec2 available = ImGui::GetContentRegionAvail();
        std::string value_str = getSourceValueStr(value_src);
        ImVec2 text_size = ImGui::CalcTextSize(value_str.c_str());
        if (available.x < text_size.x) {
            float current_font_size = ImGui::GetFontSize();
            float font_size = std::max(current_font_size * (available.x / text_size.x) - 1, 1.0f);
            ImGui::PushFont(ImGui::GetDefaultFont(), font_size);
            ImGui::Text(value_str.c_str());
            ImGui::PopFont();
        } else {
            ImGui::Text(value_str.c_str());
        }
        ImGui::SameLine();
    }

    ImGuiInputTextFlags edit_flags = ImGuiInputTextFlags_EnterReturnsTrue
                                   | ImGuiInputTextFlags_AutoSelectAll
                                   | ImGuiInputTextFlags_CharsScientific
                                   | ImGuiInputTextFlags_CallbackAlways;
    double scaled_value = getSourceValue(value_src) * scale + offset;
    char value[20];
    strncpy(value, numberAsStr(scaled_value).c_str(), 20);
    value[19] = '\0';
    ImGui::SetNextItemWidth(-FLT_MIN);
    static ImGuiKey pressed_number = ImGuiKey_None;
    if (ImGui::InputText(label.c_str(), value, sizeof(value), edit_flags, setCursorOnFirstNumberPress, (void*)&pressed_number)) {
        try {
            setSourceValue(value_src, (std::stod(value) - offset) / scale);
        } catch (std::exception const& err) {
            std::cerr << err.what() << std::endl;
        }
    };
    // When number is pressed and item is not already edited, move keyboard focus, write the pressed number and set cursor
    // after the number. This way input can be immediately edited by just typing numbers
    std::optional<ImGuiKey> number_pressed = pressedNumber();
    if (ImGui::IsItemFocused() && !ImGui::IsItemActive() && number_pressed) {
        ImGui::SetKeyboardFocusHere(-1);
        // The pressed number is global between all input fields so the value is set here for next frame and the cursor
        // editing callback will reset it
        pressed_number = *number_pressed;
    }
}

void DbgGui::addScalarContextMenu(Scalar* scalar, bool show_delete) {
    if (ImGui::BeginPopupContextItem((scalar->name_and_group + "_context_menu").c_str())) {
        double pause_level = scalar->getScaledValue();
        if (ImGui::InputDouble("Trigger level", &pause_level, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_pause_triggers.push_back(PauseTrigger(scalar, pause_level));
            ImGui::CloseCurrentPopup();
        }
        if (std::optional<std::string> error = addScalarScaleInput(scalar, m_selected_scalars)) {
            m_error_message = *error;
        }
        if (std::optional<std::string> error = addScalarOffsetInput(scalar, m_selected_scalars)) {
            m_error_message = *error;
        }

        if (ImGui::Button("Copy name")) {
            ImGui::SetClipboardText(scalar->name.c_str());
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Copy alias")) {
            ImGui::SetClipboardText(scalar->alias.c_str());
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Copy alias and value")) {
            ImGui::SetClipboardText((scalar->alias + " " + numberAsStr(scalar->getScaledValue())).c_str());
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::InputText("Alias##scalar_context_menu", &scalar->alias)) {
            if (scalar->alias.empty()) {
                scalar->alias = scalar->name;
            }
            scalar->alias_and_group = scalar->alias + " (" + scalar->group + ")";
        }
        if (show_delete) {
            ImGui::Separator();
            if (ImGui::Button("Delete")) {
                if (contains(m_selected_scalars, scalar)) {
                    for (Scalar* selected_scalar : m_selected_scalars) {
                        selected_scalar->deleted = true;
                    }
                    m_selected_scalars.clear();
                } else {
                    scalar->deleted = true;
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

double getSymbolScale(VariantSymbol& sym,
                      std::unordered_map<std::string, std::string> const& symbol_scale_settings) {
    auto scale = str::evaluateExpression(getSymbolScaleStr(sym, symbol_scale_settings));
    return scale.has_value() ? scale.value() : 1;
}

void DbgGui::addSymbolContextMenu(VariantSymbol& sym) {
    std::string full_name = sym.getFullName();
    if (ImGui::BeginPopupContextItem((full_name + "_context_menu").c_str())) {
        bool arithmetic_or_enum = sym.getType() == VariantSymbol::Type::Arithmetic
                               || sym.getType() == VariantSymbol::Type::Enum;
        if (arithmetic_or_enum) {
            if (std::optional<std::string> error = addSymbolScaleInput(sym, m_selected_symbols, m_symbol_scale_settings)) {
                m_error_message = *error;
            }
        }
        bool can_fold_children = !sym.getChildren().empty()
                              || (sym.getType() == VariantSymbol::Type::Pointer && sym.getPointedSymbol() != nullptr);
        std::function<void(VariantSymbol&, bool, std::set<VariantSymbol*>&)> fold_all =
          [&](VariantSymbol& symbol, bool opened, std::set<VariantSymbol*>& visiting) {
              // Pointer expansion can create cycles, e.g. a node pointing back to an ancestor.
              if (visiting.contains(&symbol)) {
                  return;
              }
              visiting.insert(&symbol);
              symbol.opened_manually = opened;
              if (symbol.getType() == VariantSymbol::Type::Pointer) {
                  if (VariantSymbol* pointed_symbol = symbol.getPointedSymbol()) {
                      fold_all(*pointed_symbol, opened, visiting);
                  }
              } else {
                  for (std::unique_ptr<VariantSymbol>& child : symbol.getChildren()) {
                      fold_all(*child, opened, visiting);
                  }
              }
              visiting.erase(&symbol);
          };
        if (can_fold_children) {
            ImGui::Separator();
            if (ImGui::Button("Unfold all")) {
                std::set<VariantSymbol*> visiting;
                fold_all(sym, true, visiting);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Fold all")) {
                std::set<VariantSymbol*> visiting;
                fold_all(sym, false, visiting);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::Button("Copy name")) {
            ImGui::SetClipboardText(full_name.c_str());
            ImGui::CloseCurrentPopup();
        } else if (!m_hidden_symbols.contains(full_name) && ImGui::Button("Hide")) {
            m_hidden_symbols.insert(full_name);
            m_settings["hidden_symbols"].push_back(full_name);
            ImGui::CloseCurrentPopup();
        } else if (m_hidden_symbols.contains(full_name) && ImGui::Button("Unhide")) {
            m_hidden_symbols.erase(full_name);
            for (int i = 0; i < m_settings["hidden_symbols"].size(); ++i) {
                if (m_settings["hidden_symbols"][i] == full_name) {
                    m_settings["hidden_symbols"].erase(i);
                    break;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::optional<DockSpace> getDockSpace(std::vector<DockSpace>& dockspaces, ImGuiID id) {
    for (int i = 0; i < dockspaces.size(); ++i) {
        if (dockspaces[i].dock_id == id) {
            return dockspaces[i];
        }
    }
    return std::nullopt;
}

std::optional<DockSpace> getDockSpace(std::vector<DockSpace> const& dockspaces, std::string const& name) {
    for (int i = 0; i < dockspaces.size(); ++i) {
        if (dockspaces[i].title() == name) {
            return dockspaces[i];
        }
    }
    return std::nullopt;
}

void DbgGui::showDockSpaces() {
    std::vector<DockSpace> dockspaces_temp;

    std::function<void(ImGuiDockNode*)> moveDockSpaceToEnd = [&](ImGuiDockNode* node) {
        if (node == nullptr) {
            return;
        }
        // Move this node
        std::optional<DockSpace> dockspace = getDockSpace(dockspaces_temp, node->ID);
        if (dockspace) {
            remove(dockspaces_temp, *dockspace);
            dockspaces_temp.push_back(*dockspace);
        }

        // Move child windows if there is a dockspace that matches window name
        for (int j = 0; j < node->Windows.Size; ++j) {
            ImGuiWindow* window = node->Windows.Data[j];
            dockspace = getDockSpace(dockspaces_temp, window->Name);
            if (dockspace) {
                remove(dockspaces_temp, *dockspace);
                dockspaces_temp.push_back(*dockspace);
                // Move all child nodes within the window
                ImGuiDockNode* child_node = ImGui::DockBuilderGetNode(dockspace->dock_id);
                moveDockSpaceToEnd(child_node);
            }
        }

        // Recurse child nodes
        for (int j = 0; j < 2; ++j) {
            moveDockSpaceToEnd(node->ChildNodes[j]);
        }
    };

    for (int i = 0; i < m_dockspaces.size(); ++i) {
        DockSpace& dockspace = m_dockspaces[i];
        if (!dockspace.open) {
            continue;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEAL);
        dockspace.focus.focused = ImGui::Begin(dockspace.title().c_str(), NULL);
        ImGui::PopStyleColor();
        // If window is being dragged, move it to end of the list of dockspaces so that it can
        // be docked into other dockspace
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            assert(dockspace.dock_id != 0);
            dockspaces_temp = m_dockspaces;
            moveDockSpaceToEnd(ImGui::DockBuilderGetNode(dockspace.dock_id));
        }
        dockspace.closeOnMiddleClick();
        dockspace.contextMenu();

        // Create dockspace
        dockspace.dock_id = ImGui::GetID(std::format("Dockspace_{}", dockspace.id).c_str());
        ImGui::DockSpace(dockspace.dock_id, ImVec2(0, 0), ImGuiDockNodeFlags_EvenSplit * dockspace.even_split);

        ImGui::End();
    }
    if (!dockspaces_temp.empty()) {
        m_dockspaces = dockspaces_temp;
    }
}

void DbgGui::showMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        ImGui::Text("Time %.3f s", m_plot_timestamp);
        ImGui::SameLine();
        ImGui::Separator();

        if (ImGui::BeginMenu("Menu")) {

            // Pause after
            ImGui::PushItemWidth(ImGui::CalcTextSize("XXXXXXXXXXXXX").x);
            double pause_after = std::max(m_pause_at_time - m_sample_timestamp, 0.0);
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide)) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputScalar("Pause after", ImGuiDataType_Double, &pause_after, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific)) {
                m_pause_at_time = m_sample_timestamp + pause_after;
            }
            ImGui::SameLine();
            HelpMarker("Pause after x seconds. Hotkey is \"numpad /\"");

            // Pause at
            ImGui::PushItemWidth(ImGui::CalcTextSize("XXXXXXXXXXXXX").x);
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply)) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputScalar("Pause at", ImGuiDataType_Double, &m_pause_at_time, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific);
            ImGui::SameLine();
            HelpMarker("Pause at given time. Hotkey is \"numpad *\"");

            if (ImGui::Button("Add..")) {
                ImGui::OpenPopup("##Add");
            }
            if (ImGui::BeginPopup("##Add")) {
                // Scalar plot
                if (ImGui::Button("Scalar plot")) {
                    ImGui::OpenPopup(str::ADD_SCALAR_PLOT);
                }
                ImGui::SameLine();
                HelpMarker("Hotkey to add new scalar plot is ctrl+shift+1");
                addPopupModal(str::ADD_SCALAR_PLOT);

                // Vector plot
                if (ImGui::Button("Vector plot")) {
                    ImGui::OpenPopup(str::ADD_VECTOR_PLOT);
                }
                ImGui::SameLine();
                HelpMarker("Hotkey to add new vector plot is ctrl+shift+2");
                addPopupModal(str::ADD_VECTOR_PLOT);

                // Spectrum plot
                if (ImGui::Button("Spectrum plot")) {
                    ImGui::OpenPopup(str::ADD_SPECTRUM_PLOT);
                }
                ImGui::SameLine();
                HelpMarker("Hotkey to add new spectrum plot is ctrl+shift+3");
                addPopupModal(str::ADD_SPECTRUM_PLOT);

                // Custom window
                if (ImGui::Button("Custom window")) {
                    ImGui::OpenPopup(str::ADD_CUSTOM_WINDOW);
                }
                ImGui::SameLine();
                HelpMarker("Hotkey to add new custom window is ctrl+shift+4");
                addPopupModal(str::ADD_CUSTOM_WINDOW);

                // Script window
                if (ImGui::Button("Script window")) {
                    ImGui::OpenPopup(str::ADD_SCRIPT_WINDOW);
                }
                ImGui::SameLine();
                HelpMarker("Hotkey to add new script window is ctrl+shift+5");
                addPopupModal(str::ADD_SCRIPT_WINDOW);

                // Grid window
                if (ImGui::Button("Grid window")) {
                    ImGui::OpenPopup(str::ADD_GRID_WINDOW);
                }
                addPopupModal(str::ADD_GRID_WINDOW);

                // Dockspace
                if (ImGui::Button("Dockspace")) {
                    ImGui::OpenPopup(str::ADD_DOCKSPACE);
                }
                ImGui::SameLine();
                HelpMarker("Dockspaces are empty windows to which other windows can be docked to create nested tabs.");
                addPopupModal(str::ADD_DOCKSPACE);

                // End "Add.." popup
                ImGui::EndPopup();
            }

            if (ImGui::Button("Save all plots as csv")) {
                std::vector<Scalar*> scalars;
                for (auto const& scalar : m_scalars) {
                    scalars.push_back(scalar.get());
                }
                saveScalarsAsCsv(getFilenameToSave(), scalars, m_linked_scalar_x_axis_limits);
            }

            if (ImGui::Button("Save snapshot")) {
                saveSnapshot();
            }
            ImGui::SameLine();
            HelpMarker("Save snapshot of global variables to restore the same values later. Hotkey is ctrl+S");
            ImGui::SameLine();
            if (ImGui::Button("Load snapshot")) {
                loadSnapshot();
            }
            ImGui::SameLine();
            HelpMarker("Load values of global variables from previously saved snapshot. Hotkey is ctrl+R");
            ImGui::Separator();

            // Options
            ImGui::Text("Options");
            ImGui::Checkbox("Link scalar x-axis", &m_options.link_scalar_x_axis);

            ImGui::Checkbox("Scalar plot tooltip", &m_options.scalar_plot_tooltip);
            ImGui::SameLine();
            HelpMarker("Show vertical line containing the values of the signals when hovering over scalar plot.");

            ImGui::Checkbox("Pause on close", &m_options.pause_on_close);
            ImGui::SameLine();
            HelpMarker("Pause when GUI is requested to close programmatically. Pressing start again will close the GUI.");

            ImGui::Checkbox("Show latest message on main menu bar", &m_options.show_latest_message_on_main_menu_bar);
            ImGui::Checkbox("Show vertical line in all plots", &m_options.show_vertical_line_in_all_plots);

            // Theme
            themeCombo(m_options.theme, m_window);

            static int new_buffer_size = m_options.sampling_buffer_size;
            if (ImGui::InputInt("Sampling buffer size", &new_buffer_size, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::scoped_lock<std::mutex> lock(m_sampling_mutex);
                m_options.sampling_buffer_size = new_buffer_size;
                m_sampler.setBufferSize(new_buffer_size);
            }

            if (ImGui::InputInt("Font size", &m_options.font_size, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_options.font_size = std::clamp((int)m_options.font_size, MIN_FONT_SIZE, MAX_FONT_SIZE - 1);
                ImGui::GetStyle()._NextFrameFontSizeBase = m_options.font_size;
            }

            ImGui::Separator();

            const char* env = std::getenv(USER_SETTINGS_LOCATION);
            if (env) {
                std::string settings_dir = std::format("{}/.dbg_gui/", env);
                if (ImGui::Button("Save settings")) {
                    std::string out_path = getFilenameToSave("json", settings_dir);
                    if (!out_path.empty()) {
                        std::ofstream(out_path) << std::setw(4) << m_settings;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load settings")) {
                    std::string out_path = getFilenameToOpen("json", settings_dir);
                    if (!out_path.empty()) {
                        // Overwrite existing settings. The file will be reloaded in updateSavedSettings
                        std::string settings_path = std::format("{}/settings.json", settings_dir);
                        std::filesystem::copy_file(out_path, settings_path, std::filesystem::copy_options::overwrite_existing);
                    }
                }
            }

            if (ImGui::Button("Clear saved settings") && ImGui::GetIO().KeyCtrl) {
                m_clear_saved_settings = true;
            }
            ImGui::SameLine();
            HelpMarker("Requires ctrl-click. Rewrite settings to contain only the current configuration. Removes all non-existing symbols and options.");

            ImGui::EndMenu();
        }
        ImGui::Separator();

        // Start stop
        const char* start_stop_text = m_paused ? "Start" : "Pause";
        if (ImGui::Button(start_stop_text)) {
            m_paused = !m_paused;
        }
        HelpMarker("Hotkey for start/pause is space. Shift+space advances one step. Hold shift+space to advance very slowly.");
        ImGui::SameLine();
        ImGui::Separator();

        // Simulation speed
        ImGui::PushItemWidth(ImGui::CalcTextSize("Simulation speed XXXXXXX").x);
        ImGui::SliderFloat("##Simulation speed", &m_simulation_speed, 1e-4f, 10, "Simulation speed %.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        ImGui::SameLine();
        HelpMarker("Simulated speed relative to real time. Hotkey to double speed is \"numpad +\" and halve \"numpad -\".");
        ImGui::SameLine();
        ImGui::Separator();

        if (m_pause_at_time > m_sample_timestamp + std::numeric_limits<double>::epsilon()) {
            ImGui::Text("Pausing after %g", m_pause_at_time - m_plot_timestamp);
            ImGui::Separator();
        }

        if (m_options.show_latest_message_on_main_menu_bar
            && !m_message_queue.empty()) {
            ImGui::Text(m_message_queue.back().c_str());
            if (ImGui::IsItemHovered()) {
                std::string m;
                for (std::string const& msg : m_message_queue) {
                    m += msg;
                }
                ImGui::SetTooltip(m.c_str());
            }
        }

        ImGui::EndMainMenuBar();
    }
}

void DbgGui::showLogWindow() {
    m_window_focus.log.focused = ImGui::Begin("Log", NULL, ImGuiWindowFlags_NoNavFocus);
    if (!m_window_focus.log.focused) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(m_all_messages.c_str());
    // Autoscroll
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::End();
}

void DbgGui::showScalarWindow() {
    m_window_focus.scalars.focused = ImGui::Begin("Scalars", NULL, ImGuiWindowFlags_NoNavFocus);
    if (!m_window_focus.scalars.focused) {
        ImGui::End();
        return;
    }
    static std::string scalar_name_filter;
    ImGui::InputText("Filter", &scalar_name_filter);

    if (ImGui::BeginTable("scalar_table",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, num_width);

        ImGuiMultiSelectFlags ms_flags = ImGuiMultiSelectFlags_ClearOnEscape
                                       | ImGuiMultiSelectFlags_BoxSelect2d
                                       | ImGuiMultiSelectFlags_ScopeRect;
        ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags,
                                                            (int)m_selected_scalars.size(),
                                                            -1);
        applyMultiSelectRequests(ms_io, m_selected_scalars, m_visible_scalars);
        m_visible_scalars.clear();

        std::function<void(SignalGroup<Scalar>&, bool)> show_scalar_group = [&](SignalGroup<Scalar>& group, bool delete_entire_group) {
            std::vector<Scalar*> const& scalars = group.signals;
            // Do not show group if there are no visible items in it
            if (!group.hasVisibleItems(scalar_name_filter)) {
                return;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Group has to be opened automatically if signal in it matches the filter.
            // If there is no filter, then it should be kept open if it has been opened
            // manually before.
            if (!scalar_name_filter.empty()) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            } else {
                ImGui::SetNextItemOpen(group.opened_manually, ImGuiCond_Always);
            }
            bool group_opened = ImGui::TreeNode(group.name.c_str());
            if (scalar_name_filter.empty()) {
                group.opened_manually = group_opened;
            }

            // Right click context menu
            if (ImGui::BeginPopupContextItem((group.name + "_context_menu").c_str())) {
                std::function<void(SignalGroup<Scalar>&, bool)> fold_all = [&](SignalGroup<Scalar>& group, bool fold) {
                    group.opened_manually = fold;
                    for (auto& subgroup : group.subgroups) {
                        fold_all(subgroup.second, fold);
                    }
                };
                if (ImGui::Button("Unfold all")) {
                    fold_all(group, true);
                }
                if (ImGui::Button("Fold all")) {
                    fold_all(group, false);
                }
                ImGui::EndPopup();
            }

            // Symbols can be dragged from one group to another for easier reorganizing if symbol
            // is initially added to wrong group
            if (ImGui::BeginDragDropTarget()) {
                auto move_scalar_to_group = [&](Scalar* scalar) {
                    if (scalar->group != group.full_name) {
                        VariantSymbol* scalar_symbol = m_symbols.getSymbol(scalar->name);
                        if (scalar_symbol) {
                            Scalar* new_scalar = addScalarSymbol(scalar_symbol, group.full_name);
                            new_scalar->alias = scalar->alias;
                            new_scalar->setScaleStr(scalar->getScaleStr());
                            new_scalar->setOffsetStr(scalar->getOffsetStr());
                            scalar->deleted = true;
                            if (m_sampler.isScalarSampled(scalar)) {
                                m_sampler.startSampling(new_scalar);
                                m_sampler.copySamples(*scalar, *new_scalar);
                            }
                            scalar->replacement = new_scalar;
                        }
                    }
                };
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
                    std::span<uint64_t> ids(reinterpret_cast<uint64_t*>(payload->Data),
                                            payload->DataSize / sizeof(uint64_t));
                    for (uint64_t id : ids) {
                        Scalar* scalar = findScalar(m_scalars, id);
                        if (scalar) {
                            move_scalar_to_group(scalar);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Show values inside the group
            if (group_opened) {
                if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_Delete)) {
                    delete_entire_group = true;
                }

                // Show subgroups first
                for (auto& subgroup : group.subgroups) {
                    show_scalar_group(subgroup.second, delete_entire_group);
                }

                // All signals in a group are shown if the group name matches filter
                bool group_matches_filter = str::fuzzy_match(scalar_name_filter.c_str(), group.full_name.c_str());
                // Show each scalar
                for (Scalar* scalar : scalars) {
                    bool hide_by_filter = !scalar_name_filter.empty()
                                       && !str::fuzzy_match(scalar_name_filter.c_str(), scalar->alias.c_str())
                                       && !group_matches_filter;
                    if (scalar->hide_from_scalars_window || hide_by_filter) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    // Render as a leaf tree node so the multi-select API can manage selection.
                    bool const selected = contains(m_selected_scalars, scalar);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                             | ImGuiTreeNodeFlags_SpanAvailWidth
                                             | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)m_visible_scalars.size());
                    if (scalar->customScaleOrOffset()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, COLOR_LIGHT_BLUE);
                        ImGui::TreeNodeEx(scalar->alias.c_str(), flags);
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TreeNodeEx(scalar->alias.c_str(), flags);
                    }
                    m_visible_scalars.push_back(scalar);

                    // Drag-and-drop: carry all selected scalars if this one is selected,
                    // otherwise just the dragged scalar.
                    if (ImGui::BeginDragDropSource()) {
                        std::vector<uint64_t> ids;
                        if (contains(m_selected_scalars, scalar)) {
                            for (Scalar* s : m_selected_scalars) {
                                ids.push_back(s->id);
                            }
                        } else {
                            ids.push_back(scalar->id);
                        }
                        ImGui::SetDragDropPayload("SCALAR_ID_MULTI",
                                                  ids.data(),
                                                  ids.size() * sizeof(uint64_t));
                        if (ids.size() == 1) {
                            ImGui::Text("Drag to plot");
                        } else {
                            ImGui::Text("Drag %d scalars", (int)ids.size());
                        }
                        ImGui::EndDragDropSource();
                    }

                    // Mark signal as deleted
                    if ((ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_Delete))) {
                        scalar->deleted = true;
                    }
                    addScalarContextMenu(scalar, true);

                    // Show value
                    ImGui::TableNextColumn();
                    addInputScalar(scalar->src,
                                   "##scalar_" + scalar->name_and_group,
                                   scalar->getScale(),
                                   scalar->getOffset(),
                                   scalar->read_only);
                }

                ImGui::TreePop();
            }

            // If group is marked as deleted, all signals in it and all its subgroups must also be marked as deleted
            // even if this group is not open
            if (delete_entire_group) {
                for (Scalar* scalar : scalars) {
                    scalar->deleted = true;
                }
                for (auto& subgroup : group.subgroups) {
                    show_scalar_group(subgroup.second, delete_entire_group);
                }
                group.subgroups.clear();
            }
        };

        for (auto it = m_scalar_groups.begin(); it != m_scalar_groups.end(); it++) {
            show_scalar_group(it->second, false);
        }

        ms_io = ImGui::EndMultiSelect();
        applyMultiSelectRequests(ms_io, m_selected_scalars, m_visible_scalars);

        ImGui::EndTable();
    }
    ImGui::End();
}

void DbgGui::showVectorWindow() {
    m_window_focus.vectors.focused = ImGui::Begin("Vectors", NULL, ImGuiWindowFlags_NoNavFocus);
    if (!m_window_focus.vectors.focused) {
        ImGui::End();
        return;
    }

    static std::string vector_name_filter;
    ImGui::InputText("Filter", &vector_name_filter);

    if (ImGui::BeginTable("vector_table",
                          3,
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, num_width);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, num_width);

        ImGuiMultiSelectFlags ms_flags = ImGuiMultiSelectFlags_ClearOnEscape
                                       | ImGuiMultiSelectFlags_BoxSelect2d
                                       | ImGuiMultiSelectFlags_ScopeRect;
        ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags,
                                                            (int)m_selected_vectors.size(),
                                                            -1);
        applyMultiSelectRequests(ms_io, m_selected_vectors, m_visible_vectors);
        m_visible_vectors.clear();

        std::function<void(SignalGroup<Vector2D>&, bool)> show_vector_group = [&](SignalGroup<Vector2D>& group, bool delete_entire_group) {
            std::vector<Vector2D*> const& vectors = group.signals;
            if (!group.hasVisibleItems(vector_name_filter)) {
                return;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Group has to be opened automatically if signal in it matches the filter.
            // If there is no filter, then it should be kept open if it has been opened
            // manually before.
            if (!vector_name_filter.empty()) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            } else if (!group.opened_manually) {
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);
            }
            bool group_opened = ImGui::TreeNode(group.name.c_str());
            if (vector_name_filter.empty()) {
                group.opened_manually = group_opened;
            }

            // Symbols can be dragged from one group to another for easier reorganizing if symbol
            // is initially added to wrong group
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_ID")) {
                    uint64_t id = *(uint64_t*)payload->Data;
                    Vector2D* vector = findVector(m_vectors, id);
                    // Do nothing if dragged to same group.
                    // Old one will be deleted if new one is added.
                    if (vector && vector->group != group.full_name) {
                        VariantSymbol* x = m_symbols.getSymbol(vector->x->name);
                        VariantSymbol* y = m_symbols.getSymbol(vector->y->name);
                        if (x && y) {
                            Vector2D* new_vector = addVectorSymbol(x, y, group.full_name);
                            new_vector->x->setScaleStr(vector->x->getScaleStr());
                            new_vector->y->setScaleStr(vector->y->getScaleStr());
                            new_vector->x->setOffsetStr(vector->x->getOffsetStr());
                            new_vector->y->setOffsetStr(vector->y->getOffsetStr());
                            if (m_sampler.isScalarSampled(vector->x) || m_sampler.isScalarSampled(vector->y)) {
                                m_sampler.startSampling(new_vector);
                                m_sampler.copySamples(*vector, *new_vector);
                            }
                            vector->deleted = true;
                            vector->replacement = new_vector;
                            vector->x->replacement = new_vector->x;
                            vector->y->replacement = new_vector->y;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (group_opened) {
                // Show subgroups first
                for (auto& subgroup : group.subgroups) {
                    show_vector_group(subgroup.second, delete_entire_group);
                }

                // All vectors in a group are shown if the group name matches filter
                bool group_matches_filter = str::fuzzy_match(vector_name_filter.c_str(), group.full_name.c_str());
                for (Vector2D* vector : vectors) {
                    if (!vector_name_filter.empty()
                        && !str::fuzzy_match(vector_name_filter.c_str(), vector->name.c_str())
                        && !group_matches_filter) {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    bool const selected = contains(m_selected_vectors, vector);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                             | ImGuiTreeNodeFlags_SpanAvailWidth
                                             | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)m_visible_vectors.size());
                    ImGui::TreeNodeEx(std::format("{}##{}", vector->name, vector->name_and_group).c_str(), flags);
                    m_visible_vectors.push_back(vector);

                    if (ImGui::BeginPopupContextItem((vector->name_and_group + "_vector_context_menu").c_str())) {
                        if (ImGui::Button("Copy name")) {
                            ImGui::SetClipboardText(vector->name.c_str());
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::Separator();
                        if (ImGui::Button("Delete")) {
                            if (contains(m_selected_vectors, vector)) {
                                for (Vector2D* selected_vector : m_selected_vectors) {
                                    selected_vector->deleted = true;
                                }
                                m_selected_vectors.clear();
                            } else {
                                vector->deleted = true;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    // Make text drag-and-droppable
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("VECTOR_ID", &vector->id, sizeof(uint64_t));
                        ImGui::Text("Drag to vector plot");
                        ImGui::EndDragDropSource();
                    }

                    // Mark vector as deleted
                    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_Delete)) {
                        vector->deleted = true;
                    }

                    // Show x-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::format("##{}x", vector->x->name_and_group).c_str());
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &vector->x->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    if (vector->x->customScaleOrOffset()) {
                        ImGui::TextColored(COLOR_LIGHT_BLUE, numberAsStr(vector->x->getScaledValue()).c_str());
                    } else {
                        ImGui::Text(numberAsStr(vector->x->getValue()).c_str());
                    }
                    addScalarContextMenu(vector->x);

                    // Show y-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::format("##{}y", vector->y->name_and_group).c_str());
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &vector->y->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    if (vector->y->customScaleOrOffset()) {
                        ImGui::TextColored(COLOR_LIGHT_BLUE, numberAsStr(vector->y->getScaledValue()).c_str());
                    } else {
                        ImGui::Text(numberAsStr(vector->y->getValue()).c_str());
                    }
                    addScalarContextMenu(vector->y);
                }
                ImGui::TreePop();
            }
        };
        for (auto it = m_vector_groups.begin(); it != m_vector_groups.end(); it++) {
            show_vector_group(it->second, false);
        }

        ms_io = ImGui::EndMultiSelect();
        applyMultiSelectRequests(ms_io, m_selected_vectors, m_visible_vectors);

        ImGui::EndTable();
    }
    ImGui::End();
}

void DbgGui::addCustomWindowDragAndDrop(CustomWindow& custom_window) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
            std::span<uint64_t> ids(reinterpret_cast<uint64_t*>(payload->Data),
                                    payload->DataSize / sizeof(uint64_t));
            for (uint64_t id : ids) {
                Scalar* scalar = findScalar(m_scalars, id);
                if (scalar) {
                    custom_window.addScalar(scalar);
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
            VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
            Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
            custom_window.addScalar(scalar);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL_MULTI")) {
            std::span<VariantSymbol*> symbols(reinterpret_cast<VariantSymbol**>(payload->Data),
                                              payload->DataSize / sizeof(VariantSymbol*));
            for (VariantSymbol* symbol : symbols) {
                Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                custom_window.addScalar(scalar);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJECT_SYMBOL")) {
            char* symbol_name = (char*)payload->Data;
            VariantSymbol* dragged_symbol = m_symbols.getSymbol(symbol_name);
            if (dragged_symbol) {
                // Add children recursively
                std::function<void(VariantSymbol*)> add_children = [&](VariantSymbol* sym) {
                    bool arithmetic_or_enum = sym->getType() == VariantSymbol::Type::Arithmetic
                                           || sym->getType() == VariantSymbol::Type::Enum;
                    if (arithmetic_or_enum) {
                        Scalar* scalar = addScalarSymbol(sym, m_group_to_add_symbols);
                        custom_window.addScalar(scalar);
                    }

                    for (auto& child : sym->getChildren()) {
                        // Don't add insane amount of signals e.g. sampling buffers
                        if (child->getChildren().size() < 100) {
                            add_children(child.get());
                        }
                    }
                };
                add_children(dragged_symbol);
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void DbgGui::showCustomWindow() {
    for (CustomWindow& custom_window : m_custom_windows) {
        if (!custom_window.open) {
            continue;
        }

        custom_window.focus.focused = ImGui::Begin(custom_window.title().c_str(), NULL, ImGuiWindowFlags_NoNavFocus);
        custom_window.closeOnMiddleClick();
        custom_window.contextMenu();
        if (!custom_window.focus.focused) {
            ImGui::End();
            continue;
        }
        Scalar* scalar_to_remove = nullptr;

        if (ImGui::BeginTable("custom_table",
                              2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, num_width);
            for (Scalar* scalar : custom_window.scalars) {
                ImGui::TableNextColumn();
                // Show name. Text is used instead of selectable because the
                // keyboard navigation in the table does not work properly
                // and up/down changes columns
                if (scalar->customScaleOrOffset()) {
                    ImGui::TextColored(COLOR_LIGHT_BLUE, scalar->alias_and_group.c_str());
                } else {
                    ImGui::Text(scalar->alias_and_group.c_str());
                }
                addCustomWindowDragAndDrop(custom_window);
                // Make text drag-and-droppable
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &scalar->id, sizeof(uint64_t));
                    ImGui::Text("Drag to plot");
                    ImGui::EndDragDropSource();
                }
                // Hide symbol on delete
                if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_Delete)) {
                    scalar_to_remove = scalar;
                }
                addScalarContextMenu(scalar);

                // Show value
                ImGui::TableNextColumn();
                addInputScalar(scalar->src,
                               "##custom_" + scalar->name_and_group,
                               scalar->getScale(),
                               scalar->getOffset(),
                               scalar->read_only);
            }
            ImGui::EndTable();
        }

        ImGui::InvisibleButton("##canvas", ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.f), std::max(ImGui::GetContentRegionAvail().y, 1.f)));
        addCustomWindowDragAndDrop(custom_window);

        if (scalar_to_remove) {
            remove(custom_window.scalars, scalar_to_remove);
            size_t signals_removed = m_settings["custom_windows"][std::to_string(custom_window.id)]["signals"].erase(scalar_to_remove->group + " " + scalar_to_remove->name);
            assert(signals_removed > 0);
        }

        ImGui::End();
    }
}

std::vector<VariantSymbol*> DbgGui::buildSymbolSearchRoots(SymbolSearchRenderState& state) const {
    std::set<VariantSymbol*> search_root_set;
    std::vector<VariantSymbol*> search_roots;
    for (VariantSymbol* symbol : m_symbol_search_results) {
        if (symbol == nullptr) {
            continue;
        }

        VariantSymbol* root = symbol;
        state.visible_symbols.insert(symbol);
        for (VariantSymbol* parent = symbol->getParent(); parent != nullptr; parent = parent->getParent()) {
            root = parent;
            state.visible_symbols.insert(parent);
            state.auto_open_symbols.insert(parent);
        }
        if (search_root_set.insert(root).second) {
            search_roots.push_back(root);
        }
    }
    return search_roots;
}

void DbgGui::showSymbolSearchTable(std::string const& search_string, bool show_hidden_symbols, bool show_constants) {
    static ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                       | ImGuiTableFlags_BordersH
                                       | ImGuiTableFlags_Resizable
                                       | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("symbols_table", 2, table_flags)) {
        return;
    }

    // Multi-select scope wraps the whole table. BoxSelect2d because tree nodes have varying
    // indentation (2D layout); ScopeRect so box-select/clear-on-void is confined to the table
    // area, not the whole window (the search inputs above would otherwise be in scope).
    ImGuiMultiSelectFlags ms_flags = ImGuiMultiSelectFlags_ClearOnEscape
                                   | ImGuiMultiSelectFlags_BoxSelect2d
                                   | ImGuiMultiSelectFlags_ScopeRect;
    ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags,
                                                        (int)m_selected_symbols.size(),
                                                        -1); // Not used
    // Begin-side requests (e.g. Ctrl+A, Escape, auto-clear on plain click) use the previous
    // frame's m_visible_tree_symbols. The list is rebuilt below during traversal.
    applyMultiSelectRequests(ms_io, m_selected_symbols, m_visible_tree_symbols);
    m_visible_tree_symbols.clear();

    SymbolSearchRenderState state{
      .show_hidden_symbols = show_hidden_symbols,
      .show_constants = show_constants,
      .filter_recursive_tree = !search_string.empty() && m_symbol_search_depth > 0,
    };
    for (VariantSymbol* symbol : buildSymbolSearchRoots(state)) {
        showSymbolTreeNode(symbol, state, true);
    }

    ms_io = ImGui::EndMultiSelect();
    // End-side requests (e.g. SetRange from shift-click/box-select) use this frame's list.
    applyMultiSelectRequests(ms_io, m_selected_symbols, m_visible_tree_symbols);

    ImGui::EndTable();
}

void DbgGui::showSymbolTreeNode(VariantSymbol* sym,
                                SymbolSearchRenderState& state,
                                bool filter_to_search_path,
                                bool force_full_name) {
    if (sym == nullptr) {
        assert(false);
        return;
    }
    // Pointer expansion can create cycles, e.g. a node pointing back to an ancestor.
    if (state.visiting.contains(sym)) {
        return;
    }
    if (filter_to_search_path && state.filter_recursive_tree && !state.visible_symbols.contains(sym)) {
        return;
    }

    bool const hidden = m_hidden_symbols.contains(sym->getFullName());
    bool const constant = sym->isConst();
    if (!state.show_hidden_symbols && hidden) {
        return;
    }
    if (!state.show_constants && constant) {
        return;
    }

    state.visiting.insert(sym);
    bool const custom_symbol_scale = getSymbolScale(*sym, m_symbol_scale_settings) != 1;
    ImGui::PushStyleColor(ImGuiCol_Text,
                          hidden              ? COLOR_GRAY :
                          custom_symbol_scale ? COLOR_LIGHT_BLUE :
                                                ImGui::GetStyle().Colors[ImGuiCol_Text]);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // Full name is needed for top-level recursive results from multiple scopes.
    bool const show_full_name = force_full_name
                              || (sym->getParent() == nullptr && m_symbol_search_depth > 0);
    std::string const symbol_name = show_full_name ? sym->getFullName() : sym->getName();
    bool const auto_open = state.auto_open_symbols.contains(sym);
    if (sym->getType() == VariantSymbol::Type::Pointer) {
        showPointerSymbolTreeNode(sym, sym->getPointedSymbol(), symbol_name, auto_open, state, filter_to_search_path);
    } else if (!sym->getChildren().empty()) {
        showObjectSymbolTreeNode(sym, symbol_name, auto_open, state, filter_to_search_path);
    } else {
        showLeafSymbolTreeNode(sym, symbol_name);
    }

    ImGui::PopStyleColor();
    state.visiting.erase(sym);
}

void DbgGui::showPointerSymbolTreeNode(VariantSymbol* sym,
                                       VariantSymbol* pointed_symbol,
                                       std::string const& symbol_name,
                                       bool auto_open,
                                       SymbolSearchRenderState& state,
                                       bool filter_to_search_path) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
    if (!pointed_symbol) {
        flags |= ImGuiTreeNodeFlags_Leaf;
        sym->opened_manually = false;
    } else {
        ImGui::SetNextItemOpen(auto_open || sym->opened_manually, ImGuiCond_Always);
    }
    bool const open = ImGui::TreeNodeEx(symbol_name.c_str(), flags);
    if (pointed_symbol && !auto_open) {
        sym->opened_manually = open;
    }
    addSymbolContextMenu(*sym);

    // Add symbol to scalar window on double click. The pointer can be null or
    // point to function-scope static storage; then it is not dereferenced and
    // only shows NAN as value.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        addScalarSymbol(sym, m_group_to_add_symbols);
    }

    if (ImGui::BeginDragDropSource()) {
        // Drag-and-droppable to scalar plot.
        ImGui::SetDragDropPayload("SCALAR_SYMBOL", &sym, sizeof(VariantSymbol*));
        ImGui::Text("Drag to plot");
        ImGui::EndDragDropSource();
    }

    ImGui::TableNextColumn();
    ImGui::Text(sym->valueAsStr().c_str());
    if (open) {
        if (pointed_symbol) {
            showSymbolTreeNode(pointed_symbol, state, filter_to_search_path && auto_open, true);
        }
        ImGui::TreePop();
    }
}

void DbgGui::showObjectSymbolTreeNode(VariantSymbol* sym,
                                      std::string const& symbol_name,
                                      bool auto_open,
                                      SymbolSearchRenderState& state,
                                      bool filter_to_search_path) {
    ImGui::SetNextItemOpen(auto_open || sym->opened_manually, ImGuiCond_Always);
    bool const open = ImGui::TreeNodeEx(symbol_name.c_str());
    if (!auto_open) {
        sym->opened_manually = open;
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        // Drag-and-droppable to custom window.
        static char symbol_name_buffer[MAX_NAME_LENGTH];
        strncpy(symbol_name_buffer, sym->getFullName().data(), MAX_NAME_LENGTH);
        symbol_name_buffer[MAX_NAME_LENGTH - 1] = '\0';
        ImGui::SetDragDropPayload("OBJECT_SYMBOL", &symbol_name_buffer, sizeof(symbol_name_buffer));
        ImGui::Text("Drag to custom window to add all children");
        ImGui::EndDragDropSource();
    }
    addSymbolContextMenu(*sym);

    ImGui::TableNextColumn();
    ImGui::Text(sym->valueAsStr().c_str());
    if (open) {
        for (std::unique_ptr<VariantSymbol>& child : sym->getChildren()) {
            showSymbolTreeNode(child.get(), state, filter_to_search_path && auto_open);
        }
        ImGui::TreePop();
    }
}

void DbgGui::showLeafSymbolTreeNode(VariantSymbol* sym, std::string const& symbol_name) {
    bool const selected = contains(m_selected_symbols, sym);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    // Register this leaf with the multi-select system. The index is its position in
    // m_visible_tree_symbols (built in tree display order), which applySymbolSelectionRequests
    // uses to map SetRange/SetAll indices back to VariantSymbol*.
    ImGui::SetNextItemSelectionUserData((ImGuiSelectionUserData)m_visible_tree_symbols.size());
    ImGui::TreeNodeEx(symbol_name.c_str(), flags);
    ImGui::TreePop();
    m_visible_tree_symbols.push_back(sym);

    // The multi-select API handles plain-click select, ctrl-toggle (with toggle-off),
    // shift-range, box-select, Ctrl+A, and Escape. We only intercept Ctrl+Shift to open
    // the Custom Signal Creator, since that is an application-specific action.
    if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift) {
        m_show_custom_signal_creator = true;
    }

    bool const arithmetic_or_enum = sym->getType() == VariantSymbol::Type::Arithmetic
                                 || sym->getType() == VariantSymbol::Type::Enum;
    if (m_selected_symbols.size() == 2) {
        // Drag-and-droppable to vector plot.
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("VECTOR_SYMBOL", m_selected_symbols.data(), sizeof(VariantSymbol*) * 2);
            ImGui::Text("Drag to vector plot");
            ImGui::EndDragDropSource();
        }
    } else if (arithmetic_or_enum && ImGui::BeginDragDropSource()) {
        // Drag-and-droppable to scalar plot. When the symbol is part of the selection,
        // carry all selected symbols; otherwise just the dragged one.
        if (contains(m_selected_symbols, sym)) {
            ImGui::SetDragDropPayload("SCALAR_SYMBOL_MULTI",
                                      m_selected_symbols.data(),
                                      m_selected_symbols.size() * sizeof(VariantSymbol*));
            ImGui::Text("Drag %d symbols to plot", (int)m_selected_symbols.size());
        } else {
            ImGui::SetDragDropPayload("SCALAR_SYMBOL", &sym, sizeof(VariantSymbol*));
            ImGui::Text("Drag to plot");
        }
        ImGui::EndDragDropSource();
    }

    // Add symbol to scalar window on double click.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && arithmetic_or_enum) {
        addScalarSymbol(sym, m_group_to_add_symbols);
    }
    addSymbolContextMenu(*sym);

    // Add value.
    ImGui::TableNextColumn();
    if (arithmetic_or_enum) {
        addInputScalar(sym->getValueSource(),
                       "##symbol_" + sym->getFullName(),
                       getSymbolScale(*sym, m_symbol_scale_settings),
                       0,
                       sym->isConst());
    } else {
        ImGui::Text(sym->valueAsStr().c_str());
    }
}

void DbgGui::showSymbolsWindow() {
    m_window_focus.symbols.focused = ImGui::Begin("Symbols", NULL, ImGuiWindowFlags_NoNavFocus);
    if (!m_window_focus.symbols.focused) {
        ImGui::End();
        return;
    }

    static bool show_hidden_symbols = false;
    static bool show_constants = false;

    // Just manually tested width that name, group and menu boxes are visible.
    float name_and_group_boxes_width = ImGui::GetContentRegionAvail().x - 20 * ImGui::CalcTextSize("x").x;
    ImGui::PushItemWidth(name_and_group_boxes_width * 0.65f);
    static std::string symbols_to_search;
    bool search_changed = ImGui::InputText("Name", &symbols_to_search, ImGuiInputTextFlags_CharsNoBlank);
    // Group box
    ImGui::SameLine();
    ImGui::PushItemWidth(name_and_group_boxes_width * 0.35f);
    ImGui::InputText("Group", &m_group_to_add_symbols);
    // Search options
    ImGui::SameLine();
    if (ImGui::BeginMenu("Menu")) {
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputInt("Recursion depth", &m_symbol_search_depth)) {
            m_symbol_search_depth = std::max(0, m_symbol_search_depth);
            search_changed = true;
        }
        ImGui::Checkbox("Show hidden", &show_hidden_symbols);
        ImGui::Checkbox("Show constants", &show_constants);
        ImGui::EndMenu();
    }

    if (search_changed) {
        m_symbol_search_results = buildSymbolSearchResults(m_symbols, symbols_to_search, m_symbol_search_depth);
    }

    showSymbolSearchTable(symbols_to_search, show_hidden_symbols, show_constants);
    ImGui::End();
}

void DbgGui::showScriptWindow() {
    for (ScriptWindow& script_window : m_script_windows) {
        if (!script_window.open) {
            continue;
        }

        script_window.focus.focused = ImGui::Begin(script_window.title().c_str(), NULL, ImGuiWindowFlags_NoNavFocus);
        script_window.closeOnMiddleClick();
        script_window.contextMenu();
        script_window.processScript(m_plot_timestamp);
        if (!script_window.focus.focused) {
            ImGui::End();
            continue;
        }

        if (ImGui::Button("Run")) {
            m_error_message = script_window.startScript(m_plot_timestamp, m_scalars);
        }
        if (ImGui::BeginPopupContextItem("Run_context_menu")) {
            // Toggling the text edit window will cause it reapper if it's hidden behind other windows
            script_window.text_edit_open = !script_window.text_edit_open;
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &script_window.loop);

        // Stop button only visible if running
        if (script_window.running()) {

            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                script_window.stopScript();
            }
            ImGui::SameLine();
            ImGui::Text(std::format("{:.2f}", script_window.getTime(m_plot_timestamp)).c_str());
        }

        ImGui::End();

        // Open text edit in a separate window
        if (script_window.text_edit_open) {
            ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
            ImGui::Begin(std::format("{}##editor", script_window.name).c_str(), &script_window.text_edit_open);
            // Close on middle click like other windows
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                script_window.text_edit_open = false;
            }

            if (ImGui::Button("Run")) {
                m_error_message = script_window.startScript(m_plot_timestamp, m_scalars);
            }

            ImGui::SameLine();
            ImGui::Checkbox("Loop", &script_window.loop);
            // Stop button only visible if running
            if (script_window.running()) {
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    script_window.stopScript();
                }
                ImGui::SameLine();
                ImGui::Text(std::format("{:.2f}", script_window.getTime(m_plot_timestamp)).c_str());

                // Show progress of the script by adding a separator where it is currently waiting
                std::vector<std::string_view> lines = str::splitSv(script_window.text, '\n');
                showRunningScriptWithLineNumbers(lines, script_window.currentLine());
            } else {
                inputScriptTextWithLineNumbers(script_window.text, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
            }

            ImGui::End();
        }
    }
}

void DbgGui::addGridWindowDragAndDrop(GridWindow& grid_window, int row, int col) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
            std::span<uint64_t> ids(reinterpret_cast<uint64_t*>(payload->Data),
                                    payload->DataSize / sizeof(uint64_t));
            if (!ids.empty()) {
                Scalar* dropped_scalar = findScalar(m_scalars, ids[0]);
                if (dropped_scalar) {
                    grid_window.scalars[row][col] = dropped_scalar->id;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void DbgGui::showGridWindow() {
    for (GridWindow& grid_window : m_grid_windows) {
        if (!grid_window.open) {
            continue;
        }
        grid_window.focus.focused = ImGui::Begin(grid_window.title().c_str(), NULL, ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar);
        grid_window.closeOnMiddleClick();
        grid_window.contextMenu();
        if (!grid_window.focus.focused) {
            ImGui::End();
            continue;
        }
        // Calculate text & value font sizes. Not accurate but seems to be roughly correct
        auto const& style = ImGui::GetStyle();
        float padding = style.FramePadding.y + style.CellPadding.y + style.ItemSpacing.y * 0.5;
        float row_font_size = ((ImGui::GetContentRegionAvail().y - style.WindowPadding.y) / grid_window.rows - padding);
        row_font_size = (float)std::clamp((int)(row_font_size), MIN_FONT_SIZE, MAX_FONT_SIZE - 1);
        float text_font_size = grid_window.text_to_value_ratio * row_font_size;
        float value_font_size = (1.0f - grid_window.text_to_value_ratio) * row_font_size;

        float available_y = ImGui::GetContentRegionAvail().y;
        float needed_y = (padding * 2 + row_font_size) * grid_window.rows;
        if (available_y < needed_y) {
            float scale = available_y / needed_y;
            text_font_size *= scale;
            value_font_size *= scale;
        }
        value_font_size = std::max(value_font_size, float(MIN_FONT_SIZE));

        if (ImGui::BeginTable("grid_table",
                              grid_window.columns,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
            for (int row = 0; row < grid_window.rows; ++row) {
                for (int col = 0; col < grid_window.columns; ++col) {
                    ImGui::TableNextColumn();

                    Scalar* scalar = findScalar(m_scalars, grid_window.scalars[row][col]);
                    if (scalar) {
                        // Resize text so that it fits the cell
                        ImGui::PushFont(ImGui::GetDefaultFont(), text_font_size);
                        ImVec2 text_size = ImGui::CalcTextSize(scalar->alias_and_group.c_str());
                        ImVec2 available = ImGui::GetContentRegionAvail();
                        if (available.x < text_size.x) {
                            ImGui::PopFont();
                            ImGui::PushFont(ImGui::GetDefaultFont(), (text_font_size * (available.x / text_size.x) - 1));
                        }

                        // Name
                        if (scalar->customScaleOrOffset()) {
                            ImGui::TextColored(COLOR_LIGHT_BLUE, scalar->alias_and_group.c_str());
                        } else {
                            ImGui::Text(scalar->alias_and_group.c_str());
                        }
                        addScalarContextMenu(scalar);
                        // Hide symbol on delete
                        if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_Delete)) {
                            grid_window.scalars[row][col] = NULL;
                        }
                        ImGui::PopFont();
                        // Make text drag-and-droppable
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("SCALAR_ID_MULTI", &scalar->id, sizeof(uint64_t));
                            ImGui::Text("Drag to plot");
                            ImGui::EndDragDropSource();
                        }

                        // Value. Manual keyboard navigation because the text in the same cell messes up default navigation.
                        ImGui::PushFont(ImGui::GetDefaultFont(), value_font_size);
                        if (grid_window.isCellFocused({row, col})) {
                            ImGui::SetKeyboardFocusHere();
                        }
                        addInputScalar(scalar->src,
                                       "##grid_" + scalar->name_and_group,
                                       scalar->getScale(),
                                       scalar->getOffset(),
                                       scalar->read_only);
                        if (ImGui::IsItemFocused()) {
                            if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_DownArrow)) {
                                grid_window.focusCell({std::min(row + 1, grid_window.rows - 1), col});
                            } else if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_UpArrow)) {
                                grid_window.focusCell({std::max(row - 1, 0), col});
                            } else if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_RightArrow)) {
                                grid_window.focusCell({row, std::min(col + 1, grid_window.columns - 1)});
                            } else if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_LeftArrow)) {
                                grid_window.focusCell({row, std::max(col - 1, 0)});
                            }
                        }
                        ImGui::PopFont();
                    } else {
                        ImGui::PushFont(ImGui::GetDefaultFont(), text_font_size);
                        ImGui::Text("");
                        ImGui::PopFont();

                        ImGui::PushFont(ImGui::GetDefaultFont(), value_font_size);
                        ImGui::Text("-");
                        ImGui::PopFont();
                    }
                    addGridWindowDragAndDrop(grid_window, row, col);

                    // Make the empty space also valid drop target
                    ImGui::SameLine();
                    ImGui::InvisibleButton(std::format("##canvas_{}_{}", row, col).c_str(), ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.f), value_font_size));
                    addGridWindowDragAndDrop(grid_window, row, col);
                }
            }

            ImGui::EndTable();
        }
        ImGui::End();
    }
}
