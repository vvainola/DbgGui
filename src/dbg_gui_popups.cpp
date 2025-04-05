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

#include "dbg_gui.h"
#include "imgui.h"

#include <assert.h>

void DbgGui::addPopupModal(std::string const& modal_name) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        return;
    }
    static char window_or_plot_name[256] = "";
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); // Center modal
    if (modal_name == str::ADD_SCALAR_PLOT) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                ScalarPlot plot(window_or_plot_name, hashWithTime(window_or_plot_name));
                plot.y_axis = {-1, 1};
                plot.x_axis = {0, 1};
                plot.x_range = 1;
                m_scalar_plots.push_back(plot);
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::ADD_VECTOR_PLOT) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Vector plot name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_vector_plots.push_back(VectorPlot(window_or_plot_name, hashWithTime(window_or_plot_name)));
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::ADD_CUSTOM_WINDOW) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Custom window name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_custom_windows.push_back(CustomWindow(window_or_plot_name, hashWithTime(window_or_plot_name)));
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::ADD_SCRIPT_WINDOW) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Script window name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_script_windows.push_back(ScriptWindow{this, window_or_plot_name, hashWithTime(window_or_plot_name)});
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::ADD_GRID_WINDOW) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Grid window name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_grid_windows.emplace_back(GridWindow(window_or_plot_name, hashWithTime(window_or_plot_name)));
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::ADD_SPECTRUM_PLOT) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Spectrum plot name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_spectrum_plots.push_back(SpectrumPlot(window_or_plot_name, hashWithTime(window_or_plot_name)));
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::ADD_DOCKSPACE) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); // Center modal
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("Dockspace name",
                                 window_or_plot_name,
                                 IM_ARRAYSIZE(window_or_plot_name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                // Add clock to hash calculation because dockspace name can change later and the user might
                // create a new dockspace which could have same name as the original and they would result in same id
                m_dockspaces.push_back(DockSpace(window_or_plot_name, hashWithTime(window_or_plot_name)));
                strcpy_s(window_or_plot_name, "");
                ImGui::CloseCurrentPopup();
            };
            ImGui::EndPopup();
        }
    } else if (modal_name == str::PAUSE_AFTER) {
        if (ImGui::BeginPopupModal(str::PAUSE_AFTER, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            double pause_after = std::max(m_pause_at_time - m_sample_timestamp, 0.0);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputDouble("##Pause after", &pause_after, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_pause_at_time = m_sample_timestamp + pause_after;
                ImGui::CloseCurrentPopup();
            };
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else if (modal_name == str::PAUSE_AT) {
        if (ImGui::BeginPopupModal(modal_name.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputDouble("##Pause at", &m_pause_at_time, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue)) {
                ImGui::CloseCurrentPopup();
            };
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else {
        assert(0);
    }
}

void DbgGui::saveSnapshot() {
    // Pause during snapshot saving so that all symbols are from same time instant
    bool paused = m_paused;
    m_paused = true;
    // Wait until main thread goes to pause state
    while (m_next_sync_timestamp > 0) {
    }
    m_saved_snapshot = m_dbghelp_symbols.saveSnapshotToMemory();
    m_paused = paused;
}

void DbgGui::loadSnapshot() {
    // Pause during snapshot loading so that the execution continues from point when load button was pressed
    bool paused = m_paused;
    m_paused = true;
    // Wait until main thread goes to pause state
    while (m_next_sync_timestamp > 0) {
    }
    m_dbghelp_symbols.loadSnapshotFromMemory(m_saved_snapshot);
    m_paused = paused;
}

void DbgGui::showErrorModal() {
    if (!m_error_message.empty()) {
        ImGui::OpenPopup("Error");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); // Center modal
    if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || m_error_message.empty()) {
            m_error_message.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::Text(m_error_message.c_str());
        ImGui::EndPopup();
    }

    if (!m_info_message.empty()) {
        ImGui::OpenPopup("Info");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); // Center modal
    if (ImGui::BeginPopupModal("Info", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || m_info_message.empty()) {
            m_info_message.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::Text(m_info_message.c_str());
        ImGui::EndPopup();
    }
}
