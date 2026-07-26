// MIT License
//
// Copyright (c) 2024 vvainola
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

#include "csvplot.h"

#include "csv_lua_signal.h"
#include "imgui_helpers.h"
#include "imgui_stdlib.h"
#include "lua_script_editor.h"
#include "lua_syntax_highlighter.h"

#include <algorithm>
#include <chrono>
#include <format>

namespace {

void showCsvLuaHelpMarker() {
    ImGui::TextDisabled("(?)");
    if (!ImGui::IsItemHovered()) {
        return;
    }

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted("Return one number for each CSV sample.");
    ImGui::Separator();
    ImGui::TextUnformatted("CsvPlotter API:");
    ImGui::TextColored(LUA_BUILTIN_COLOR, "read(\"signal\" [, offset])");
    ImGui::TextUnformatted("Read relative to the current sample. The offset defaults to 0.");
    ImGui::BulletText("0: current sample");
    ImGui::BulletText("-1: previous sample");
    ImGui::BulletText("Out-of-range reads return 0");
    ImGui::Spacing();
    ImGui::TextUnformatted("The output signal can read its own previously generated samples.");
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

} // namespace

void CsvPlotter::showScriptWindow() {
    if (!m_show_script_window) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scripts",
                      &m_show_script_window,
                      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (!m_selected_script_id.has_value() && !m_scripts.empty()) {
        m_selected_script_id = m_scripts.front().id;
    }

    std::optional<size_t> script_to_delete;
    if (ImGui::BeginTable("scripts_layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Scripts", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextColumn();
        if (ImGui::CollapsingHeader("Menu")) {
            if (ImGui::Button("Add script")) {
                // Scripts are created by individual UI clicks, so the current
                // epoch tick is effectively unique. Still handle a restored or
                // same-tick collision before using it as a persistent ID.
                uint64_t id = static_cast<uint64_t>(
                  std::chrono::system_clock::now().time_since_epoch().count());
                while (id == 0 || std::ranges::find(m_scripts, id, &CsvScript::id) != m_scripts.end()) {
                    ++id;
                }
                std::string const name = std::format("new script {}", m_scripts.size() + 1);
                m_scripts.push_back(CsvScript{
                  .id = id,
                  .name = name,
                  .output_name = std::format("custom signal {}", m_scripts.size() + 1),
                  .text = "return read(\"signal\", 0)",
                });
                m_selected_script_id = id;
            }
        }
        ImGui::Separator();
        ImGui::BeginChild("##script_list", ImVec2(0, 0));
        // During a drag swap, the same script is submitted in two rows for one
        // frame. This is the pattern used by ImGui's simple reorder demo.
        ImGui::PushItemFlag(ImGuiItemFlags_AllowDuplicateId, true);
        for (size_t script_index = 0; script_index < m_scripts.size(); ++script_index) {
            CsvScript& script = m_scripts[script_index];
            // The stable ID lets ImGui keep the drag active after the script
            // has moved to the adjacent row.
            ImGui::PushID(std::to_string(script.id).c_str());
            if (ImGui::Selectable(script.name.c_str(), m_selected_script_id == script.id)) {
                m_selected_script_id = script.id;
            }
            bool const reorder_script = ImGui::IsItemActive() && !ImGui::IsItemHovered();
            if (ImGui::BeginPopupContextItem("Script context")) {
                ImGui::InputText("Name", &script.name);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete")) {
                    script_to_delete = script_index;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            // This follows ImGui's simple reorder demo: swap with the adjacent
            // item after a vertical drag, then reset the drag delta for repeats.
            if (!script_to_delete.has_value() && reorder_script) {
                int const next_script_index = static_cast<int>(script_index)
                                            + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
                if (next_script_index >= 0 && next_script_index < static_cast<int>(m_scripts.size())) {
                    std::swap(m_scripts[script_index], m_scripts[next_script_index]);
                    ImGui::ResetMouseDragDelta();
                }
            }
        }
        ImGui::PopItemFlag();
        ImGui::EndChild();

        if (script_to_delete.has_value()) {
            size_t const index = *script_to_delete;
            uint64_t const id = m_scripts[index].id;
            m_scripts.erase(m_scripts.begin() + index);
            if (m_selected_script_id == id) {
                if (m_scripts.empty()) {
                    m_selected_script_id.reset();
                } else {
                    m_selected_script_id = m_scripts[std::min(index, m_scripts.size() - 1)].id;
                }
            }
        }

        ImGui::TableNextColumn();
        ImGui::BeginChild("##script_editor", ImVec2(0, 0));
        auto selected = std::find_if(m_scripts.begin(), m_scripts.end(), [&](CsvScript const& script) {
            return m_selected_script_id == script.id;
        });
        if (selected == m_scripts.end()) {
            ImGui::TextDisabled("Select a script or add a new one.");
        } else {
            CsvScript& script = *selected;
            bool const has_target_file = !m_selected_signals.empty();
            CsvFileData* const target_file = has_target_file ? m_selected_signals.front()->file : nullptr;
            std::string const target_file_name = target_file != nullptr ? target_file->displayed_name : "";

            ImGui::BeginDisabled(!has_target_file);
            if (ImGui::Button("Run")) {
                CsvFileData* file = target_file;
                bool same_file = std::ranges::all_of(m_selected_signals, [&](CsvSignal const* signal) {
                    return signal->file == file;
                });
                if (!same_file) {
                    m_error_message = "Selected signals must be from the same file";
                } else if (script.output_name.empty()) {
                    m_error_message = "Output signal name cannot be empty";
                } else if (script.text.empty()) {
                    m_error_message = "Script cannot be empty";
                } else {
                    std::vector<CsvLuaSignalInput> inputs;
                    inputs.reserve(file->signals.size());
                    for (CsvSignal const& signal : file->signals) {
                        inputs.push_back({.name = signal.name, .samples = signal.samples});
                    }

                    size_t const sample_count = m_selected_signals.front()->samples.size();
                    std::expected<std::vector<double>, std::string> samples =
                        evaluateCsvLuaSignal(script.text, script.output_name, inputs, sample_count);
                    if (!samples.has_value()) {
                        m_error_message = samples.error();
                    } else {
                        // Match by creator rather than output name because the
                        // user may rename either the script or its output.
                        auto existing = std::ranges::find(file->signals, script.id, &CsvSignal::custom_script_id);
                        if (existing != file->signals.end()) {
                            std::string const old_name = existing->name;
                            existing->name = script.output_name;
                            existing->samples = std::move(*samples);
                            if (auto transform = m_signal_transform_settings.find(existing->name);
                                transform != m_signal_transform_settings.end()) {
                                existing->transform = transform->second;
                            } else {
                                existing->transform = {};
                            }
                            if (old_name != existing->name) {
                                auto rename_plot_setting = [&](PlotBase& plot) {
                                    for (std::string& name : plot.settings.scalar_signals) {
                                        if (name == old_name) {
                                            name = existing->name;
                                        }
                                    }
                                    for (auto& [first, second] : plot.settings.signal_pairs) {
                                        if (first == old_name) {
                                            first = existing->name;
                                        }
                                        if (second == old_name) {
                                            second = existing->name;
                                        }
                                    }
                                };
                                for (PlotBase& plot : m_docked_plots) {
                                    rename_plot_setting(plot);
                                }
                                for (PlotBase& plot : m_undocked_plots) {
                                    rename_plot_setting(plot);
                                }
                            }
                        } else {
                            // CsvSignal pointers are stored throughout the plot
                            // state. Never let this append reallocate the vector.
                            if (file->signals.size() == file->signals.capacity()) {
                                m_error_message = "Cannot add another custom signal without invalidating active plot references";
                            } else {
                                CsvSignal custom_signal{
                                  .name = script.output_name,
                                  .samples = std::move(*samples),
                                  .file = file,
                                  .transform = {},
                                  .custom_script_id = script.id,
                                };
                                if (auto transform = m_signal_transform_settings.find(custom_signal.name);
                                    transform != m_signal_transform_settings.end()) {
                                    custom_signal.transform = transform->second;
                                }
                                file->signals.push_back(std::move(custom_signal));
                                // The plot settings may already contain this
                                // output name from a previous session.
                                applyPlottedSignals(*file);
                            }
                        }
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(250.0f);
            ImGui::InputText("Output", &script.output_name);
            ImGui::SameLine();
            showCsvLuaHelpMarker();

            if (has_target_file) {
                ImGui::Text("Target: %s", target_file_name.c_str());
            } else {
                ImGui::TextDisabled("Select any signal from the target CSV file before running.");
            }

            ImGui::Separator();
            inputScriptTextWithLineNumbers(script.text, ImGui::GetContentRegionAvail(), true);
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
}
