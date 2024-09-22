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

#include "dbg_gui.h"
#include "imgui.h"
#include "symbols/fts_fuzzy_match.h"
#include <format>
#include <iostream>
#include "imgui_internal.h"
#include "themes.h"

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

static void HelpMarker(const char* desc) {
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
    strcpy_s(data->Buf, 20, key_name.c_str());
    data->BufTextLen = 1;
    data->BufDirty = 1;
    data->CursorPos = 1;
    data->SelectionStart = 1;
    data->SelectionEnd = 1;
    *pressed_key = ImGuiKey_None;
    return 0;
}

std::optional<ImGuiKey> pressedNumber() {
    for (ImGuiKey key = ImGuiKey_0; key <= ImGuiKey_9; key++) {
        if (ImGui::IsKeyPressed(key)) {
            return key;
        }
    }
    for (ImGuiKey key = ImGuiKey_Keypad0; key <= ImGuiKey_Keypad9; key++) {
        if (ImGui::IsKeyPressed(key)) {
            return key;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus)) {
        return ImGuiKey_Minus;
    }
    return std::nullopt;
}

void addInputScalar(ValueSource const& value_src, std::string const& label, double scale = 1, double offset = 0) {
    if (std::get_if<ReadWriteFnCustomStr>(&value_src)) {
        ImGui::Text(getSourceValueStr(value_src).c_str());
        ImGui::SameLine();
    }

    ImGuiInputTextFlags edit_flags = ImGuiInputTextFlags_EnterReturnsTrue
                                   | ImGuiInputTextFlags_AutoSelectAll
                                   | ImGuiInputTextFlags_CharsScientific
                                   | ImGuiInputTextFlags_CallbackAlways;
    double scaled_value = getSourceValue(value_src) * scale + offset;
    char value[20];
    strcpy_s(value, numberAsStr(scaled_value).c_str());
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

void DbgGui::addScalarContextMenu(Scalar* scalar) {
    if (ImGui::BeginPopupContextItem((scalar->name_and_group + "_context_menu").c_str())) {
        double pause_level = scalar->getScaledValue();
        if (ImGui::InputDouble("Trigger level", &pause_level, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_pause_triggers.push_back(PauseTrigger(scalar, pause_level));
            ImGui::CloseCurrentPopup();
        }
        addScalarScaleInput(scalar);
        addScalarOffsetInput(scalar);

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

        scalar->alias.reserve(MAX_NAME_LENGTH);
        if (ImGui::InputText("Alias##scalar_context_menu",
                             scalar->alias.data(),
                             MAX_NAME_LENGTH)) {
            scalar->alias = std::string(scalar->alias.data());
            if (scalar->alias.empty()) {
                scalar->alias = scalar->name;
            }
            scalar->alias_and_group = std::string(scalar->alias.data()) + " (" + scalar->group + ")";
        }
        ImGui::EndPopup();
    }
}

void DbgGui::addScalarScaleInput(Scalar* scalar) {
    char buffer[1024];
    std::memcpy(buffer, scalar->getScaleStr().data(), scalar->getScaleStr().size());
    buffer[scalar->getScaleStr().size()] = '\0';
    if (ImGui::InputText("Scale", buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto scale = str::evaluateExpression(buffer);
        if (scale.has_value()) {
            scalar->setScaleStr(buffer);
        } else {
            m_error_message = scale.error();
        }
    }
}

void DbgGui::addScalarOffsetInput(Scalar* scalar) {
    char buffer[1024];
    std::memcpy(buffer, scalar->getOffsetStr().data(), scalar->getOffsetStr().size());
    buffer[scalar->getOffsetStr().size()] = '\0';
    if (ImGui::InputText("Offset", buffer, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto offset = str::evaluateExpression(buffer);
        if (offset.has_value()) {
            scalar->setOffsetStr(buffer);
        } else {
            m_error_message = offset.error();
        }
    }
}

void DbgGui::addSymbolContextMenu(VariantSymbol& sym) {
    std::string full_name = sym.getFullName();
    if (ImGui::BeginPopupContextItem((full_name + "_context_menu").c_str())) {
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
        ImGui::DockSpace(dockspace.dock_id);

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
                saveScalarsAsCsv(scalars, m_linked_scalar_x_axis_limits);
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
            ImGui::Checkbox("Scalar plot x-tick labels", &m_options.x_tick_labels);

            ImGui::Checkbox("Scalar plot tooltip", &m_options.scalar_plot_tooltip);
            ImGui::SameLine();
            HelpMarker("Show vertical line containing the values of the signals when hovering over scalar plot.");

            ImGui::Checkbox("Pause on close", &m_options.pause_on_close);
            ImGui::SameLine();
            HelpMarker("Pause when GUI is requested to close programmatically. Pressing start again will close the GUI.");

            ImGui::Checkbox("Show latest message on main menu bar", &m_options.show_latest_message_on_main_menu_bar);

            // Theme
            themeCombo(m_options.theme, m_window);

            // Order here must match the order in which fonts were added
            if (ImGui::RadioButton("Cousine regular", reinterpret_cast<int*>(&m_options.font_selection), 0)) {
                ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->Fonts[0];
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Calibri", reinterpret_cast<int*>(&m_options.font_selection), 1)) {
                ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->Fonts[1];
            }
            ImGui::InputInt("Sampling buffer size", &m_options.sampling_buffer_size, 0);
            ImGui::SameLine();
            HelpMarker("Changing requires restart to take effect. Default = 1'000'000");

            ImGui::InputFloat("Font size", &m_options.font_size, 0, 0, "%.1f");
            ImGui::SameLine();
            HelpMarker("Changing requires restart to take effect. Default = 13.0");

            ImGui::Separator();

            if (ImGui::Button("Clear saved settings") && ImGui::GetIO().KeyCtrl) {
                m_options.clear_saved_settings = true;
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
    static char scalar_name_filter_buffer[256] = "";
    ImGui::InputText("Filter", scalar_name_filter_buffer, IM_ARRAYSIZE(scalar_name_filter_buffer));
    std::string scalar_name_filter = std::string(scalar_name_filter_buffer);

    if (ImGui::BeginTable("scalar_table",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, num_width);

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
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
                    uint64_t id = *(uint64_t*)payload->Data;
                    Scalar* scalar = getScalar(id);
                    // Do nothing if dragged to same group.
                    // Old one will be deleted if new one is added.
                    if (scalar->group != group.full_name) {
                        VariantSymbol* scalar_symbol = m_dbghelp_symbols.getSymbol(scalar->name);
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
                }
                ImGui::EndDragDropTarget();
            }

            // Show values inside the group
            if (group_opened) {
                if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                    delete_entire_group = true;
                }

                // Show subgroups first
                for (auto& subgroup : group.subgroups) {
                    show_scalar_group(subgroup.second, delete_entire_group);
                }

                // All signals in a group are shown if the group name matches filter
                bool group_matches_filter = fts::fuzzy_match_simple(scalar_name_filter.c_str(), group.full_name.c_str());
                // Show each scalar
                for (Scalar* scalar : scalars) {
                    bool hide_by_filter = !scalar_name_filter.empty()
                                       && !fts::fuzzy_match_simple(scalar_name_filter.c_str(), scalar->alias.c_str())
                                       && !group_matches_filter;
                    if (scalar->hide_from_scalars_window || hide_by_filter) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    // Show name. Text is used instead of selectable because the
                    // keyboard navigation in the table does not work properly
                    // and up/down changes columns
                    if (scalar->customScaleOrOffset()) {
                        ImGui::TextColored(COLOR_GRAY, scalar->alias.c_str());
                    } else {
                        ImGui::Text(scalar->alias.c_str());
                    }
                    // Make text drag-and-droppable
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &scalar->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    // Mark signal as deleted
                    if ((ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete))) {
                        scalar->deleted = true;
                    }
                    addScalarContextMenu(scalar);

                    // Show value
                    ImGui::TableNextColumn();
                    addInputScalar(scalar->src, "##scalar_" + scalar->name_and_group, scalar->getScale(), scalar->getOffset());
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

    static char vector_name_filter_buffer[256] = "";
    ImGui::InputText("Filter", vector_name_filter_buffer, IM_ARRAYSIZE(vector_name_filter_buffer));
    std::string vector_name_filter = std::string(vector_name_filter_buffer);

    if (ImGui::BeginTable("vector_table",
                          3,
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, num_width);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, num_width);

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
                    Vector2D* vector = getVector(id);
                    // Do nothing if dragged to same group.
                    // Old one will be deleted if new one is added.
                    if (vector->group != group.full_name) {
                        VariantSymbol* x = m_dbghelp_symbols.getSymbol(vector->x->name);
                        VariantSymbol* y = m_dbghelp_symbols.getSymbol(vector->y->name);
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
                bool group_matches_filter = fts::fuzzy_match_simple(vector_name_filter.c_str(), group.full_name.c_str());
                for (Vector2D* vector : vectors) {
                    if (!vector_name_filter.empty()
                        && !fts::fuzzy_match_simple(vector_name_filter.c_str(), vector->name.c_str())
                        && !group_matches_filter) {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    // Show name
                    ImGui::Text(vector->name.c_str());
                    // Make text drag-and-droppable
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("VECTOR_ID", &vector->id, sizeof(uint64_t));
                        ImGui::Text("Drag to vector plot");
                        ImGui::EndDragDropSource();
                    }

                    // Mark vector as deleted
                    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                        vector->deleted = true;
                    }

                    // Show x-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::format("##{}x", vector->x->name_and_group).c_str());
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &vector->x->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    if (vector->x->customScaleOrOffset()) {
                        ImGui::TextColored(COLOR_GRAY, numberAsStr(vector->x->getScaledValue()).c_str());
                    } else {
                        ImGui::Text(numberAsStr(vector->x->getValue()).c_str());
                    }
                    addScalarContextMenu(vector->x);

                    // Show y-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::format("##{}y", vector->y->name_and_group).c_str());
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &vector->y->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    if (vector->y->customScaleOrOffset()) {
                        ImGui::TextColored(COLOR_GRAY, numberAsStr(vector->y->getScaledValue()).c_str());
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

        ImGui::EndTable();
    }
    ImGui::End();
}

void DbgGui::addCustomWindowDragAndDrop(CustomWindow& custom_window) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
            uint64_t id = *(uint64_t*)payload->Data;
            Scalar* scalar = getScalar(id);
            if (!contains(custom_window.scalars, scalar)) {
                custom_window.scalars.push_back(scalar);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
            VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
            Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
            if (!contains(custom_window.scalars, scalar)) {
                custom_window.scalars.push_back(scalar);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJECT_SYMBOL")) {
            char* symbol_name = (char*)payload->Data;
            VariantSymbol* dragged_symbol = m_dbghelp_symbols.getSymbol(symbol_name);
            if (dragged_symbol) {
                // Add children recursively
                std::function<void(VariantSymbol*)> add_children = [&](VariantSymbol* sym) {
                    bool arithmetic_or_enum = sym->getType() == VariantSymbol::Type::Arithmetic
                                           || sym->getType() == VariantSymbol::Type::Enum;
                    if (arithmetic_or_enum) {
                        Scalar* scalar = addScalarSymbol(sym, m_group_to_add_symbols);
                        if (!contains(custom_window.scalars, scalar)) {
                            custom_window.scalars.push_back(scalar);
                        }
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
                    ImGui::TextColored(COLOR_GRAY, scalar->alias_and_group.c_str());
                } else {
                    ImGui::Text(scalar->alias_and_group.c_str());
                }
                addCustomWindowDragAndDrop(custom_window);
                // Make text drag-and-droppable
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("SCALAR_ID", &scalar->id, sizeof(uint64_t));
                    ImGui::Text("Drag to plot");
                    ImGui::EndDragDropSource();
                }
                // Hide symbol on delete
                if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                    scalar_to_remove = scalar;
                }
                addScalarContextMenu(scalar);

                // Show value
                ImGui::TableNextColumn();
                addInputScalar(scalar->src, "##custom_" + scalar->name_and_group, scalar->getScale(), scalar->getOffset());
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

void DbgGui::showSymbolsWindow() {
    m_window_focus.symbols.focused = ImGui::Begin("Symbols", NULL, ImGuiWindowFlags_NoNavFocus);
    if (!m_window_focus.symbols.focused) {
        ImGui::End();
        return;
    }

    static bool recursive_symbol_search = false;
    static bool recursive_search_toggled = false;
    static bool show_hidden_symbols = false;

    // Just manually tested width that name, group and menu boxes are visible.
    float name_and_group_boxes_width = ImGui::GetContentRegionAvail().x - 20 * ImGui::CalcTextSize("x").x;
    ImGui::PushItemWidth(name_and_group_boxes_width * 0.65f);
    static char symbols_to_search[MAX_NAME_LENGTH];
    if (ImGui::InputText("Name", symbols_to_search, MAX_NAME_LENGTH, ImGuiInputTextFlags_CharsNoBlank) || recursive_search_toggled) {
        if (std::string(symbols_to_search).size() > 2) {
            m_symbol_search_results = m_dbghelp_symbols.findMatchingSymbols(symbols_to_search, recursive_symbol_search);
            auto begin_it = m_symbol_search_results.begin();
            // Don't sort first element if it is an exact match
            if (m_symbol_search_results.size() > 0 && m_symbol_search_results[0]->getFullName() == symbols_to_search) {
                begin_it++;
            }
            // Sort search results
            std::sort(begin_it, m_symbol_search_results.end(), [](VariantSymbol* l, VariantSymbol* r) {
                return l->getFullName() < r->getFullName();
            });
        } else {
            m_symbol_search_results.clear();
        }
    }
    // Group box
    ImGui::SameLine();
    ImGui::PushItemWidth(name_and_group_boxes_width * 0.35f);
    ImGui::InputText("Group", m_group_to_add_symbols, MAX_NAME_LENGTH);
    // Recursive checkbox
    ImGui::SameLine();
    if (ImGui::BeginMenu("Menu")) {
        recursive_search_toggled = ImGui::Checkbox("Recursive", &recursive_symbol_search);
        ImGui::Checkbox("Show hidden", &show_hidden_symbols);
        ImGui::EndMenu();
    }

    static ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                       | ImGuiTableFlags_BordersH
                                       | ImGuiTableFlags_Resizable
                                       | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("symbols_table", 2, table_flags)) {
        for (VariantSymbol* symbol : m_symbol_search_results) {
            // Recursive lambda for displaying children in the table
            std::function<void(VariantSymbol*)> show_children = [&](VariantSymbol* sym) {
                // Hide "C6011 Deferencing NULL pointer 'sym'" warning.
                if (sym == nullptr) {
                    assert(false);
                    return;
                }

                // Skip hidden symbols
                bool hidden = m_hidden_symbols.contains(sym->getFullName());
                if (!show_hidden_symbols && hidden) {
                    return;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, hidden ? COLOR_GRAY : ImGui::GetStyle().Colors[ImGuiCol_Text]);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // Full name has to be displayed with recursive search
                std::string symbol_name = recursive_symbol_search ? sym->getFullName() : sym->getName();
                std::vector<std::unique_ptr<VariantSymbol>>& children = sym->getChildren();
                if (children.size() > 0) {
                    // Object/array
                    bool open = ImGui::TreeNodeEx(symbol_name.c_str());
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        static char symbol_name_buffer[MAX_NAME_LENGTH];
                        strcpy_s(symbol_name_buffer, sym->getFullName().data());
                        ImGui::SetDragDropPayload("OBJECT_SYMBOL", &symbol_name_buffer, sizeof(symbol_name_buffer));
                        ImGui::Text("Drag to custom window to add all children");
                        ImGui::EndDragDropSource();
                    }
                    addSymbolContextMenu(*sym);

                    ImGui::TableNextColumn();
                    ImGui::Text(sym->valueAsStr().c_str());
                    if (open) {
                        for (std::unique_ptr<VariantSymbol>& child : children) {
                            show_children(child.get());
                        }
                        ImGui::TreePop();
                    }
                } else if (sym->getType() == VariantSymbol::Type::Pointer) {
                    // Pointer
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
                    VariantSymbol* pointed_symbol = sym->getPointedSymbol();
                    if (!pointed_symbol) {
                        flags |= ImGuiTreeNodeFlags_Leaf;
                    }
                    bool open = ImGui::TreeNodeEx(symbol_name.c_str(), flags);
                    addSymbolContextMenu(*sym);

                    // Add symbol to scalar window on double click. The pointer can be null pointer or
                    // point to function scope static or something but in that case it will not be
                    // dereferenced and will show only NAN as value
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        addScalarSymbol(sym, m_group_to_add_symbols);
                    }

                    if (ImGui::BeginDragDropSource()) {
                        // drag-and-droppable to scalar plot
                        ImGui::SetDragDropPayload("SCALAR_SYMBOL", &sym, sizeof(VariantSymbol*));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text(sym->valueAsStr().c_str());
                    if (open) {
                        if (pointed_symbol) {
                            show_children(pointed_symbol);
                        }
                        ImGui::TreePop();
                    }
                } else {
                    // Rest
                    static bool selected_symbol_idx = 0;
                    static VariantSymbol* selected_symbols[2] = {nullptr, nullptr};
                    bool selected = (sym == selected_symbols[0]) || (sym == selected_symbols[1]);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf;
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    ImGui::TreeNodeEx(symbol_name.c_str(), flags);
                    ImGui::TreePop();
                    if (ImGui::IsItemClicked()) {
                        if (ImGui::GetIO().KeyCtrl) {
                            // Clear if already selected
                            if (selected_symbols[0] == sym || selected_symbols[1] == sym) {
                                selected_symbol_idx = 0;
                                selected_symbols[0] = nullptr;
                                selected_symbols[1] = nullptr;
                            } else {
                                selected_symbols[selected_symbol_idx] = sym;
                                selected_symbol_idx = (selected_symbol_idx + 1) % 2;
                            }
                        } else if (!(flags & ImGuiTreeNodeFlags_Selected)) {
                            // Clear if clicking something else than the selected
                            selected_symbol_idx = 0;
                            selected_symbols[0] = nullptr;
                            selected_symbols[1] = nullptr;
                        }
                    }

                    bool arithmetic_or_enum = sym->getType() == VariantSymbol::Type::Arithmetic
                                           || sym->getType() == VariantSymbol::Type::Enum;
                    if (selected_symbols[0] != nullptr && selected_symbols[1] != nullptr) {
                        // drag-and-droppable to vector plot
                        if (ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload("VECTOR_SYMBOL", &selected_symbols[0], sizeof(VariantSymbol*) * 2);
                            ImGui::Text("Drag to vector plot");
                            ImGui::EndDragDropSource();
                        }
                    } else if (arithmetic_or_enum && ImGui::BeginDragDropSource()) {
                        // drag-and-droppable to scalar plot
                        ImGui::SetDragDropPayload("SCALAR_SYMBOL", &sym, sizeof(VariantSymbol*));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }

                    // Add symbol to scalar window on double click
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && arithmetic_or_enum) {
                        addScalarSymbol(sym, m_group_to_add_symbols);
                    }
                    addSymbolContextMenu(*sym);

                    // Add value
                    ImGui::TableNextColumn();
                    if (arithmetic_or_enum) {
                        addInputScalar(sym->getValueSource(), "##symbol_" + sym->getFullName());
                    } else {
                        ImGui::Text(sym->valueAsStr().c_str());
                    }
                }
                ImGui::PopStyleColor();
            };
            show_children(symbol);
        }
        ImGui::EndTable();
    }
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
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &script_window.loop);

        // Stop button only visible if running
        if (script_window.running()) {

            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                script_window.stopScript();
            }
            ImGui::SameLine();
            ImGui::Text(std::format("{:g}", script_window.getTime(m_plot_timestamp)).c_str());
        }

        ImGui::InputTextMultiline("##source", script_window.text, IM_ARRAYSIZE(script_window.text), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16), NULL);

        ImGui::End();
    }
}
