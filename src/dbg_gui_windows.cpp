// Dear ImGui: standalone example application for GLFW + OpenGL 3, using
// programmable pipeline (GLFW is a cross-platform general purpose library for
// handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation,
// etc.) If you are new to Dear ImGui, read documentation from the docs/ folder
// + read the top of imgui.cpp. Read online:
// https://github.com/ocornut/imgui/tree/master/docs

#include "dbg_gui.h"
#include "imgui.h"

template <typename T>
std::string numberAsStr(T number) {
    if constexpr (std::is_same_v<double, T> || std::is_same_v<float, T>) {
        // Remove trailing zeros
        std::stringstream ss;
        ss << number;
        return ss.str();
    } else {
        return std::to_string(number);
    }
}

static std::string getSourceValueStr(ValueSource src) {
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
    if (ImGui::IsKeyPressed(ImGuiKey_Minus)) {
        return ImGuiKey_Minus;
    }
    return std::nullopt;
}

void addInputScalar(ValueSource const& signal_src, std::string const& label) {
    if (std::get_if<ReadWriteFnCustomStr>(&signal_src)) {
        ImGui::Text(getSourceValueStr(signal_src).c_str());
        ImGui::SameLine();
    }

    ImGuiInputTextFlags edit_flags = ImGuiInputTextFlags_EnterReturnsTrue
                                   | ImGuiInputTextFlags_AutoSelectAll
                                   | ImGuiInputTextFlags_CharsScientific
                                   | ImGuiInputTextFlags_CharsDecimal
                                   | ImGuiInputTextFlags_CallbackAlways;
    char value[20];
    strcpy_s(value, numberAsStr(getSourceValue(signal_src)).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    static ImGuiKey pressed_number = ImGuiKey_None;
    if (ImGui::InputText(label.c_str(), value, sizeof(value), edit_flags, setCursorOnFirstNumberPress, (void*)&pressed_number)) {
        setSourceValue(signal_src, std::stod(value));
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
        double pause_level = getSourceValue(scalar->src);
        ImGui::InputDouble("Trigger level", &pause_level, 0, 0, "%.3f");
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            scalar->addTrigger(pause_level);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Copy name")) {
            ImGui::SetClipboardText(scalar->alias.c_str());
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Copy name and value")) {
            ImGui::SetClipboardText((scalar->alias + " " + getSourceValueStr(scalar->src)).c_str());
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

void DbgGui::showConfigurationWindow() {
    if (!ImGui::Begin("Configuration")) {
        ImGui::End();
        return;
    }

    ImGui::Text("Time %.3f s", m_timestamp);
    ImGui::SameLine();
    const char* start_stop_text = m_paused ? "Start" : "Pause";
    if (ImGui::Button(start_stop_text)) {
        m_paused = !m_paused;
    }
    ImGui::PushItemWidth(0.5f * ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("Simulation speed", &m_simulation_speed, 1e-5f, 10, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::InputScalar("Pause after", ImGuiDataType_Double, &m_time_until_pause, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);

    if (ImGui::Button("Add..")) {
        ImGui::OpenPopup("##Add");
    }

    if (ImGui::Button("Save state")) {
        m_dbghelp_symbols.saveState();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load state")) {
        m_dbghelp_symbols.loadState();
    }

    if (ImGui::BeginPopup("##Add")) {
        // Always center this window when appearing
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::Button("Scalar plot")) {
            ImGui::OpenPopup("Add scalar plot");
        }
        static char plot_name[256] = "";
        if (ImGui::BeginPopupModal("Add scalar plot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::InputText("Name",
                                 plot_name,
                                 IM_ARRAYSIZE(plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_scalar_plots.push_back(ScalarPlot{.name = plot_name,
                                                    .y_axis_min = -1,
                                                    .y_axis_max = 1,
                                                    .x_range = 1});
                strcpy_s(plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }

        if (ImGui::Button("Vector plot")) {
            ImGui::OpenPopup("Add vector plot");
        }
        if (ImGui::BeginPopupModal("Add vector plot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::InputText("Vector plot name",
                                 plot_name,
                                 IM_ARRAYSIZE(plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_vector_plots.push_back(VectorPlot{.name = plot_name});
                strcpy_s(plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }

        if (ImGui::Button("Spectrum plot")) {
            ImGui::OpenPopup("Add spectrum plot");
        }
        if (ImGui::BeginPopupModal("Add spectrum plot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::InputText("Spectrum plot name",
                                 plot_name,
                                 IM_ARRAYSIZE(plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_spectrum_plots.push_back(SpectrumPlot{.name = plot_name});
                strcpy_s(plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);
    ImGui::End();
}

void DbgGui::showScalarWindow() {
    if (!ImGui::Begin("Scalars")) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("scalar_table",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, num_width);

        for (auto it = m_scalar_groups.begin(); it != m_scalar_groups.end(); it++) {
            std::vector<Scalar*> const& scalars = it->second;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::TreeNode(it->first.c_str())) {
                for (int row = 0; row < scalars.size(); row++) {
                    Scalar* scalar = scalars[row];
                    if (scalar->hide_from_scalars_window) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    // Show name. Text is used instead of selectable because the
                    // keyboard navigation in the table does not work properly
                    // and up/down changes columns
                    ImGui::Text(scalar->alias.c_str());
                    // Make text drag-and-droppable
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &scalar->id, sizeof(size_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    // Hide symbol on delete. It will be removed for real on next start
                    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                        m_settings["scalar_symbols"].erase(scalar->name_and_group);
                        m_settings["scalars"].erase(scalar->name_and_group);
                        scalar->hide_from_scalars_window = true;
                    }
                    addScalarContextMenu(scalar);

                    // Show value
                    ImGui::TableNextColumn();
                    addInputScalar(scalar->src, "##scalar_" + scalar->name_and_group);
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DbgGui::showVectorWindow() {
    if (!ImGui::Begin("Vectors")) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("vector_table",
                          3,
                          ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, num_width);
        ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, num_width);
        for (auto it = m_vector_groups.begin(); it != m_vector_groups.end(); it++) {
            std::vector<Vector2D*> const& vectors = it->second;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::TreeNode(it->first.c_str())) {
                for (int row = 0; row < vectors.size(); row++) {
                    Vector2D* signal = vectors[row];
                    if (signal->hide_from_vector_window) {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    // Show name
                    ImGui::Text(signal->name.c_str());
                    // Make text drag-and-droppable
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("VECTOR_ID", &signal->id, sizeof(size_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }

                    // Hide symbol on delete. It will be removed for real on next start
                    if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                        m_settings["vector_symbols"].erase(signal->name_and_group);
                        m_settings["scalars"].erase(signal->x->name_and_group);
                        m_settings["scalars"].erase(signal->y->name_and_group);
                        signal->hide_from_vector_window = true;
                    }

                    // Show x-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable("##x");
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &signal->x->id, sizeof(size_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    std::string value = getSourceValueStr(signal->x->src);
                    ImGui::Text(value.c_str());

                    // Show y-value
                    ImGui::TableNextColumn();
                    ImGui::Selectable("##y");
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("SCALAR_ID", &signal->y->id, sizeof(size_t));
                        ImGui::Text("Drag to plot");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    value = getSourceValueStr(signal->y->src);
                    ImGui::Text(value.c_str());
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DbgGui::showCustomWindow() {
    if (!ImGui::Begin("Custom")) {
        ImGui::End();
        return;
    }
    Scalar* signal_to_remove = nullptr;
    if (ImGui::BeginTable("custom_table",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, num_width);
        for (Scalar* scalar : m_custom_window_scalars) {
            ImGui::TableNextColumn();
            // Show name. Text is used instead of selectable because the
            // keyboard navigation in the table does not work properly
            // and up/down changes columns
            ImGui::Text(scalar->alias_and_group.c_str());
            // Make text drag-and-droppable
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("SCALAR_ID", &scalar->id, sizeof(size_t));
                ImGui::Text("Drag to plot");
                ImGui::EndDragDropSource();
            }
            // Hide symbol on delete
            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_::ImGuiKey_Delete)) {
                signal_to_remove = scalar;
                m_settings["custom_window_signals"].erase(scalar->name_and_group);
            }
            addScalarContextMenu(scalar);

            // Show value
            ImGui::TableNextColumn();
            addInputScalar(scalar->src, "##custom_" + scalar->name_and_group);
        }
        ImGui::EndTable();
    }

    ImGui::InvisibleButton("##canvas", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
            size_t id = *(size_t*)payload->Data;
            Scalar* scalar = m_scalars[id].get();
            m_custom_window_scalars.push_back(scalar);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
            VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
            Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
            m_custom_window_scalars.push_back(scalar);
        }
        ImGui::EndDragDropTarget();
    }
    if (signal_to_remove) {
        remove(m_custom_window_scalars, signal_to_remove);
    }

    ImGui::End();
}

void DbgGui::showSymbolsWindow() {
    if (!ImGui::Begin("Symbols")) {
        ImGui::End();
        return;
    }

    static ImGuiTableFlags table_flags = ImGuiTableFlags_BordersV
                                       | ImGuiTableFlags_BordersH
                                       | ImGuiTableFlags_Resizable
                                       | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("symbols_table", 2, table_flags)) {
        ImGui::TableNextColumn();
        static char symbols_to_search[MAX_NAME_LENGTH];
        if (ImGui::InputText("Name", symbols_to_search, MAX_NAME_LENGTH, ImGuiInputTextFlags_CharsNoBlank)) {
            if (std::string(symbols_to_search).size() > 2) {
                m_symbol_search_results = m_dbghelp_symbols.findMatchingRootSymbols(symbols_to_search);
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
        ImGui::TableNextColumn();
        ImGui::InputText("Group", m_group_to_add_symbols, MAX_NAME_LENGTH);
        for (int i = 0; i < m_symbol_search_results.size(); ++i) {
            VariantSymbol* symbol = m_symbol_search_results[i];

            std::function<void(VariantSymbol*)> show_children = [&](VariantSymbol* sym) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                std::vector<std::unique_ptr<VariantSymbol>>& children = sym->getChildren();
                if (children.size() > 0) {
                    bool open = ImGui::TreeNodeEx(sym->getName().c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text(sym->valueAsStr().c_str());
                    if (open) {
                        for (std::unique_ptr<VariantSymbol>& child : children) {
                            show_children(child.get());
                        }
                        ImGui::TreePop();
                    }
                } else if (sym->getType() == VariantSymbol::Type::Pointer) {
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
                    VariantSymbol* pointed_symbol = sym->getPointedSymbol();
                    if (!pointed_symbol) {
                        flags |= ImGuiTreeNodeFlags_Leaf;
                    }
                    bool open = ImGui::TreeNodeEx(sym->getName().c_str(), flags);
                    ImGui::TableNextColumn();
                    ImGui::Text(sym->valueAsStr().c_str());
                    if (open) {
                        if (pointed_symbol) {
                            show_children(pointed_symbol);
                        }
                        ImGui::TreePop();
                    }
                } else {
                    static bool selected_symbol_idx = 0;
                    static VariantSymbol* selected_symbols[2] = {nullptr, nullptr};
                    bool selected = (sym == selected_symbols[0]) || (sym == selected_symbols[1]);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf;
                    if (selected) {
                        flags |= ImGuiTreeNodeFlags_Selected;
                    }
                    ImGui::TreeNodeEx(sym->getName().c_str(), flags);
                    ImGui::TreePop();
                    if (ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
                        // Clear if already selected
                        if (selected_symbols[0] == sym || selected_symbols[1] == sym) {
                            selected_symbol_idx = 0;
                            selected_symbols[0] = nullptr;
                            selected_symbols[1] = nullptr;
                        } else {
                            selected_symbols[selected_symbol_idx] = sym;
                            selected_symbol_idx = (selected_symbol_idx + 1) % 2;
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
                        if (arithmetic_or_enum)
                            addScalarSymbol(sym, m_group_to_add_symbols);
                    }

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
