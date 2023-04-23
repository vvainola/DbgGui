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

template <typename T>
inline std::string numberAsStr(T number) {
    return std::format("{:g}", double(number));
}

inline static std::string getSourceValueStr(ValueSource src) {
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

void addInputScalar(ValueSource const& signal_src, std::string const& label, double scale = 1, double offset = 0) {
    if (std::get_if<ReadWriteFnCustomStr>(&signal_src)) {
        ImGui::Text(getSourceValueStr(signal_src).c_str());
        ImGui::SameLine();
    }

    ImGuiInputTextFlags edit_flags = ImGuiInputTextFlags_EnterReturnsTrue
                                   | ImGuiInputTextFlags_AutoSelectAll
                                   | ImGuiInputTextFlags_CharsScientific
                                   | ImGuiInputTextFlags_CallbackAlways;
    double scaled_value = getSourceValue(signal_src) * scale + offset;
    char value[20];
    strcpy_s(value, numberAsStr(scaled_value).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    static ImGuiKey pressed_number = ImGuiKey_None;
    if (ImGui::InputText(label.c_str(), value, sizeof(value), edit_flags, setCursorOnFirstNumberPress, (void*)&pressed_number)) {
        try {
            setSourceValue(signal_src, (std::stod(value) - offset) / scale);
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

void addScalarContextMenu(Scalar* scalar) {
    if (ImGui::BeginPopupContextItem((scalar->name_and_group + "_context_menu").c_str())) {
        double pause_level = scalar->getScaledValue();
        if (ImGui::InputDouble("Trigger level", &pause_level, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue)) {
            scalar->addTrigger(pause_level);
            ImGui::CloseCurrentPopup();
        }
        ImGui::InputDouble("Scale", &scalar->scale, 0, 0, "%g", ImGuiInputTextFlags_CharsScientific);
        ImGui::InputDouble("Offset", &scalar->offset, 0, 0, "%g", ImGuiInputTextFlags_CharsScientific);

        if (ImGui::Button("Copy name")) {
            ImGui::SetClipboardText(scalar->alias.c_str());
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Copy name and value")) {
            ImGui::SetClipboardText((scalar->alias + " " + numberAsStr(scalar->getScaledValue())).c_str());
            ImGui::CloseCurrentPopup();
        }

        scalar->alias.reserve(MAX_NAME_LENGTH);
        if (ImGui::InputText("Name##scalar_context_menu",
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

void addSymbolContextMenu(VariantSymbol& sym) {
    std::string full_name = sym.getFullName();
    if (ImGui::BeginPopupContextItem((full_name + "_context_menu").c_str())) {
        if (ImGui::Button("Copy name")) {
            ImGui::SetClipboardText(full_name.c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DbgGui::showMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        ImGui::Text("Time %.3f s", m_plot_timestamp);
        ImGui::SameLine();

        if (ImGui::BeginMenu("Menu")) {
            ImGui::Checkbox("Link scalar x-axis", &m_options.link_scalar_x_axis);
            ImGui::Checkbox("Scalar plot x-tick labels", &m_options.x_tick_labels);
            ImGui::Checkbox("Pause on close", &m_options.pause_on_close);
            ImGui::SameLine();
            HelpMarker("Pause when GUI is requested to close programmatically. Pressing start again will close the GUI.");

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
                // Always center this window when appearing
                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

                if (ImGui::Button("Scalar plot")) {
                    ImGui::OpenPopup("Add scalar plot");
                }
                static char window_or_plot_name[256] = "";
                if (ImGui::BeginPopupModal("Add scalar plot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (ImGui::InputText("Name",
                                         window_or_plot_name,
                                         IM_ARRAYSIZE(window_or_plot_name),
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                        m_scalar_plots.push_back(ScalarPlot{.name = window_or_plot_name,
                                                            .y_axis = {-1, 1},
                                                            .x_axis = {0, 1},
                                                            .x_range = 1});
                        strcpy_s(window_or_plot_name, "");
                        ImGui::CloseCurrentPopup();
                    };
                    ImGui::EndPopup();
                }

                if (ImGui::Button("Vector plot")) {
                    ImGui::OpenPopup("Add vector plot");
                }
                if (ImGui::BeginPopupModal("Add vector plot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (ImGui::InputText("Vector plot name",
                                         window_or_plot_name,
                                         IM_ARRAYSIZE(window_or_plot_name),
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                        m_vector_plots.push_back(VectorPlot{.name = window_or_plot_name});
                        strcpy_s(window_or_plot_name, "");
                        ImGui::CloseCurrentPopup();
                    };
                    ImGui::EndPopup();
                }

                if (ImGui::Button("Spectrum plot")) {
                    ImGui::OpenPopup("Add spectrum plot");
                }
                if (ImGui::BeginPopupModal("Add spectrum plot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (ImGui::InputText("Spectrum plot name",
                                         window_or_plot_name,
                                         IM_ARRAYSIZE(window_or_plot_name),
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                        m_spectrum_plots.push_back(SpectrumPlot{.name = window_or_plot_name});
                        strcpy_s(window_or_plot_name, "");
                        ImGui::CloseCurrentPopup();
                    };
                    ImGui::EndPopup();
                }

                if (ImGui::Button("Custom window")) {
                    ImGui::OpenPopup("Add custom window");
                }
                if (ImGui::BeginPopupModal("Add custom window", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    if (ImGui::InputText("Custom window name",
                                         window_or_plot_name,
                                         IM_ARRAYSIZE(window_or_plot_name),
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                        m_custom_windows.push_back(CustomWindow{.name = window_or_plot_name});
                        strcpy_s(window_or_plot_name, "");
                        ImGui::CloseCurrentPopup();
                    };
                    ImGui::EndPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::Button("Save snapshot")) {
                m_dbghelp_symbols.saveSnapshotToFile("snapshot.json");
            }
            if (ImGui::Button("Load snapshot")) {
                // Pause during snapshot loading so that the execution continues from point when
                // load button was pressed
                bool paused = m_paused;
                m_paused = true;
                // Wait until main thread goes to pause state
                while (m_next_sync_timestamp > 0) {
                }
                m_dbghelp_symbols.loadSnapshotFromFile("snapshot.json");
                m_paused = paused;
            }

            if (ImGui::Button("Clear saved settings")) {
                m_options.clear_saved_settings = true;
            }

            ImGui::EndMenu();
        }

        // Start stop
        const char* start_stop_text = m_paused ? "Start" : "Pause";
        if (ImGui::Button(start_stop_text)) {
            m_paused = !m_paused;
        }
        HelpMarker("Hotkey for start/pause is space. Shift+space advances one step. Hold shift+space to advance very slowly.");
        ImGui::SameLine();

        // Simulation speed
        ImGui::PushItemWidth(ImGui::CalcTextSize("Simulation speed XXXXXXX").x);
        ImGui::SliderFloat("##Simulation speed", &m_simulation_speed, 1e-4f, 10, "Simulation speed %.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        ImGui::SameLine();
        HelpMarker("Simulated speed relative to real time. Hotkey to double speed is \"numpad +\" and halve \"numpad -\".");
        ImGui::SameLine();

        if (m_pause_at_time > m_sample_timestamp + std::numeric_limits<double>::epsilon()) {
            ImGui::Text("Pausing after %g", m_pause_at_time - m_plot_timestamp);
        }

        ImGui::EndMainMenuBar();
    }
}

bool scalarGroupHasVisibleItems(SignalGroup<Scalar> const& group, std::string const& filter) {
    bool group_has_visible_items = false;
    std::function<void(SignalGroup<Scalar> const&, std::string const&)> check_group_for_visible_items =
        [&](SignalGroup<Scalar> const& group, std::string const& filter) {
            for (Scalar* scalar : group.signals) {
                if (filter.empty()) {
                    group_has_visible_items |= !scalar->hide_from_scalars_window;
                } else if (!scalar->hide_from_scalars_window) {
                    group_has_visible_items |= fts::fuzzy_match_simple(filter.c_str(), scalar->name.c_str());
                }
            }
            for (auto const& subgroup : group.subgroups) {
                check_group_for_visible_items(subgroup.second, filter);
            }
        };
    check_group_for_visible_items(group, filter);
    for (auto const& subgroup : group.subgroups) {
        check_group_for_visible_items(subgroup.second, filter);
    }
    return group_has_visible_items;
}

void DbgGui::showScalarWindow() {
    m_scalar_window_focus.focused = ImGui::Begin("Scalars");
    if (!m_scalar_window_focus.focused) {
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
            if (!scalarGroupHasVisibleItems(group, scalar_name_filter)) {
                return;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Group has to be opened automatically if signal in it matches the filter.
            // If there is no filter, then it should be kept open if it has been opened
            // manually before.
            if (!scalar_name_filter.empty()) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            } else if (!group.opened_manually) {
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);
            }
            bool group_opened = ImGui::TreeNode(group.name.c_str());
            if (scalar_name_filter.empty()) {
                group.opened_manually = group_opened;
            }

            // Show values inside the group
            if (group_opened) {
                if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                    delete_entire_group = true;
                }
                // Symbols can be dragged from one group to another for easier reorganizing if symbol
                // is initially added to wrong group
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
                        uint64_t id = *(uint64_t*)payload->Data;
                        Scalar* scalar = getScalar(id);
                        // Do nothing if dragged to same group.
                        // Old one will be deleted if new one is added.
                        if (scalar->group != group.full_name
                            && addSymbol(scalar->name, group.full_name, scalar->alias, scalar->scale, scalar->offset)) {
                            scalar->deleted = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Show subgroups first
                for (auto& subgroup : group.subgroups) {
                    show_scalar_group(subgroup.second, delete_entire_group);
                }

                // Show each scalar
                for (Scalar* scalar : scalars) {
                    bool hide_by_filter = !scalar_name_filter.empty() && !fts::fuzzy_match_simple(scalar_name_filter.c_str(), scalar->name.c_str());
                    if (scalar->hide_from_scalars_window || hide_by_filter) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    // Show name. Text is used instead of selectable because the
                    // keyboard navigation in the table does not work properly
                    // and up/down changes columns
                    bool custom_scale_or_offset = scalar->scale != 1 || scalar->offset != 0;
                    if (custom_scale_or_offset) {
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
                    addInputScalar(scalar->src, "##scalar_" + scalar->name_and_group, scalar->scale, scalar->offset);
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

bool vectorGroupHasVisibleItems(SignalGroup<Vector2D> const& group, std::string const& filter) {
    bool group_has_visible_items = false;
    std::function<void(SignalGroup<Vector2D> const&, std::string const&)> check_group_for_visible_items =
        [&](SignalGroup<Vector2D> const& group, std::string const& filter) {
            for (Vector2D* vector : group.signals) {
                group_has_visible_items |= filter.empty() || fts::fuzzy_match_simple(filter.c_str(), vector->name.c_str());
            }
            for (auto const& subgroup : group.subgroups) {
                check_group_for_visible_items(subgroup.second, filter);
            }
        };
    check_group_for_visible_items(group, filter);
    for (auto const& subgroup : group.subgroups) {
        check_group_for_visible_items(subgroup.second, filter);
    }
    return group_has_visible_items;
}

void DbgGui::showVectorWindow() {
    m_vector_window_focus.focused = ImGui::Begin("Vectors");
    if (!m_vector_window_focus.focused) {
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
            if (!vectorGroupHasVisibleItems(group, vector_name_filter)) {
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

            if (group_opened) {
                // Show subgroups first
                for (auto& subgroup : group.subgroups) {
                    show_vector_group(subgroup.second, delete_entire_group);
                }

                for (Vector2D* signal : vectors) {
                    if (!vector_name_filter.empty() && !fts::fuzzy_match_simple(vector_name_filter.c_str(), signal->name.c_str())) {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    // Show name
                    ImGui::Text(signal->name.c_str());
                    // Make text drag-and-droppable
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("VECTOR_ID", &signal->id, sizeof(uint64_t));
                        ImGui::Text("Drag to vector plot");
                        ImGui::EndDragDropSource();
                    }

                    // Mark signal as deleted
                    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                        signal->deleted = true;
                    }

                    // Show x-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::format("##{}x", signal->x->name_and_group).c_str());
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &signal->x->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    bool custom_x_scale_or_offset = signal->x->scale != 1 || signal->x->offset != 0;
                    if (custom_x_scale_or_offset) {
                        ImGui::TextColored(COLOR_GRAY, numberAsStr(signal->x->getScaledValue()).c_str());
                    } else {
                        ImGui::Text(numberAsStr(signal->x->getValue()).c_str());
                    }
                    addScalarContextMenu(signal->x);

                    // Show y-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable(std::format("##{}y", signal->y->name_and_group).c_str());
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &signal->y->id, sizeof(uint64_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    bool custom_y_scale_or_offset = signal->y->scale != 1 || signal->y->offset != 0;
                    if (custom_y_scale_or_offset) {
                        ImGui::TextColored(COLOR_GRAY, numberAsStr(signal->y->getScaledValue()).c_str());
                    } else {
                        ImGui::Text(numberAsStr(signal->y->getValue()).c_str());
                    }
                    addScalarContextMenu(signal->y);
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

        custom_window.focus.focused = ImGui::Begin(custom_window.name.c_str(), &custom_window.open);
        if (!custom_window.focus.focused) {
            ImGui::End();
            continue;
        }
        Scalar* signal_to_remove = nullptr;

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
                bool custom_scale_or_offset = scalar->scale != 1 || scalar->offset != 0;
                if (custom_scale_or_offset) {
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
                    signal_to_remove = scalar;
                }
                addScalarContextMenu(scalar);

                // Show value
                ImGui::TableNextColumn();
                addInputScalar(scalar->src, "##custom_" + scalar->name_and_group, scalar->scale, scalar->offset);
            }
            ImGui::EndTable();
        }

        ImGui::InvisibleButton("##canvas", ImVec2(std::max(ImGui::GetContentRegionAvail().x, 1.f), std::max(ImGui::GetContentRegionAvail().y, 1.f)));
        addCustomWindowDragAndDrop(custom_window);

        if (signal_to_remove) {
            remove(custom_window.scalars, signal_to_remove);
            size_t signals_removed = m_settings["custom_windows"][custom_window.name]["signals"].erase(signal_to_remove->group + " " + signal_to_remove->name);
            assert(signals_removed > 0);
        }

        ImGui::End();
    }
}

void DbgGui::showSymbolsWindow() {
    if (!ImGui::Begin("Symbols")) {
        ImGui::End();
        return;
    }
    static bool recursive_symbol_search = false;
    static bool recursive_search_toggled = false;

    // Just manually tested width that name, group and recursive boxes are visible.
    float name_and_group_boxes_width = ImGui::GetContentRegionAvail().x - 25 * ImGui::CalcTextSize("x").x;
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
    recursive_search_toggled = ImGui::Checkbox("Recursive", &recursive_symbol_search);

    static ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                       | ImGuiTableFlags_BordersH
                                       | ImGuiTableFlags_Resizable
                                       | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("symbols_table", 2, table_flags)) {
        for (VariantSymbol* symbol : m_symbol_search_results) {
            // Recursive lambda for displaying children in the table
            std::function<void(VariantSymbol*)> show_children = [&](VariantSymbol* sym) {
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
            };
            show_children(symbol);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
