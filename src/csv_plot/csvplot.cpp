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

#define _CRT_SECURE_NO_WARNINGS

#include <string>

#if WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "resource.h"
#endif

#if WINDOWS
inline const std::string SETTINGS_LOCATION = "USERPROFILE";
#else
inline const std::string SETTINGS_LOCATION = "HOME";
#endif

#include "csvplot.h"
#include "themes.h"
#include "imgui.h"
#include "imgui_helpers.h"
#include "imgui_stdlib.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "save_image.h"
#include "multi_select_helpers.h"
#include "csv_helpers.h"
#include "custom_signal.hpp"
#include "imgui_settings_migration.h"
#include "minmax.h"
#include "sample_clipboard.h"
#include "stb_image.h"
#include "version.h"
#include "magic_enum.hpp"

#include <format>
#include <cmath>
#include <nfd.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <span>
#include <set>
#include <charconv>
#include <optional>

#define TRY(expression)                              \
    try {                                            \
        expression                                   \
    } catch (nlohmann::json::exception const& err) { \
        addSettingsLoadError(err.what());            \
    } catch (std::exception const& err) {            \
        addSettingsLoadError(err.what());            \
    }

inline constexpr int32_t MIN_FONT_SIZE = 8;
inline constexpr int32_t MAX_FONT_SIZE = 100;
inline constexpr ImVec4 COLOR_TOOLTIP_LINE = ImVec4(0.7f, 0.7f, 0.7f, 0.6f);
inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.7f, 0.7f, 0.7f, 1);
inline constexpr ImVec4 COLOR_WHITE = ImVec4(1, 1, 1, 1);
inline constexpr ImVec4 COLOR_LIGHT_BLUE = ImVec4(0.4f, 0.6f, 0.9f, 1);
inline constexpr ImVec4 COLOR_GREEN = ImVec4(0.3f, 0.9f, 0.3f, 1);
// Render few frames before saving image because plot are not immediately autofitted correctly
inline int IMAGE_SAVE_FRAME_COUNT = 3;
inline constexpr unsigned MAX_NAME_LENGTH = 255;
constexpr int CUSTOM_SIGNAL_CAPACITY = 10;
inline float COMPARISON_MODE_ACTIVATION_TIME = 0.5f;
std::vector<double> ASCENDING_NUMBERS;

std::unique_ptr<CsvFileData> parseCsvData(std::string filename);
std::vector<std::unique_ptr<CsvFileData>> openCsvFromFileDialog();
std::vector<std::string> openDialogMultiple();
void setLayout(ImGuiID main_dock, int rows, int cols, float signals_window_width);
std::pair<int32_t, int32_t> getTimeIndices(std::span<double const> time, double start_time, double end_time);

std::optional<int> parseInt(std::string_view text) {
    int value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

int stablePlotIndex(int row, int col) {
    return row * MAX_PLOTS + col;
}

int visibleToStablePlotIndex(int plot_idx, int cols) {
    cols = std::clamp(cols, 1, MAX_PLOTS);
    return stablePlotIndex(plot_idx / cols, plot_idx % cols);
}

bool plotCsvSamples(std::string const& label_id,
                    double const* x,
                    double const* y,
                    int count,
                    CsvPlotStyle plot_style) {
    if (plot_style == CsvPlotStyle::Linear) {
        return ImPlot::PlotLine(label_id.c_str(), x, y, count, ImPlotLineFlags_None);
    }

    ImPlotStairsFlags stairs_flags = plot_style == CsvPlotStyle::LeadingStairs ? ImPlotStairsFlags_PreStep : ImPlotStairsFlags_None;
    return ImPlot::PlotStairs(label_id.c_str(), x, y, count, stairs_flags);
}

CsvSignal* findSignalByName(CsvFileData& file, std::string const& name) {
    for (CsvSignal& signal : file.signals) {
        if (signal.name == name) {
            return &signal;
        }
    }
    return nullptr;
}

std::vector<CsvSignal*> selectedOrClickedSignals(CsvSignal* signal,
                                                 std::vector<CsvSignal*> const& selected_signals) {
    if (contains(selected_signals, signal)) {
        return selected_signals;
    }
    return {signal};
}

void addUniquePair(std::vector<std::pair<std::string, std::string>>& pairs,
                   std::pair<std::string, std::string> const& pair) {
    if (std::find(pairs.begin(), pairs.end(), pair) == pairs.end()) {
        pairs.push_back(pair);
    }
}

bool pairTouchesLoadedSignal(std::pair<std::string, std::string> const& pair,
                             std::set<std::string> const& loaded_names) {
    return loaded_names.contains(pair.first)
        || (!pair.second.empty() && loaded_names.contains(pair.second));
}

static std::string jsonValueToExpressionString(nlohmann::json const& value) {
    if (value.is_number()) {
        return std::format("{:g}", value.get<double>());
    }
    return value.get<std::string>();
}

static std::expected<void, std::string> setSignalTransformScale(CsvSignalTransform& transform,
                                                                std::string const& scale_expression) {
    auto scale = str::evaluateExpression(scale_expression);
    if (!scale.has_value()) {
        return std::unexpected(scale.error());
    }

    transform.scale_expression = scale_expression;
    transform.scale = scale.value();
    return {};
}

static std::expected<void, std::string> setSignalTransformOffset(CsvSignalTransform& transform,
                                                                 std::string const& offset_expression) {
    auto offset = str::evaluateExpression(offset_expression);
    if (!offset.has_value()) {
        return std::unexpected(offset.error());
    }

    transform.offset_expression = offset_expression;
    transform.offset = offset.value();
    return {};
}

static void loadImguiLayout(std::string layout) {
    imgui_settings::migrateLayoutIniHashes(layout);
    ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
}

int32_t binarySearch(std::span<double const> values, double searched_value, int32_t start, int32_t end) {
    int32_t original_start = start;
    int32_t mid = std::midpoint(start, end);
    while (start <= end) {
        mid = std::midpoint(start, end);
        double val = values[mid];
        if (val < searched_value) {
            start = mid + 1;
        } else if (val > searched_value) {
            end = mid - 1;
        } else {
            return mid;
        }
    }
    return std::max(original_start, end);
}

std::pair<int32_t, int32_t> getTimeIndices(std::span<double const> time, double start_time, double end_time) {
    int32_t start_idx = 0;
    int32_t end_idx = int32_t(time.size() - 1);
    // Add 1 extra point to both ends not have blanks at the edges. +2 because end range is exclusive
    start_idx = binarySearch(time, start_time, start_idx, end_idx) - 1;
    end_idx = binarySearch(time, end_time, start_idx, end_idx) + 2;
    end_idx = std::max(end_idx, start_idx);
    // Prevent overflows
    start_idx = std::max(start_idx, 0);
    end_idx = std::min(end_idx, (int32_t)time.size() - 1);
    return {start_idx, end_idx};
}

void CsvPlotter::applySignalTransforms(CsvFileData& file) {
    for (CsvSignal& signal : file.signals) {
        auto transform_it = m_signal_transform_settings.find(signal.name);
        signal.transform = transform_it == m_signal_transform_settings.end() ? CsvSignalTransform{} : transform_it->second;
    }
}

void CsvPlotter::applyPlottedSignals(CsvFileData& file) {
    auto apply_plot_settings = [&](PlotBase& plot) {
        CsvPlotSignalSettings const& settings = plot.settings;
        if (auto* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
            for (std::string const& signal_name : settings.scalar_signals) {
                if (CsvSignal* signal = findSignalByName(file, signal_name)) {
                    scalar_plot->addSignal(signal);
                }
            }
        } else if (auto* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
            for (auto const& signal_names : settings.signal_pairs) {
                CsvSignal* x = findSignalByName(file, signal_names.first);
                CsvSignal* y = findSignalByName(file, signal_names.second);
                if (x && y) {
                    vector_plot->addSignal({x, y});
                }
            }
        } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
            for (auto const& signal_names : settings.signal_pairs) {
                CsvSignal* real = findSignalByName(file, signal_names.first);
                CsvSignal* imag = signal_names.second.empty() ? nullptr : findSignalByName(file, signal_names.second);
                if (real && (signal_names.second.empty() || imag)) {
                    spectrum_plot->addSignal(real, imag);
                }
            }
        }
    };

    // Apply settings to every stored slot, including hidden docked/undocked plots,
    // so increasing rows/columns or undocked count later reveals already-restored state.
    for (PlotBase& plot : m_docked_plots) {
        apply_plot_settings(plot);
    }
    for (PlotBase& plot : m_undocked_plots) {
        apply_plot_settings(plot);
    }
}

void CsvPlotter::snapshotComparisonPlotSettings() {
    // Store names instead of CsvSignal pointers: a later file has different signal
    // instances, and a signal missing from one file must be eligible to return later.
    auto snapshot_plot = [](PlotBase const& plot) {
        CsvPlotSignalSettings settings;
        if (auto const* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
            for (CsvSignal const* signal : scalar_plot->signals) {
                if (!contains(settings.scalar_signals, signal->name)) {
                    settings.scalar_signals.push_back(signal->name);
                }
            }
        } else if (auto const* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
            for (auto const& signals : vector_plot->signals) {
                addUniquePair(settings.signal_pairs, {signals.first->name, signals.second->name});
            }
        } else if (auto const* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
            for (auto const& spectrum : spectrum_plot->spectrum) {
                addUniquePair(settings.signal_pairs,
                              {spectrum.real->name, spectrum.imag == nullptr ? "" : spectrum.imag->name});
            }
        }
        return settings;
    };

    for (int i = 0; i < MAX_PLOT_WINDOWS; ++i) {
        m_comparison.docked_plot_settings[i] = snapshot_plot(m_docked_plots[i]);
    }
    for (int i = 0; i < MAX_UNDOCKED_PLOTS; ++i) {
        m_comparison.undocked_plot_settings[i] = snapshot_plot(m_undocked_plots[i]);
    }
}

void CsvPlotter::showComparisonFile(CsvFileData& file) {
    // Series IDs contain the file name, so reset ImPlot's cache to assign the
    // colormap in the same snapshot order for every comparison file.
    m_comparison.reset_colors = true;
    auto populate_plot = [&](PlotBase& plot, CsvPlotSignalSettings const& settings) {
        // Discard manual additions from the previous comparison file before
        // rebuilding this plot from the session's original name-based layout.
        plot.clearPlot();
        if (auto* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
            for (std::string const& signal_name : settings.scalar_signals) {
                if (CsvSignal* signal = findSignalByName(file, signal_name)) {
                    scalar_plot->addSignal(signal);
                }
            }
        } else if (auto* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
            for (auto const& signal_names : settings.signal_pairs) {
                CsvSignal* x = findSignalByName(file, signal_names.first);
                CsvSignal* y = findSignalByName(file, signal_names.second);
                if (x && y) {
                    vector_plot->addSignal({x, y});
                }
            }
        } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
            for (auto const& signal_names : settings.signal_pairs) {
                CsvSignal* real = findSignalByName(file, signal_names.first);
                CsvSignal* imag = signal_names.second.empty() ? nullptr : findSignalByName(file, signal_names.second);
                if (real && (signal_names.second.empty() || imag)) {
                    spectrum_plot->addSignal(real, imag);
                }
            }
        }
    };

    for (int i = 0; i < MAX_PLOT_WINDOWS; ++i) {
        populate_plot(m_docked_plots[i], m_comparison.docked_plot_settings[i]);
    }
    for (int i = 0; i < MAX_UNDOCKED_PLOTS; ++i) {
        populate_plot(m_undocked_plots[i], m_comparison.undocked_plot_settings[i]);
    }
}

void CsvPlotter::updateComparisonMode() {
    if (!ImGui::GetIO().KeyAlt || m_csv_data.empty()) {
        // Releasing Alt leaves the last file plotted, but keeps its index for
        // the next comparison session.
        m_comparison.active = false;
        m_comparison.alt_hold_duration = 0;
        if (m_csv_data.empty()) {
            m_comparison.selected_file_index = 0;
        }
        return;
    }

    // Files may have been removed since the prior comparison session.
    m_comparison.selected_file_index = std::clamp(m_comparison.selected_file_index,
                                                  0,
                                                  (int)m_csv_data.size() - 1);
    if (!m_comparison.active) {
        m_comparison.alt_hold_duration += ImGui::GetIO().DeltaTime;
        if (m_comparison.alt_hold_duration < COMPARISON_MODE_ACTIVATION_TIME) {
            return;
        }

        m_comparison.active = true;
        // The snapshot remains unchanged while Alt is held, so missing signals
        // can reappear after cycling past a file that does not contain them.
        snapshotComparisonPlotSettings();
        showComparisonFile(*m_csv_data[m_comparison.selected_file_index]);
        return;
    }

    int file_index = m_comparison.selected_file_index;
    // Repeated key presses make it practical to scan a long list of files.
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat)) {
        file_index = (file_index + (int)m_csv_data.size() - 1) % (int)m_csv_data.size();
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat)) {
        file_index = (file_index + 1) % (int)m_csv_data.size();
    }
    if (file_index != m_comparison.selected_file_index) {
        m_comparison.selected_file_index = file_index;
        showComparisonFile(*m_csv_data[file_index]);
    }
}

void CsvPlotter::updatePlottedSignalSettings() {
    // Reconcile plot.settings with the signals currently plotted. Kept names:
    //   1. Currently plotted  - user still sees them.
    //   2. Loaded but cleared - dropped (user removed them).
    //   3. "Ghost"            - signal not in any open file; kept so its file
    //                           can restore it when (re)opened later.
    std::set<std::string> loaded_names;
    for (auto& file : m_csv_data) {
        for (CsvSignal& signal : file->signals) {
            loaded_names.insert(signal.name);
        }
    }

    auto update_plot_settings = [&](PlotBase& plot) {
        // Snapshot names of signals currently in the active variant.
        CsvPlotSignalSettings current_settings;
        if (auto* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
            for (CsvSignal* signal : scalar_plot->signals) {
                if (!contains(current_settings.scalar_signals, signal->name)) {
                    current_settings.scalar_signals.push_back(signal->name);
                }
            }
        } else if (auto* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
            for (auto const& signals : vector_plot->signals) {
                addUniquePair(current_settings.signal_pairs, {signals.first->name, signals.second->name});
            }
        } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
            for (auto const& spec : spectrum_plot->spectrum) {
                addUniquePair(current_settings.signal_pairs,
                              {spec.real->name, spec.imag == nullptr ? "" : spec.imag->name});
            }
        }

        // Merge: keep a saved name if still plotted or a ghost; drop cleared
        // loaded ones. Then append any newly plotted names.
        CsvPlotSignalSettings updated;
        for (std::string const& signal_name : plot.settings.scalar_signals) {
            bool const signal_is_current = contains(current_settings.scalar_signals, signal_name);
            if (signal_is_current || !loaded_names.contains(signal_name)) {
                updated.scalar_signals.push_back(signal_name);
            }
        }
        for (std::string const& signal_name : current_settings.scalar_signals) {
            if (!contains(updated.scalar_signals, signal_name)) {
                updated.scalar_signals.push_back(signal_name);
            }
        }

        // Same merge for signal pairs (vector/spectrum); a pair is a ghost if
        // neither of its signals is in any open file.
        for (auto const& pair : plot.settings.signal_pairs) {
            bool const pair_is_current = std::find(current_settings.signal_pairs.begin(),
                                                   current_settings.signal_pairs.end(),
                                                   pair)
                                      != current_settings.signal_pairs.end();
            if (pair_is_current || !pairTouchesLoadedSignal(pair, loaded_names)) {
                addUniquePair(updated.signal_pairs, pair);
            }
        }
        for (auto const& pair : current_settings.signal_pairs) {
            addUniquePair(updated.signal_pairs, pair);
        }
        plot.settings = std::move(updated);
    };

    for (PlotBase& plot : m_docked_plots) {
        update_plot_settings(plot);
    }
    for (PlotBase& plot : m_undocked_plots) {
        update_plot_settings(plot);
    }
}

int CsvPlotter::dockedPlotCount() const {
    return std::clamp(m_rows * m_cols, 1, MAX_PLOT_WINDOWS);
}

int CsvPlotter::activePlotCount() const {
    return dockedPlotCount() + m_undocked_plot_count;
}

PlotBase& CsvPlotter::plotAt(int visible_plot_idx) {
    int d = dockedPlotCount();
    if (visible_plot_idx < d) {
        return m_docked_plots[visibleToStablePlotIndex(visible_plot_idx, m_cols)];
    }
    return m_undocked_plots[visible_plot_idx - d];
}

PlotBase const& CsvPlotter::plotAt(int visible_plot_idx) const {
    int d = dockedPlotCount();
    if (visible_plot_idx < d) {
        return m_docked_plots[visibleToStablePlotIndex(visible_plot_idx, m_cols)];
    }
    return m_undocked_plots[visible_plot_idx - d];
}

std::string CsvPlotter::plotWindowName(int visible_plot_idx) const {
    int docked = dockedPlotCount();
    if (visible_plot_idx < docked) {
        return std::format("Plot {}", visible_plot_idx);
    }
    return std::format("Undocked Plot {}", visible_plot_idx - docked);
}

template <typename Fn>
void CsvPlotter::forEachPlot(Fn&& fn) {
    for (PlotBase& plot : m_docked_plots) {
        fn(plot);
    }
    for (PlotBase& plot : m_undocked_plots) {
        fn(plot);
    }
}

bool CsvPlotter::isSignalPlotted(CsvSignal* signal) const {
    auto is_signal_plotted_in = [&](PlotBase const& plot) {
        if (auto const* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
            if (contains(scalar_plot->signals, signal)) {
                return true;
            }
        } else if (auto const* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
            for (auto const& signals : vector_plot->signals) {
                if (signals.first == signal || signals.second == signal) {
                    return true;
                }
            }
        } else if (auto const* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
            for (auto const& spec : spectrum_plot->spectrum) {
                if (spec.real == signal || spec.imag == signal) {
                    return true;
                }
            }
        }
        return false;
    };

    for (PlotBase const& plot : m_docked_plots) {
        if (is_signal_plotted_in(plot)) {
            return true;
        }
    }
    for (PlotBase const& plot : m_undocked_plots) {
        if (is_signal_plotted_in(plot)) {
            return true;
        }
    }
    return false;
}

std::vector<CsvSignal*> CsvPlotter::sameNamedSignalsFromOpenFiles(std::vector<CsvSignal*> const& signals) {
    std::vector<CsvSignal*> same_named_signals;
    for (CsvSignal* signal : signals) {
        for (auto& file : m_csv_data) {
            CsvSignal* same_named_signal = findSignalByName(*file, signal->name);
            if (same_named_signal != nullptr && !contains(same_named_signals, same_named_signal)) {
                same_named_signals.push_back(same_named_signal);
            }
        }
    }
    return same_named_signals;
}

void CsvPlotter::removeSignalFromAllPlots(CsvSignal* signal) {
    for (PlotBase& plot : m_docked_plots) {
        plot.removeSignal(signal);
    }
    for (PlotBase& plot : m_undocked_plots) {
        plot.removeSignal(signal);
    }
}

void CsvPlotter::replaceReloadedFileSignals(CsvFileData& old_file, CsvFileData& new_file) {
    auto replacement_for = [&](CsvSignal* signal) -> CsvSignal* {
        if (signal == nullptr || signal->file != &old_file) {
            return signal;
        }
        for (int i = 0; i < (int)old_file.signals.size(); ++i) {
            if (&old_file.signals[i] == signal) {
                return &new_file.signals[i];
            }
        }
        return signal;
    };

    auto replace_plot_signals = [&](PlotBase& plot) {
        if (auto* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
            for (int i = 0; i < (int)old_file.signals.size(); ++i) {
                CsvSignal* old_signal = &old_file.signals[i];
                if (contains(scalar_plot->signals, old_signal)) {
                    scalar_plot->addSignal(&new_file.signals[i]);
                    if (!m_options.keep_old_signals_on_reload) {
                        scalar_plot->removeSignal(old_signal);
                    }
                }
            }
        } else if (auto* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
            std::vector<std::pair<CsvSignal*, CsvSignal*>> reloaded_signals;
            for (auto& signals : vector_plot->signals) {
                std::pair<CsvSignal*, CsvSignal*> replacement = {replacement_for(signals.first),
                                                                 replacement_for(signals.second)};
                if (replacement != signals) {
                    if (m_options.keep_old_signals_on_reload) {
                        reloaded_signals.push_back(replacement);
                    } else {
                        signals = replacement;
                    }
                }
            }
            for (auto const& signals : reloaded_signals) {
                vector_plot->addSignal(signals);
            }
        } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
            std::vector<std::pair<CsvSignal*, CsvSignal*>> reloaded_signals;
            for (auto& spec : spectrum_plot->spectrum) {
                CsvSignal* real = replacement_for(spec.real);
                CsvSignal* imag = replacement_for(spec.imag);
                if (real != spec.real || imag != spec.imag) {
                    if (m_options.keep_old_signals_on_reload) {
                        reloaded_signals.push_back({real, imag});
                    } else {
                        spec.real = real;
                        spec.imag = imag;
                        spec.data = {};
                        spec.calculation = std::future<SpectrumData>();
                        spectrum_plot->prev_x_range = {0, 0};
                    }
                }
            }
            for (auto const& signals : reloaded_signals) {
                spectrum_plot->addSignal(signals.first, signals.second);
                spectrum_plot->prev_x_range = {0, 0};
            }
        }
    };

    for (PlotBase& plot : m_docked_plots) {
        replace_plot_signals(plot);
    }
    for (PlotBase& plot : m_undocked_plots) {
        replace_plot_signals(plot);
    }

    for (int i = 0; i < (int)old_file.signals.size(); ++i) {
        for (int j = 0; j < (int)m_selected_signals.size(); ++j) {
            if (m_selected_signals[j] == &old_file.signals[i]) {
                m_selected_signals[j] = &new_file.signals[i];
            }
        }
    }
}

bool CsvPlotter::showPlotContextMenu(PlotBase& plot) {
    bool plot_type_changed = false;
    if (ImGui::BeginPopupContextItem("Plot_context_menu")) {
        CsvPlotType current_type = plot.plotType();
        std::string preview(magic_enum::enum_name(current_type));
        ImGui::SetNextItemWidth(120);
        if (ImGui::BeginCombo("Type", preview.c_str())) {
            for (CsvPlotType type : magic_enum::enum_values<CsvPlotType>()) {
                std::string type_name(magic_enum::enum_name(type));
                bool is_selected = type == current_type;
                if (ImGui::Selectable(type_name.c_str(), is_selected)) {
                    plot.setPlotType(type);
                    plot_type_changed = type != current_type;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Remove all signals")) {
            plot.clearPlot();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return plot_type_changed;
}

void CsvPlotter::setSignalTransform(std::string const& signal_name, CsvSignalTransform const& transform) {
    if (transform.isDefault()) {
        m_signal_transform_settings.erase(signal_name);
    } else {
        m_signal_transform_settings[signal_name] = transform;
    }

    // Keep cached per-signal transforms in sync with the per-name settings map.
    for (auto& file : m_csv_data) {
        for (CsvSignal& signal : file->signals) {
            if (signal.name == signal_name) {
                signal.transform = transform;
            }
        }
    }
}

std::expected<void, std::string> CsvPlotter::setSignalScale(std::vector<CsvSignal*> const& signals,
                                                            std::string const& scale_expression) {
    CsvSignalTransform changed_transform;
    std::expected<void, std::string> scale_result = setSignalTransformScale(changed_transform, scale_expression);
    if (!scale_result.has_value()) {
        return std::unexpected(scale_result.error());
    }

    for (CsvSignal* target : signals) {
        if (target == nullptr) {
            continue;
        }
        CsvSignalTransform target_transform = target->transform;
        target_transform.scale_expression = changed_transform.scale_expression;
        target_transform.scale = changed_transform.scale;
        setSignalTransform(target->name, target_transform);
    }
    return {};
}

std::expected<void, std::string> CsvPlotter::setSignalOffset(std::vector<CsvSignal*> const& signals,
                                                             std::string const& offset_expression) {
    CsvSignalTransform changed_transform;
    std::expected<void, std::string> offset_result = setSignalTransformOffset(changed_transform, offset_expression);
    if (!offset_result.has_value()) {
        return std::unexpected(offset_result.error());
    }

    for (CsvSignal* target : signals) {
        if (target == nullptr) {
            continue;
        }
        CsvSignalTransform target_transform = target->transform;
        target_transform.offset_expression = changed_transform.offset_expression;
        target_transform.offset = changed_transform.offset;
        setSignalTransform(target->name, target_transform);
    }
    return {};
}

void CsvPlotter::setSignalPlotStyle(std::vector<CsvSignal*> const& signals, std::optional<CsvPlotStyle> plot_style) {
    for (CsvSignal* signal : signals) {
        if (signal == nullptr) {
            continue;
        }
        if (plot_style.has_value()) {
            m_signal_plot_style_settings[signal->name] = plot_style.value();
        } else {
            m_signal_plot_style_settings.erase(signal->name);
        }
    }
}

void CsvPlotter::removeFileFromPlots(CsvFileData& file) {
    for (CsvSignal& signal : file.signals) {
        removeSignalFromAllPlots(&signal);
        remove(m_selected_signals, &signal);
    }
}

void CsvPlotter::removeAllFiles() {
    for (auto& file : m_csv_data) {
        removeFileFromPlots(*file);
    }
    m_selected_signals.clear();
    m_csv_data.clear();
}

void CsvPlotter::openFilesFromDialog() {
    std::vector<std::unique_ptr<CsvFileData>> csv_datas = openCsvFromFileDialog();
    if (!csv_datas.empty() && m_save_settings) {
        // Opening files interactively resumes saved plotted signal restore in normal GUI sessions.
        m_use_saved_plotted_signals = true;
    }
    for (auto& csv_data : csv_datas) {
        applySignalTransforms(*csv_data);
        if (m_use_saved_plotted_signals) {
            applyPlottedSignals(*csv_data);
        }
        m_csv_data.push_back(std::move(csv_data));
    }
}

void CsvPlotter::clearPlots() {
    for (PlotBase& plot : m_docked_plots) {
        plot.clearPlot();
    }
    for (PlotBase& plot : m_undocked_plots) {
        plot.clearPlot();
    }
}

void CsvPlotter::copyPlottedSignalArgumentsToClipboard() {
    std::stringstream ss_signals;
    std::stringstream ss_plots;
    ss_signals << "\"";
    ss_plots << "\"";
    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            int plot_idx = stablePlotIndex(row, col);
            if (auto* plot = std::get_if<ScalarPlot>(&m_docked_plots[plot_idx].variant)) {
                for (CsvSignal* signal : plot->signals) {
                    ss_signals << signal->name << ",";
                    ss_plots << plot_idx << ",";
                }
            }
        }
    }
    ss_signals << "\"";
    ss_plots << "\"";
    ImGui::SetClipboardText(std::format("--names {} --plots {} --rows {} --cols {}",
                                        ss_signals.str(),
                                        ss_plots.str(),
                                        std::to_string(m_rows),
                                        std::to_string(m_cols))
                              .c_str());
}

void CsvPlotter::addClipboardFileFromClipboard() {
    std::expected<SampleClipboardData, std::string> parsed_columns = readSamplesFromClipboard();
    if (!parsed_columns.has_value()) {
        m_error_message = parsed_columns.error();
        return;
    }
    SampleClipboardData columns = std::move(parsed_columns.value());
    if (columns.data.empty() || columns.data[0].empty()) {
        m_error_message = "Clipboard samples contain no data";
        return;
    }

    size_t row_count = columns.data[0].size();
    for (std::vector<double> const& column : columns.data) {
        if (column.size() != row_count) {
            m_error_message = "Clipboard sample columns have different sample counts";
            return;
        }
    }

    std::vector<std::string> signal_names = makeUniqueCsvSignalNames(columns.header);
    for (size_t i = ASCENDING_NUMBERS.size(); i < row_count; ++i) {
        ASCENDING_NUMBERS.push_back(double(i));
    }

    ++m_clipboard_file_count;
    auto file = std::make_unique<CsvFileData>();
    CsvFileData* file_ptr = file.get();
    file->name = std::format("<clipboard {}>", m_clipboard_file_count);
    file->displayed_name = std::format("Clipboard {}", m_clipboard_file_count);
    file->write_time = std::filesystem::file_time_type();
    file->in_memory = true;
    file->signals.reserve(signal_names.size() + CUSTOM_SIGNAL_CAPACITY);

    for (size_t i = 0; i < signal_names.size(); ++i) {
        file->signals.push_back(CsvSignal{
          .name = signal_names[i],
          .samples = std::move(columns.data[i]),
          .file = file_ptr});
    }
    applySignalTransforms(*file);
    if (m_use_saved_plotted_signals) {
        applyPlottedSignals(*file);
    }
    m_csv_data.push_back(std::move(file));
}

CsvPlotStyle CsvPlotter::getSignalPlotStyle(CsvSignal const& signal) const {
    auto it = m_signal_plot_style_settings.find(signal.name);
    return it == m_signal_plot_style_settings.end() ? m_options.plot_style : it->second;
}

void CsvPlotter::showSignalPlotStyleCombo(CsvSignal const& signal, std::vector<CsvSignal*> const& signals_to_update) {
    std::string global_name(magic_enum::enum_name(m_options.plot_style));
    std::string global_label = std::format("Global ({})", global_name);
    std::string const& signal_name = signal.name;
    auto signal_style_it = m_signal_plot_style_settings.find(signal_name);
    bool has_signal_style = signal_style_it != m_signal_plot_style_settings.end();
    CsvPlotStyle signal_style = has_signal_style ? signal_style_it->second : m_options.plot_style;
    std::string preview = has_signal_style ? std::string(magic_enum::enum_name(signal_style)) : global_label;

    ImGui::SetNextItemWidth(185);
    if (ImGui::BeginCombo("Plot style", preview.c_str())) {
        if (ImGui::Selectable(global_label.c_str(), !has_signal_style)) {
            setSignalPlotStyle(signals_to_update, std::nullopt);
            has_signal_style = false;
        }
        if (!has_signal_style) {
            ImGui::SetItemDefaultFocus();
        }

        for (CsvPlotStyle plot_style : magic_enum::enum_values<CsvPlotStyle>()) {
            std::string plot_style_name(magic_enum::enum_name(plot_style));
            bool is_selected = has_signal_style && signal_style == plot_style;
            if (ImGui::Selectable(plot_style_name.c_str(), is_selected)) {
                setSignalPlotStyle(signals_to_update, plot_style);
                has_signal_style = true;
                signal_style = plot_style;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

std::vector<double> CsvPlotter::getVisibleSamples(CsvSignal const& signal) {
    std::span<double const> x_samples = getXSignalSamples(*signal.file);
    std::vector<double> const& all_samples = signal.samples;
    double x_offset = m_options.shift_samples_to_start_from_zero ? x_samples[0] : 0;
    x_offset -= signal.file->x_axis_shift;

    std::pair<int32_t, int32_t> indices;
    if (m_options.x_signal_name == "ASCENDING_NUMBERS") {
        indices = {std::max(0, (int)std::floor(m_x_axis.min)),
                   std::min((int)all_samples.size(), (int)std::ceil(m_x_axis.max))};
    } else {
        indices = getTimeIndices(x_samples, m_x_axis.min + x_offset, m_x_axis.max + x_offset);
    }
    std::vector<double> plotted_samples(all_samples.begin() + indices.first, all_samples.begin() + indices.second);
    for (double& sample : plotted_samples) {
        sample = sample * signal.transform.scale + signal.transform.offset;
    }

    return plotted_samples;
}

std::span<double const> CsvPlotter::getXSignalSamples(CsvFileData const& file) {
    if (m_options.x_signal_name == "ASCENDING_NUMBERS") {
        return ASCENDING_NUMBERS;
    }
    // Find signal with matching name. Use first signal as x-axis if signal is not found
    for (auto const& sig : file.signals) {
        if (sig.name == m_options.x_signal_name) {
            return std::span<double const>(sig.samples);
        }
    }
    if (!file.signals.empty()) {
        return std::span<double const>(file.signals[0].samples);
    }
    // Fallback to ascending numbers if no signals in file
    return ASCENDING_NUMBERS;
}

double CsvPlotter::getScalarPlotXOrigin(ScalarPlot const& plot) {
    if (!m_options.shift_samples_to_start_from_zero) {
        return 0;
    }

    bool has_origin = false;
    double origin = 0;
    auto update_origin = [&](ScalarPlot const& plot_to_check) {
        for (CsvSignal const* signal : plot_to_check.signals) {
            std::span<double const> x_samples = getXSignalSamples(*signal->file);
            if (x_samples.empty()) {
                continue;
            }
            // Manual x-axis shift moves the file in raw plot space, so include it
            // when choosing which signal should become the displayed zero point.
            double signal_origin = x_samples[0] + signal->file->x_axis_shift;
            if (!has_origin || signal_origin < origin) {
                origin = signal_origin;
                has_origin = true;
            }
        }
    };

    if (m_options.link_axis) {
        // Linked scalar plots share x-axis limits, so they also need one shared
        // zero origin. Otherwise the same file can appear at different x values
        // depending on which other files happen to be present in each plot.
        for (int visible_plot_idx = 0; visible_plot_idx < activePlotCount(); ++visible_plot_idx) {
            if (auto const* scalar_plot = std::get_if<ScalarPlot>(&plotAt(visible_plot_idx).variant)) {
                update_origin(*scalar_plot);
            }
        }
    } else {
        // Unlinked plots keep their origin local to the signals in this plot.
        update_origin(plot);
    }

    return has_origin ? origin : 0;
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

CsvPlotter::CsvPlotter(std::vector<std::string> files,
                       std::map<std::string, int> name_and_plot_idx,
                       MinMax const& xlimits,
                       int rows,
                       int cols,
                       std::string const& image_filepath) {
    if (xlimits != AUTOFIT_AXIS) {
        m_x_axis.min = std::min(xlimits.min, xlimits.max);
        m_x_axis.max = std::max(xlimits.min, xlimits.max);
    }

    // Command line signal selection overrides saved plotted signals and must not update settings.
    m_use_saved_plotted_signals = name_and_plot_idx.empty();
    m_save_settings = name_and_plot_idx.empty();

    for (std::string file : files) {
        std::unique_ptr<CsvFileData> data = parseCsvData(file);
        if (data) {
            m_csv_data.push_back(std::move(data));
        } else {
            std::abort();
        }
    }

    //---------- Initializations ----------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        std::abort();
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    m_window = glfwCreateWindow(1280, 720, std::format("CSV Plotter {}", SW_VERSION).c_str(), NULL, NULL);
    if (m_window == NULL)
        std::abort();
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    glfwSetWindowPos(m_window, 0, 0);

#if WINDOWS
    // Load icon
    HRSRC resource_handle = FindResource(nullptr, MAKEINTRESOURCEA(ICON_PNG), "PNG");
    HGLOBAL resource_memory_handle = LoadResource(nullptr, resource_handle);
    size_t size_bytes = SizeofResource(nullptr, resource_handle);
    void* resource_buffer = LockResource(resource_memory_handle);
    GLFWimage image;
    image.pixels = stbi_load_from_memory((stbi_uc*)resource_buffer, (int)size_bytes, &image.width, &image.height, 0, STBI_rgb_alpha);
    glfwSetWindowIcon(m_window, 1, &image);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport
    io.IniFilename = NULL;                                // Set to NULL because ini file file is loaded manually

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    loadPreviousSessionSettings();
    if (rows > 0) {
        m_rows = rows;
    }
    if (cols > 0) {
        m_cols = cols;
    }
    m_rows = std::clamp(m_rows, 1, MAX_PLOTS);
    m_cols = std::clamp(m_cols, 1, MAX_PLOTS);

    extern unsigned int cousine_regular_compressed_size;
    extern unsigned int cousine_regular_compressed_data[];
    io.Fonts->AddFontFromMemoryCompressedTTF(cousine_regular_compressed_data, cousine_regular_compressed_size, (float)m_options.font_size);
    ImGui::PushFont(ImGui::GetDefaultFont(), (float)m_options.font_size);

    // Move window out of sight if creating image to avoid popups. Docking layout sometimes does not get applied
    // if window is not visible so only 1 pixel is visible to have correct layout.
    if (!image_filepath.empty()) {
        int xpos = 0;
        int ypos = 0;
        m_undocked_plot_count = 0;
        glfwGetWindowSize(m_window, &xpos, &ypos);
        glfwSetWindowPos(m_window, 0, -ypos + 1);
        // Set window minimum size to 1 to hide the signals window which is docked
        // to the main dock. Signals window has to be included because docking some
        // plot to the main dock will make it the wrong size https://github.com/ocornut/imgui/issues/6095
        auto& style = ImGui::GetStyle();
        style.WindowMinSize.x = 1;
        m_signals_window_width = 0;
    }

    // Apply command-line signal assignments to scalar docked grid plots only.
    if (!name_and_plot_idx.empty()) {
        m_undocked_plot_count = 0;
        for (auto& file : m_csv_data) {
            for (CsvSignal& signal : file->signals) {
                auto it = name_and_plot_idx.find(signal.name);
                if (it == name_and_plot_idx.end()) {
                    continue;
                }
                int plot_idx = it->second;
                if (plot_idx >= 0 && plot_idx < MAX_PLOT_WINDOWS) {
                    std::get<ScalarPlot>(m_docked_plots[plot_idx].variant).addSignal(&signal);
                }
            }
        }
    }

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiID main_dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();

        //---------- Main windows ----------
        showErrorModal();
        showSignalWindow();
        showPlots();

        // Settings are not saved when creating image because the window
        // is moved out of sight and should not be restored there
        if (image_filepath.empty() && m_save_settings) {
            updateSavedSettings();
        }

        updateComparisonMode();
        setLayout(main_dock, m_rows, m_cols, m_signals_window_width);

        //---------- Rendering ----------
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(m_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we
        // save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call
        //  glfwMakeContextCurrent(window) directly)
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        glfwSwapBuffers(m_window);

        if (!image_filepath.empty() && ImGui::GetFrameCount() > IMAGE_SAVE_FRAME_COUNT) {
            saveImage(image_filepath.c_str(), m_window);
            glfwSetWindowShouldClose(m_window, true);
        }
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void CsvPlotter::loadPreviousSessionSettings() {
    std::string settings_dir = std::format("{}/.csvplot/", std::getenv(SETTINGS_LOCATION.c_str()));
    std::ifstream f(settings_dir + "settings.json");
    if (f.is_open()) {
        auto addSettingsLoadError = [&](std::string const& error) {
            if (m_error_message.empty()) {
                m_error_message = "Failed to load previous session settings:\n";
            }
            m_error_message += error + "\n";
        };

        try {
            auto settings = nlohmann::json::parse(f);

            if (settings.contains("layout")) {
                std::string layout = settings["layout"];
                loadImguiLayout(layout);
            }

            TRY(m_rows = int(settings["window"]["rows"]);)
            TRY(m_cols = int(settings["window"]["cols"]);)
            m_rows = std::clamp(m_rows, 1, MAX_PLOTS);
            m_cols = std::clamp(m_cols, 1, MAX_PLOTS);
            int saved_undocked_count = settings["window"].value("undocked_plot_count", 0);
            if (m_use_saved_plotted_signals) {
                m_undocked_plot_count = std::clamp(saved_undocked_count, 0, MAX_UNDOCKED_PLOTS);
            } else {
                m_undocked_plot_count = 0;
            }
            TRY(m_options.x_signal_name = std::string(settings["window"]["x_signal_name"]);)
            TRY(m_options.link_axis = settings["window"]["link_axis"];)
            TRY(m_options.autofit_y_axis = settings["window"]["autofit_y_axis"];)
            TRY(m_options.show_vertical_line_in_all_plots = settings["window"]["show_vertical_line_in_all_plots"];)
            TRY(m_options.shift_samples_to_start_from_zero = settings["window"]["shift_samples_to_start_from_zero"];)
            TRY(m_options.keep_old_signals_on_reload = settings["window"]["keep_old_signals_on_reload"];)
            TRY(m_options.interpolate_tooltip = settings["window"]["interpolate_tooltip"];)
            TRY(m_options.plot_style = magic_enum::enum_cast<CsvPlotStyle>(
                                         settings["window"].value("plot_style", std::string(magic_enum::enum_name(CsvPlotStyle::Linear))))
                                         .value_or(CsvPlotStyle::Linear);)
            TRY(m_options.theme = settings["window"]["theme"];)
            TRY(m_options.font_size = settings["window"]["font_size"];)
            setTheme(m_options.theme, m_window);
            if (settings.contains("scales") && settings["scales"].is_object()) {
                for (auto const& scale : settings["scales"].items()) {
                    std::string scale_expression = jsonValueToExpressionString(scale.value());
                    std::expected<void, std::string> scale_result = setSignalTransformScale(m_signal_transform_settings[scale.key()], scale_expression);
                    if (!scale_result.has_value()) {
                        addSettingsLoadError(scale_result.error());
                    }
                }
            }
            if (settings.contains("offsets") && settings["offsets"].is_object()) {
                for (auto const& offset : settings["offsets"].items()) {
                    std::string offset_expression = jsonValueToExpressionString(offset.value());
                    std::expected<void, std::string> offset_result = setSignalTransformOffset(m_signal_transform_settings[offset.key()], offset_expression);
                    if (!offset_result.has_value()) {
                        addSettingsLoadError(offset_result.error());
                    }
                }
            }
            for (auto it = m_signal_transform_settings.begin(); it != m_signal_transform_settings.end();) {
                if (it->second.isDefault()) {
                    it = m_signal_transform_settings.erase(it);
                } else {
                    ++it;
                }
            }
            if (settings.contains("plot_styles") && settings["plot_styles"].is_object()) {
                for (auto const& item : settings["plot_styles"].items()) {
                    TRY(auto plot_style = magic_enum::enum_cast<CsvPlotStyle>(item.value().get<std::string>());
                        if (plot_style.has_value()) {
                            m_signal_plot_style_settings[item.key()] = plot_style.value();
                        })
                }
            }
            if (m_use_saved_plotted_signals && (settings.contains("plots") || settings.contains("undocked_plots"))) {
                auto load_plot_settings = [&](PlotBase& plot, nlohmann::json const& plot_settings) {
                    if (!plot_settings.is_object()) {
                        return;
                    }
                    CsvPlotType type = magic_enum::enum_cast<CsvPlotType>(
                                         plot_settings.value("type", std::string(magic_enum::enum_name(CsvPlotType::Scalar))))
                                         .value_or(CsvPlotType::Scalar);
                    plot.settings = {};
                    plot.setPlotType(type);
                    if (plot_settings.contains("signals") && plot_settings["signals"].is_array()) {
                        for (auto const& signal : plot_settings["signals"]) {
                            if (type == CsvPlotType::Scalar) {
                                if (signal.is_string()) {
                                    plot.settings.scalar_signals.push_back(signal.get<std::string>());
                                }
                            } else if (signal.is_array() && signal.size() >= 1 && signal[0].is_string()) {
                                std::string first = signal[0].get<std::string>();
                                std::string second;
                                if (signal.size() >= 2 && signal[1].is_string()) {
                                    second = signal[1].get<std::string>();
                                }
                                addUniquePair(plot.settings.signal_pairs, {first, second});
                            }
                        }
                    }
                };

                if (settings.contains("plots")) {
                    if (settings["plots"].is_array()) {
                        int plot_idx = 0;
                        for (auto const& plot_settings : settings["plots"]) {
                            if (plot_idx >= MAX_PLOT_WINDOWS) {
                                break;
                            }
                            load_plot_settings(m_docked_plots[plot_idx], plot_settings);
                            ++plot_idx;
                        }
                    } else if (settings["plots"].is_object()) {
                        for (auto const& item : settings["plots"].items()) {
                            TRY(std::optional<int> plot_idx = parseInt(item.key());
                                if (plot_idx.has_value() && plot_idx.value() >= 0 && plot_idx.value() < MAX_PLOT_WINDOWS) {
                                    load_plot_settings(m_docked_plots[plot_idx.value()], item.value());
                                })
                        }
                    }
                }
                if (settings.contains("undocked_plots") && settings["undocked_plots"].is_object()) {
                    for (auto const& item : settings["undocked_plots"].items()) {
                        TRY(int u = std::stoi(item.key());
                            if (u >= 0 && u < MAX_UNDOCKED_PLOTS) {
                                load_plot_settings(m_undocked_plots[u], item.value());
                            })
                    }
                }
            } else if (m_use_saved_plotted_signals && settings.contains("plotted_signals") && settings["plotted_signals"].is_object()) {
                for (auto const& item : settings["plotted_signals"].items()) {
                    TRY(for (int plot_idx : item.value().get<std::vector<int>>()) {
                        if (plot_idx < 0 || plot_idx >= MAX_PLOT_WINDOWS)
                            continue;
                        m_docked_plots[plot_idx].settings.scalar_signals.push_back(item.key());
                    })
                }
            }
            if (settings.contains("recent_custom_equations") && settings["recent_custom_equations"].is_array()) {
                m_recent_custom_equations.clear();
                for (auto const& item : settings["recent_custom_equations"]) {
                    if (m_recent_custom_equations.size() >= MAX_RECENT_CUSTOM_EQUATIONS) {
                        break;
                    }
                    if (!item.is_object()
                        || !item.contains("name")
                        || !item.contains("equation")
                        || !item["name"].is_string()
                        || !item["equation"].is_string()) {
                        continue;
                    }

                    std::string name = item["name"].get<std::string>();
                    std::string equation = item["equation"].get<std::string>();
                    if (name.empty()
                        || equation.empty()
                        || name.size() >= MAX_CUSTOM_EQ_NAME
                        || equation.size() >= MAX_CUSTOM_EQ_LENGTH) {
                        continue;
                    }
                    m_recent_custom_equations.push_back(RecentCustomEquation{.name = name, .equation = equation});
                }
            }
            for (auto& file : m_csv_data) {
                applySignalTransforms(*file);
                if (m_use_saved_plotted_signals) {
                    applyPlottedSignals(*file);
                }
            }
            TRY(m_signals_window_width = settings["window"]["signals_window_width"];)

            int xpos = std::max(0, int(settings["window"]["xpos"]));
            int ypos = std::max(0, int(settings["window"]["ypos"]));
            glfwSetWindowPos(m_window, xpos, ypos);
            glfwSetWindowSize(m_window, settings["window"]["width"], settings["window"]["height"]);
        } catch (nlohmann::json::exception const& err) {
            addSettingsLoadError(err.what());
        } catch (std::exception const& err) {
            addSettingsLoadError(err.what());
        }
    }
}

void CsvPlotter::updateSavedSettings() {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    int xpos, ypos;
    glfwGetWindowPos(m_window, &xpos, &ypos);
    // Early return in case something is wrong
    if (width == 0 || height == 0) {
        return;
    }

    nlohmann::json settings;
    settings["window"]["width"] = width;
    settings["window"]["height"] = height;
    settings["window"]["xpos"] = xpos;
    settings["window"]["ypos"] = ypos;
    settings["window"]["rows"] = m_rows;
    settings["window"]["cols"] = m_cols;
    settings["window"]["undocked_plot_count"] = m_undocked_plot_count;
    settings["window"]["x_signal_name"] = m_options.x_signal_name;
    settings["window"]["link_axis"] = m_options.link_axis;
    settings["window"]["autofit_y_axis"] = m_options.autofit_y_axis;
    settings["window"]["show_vertical_line_in_all_plots"] = m_options.show_vertical_line_in_all_plots;
    settings["window"]["shift_samples_to_start_from_zero"] = m_options.shift_samples_to_start_from_zero;
    settings["window"]["keep_old_signals_on_reload"] = m_options.keep_old_signals_on_reload;
    settings["window"]["interpolate_tooltip"] = m_options.interpolate_tooltip;
    settings["window"]["plot_style"] = magic_enum::enum_name(m_options.plot_style);
    settings["window"]["theme"] = m_options.theme;
    settings["window"]["font_size"] = m_options.font_size;
    settings["layout"] = ImGui::SaveIniSettingsToMemory(nullptr);
    settings["window"]["signals_window_width"] = m_signals_window_width;
    for (auto const& [name, transform] : m_signal_transform_settings) {
        if (transform.scale != 1) {
            settings["scales"][name] = transform.scale_expression;
        }
        if (transform.offset != 0) {
            settings["offsets"][name] = transform.offset_expression;
        }
    }
    for (auto const& [name, plot_style] : m_signal_plot_style_settings) {
        settings["plot_styles"][name] = magic_enum::enum_name(plot_style);
    }
    updatePlottedSignalSettings();
    auto has_saved_plot_state = [](PlotBase const& plot) {
        return plot.plotType() != CsvPlotType::Scalar
            || !plot.settings.scalar_signals.empty()
            || !plot.settings.signal_pairs.empty();
    };
    auto save_plot = [&](PlotBase& plot, nlohmann::json& plot_obj, std::string const& key) {
        CsvPlotType type = plot.plotType();
        nlohmann::json& plot_settings = plot_obj[key];
        plot_settings["type"] = magic_enum::enum_name(type);
        plot_settings["signals"] = nlohmann::json::array();
        if (type == CsvPlotType::Scalar) {
            for (std::string const& signal_name : plot.settings.scalar_signals) {
                plot_settings["signals"].push_back(signal_name);
            }
        } else {
            for (auto const& signal_pair : plot.settings.signal_pairs) {
                plot_settings["signals"].push_back(nlohmann::json::array({signal_pair.first, signal_pair.second}));
            }
        }
    };
    for (int row = 0; row < MAX_PLOTS; ++row) {
        for (int col = 0; col < MAX_PLOTS; ++col) {
            int plot_idx = stablePlotIndex(row, col);
            bool visible = row < m_rows && col < m_cols;
            if (visible || has_saved_plot_state(m_docked_plots[plot_idx])) {
                save_plot(m_docked_plots[plot_idx], settings["plots"], std::to_string(plot_idx));
            }
        }
    }
    for (int u = 0; u < MAX_UNDOCKED_PLOTS; ++u) {
        if (u < m_undocked_plot_count || has_saved_plot_state(m_undocked_plots[u])) {
            save_plot(m_undocked_plots[u], settings["undocked_plots"], std::to_string(u));
        }
    }
    for (auto const& recent : m_recent_custom_equations) {
        settings["recent_custom_equations"].push_back({{"name", recent.name}, {"equation", recent.equation}});
    }
    static nlohmann::json settings_saved = settings;
    if (settings != settings_saved) {
        settings_saved = settings;

        std::string settings_dir = std::format("{}/.csvplot/", std::getenv(SETTINGS_LOCATION.c_str()));
        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        // Write settings to tmp file first to avoid corrupting the file if program is closed mid-write
        std::ofstream(settings_dir + "settings.json.tmp") << std::setw(4) << settings;
        std::filesystem::copy_file(settings_dir + "settings.json.tmp",
                                   settings_dir + "settings.json",
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

std::unique_ptr<CsvFileData> parseCsvData(std::string filename) {
    std::string csv_filename = filename;
    if (filename.ends_with(".inf")) {
        bool csv_file_created = pscadInfToCsv(filename);
        if (!csv_file_created) {
            return nullptr;
        }
        std::string basename = filename.substr(0, filename.find_last_of("."));
        csv_filename = basename + ".csv";
        // Set the _01.out file as the filename instead of inf because PSCAD writes the inf file immediately
        // but the out files do not have all the data yet. The out file is watched for changes so that the
        // files are not re-read too early
        filename = basename + "_01.out";
    } else if (filename.ends_with(".out")) {
        // A inf file has been read earlier. Re-read the inf file and re-create the csv file.
        std::string basename = filename.substr(0, filename.find_last_of(".") - 3);
        std::string inf_filename = basename + ".inf";
        bool csv_file_created = pscadInfToCsv(inf_filename);
        if (!csv_file_created) {
            return nullptr;
        }
        csv_filename = basename + ".csv";
    }

    std::expected<std::string, std::string> csv_str = str::readFile(csv_filename);
    if (!csv_str.has_value()) {
        std::cerr << csv_str.error() << std::endl;
        return nullptr;
    }
    std::vector<std::string_view> csv_lines = str::splitSv(csv_str.value(), '\n');
    if (csv_lines.size() < 3) {
        std::cerr << "File " + csv_filename << " has less than 3 lines of data" << std::endl;
        return nullptr;
    }
    std::string third_last_line = std::string(csv_lines[csv_lines.size() - 3]);
    str::trim(third_last_line);
    if (third_last_line.empty()) {
        std::cerr << "No data in file " + csv_filename << std::endl;
        return nullptr;
    }

    // Try detect delimiter from the end of file as that part likely doesn't contain extra information
    // Use third last line in case the last line gets modified suddenly
    char delimiter = '\0';
    size_t element_count = 0;
    std::vector<std::string> values_comma = str::split(third_last_line, ',');
    std::vector<std::string> values_semicolon = str::split(third_last_line, ';');
    std::vector<std::string> values_tab = str::split(third_last_line, '\t');
    if (values_comma.size() > values_semicolon.size() && values_comma.size() > values_tab.size()) {
        delimiter = ',';
        element_count = values_comma.size();
    } else if (values_semicolon.size() > values_comma.size() && values_semicolon.size() > values_tab.size()) {
        delimiter = ';';
        element_count = values_semicolon.size();
    } else if (values_tab.size() > values_comma.size() && values_tab.size() > values_semicolon.size()) {
        delimiter = '\t';
        element_count = values_tab.size();
    }
    if (delimiter == '\0') {
        std::cerr << std::format("Unable to detect delimiter from third last line of the file \"{}\"\n", csv_filename);
        return nullptr;
    }

    // Find first line where header begins
    int header_line_idx = 0;
    for (std::string_view line_sv : csv_lines) {
        std::string line(line_sv);
        str::trim(line);
        if (str::splitSv(line, delimiter).size() == element_count) {
            break;
        }
        ++header_line_idx;
    }
    if (header_line_idx >= csv_lines.size()) {
        std::cerr << std::format("Unable to find CSV header in file \"{}\"\n", csv_filename);
        return nullptr;
    }
    std::string header_line(csv_lines[header_line_idx]);
    str::trim(header_line);
    std::vector<std::string> signal_names = str::split(header_line, delimiter);
    if (signal_names.empty()) {
        std::cerr << std::format("No signals found in CSV file \"{}\"\n", csv_filename);
        return nullptr;
    }

    std::vector<CsvSignal> csv_signals;
    csv_signals.reserve(signal_names.size());
    for (std::string const& signal_name : makeUniqueCsvSignalNames(signal_names)) {
        csv_signals.push_back(CsvSignal{.name = signal_name});
    }
    for (int i = header_line_idx + 1; i < csv_lines.size(); ++i) {
        std::string line = str::removeWhitespace(csv_lines[i]);
        std::vector<std::string_view> values = str::splitSv(line, delimiter, (int)csv_signals.size());
        if (values.size() != signal_names.size()) {
            break;
        }
        for (size_t j = 0; j < values.size(); ++j) {
            double value;
            std::from_chars_result result = std::from_chars(values[j].data(), values[j].data() + values[j].size(), value);
            if (result.ec != std::errc()) {
                value = NAN;
            }
            csv_signals[j].samples.push_back(value);
        }
    }

    // Sort signals alphabetically, skip first since that is usually time
    std::sort(csv_signals.begin() + 1, csv_signals.end(), [](CsvSignal const& l, CsvSignal const& r) {
        std::string l_name = l.name;
        std::string r_name = r.name;
        std::transform(l_name.begin(), l_name.end(), l_name.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
        std::transform(r_name.begin(), r_name.end(), r_name.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
        return l_name < r_name;
    });

    for (size_t i = ASCENDING_NUMBERS.size(); i < csv_signals[0].samples.size(); ++i) {
        ASCENDING_NUMBERS.push_back(double(i));
    }

    std::unique_ptr<CsvFileData> csv_data = std::make_unique<CsvFileData>();
    csv_data->name = std::filesystem::relative(filename).string();
    csv_data->displayed_name = std::filesystem::relative(filename).string();
    csv_data->signals = std::move(csv_signals);
    csv_data->write_time = std::filesystem::last_write_time(filename);
    for (CsvSignal& signal : csv_data->signals) {
        signal.file = csv_data.get();
    }
    // Add extra capacity to the vector so that it does not get reallocated when adding custom signals which invalidates
    // pointers to the signals in vector & spectrum plots
    csv_data->signals.reserve(csv_data->signals.size() + CUSTOM_SIGNAL_CAPACITY);
    return std::move(csv_data);
}

void CsvPlotter::showErrorModal() {
    if (!m_error_message.empty()) {
        ImGui::OpenPopup("Error");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); // Center modal
    if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_error_message.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::Text(m_error_message.c_str());
        ImGui::EndPopup();
    }
}

void CsvPlotter::showXSignalCombo() {
    // Collect combo items
    std::vector<std::string> x_signal_combo_items;
    x_signal_combo_items.push_back("Index");
    CsvFileData const* x_signal_file = nullptr;
    for (auto const& file : m_csv_data) {
        if (!file->signals.empty()) {
            x_signal_file = file.get();
            break;
        }
    }
    if (x_signal_file) {
        for (auto const& signal : x_signal_file->signals) {
            x_signal_combo_items.push_back(signal.name);
        }
    }
    // Get index of selected x-signal, default to first signal
    int x_signal_idx = m_options.x_signal_name == "ASCENDING_NUMBERS" ? 0 : 1;
    if (!m_options.x_signal_name.empty()) {
        for (int n = 1; n < (int)x_signal_combo_items.size(); n++) {
            if (x_signal_combo_items[n] == m_options.x_signal_name) {
                x_signal_idx = n;
                break;
            }
        }
    }
    // Show combo
    std::string x_signal_combo_preview = x_signal_combo_items.size() < 2 ? "" : x_signal_combo_items[x_signal_idx];
    ImGui::SetNextItemWidth(185);
    if (ImGui::BeginCombo("X-axis signal", x_signal_combo_preview.c_str())) {
        for (int n = 0; n < (int)x_signal_combo_items.size(); n++) {
            bool is_selected = (x_signal_idx == n);
            if (ImGui::Selectable(x_signal_combo_items[n].c_str(), is_selected)) {
                if (n == 0) {
                    m_options.x_signal_name = "ASCENDING_NUMBERS";
                } else {
                    m_options.x_signal_name = x_signal_combo_items[n];
                }
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void CsvPlotter::showSignalWindow() {
    ImGui::Begin("Signals");

    // Adjust window size only when something is being changed and when the actual size is close to the
    // wanted size because in full screen mode the requested size is not fulfilled and if the requested
    // is updated to match actual, the signals window will always expand to take the full screen.
    if (ImGui::IsAnyMouseDown()) {
        ImVec2 size = ImGui::GetWindowSize();
        int width;
        int height;
        glfwGetWindowSize(m_window, &width, &height);
        // The width has to be rounded because otherwise the window size will slide down to zero when
        // holding mouse down
        float rel_width = std::round(size.x * 100.0f / width) / 100.0f;
        if (abs(m_signals_window_width - rel_width) < 0.03) {
            m_signals_window_width = std::min(rel_width, 0.5f);
        }
    }

    if (ImGui::Button("Open")) {
        openFilesFromDialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        clearPlots();
    }
    ImGui::SameLine();
    char const* remove_all_files_popup_title = "Remove all files  ";
    if (ImGui::Button("Remove all files")) {
        ImGui::OpenPopup(remove_all_files_popup_title);
    }
    float const remove_all_files_popup_width = ImGui::CalcTextSize(remove_all_files_popup_title).x;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(remove_all_files_popup_width, 0));
    if (ImGui::BeginPopupModal(remove_all_files_popup_title, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextUnformatted("Are you sure?");
        ImGui::Separator();
        if (ImGui::Button("Yes")) {
            removeAllFiles();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // The flag is active for only a single frame. A comparison-file switch is
    // handled before this window is rendered, so carry its pending reset here.
    m_flags.reset_colors = m_comparison.reset_colors;
    m_comparison.reset_colors = false;
    if (ImGui::Button("Reset colors")) {
        m_flags.reset_colors = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New file from clipboard")) {
        addClipboardFileFromClipboard();
    }

    if (ImGui::Button("Copy signals to clipboard")) {
        copyPlottedSignalArgumentsToClipboard();
    }

    if (ImGui::CollapsingHeader("Options")) {
        int new_rows = m_rows;
        int new_cols = m_cols;
        ImGui::SetNextItemWidth(75);
        ImGui::InputInt("Rows", &new_rows, 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(75);
        ImGui::InputInt("Columns", &new_cols, 1);
        new_rows = std::clamp(new_rows, 1, MAX_PLOTS);
        new_cols = std::clamp(new_cols, 1, MAX_PLOTS);
        m_rows = new_rows;
        m_cols = new_cols;
        ImGui::SetNextItemWidth(75);
        int undocked_count = m_undocked_plot_count;
        if (ImGui::InputInt("Undocked plots", &undocked_count, 1)) {
            m_undocked_plot_count = std::clamp(undocked_count, 0, MAX_UNDOCKED_PLOTS);
        }
        showXSignalCombo();
        ImGui::SetNextItemWidth(185);
        themeCombo(m_options.theme, m_window);
        std::string plot_style_preview(magic_enum::enum_name(m_options.plot_style));
        ImGui::SetNextItemWidth(185);
        if (ImGui::BeginCombo("Plot style", plot_style_preview.c_str())) {
            for (CsvPlotStyle plot_style : magic_enum::enum_values<CsvPlotStyle>()) {
                std::string plot_style_name(magic_enum::enum_name(plot_style));
                bool is_selected = plot_style == m_options.plot_style;
                if (ImGui::Selectable(plot_style_name.c_str(), is_selected)) {
                    m_options.plot_style = plot_style;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::InputInt("Font size", &m_options.font_size, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_options.font_size = std::clamp((int)m_options.font_size, MIN_FONT_SIZE, MAX_FONT_SIZE);
            ImGui::GetStyle()._NextFrameFontSizeBase = (float)m_options.font_size;
        }
        ImGui::Checkbox("Shift samples to start from zero", &m_options.shift_samples_to_start_from_zero);
        ImGui::Checkbox("Link x-axis", &m_options.link_axis);
        ImGui::Checkbox("Autofix y-axis", &m_options.autofit_y_axis);
        ImGui::Checkbox("Keep old signals on reload", &m_options.keep_old_signals_on_reload);
        ImGui::Checkbox("Cursor measurements", &m_options.cursor_measurements);
        ImGui::Checkbox("Show vertical line in all plots", &m_options.show_vertical_line_in_all_plots);
        ImGui::Checkbox("Interpolate tooltip values", &m_options.interpolate_tooltip);
    }

    if (ImGui::CollapsingHeader("Process .csv files into images")) {
        static std::string config_buffer;
        if (ImGui::InputText("Config", &config_buffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
            for (std::string const& path : openDialogMultiple()) {
#if WINDOWS
                // Call the current process again with the config and file path
                char exe_path[MAX_PATH] = {0};
                if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) == 0) {
                    m_error_message = "Failed to get executable path";
                    continue;
                }
                std::string exe = std::filesystem::path(exe_path).string();
#else
                std::string exe = "/proc/self/exe";
#endif
                std::string command = std::format("{} --image \"{}.png\" --files \"{}\" {}",
                                                  exe,
                                                  path,
                                                  path,
                                                  config_buffer);
                system(command.c_str());
            }
        }
        ImGui::SameLine();
        HelpMarker("Use \"Copy signals to clipboard\" button to get the command line arguments for the current selection of signals and plots");
    }
    if (ImGui::CollapsingHeader("Create custom signal")) {
        showCustomSignalCreator();
    }
    static std::string signal_name_filter;
    ImGui::InputText("Filter", &signal_name_filter);
    ImGui::Separator();

    ImGui::BeginChild("Signal selection", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));

    // m_visible_signals is rebuilt during submission below (only signals that are actually
    // rendered get pushed). Begin-side requests use the previous frame's list, matching the
    // pattern used in DbgGui's scalar/vector/symbol trees.
    ImGuiMultiSelectFlags ms_flags = ImGuiMultiSelectFlags_ClearOnEscape
                                   | ImGuiMultiSelectFlags_BoxSelect1d;
    ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags,
                                                        (int)m_selected_signals.size(),
                                                        -1);
    applyMultiSelectRequests(ms_io, m_selected_signals, m_visible_signals);
    m_visible_signals.clear();

    std::unique_ptr<CsvFileData>* file_to_remove = nullptr;
    int file_to_remove_index = -1;

    for (int file_index = 0; file_index < (int)m_csv_data.size(); ++file_index) {
        auto& file = m_csv_data[file_index];
        // Reload file if it has been rewritten. Wait that file has not been modified in the last 2 seconds
        // in case it is still being written
        if (!file->in_memory && std::filesystem::exists(file->name)) {
            auto last_write_time = std::filesystem::last_write_time(file->name);
            auto now = std::chrono::file_clock::now();
            auto write_time_plus_2s = last_write_time + std::chrono::seconds(2);

            if (last_write_time != file->write_time
                && now > write_time_plus_2s
                && file->write_time != std::filesystem::file_time_type()) {
                std::unique_ptr<CsvFileData> csv_data = parseCsvData(file->name);
                if (csv_data && csv_data->signals.size() == file->signals.size()) {
                    applySignalTransforms(*csv_data);
                    replaceReloadedFileSignals(*file, *csv_data);
                    // Set write time to default so that the file gets reloaded again for the latest dataset
                    file->write_time = std::filesystem::file_time_type();
                    file->run_number++;
                    csv_data->run_number = file->run_number;
                    file->displayed_name += " " + std::to_string(file->run_number);
                    m_csv_data.push_back(std::move(csv_data));
                    break;
                }
            }
        }

        // Only the file label is highlighted; signal selection and editing stay
        // available so normal interactions still work during comparison mode.
        bool const comparison_file_selected = m_comparison.active
                                           && m_comparison.selected_file_index == file_index;
        if (comparison_file_selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, COLOR_GREEN);
        }
        bool opened = ImGui::TreeNode(file->displayed_name.c_str());
        if (comparison_file_selected) {
            ImGui::PopStyleColor();
        }
        // Make displayed name editable
        if (ImGui::BeginPopupContextItem((file->displayed_name + "context_menu").c_str())) {
            static std::string displayed_name_edit = file->displayed_name;
            displayed_name_edit = file->displayed_name;
            if (ImGui::InputText("Name##scalar_context_menu", &displayed_name_edit, ImGuiInputTextFlags_EnterReturnsTrue)) {
                file->displayed_name = displayed_name_edit;
                if (file->displayed_name.empty()) {
                    file->displayed_name = file->name;
                }
            }
            // Center the slider range on the current shift, but keep the
            // captured range stable while the slider is actively being dragged.
            double visible_range = std::abs(m_x_axis.max - m_x_axis.min);
            if (!std::isfinite(visible_range) || visible_range <= 0) {
                visible_range = 1;
            }
            double shift_margin = 0.1 * visible_range;
            MinMax shift_slider_range = {file->x_axis_shift - shift_margin, file->x_axis_shift + shift_margin};
            if (m_x_shift_slider_active) {
                shift_slider_range = m_x_shift_slider_range;
            }
            std::string shift_label = std::format("X-axis shift##x_shift_{}", file->name);
            ImGui::SliderScalar(shift_label.c_str(),
                                ImGuiDataType_Double,
                                &file->x_axis_shift,
                                &shift_slider_range.min,
                                &shift_slider_range.max,
                                "%g");
            m_x_shift_slider_active = ImGui::IsItemActive();
            if (!m_x_shift_slider_active) {
                m_x_shift_slider_range = shift_slider_range;
            }
            if (ImGui::Button("Add same signals to plots")) {
                forEachPlot([&](PlotBase& plot) {
                    if (auto* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
                        std::vector<CsvSignal*> signals_to_add;
                        for (auto& signal : file->signals) {
                            for (CsvSignal* plot_signal : scalar_plot->signals) {
                                if (plot_signal->name == signal.name) {
                                    signals_to_add.push_back(&signal);
                                    break;
                                }
                            }
                        }
                        for (CsvSignal* signal : signals_to_add) {
                            scalar_plot->addSignal(signal);
                        }
                    } else if (auto* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
                        std::vector<std::pair<CsvSignal*, CsvSignal*>> signals_to_add;
                        for (auto const& signals : vector_plot->signals) {
                            CsvSignal* x = findSignalByName(*file, signals.first->name);
                            CsvSignal* y = findSignalByName(*file, signals.second->name);
                            if (x && y) {
                                signals_to_add.push_back({x, y});
                            }
                        }
                        for (auto const& signals : signals_to_add) {
                            vector_plot->addSignal(signals);
                        }
                    } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
                        std::vector<std::pair<CsvSignal*, CsvSignal*>> signals_to_add;
                        for (auto const& spec : spectrum_plot->spectrum) {
                            CsvSignal* real = findSignalByName(*file, spec.real->name);
                            CsvSignal* imag = spec.imag == nullptr ? nullptr : findSignalByName(*file, spec.imag->name);
                            if (real && (spec.imag == nullptr || imag)) {
                                signals_to_add.push_back({real, imag});
                            }
                        }
                        for (auto const& signals : signals_to_add) {
                            spectrum_plot->addSignal(signals.first, signals.second);
                        }
                    }
                });
            }
            if (ImGui::Button("Replace plotted signals with this file's")) {
                auto replacement_in_file = [&](CsvSignal* signal) -> CsvSignal* {
                    if (signal == nullptr) {
                        return nullptr;
                    }
                    CsvSignal* replacement = findSignalByName(*file, signal->name);
                    return replacement == nullptr ? signal : replacement;
                };
                forEachPlot([&](PlotBase& plot) {
                    if (auto* scalar_plot = std::get_if<ScalarPlot>(&plot.variant)) {
                        std::vector<CsvSignal*> replacements;
                        for (CsvSignal* plot_signal : scalar_plot->signals) {
                            CsvSignal* replacement = replacement_in_file(plot_signal);
                            if (replacement != plot_signal) {
                                replacements.push_back(plot_signal);
                                scalar_plot->addSignal(replacement);
                            }
                        }
                        for (CsvSignal* plot_signal : replacements) {
                            scalar_plot->removeSignal(plot_signal);
                        }
                    } else if (auto* vector_plot = std::get_if<VectorPlot>(&plot.variant)) {
                        for (auto& signals : vector_plot->signals) {
                            signals = {replacement_in_file(signals.first), replacement_in_file(signals.second)};
                        }
                    } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&plot.variant)) {
                        for (auto& spec : spectrum_plot->spectrum) {
                            CsvSignal* real = replacement_in_file(spec.real);
                            CsvSignal* imag = replacement_in_file(spec.imag);
                            if (real != spec.real || imag != spec.imag) {
                                spec.real = real;
                                spec.imag = imag;
                                spec.data = {};
                                spec.calculation = std::future<SpectrumData>();
                                spectrum_plot->prev_x_range = {0, 0};
                            }
                        }
                    }
                });
            }

            if (ImGui::Button("Remove signals from plots")) {
                for (auto& signal : file->signals) {
                    removeSignalFromAllPlots(&signal);
                }
            }
            if (ImGui::Button("Remove file")) {
                file_to_remove = &file;
                file_to_remove_index = file_index;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save as CSV")) {
                if (file->signals.empty()) {
                    m_error_message = "Cannot save an empty CSV file";
                    ImGui::CloseCurrentPopup();
                } else {
                    nfdchar_t* out_path = NULL;
                    auto cwd = std::filesystem::current_path();
                    nfdresult_t result = NFD_SaveDialog("csv", cwd.string().c_str(), &out_path);
                    if (result == NFD_OKAY) {
                        std::string out(out_path);
                        if (!out.ends_with(".csv")) {
                            out += ".csv";
                        }
                        free(out_path);
                        std::vector<std::string> header;
                        std::vector<std::vector<double>> samples;
                        for (CsvSignal& signal : file->signals) {
                            header.push_back(signal.name);
                            samples.push_back(signal.samples);
                        }
                        saveAsCsv(out, header, samples);
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImRightAlign(std::format("##right_align_{}_{}", file->name, file->run_number).c_str())) {
            ImGui::Checkbox(std::format("##enabled_{}_{}", file->name, file->run_number).c_str(), &file->enabled);
            ImEndRightAlign();
        }

        if (opened) {
            for (CsvSignal& signal : file->signals) {
                // Skip signal if it doesn't match the filter
                if (!signal_name_filter.empty()
                    && !str::fuzzy_match(signal_name_filter, signal.name.c_str())) {
                    continue;
                }

                // Plotted marker: prefix with "*" when the signal is on any plot.
                // This is separate from the multi-select highlight (which tracks
                // m_selected_signals only).
                bool is_plotted = isSignalPlotted(&signal);

                ImGui::PushStyleColor(ImGuiCol_Text,
                                      signal.transform.isDefault() ?
                                        ImGui::GetStyle().Colors[ImGuiCol_Text] :
                                        COLOR_LIGHT_BLUE);
                std::string label = std::format("{}{}", is_plotted ? "* " : "  ", signal.name);
                bool item_is_selected = contains(m_selected_signals, &signal);
                m_visible_signals.push_back(&signal);
                ImGui::SetNextItemSelectionUserData((int)m_visible_signals.size() - 1);
                ImGui::Selectable(label.c_str(), item_is_selected);

                // Add signal to the visible plot number while clicking. This makes it easier to
                // add signals with same name from different files to same plot.
                std::optional<int> pressed_number = pressedNumber();
                if (pressed_number && ImGui::IsItemClicked()) {
                    int visible_plot_idx = *pressed_number;
                    if (visible_plot_idx < activePlotCount()) {
                        if (auto* scalar_plot = std::get_if<ScalarPlot>(&plotAt(visible_plot_idx).variant)) {
                            scalar_plot->addSignal(&signal);
                        }
                    }
                }

                ImGui::PopStyleColor();

                // Drag the whole selection (or just this signal if it is not selected).
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    std::vector<CsvSignal*> payload_signals;
                    if (item_is_selected) {
                        payload_signals = m_selected_signals;
                    } else {
                        payload_signals.push_back(&signal);
                    }
                    std::vector<CsvSignal*> dragged_signals = payload_signals;
                    bool const add_same_named_signals = ImGui::GetIO().KeyShift;
                    if (add_same_named_signals) {
                        payload_signals = sameNamedSignalsFromOpenFiles(payload_signals);
                    }
                    ImGui::SetDragDropPayload("CSV_MULTI",
                                              payload_signals.data(),
                                              payload_signals.size() * sizeof(CsvSignal*));
                    std::string base_text = add_same_named_signals ? "Drag to plot from all files" : "Drag to plot";
                    ImGui::TextUnformatted(base_text.c_str());
                    for (CsvSignal* dragged_signal : dragged_signals) {
                        ImGui::Text("  %s", dragged_signal->name.c_str());
                    }
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginPopupContextItem((file->displayed_name + signal.name + "context_menu").c_str())) {
                    CsvSignalTransform signal_transform = signal.transform;
                    std::vector<CsvSignal*> signals_to_update = selectedOrClickedSignals(&signal, m_selected_signals);

                    std::array<char, 1024> scale_buffer = {};
                    signal_transform.scale_expression.copy(scale_buffer.data(),
                                                           std::min(signal_transform.scale_expression.size(), scale_buffer.size() - 1));
                    if (ImGui::InputText("Scale", scale_buffer.data(), scale_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::expected<void, std::string> scale_result = setSignalScale(signals_to_update, scale_buffer.data());
                        if (!scale_result.has_value()) {
                            m_error_message = scale_result.error();
                        }
                    }

                    std::array<char, 1024> offset_buffer = {};
                    signal_transform.offset_expression.copy(offset_buffer.data(),
                                                            std::min(signal_transform.offset_expression.size(), offset_buffer.size() - 1));
                    if (ImGui::InputText("Offset", offset_buffer.data(), offset_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        std::expected<void, std::string> offset_result = setSignalOffset(signals_to_update, offset_buffer.data());
                        if (!offset_result.has_value()) {
                            m_error_message = offset_result.error();
                        }
                    }

                    showSignalPlotStyleCombo(signal, signals_to_update);

                    if (ImGui::Button("Copy name")) {
                        ImGui::SetClipboardText(signal.name.c_str());
                        ImGui::CloseCurrentPopup();
                    }

                    if (contains(m_selected_signals, &signal)) {
                        if (ImGui::Button("Remove selected from plots")) {
                            for (CsvSignal* sel : m_selected_signals) {
                                removeSignalFromAllPlots(sel);
                            }
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
    }

    ms_io = ImGui::EndMultiSelect();
    applyMultiSelectRequests(ms_io, m_selected_signals, m_visible_signals);

    ImGui::EndChild();
    ImGui::End();

    if (file_to_remove) {
        removeFileFromPlots(**file_to_remove);
        remove(m_csv_data, *file_to_remove);
        if (m_csv_data.empty()) {
            m_comparison.active = false;
            m_comparison.alt_hold_duration = 0;
            m_comparison.selected_file_index = 0;
        } else if (file_to_remove_index < m_comparison.selected_file_index) {
            // Preserve the same selected file after an earlier list entry moves.
            --m_comparison.selected_file_index;
        } else if (file_to_remove_index == m_comparison.selected_file_index) {
            // Do not retain a snapshot whose selected file has been destroyed.
            m_comparison.active = false;
            m_comparison.alt_hold_duration = 0;
            m_comparison.selected_file_index = std::min(m_comparison.selected_file_index,
                                                        (int)m_csv_data.size() - 1);
        }
    }
}

void CsvPlotter::showPlots() {
    // Scalar plots share an aligned-plot group so their axis padding lines up. Vector
    // and spectrum plots would be forced into that scalar alignment if rendered
    // inside the group. To keep their axes independent (e.g. vector plots'
    // equal-aspect mode) we defer non-scalar plots to a second pass.
    static double vertical_line_time_next = 0;
    double vertical_line_time = vertical_line_time_next;
    vertical_line_time_next = NAN;

    bool aligned = ImPlot::BeginAlignedPlots("AlignedGroup");
    for (int visible_plot_idx = 0; visible_plot_idx < activePlotCount(); ++visible_plot_idx) {
        PlotBase& plot = plotAt(visible_plot_idx);
        if (plot.plotType() == CsvPlotType::Scalar) {
            showScalarPlot(plot, visible_plot_idx, vertical_line_time, vertical_line_time_next);
        }
    }
    if (aligned) {
        ImPlot::EndAlignedPlots();
    }

    for (int visible_plot_idx = 0; visible_plot_idx < activePlotCount(); ++visible_plot_idx) {
        PlotBase& plot = plotAt(visible_plot_idx);
        CsvPlotType type = plot.plotType();
        if (type == CsvPlotType::Vector) {
            showVectorPlot(plot, visible_plot_idx);
        } else if (type == CsvPlotType::Spectrum) {
            showSpectrumPlot(plot, visible_plot_idx);
        }
    }
}

void CsvPlotter::showScalarPlot(PlotBase& plot_base, int visible_plot_idx, double& vertical_line_time, double& vertical_line_time_next) {
    ScalarPlot& plot = std::get<ScalarPlot>(plot_base.variant);
    std::string name = plotWindowName(visible_plot_idx);
    ImGui::Begin(name.c_str());
    if (showPlotContextMenu(plot_base)) {
        ImGui::End();
        return;
    }
    double plot_x_origin = getScalarPlotXOrigin(plot);
    bool autofit_x_axis = (m_x_axis == AUTOFIT_AXIS);
    bool fit_data = plot.autofit_next_frame;
    if (plot.autofit_next_frame || autofit_x_axis) {
        ImPlot::SetNextAxesToFit();
        plot.autofit_next_frame = false;
    }
    if (m_options.autofit_y_axis) {
        ImPlot::SetNextAxisToFit(ImAxis_Y1);
    }

    // Calculate the longest name for table column width
    size_t longest_name_length = 1;
    size_t longest_file_length = 1;
    for (CsvSignal* signal : plot.signals) {
        longest_name_length = std::max(longest_name_length, signal->name.size());
        longest_file_length = std::max(longest_file_length, signal->file->displayed_name.size());
    }

    if (m_options.cursor_measurements) {
        if (ImGui::BeginTable("Delta", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("X").x * (longest_name_length + longest_file_length + 5));
            const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
            ImGui::TableSetupColumn("y1", ImGuiTableColumnFlags_WidthFixed, num_width);
            ImGui::TableSetupColumn("y2", ImGuiTableColumnFlags_WidthFixed, num_width);
            ImGui::TableSetupColumn("delta (y2 - y1)", ImGuiTableColumnFlags_WidthFixed, num_width);
            ImGui::TableHeadersRow();

            // Time row
            ImGui::TableNextColumn();
            ImGui::Text("Time");
            ImGui::TableNextColumn();
            ImGui::Text(std::format("{:g}", m_drag_x1).c_str());
            ImGui::TableNextColumn();
            ImGui::Text(std::format("{:g}", m_drag_x2).c_str());
            ImGui::TableNextColumn();
            ImGui::Text(std::format("{:g}", m_drag_x2 - m_drag_x1).c_str());

            for (int i = 0; i < plot.signals.size(); ++i) {
                CsvSignal* signal = plot.signals[i];
                if (!signal->file->enabled) {
                    continue;
                }
                std::span<double const> all_x_values = getXSignalSamples(*signal->file);
                std::vector<double> const& all_y_values = signal->samples;
                double x_offset = plot_x_origin - signal->file->x_axis_shift;
                CsvPlotStyle signal_plot_style = getSignalPlotStyle(*signal);
                double y1 = getPlotValueAtX(signal_plot_style, all_x_values, all_y_values, m_drag_x1 + x_offset, true);
                double y2 = getPlotValueAtX(signal_plot_style, all_x_values, all_y_values, m_drag_x2 + x_offset, true);
                y1 = y1 * signal->transform.scale + signal->transform.offset;
                y2 = y2 * signal->transform.scale + signal->transform.offset;

                std::stringstream ss;
                ss << std::left << std::setw(longest_name_length) << signal->name << " | " << signal->file->displayed_name;
                std::string displayed_signal_name = ss.str();

                ImVec4 color = ImPlot::GetColormapColor(i);
                ImGui::TableNextColumn();
                ImGui::TextColored(color, displayed_signal_name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(color, std::format("{:g}", y1).c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(color, std::format("{:g}", y2).c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(color, std::format("{:g}", y2 - y1).c_str());
            }
            ImGui::EndTable();
        }
    }

    // Reset plot colors if there are no signals in it
    if (plot.signals.size() == 0 || m_flags.reset_colors) {
        ImPlot::BustColorCache("##DND");
    }

    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
    if (ImPlot::BeginPlot("##DND", ImVec2(-1, -1))) {
        ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_None);
        if (m_options.link_axis) {
            ImPlot::SetupAxisLinks(ImAxis_X1, &m_x_axis.min, &m_x_axis.max);
            if (!autofit_x_axis) {
                ImPlot::SetupAxisLimits(ImAxis_X1, m_x_axis.min, m_x_axis.max);
            }
        }

        if (ImPlot::BeginDragDropTargetPlot()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CSV_MULTI")) {
                std::span<CsvSignal*> sigs(reinterpret_cast<CsvSignal**>(payload->Data),
                                           payload->DataSize / sizeof(CsvSignal*));
                for (CsvSignal* sig : sigs) {
                    plot.addSignal(sig);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LEGEND")) {
                std::span<void* const> legend_payload(static_cast<void* const*>(payload->Data),
                                                      payload->DataSize / sizeof(void*));
                ScalarPlot* source_plot = static_cast<ScalarPlot*>(legend_payload.front());
                if (&plot != source_plot) {
                    for (void* payload_signal : legend_payload.subspan(1)) {
                        CsvSignal* signal = static_cast<CsvSignal*>(payload_signal);
                        plot.addSignal(signal);
                        source_plot->removeSignal(signal);
                    }
                }
            }
            ImPlot::EndDragDropTarget();
        }

        int point_count = int(2.0f * ImPlot::GetPlotSize().x);
        CsvSignal* signal_to_remove = nullptr;
        for (CsvSignal* signal : plot.signals) {
            if (!signal->file->enabled) {
                continue;
            }
            // Collect only values that are within plot range so that the autofit fits to the plotted values instead
            // of the entire data
            ImPlotRect limits = ImPlot::GetPlotLimits();
            if (autofit_x_axis) {
                limits.X.Min = -INFINITY;
                limits.X.Max = INFINITY;
            }
            std::span<double const> x_values = getXSignalSamples(*signal->file);
            std::span<double const> y_values(signal->samples);
            size_t sample_count = MIN(x_values.size(), y_values.size());
            if (sample_count == 0) {
                continue;
            }
            x_values = x_values.first(sample_count);
            y_values = y_values.first(sample_count);
            double x_offset = plot_x_origin - signal->file->x_axis_shift;
            std::pair<int32_t, int32_t> indices = getTimeIndices(x_values, limits.X.Min + x_offset, limits.X.Max + x_offset);
            size_t range_start = size_t(std::clamp(indices.first, 0, int32_t(sample_count)));
            size_t range_end = size_t(std::clamp(indices.second, int32_t(range_start), int32_t(sample_count)));
            if (fit_data) {
                range_start = 0;
                range_end = sample_count;
            }

            // Decimate values because plotting very large amount of samples is slow and the GUI becomes unresponsive
            DecimatedValues plotted_values = decimateValues(x_values,
                                                            y_values,
                                                            range_start,
                                                            range_end,
                                                            point_count,
                                                            {.x_offset = -x_offset,
                                                             .y_scale = signal->transform.scale,
                                                             .y_offset = signal->transform.offset});

            std::stringstream ss;
            ss << std::left << std::setw(longest_name_length) << signal->name << " | " << signal->file->displayed_name;
            std::string label_id = std::format("{}###{}", ss.str(), signal->name + signal->file->displayed_name);
            CsvPlotStyle signal_plot_style = getSignalPlotStyle(*signal);
            int plotted_count = int(MIN(plotted_values.x.size(), plotted_values.y_min.size(), plotted_values.y_max.size()));
            if (plotted_count == 0) {
                continue;
            }
            bool signal_visible = plotCsvSamples(label_id,
                                                 plotted_values.x.data(),
                                                 plotted_values.y_min.data(),
                                                 plotted_count,
                                                 signal_plot_style);
            ImVec4 line_color = ImPlot::GetLastItemColor();
            plotCsvSamples(label_id,
                           plotted_values.x.data(),
                           plotted_values.y_max.data(),
                           plotted_count,
                           signal_plot_style);
            ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.4f);
            ImPlot::PlotShaded(label_id.c_str(),
                               plotted_values.x.data(),
                               plotted_values.y_min.data(),
                               plotted_values.y_max.data(),
                               plotted_count,
                               ImPlotLineFlags_None);

            // Tooltip
            if (ImPlot::IsPlotHovered()) {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                ImPlot::PushStyleColor(ImPlotCol_Line, COLOR_TOOLTIP_LINE);
                ImPlot::PlotInfLines("##", &mouse.x, 1);
                ImPlot::PopStyleColor();

                double mouse_raw_x = mouse.x + x_offset;
                int idx = binarySearch(x_values, mouse_raw_x, 0, int(sample_count - 1));
                double tooltip_value = getPlotValueAtX(signal_plot_style,
                                                       x_values,
                                                       y_values,
                                                       mouse_raw_x,
                                                       m_options.interpolate_tooltip);
                tooltip_value = tooltip_value * signal->transform.scale + signal->transform.offset;
                double tooltip_x = mouse.x;
                if (signal_plot_style == CsvPlotStyle::Linear && !m_options.interpolate_tooltip) {
                    tooltip_x = x_values[idx] - x_offset;
                }
                if (signal_visible) {
                    ImPlot::PushStyleColor(ImPlotCol_Line, line_color);
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3);
                    ImPlot::PlotScatter(("##Point" + signal->name).c_str(), &tooltip_x, &tooltip_value, 1);
                    ImPlot::PopStyleColor();
                }

                vertical_line_time_next = mouse.x;
                ImGui::BeginTooltip();
                ss.str("");
                ss << signal->name << " : " << tooltip_value;
                ImGui::PushStyleColor(ImGuiCol_Text, line_color);
                ImGui::Text(ss.str().c_str());
                ImGui::PopStyleColor();
                ImGui::EndTooltip();
            } else if (m_options.show_vertical_line_in_all_plots && !std::isnan(vertical_line_time)) {
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
                ImPlot::PlotInfLines("##", &vertical_line_time, 1);
                ImPlot::PopStyleColor(1);
            }

            // Legend right click
            if (ImPlot::BeginLegendPopup(label_id.c_str())) {
                if (ImGui::Button("Remove")) {
                    signal_to_remove = signal;
                }
                ImPlot::EndLegendPopup();
            }

            // Fit to the entire data with mouse middle button
            if (ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                plot.autofit_next_frame = true;
            }

            // Legend items can be dragged to other plots to move the signal.
            if (ImPlot::BeginDragDropSourceItem(label_id.c_str(), ImGuiDragDropFlags_None)) {
                bool const move_all_signals = ImGui::GetIO().KeyShift;
                std::vector<void*> legend_payload = {&plot};
                std::span<CsvSignal*> signals_to_drag = move_all_signals ? plot.signals : std::span<CsvSignal*>{&signal, 1};
                for (CsvSignal* source_signal : signals_to_drag) {
                    legend_payload.push_back(source_signal);
                }
                ImGui::SetDragDropPayload("LEGEND", legend_payload.data(), legend_payload.size() * sizeof(void*));
                ImGui::TextUnformatted("Drag to plot");
                for (void* payload_signal : std::span(legend_payload).subspan(1)) {
                    CsvSignal* source_signal = static_cast<CsvSignal*>(payload_signal);
                    ImGui::Text("  %s", source_signal->name.c_str());
                }
                ImPlot::EndDragDropSource();
            }
        }
        if (signal_to_remove) {
            plot.removeSignal(signal_to_remove);
        }

        // Vertical lines for cursor measurements
        if (m_options.cursor_measurements) {
            ImPlotRect plot_limits = ImPlot::GetPlotLimits();
            ImPlotRange x_range = plot_limits.X;
            if (m_drag_x1 == 0 && m_drag_x2 == 0) {
                m_drag_x1 = x_range.Min + 0.1 * x_range.Size();
                m_drag_x2 = x_range.Max - 0.1 * x_range.Size();
            }
            ImPlot::DragLineX(0, &m_drag_x1, COLOR_TOOLTIP_LINE);
            ImPlot::DragLineX(1, &m_drag_x2, COLOR_TOOLTIP_LINE);
            ImPlot::PlotText(std::format("x1 : {:g}", m_drag_x1).c_str(), m_drag_x1, plot_limits.Y.Min + 0.2 * plot_limits.Y.Size());
            ImPlot::PlotText(std::format("x2 : {:g}", m_drag_x2).c_str(), m_drag_x2, plot_limits.Y.Min + 0.2 * plot_limits.Y.Size());
        } else {
            m_drag_x1 = 0;
            m_drag_x2 = 0;
        }

        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    ImGui::End();
}

std::vector<std::string> openDialogMultiple() {
    nfdpathset_t path_set;
    static std::filesystem::path dir = std::filesystem::current_path();
    nfdresult_t result = NFD_OpenDialogMultiple("csv,inf", dir.string().c_str(), &path_set);
    std::vector<std::string> paths;
    if (result == NFD_OKAY) {
        for (int i = 0; i < path_set.count; ++i) {
            std::string out(NFD_PathSet_GetPath(&path_set, i));
            paths.push_back(out);
            dir = std::filesystem::path(out).parent_path();
        }
        NFD_PathSet_Free(&path_set);
    } else if (result == NFD_ERROR) {
        std::cerr << NFD_GetError() << std::endl;
    }
    return paths;
}

std::vector<std::unique_ptr<CsvFileData>> openCsvFromFileDialog() {
    std::vector<std::unique_ptr<CsvFileData>> csv_datas;
    for (std::string const& path : openDialogMultiple()) {
        std::unique_ptr<CsvFileData> csv_data = parseCsvData(path);
        if (csv_data) {
            csv_datas.push_back(std::move(csv_data));
        }
    }
    return csv_datas;
}

void setLayout(ImGuiID main_dock, int rows, int cols, float signals_window_width) {
    // Remove the existing main dock node and all its subnodes to be able to split it
    ImGui::DockBuilderRemoveNode(main_dock);
    // Create new main dock
    main_dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    ImGuiID docks[MAX_PLOTS][MAX_PLOTS]{};
    ImGuiID dock_signals = 0;
    ImGui::DockBuilderSplitNode(main_dock, ImGuiDir_Right, 1.0f - signals_window_width, &docks[0][0], &dock_signals);
    // Split the grid nodes
    for (int row = 0; row < rows; ++row) {
        float row_height = 1.0f / (rows - row);
        // Dont split the last row
        if (row < rows - 1) {
            ImGui::DockBuilderSplitNode(docks[row][0], ImGuiDir_Up, row_height, &docks[row][0], &docks[row + 1][0]);
        }
        for (int col = 0; col < cols - 1; ++col) {
            float col_width = 1.0f / (cols - col);
            ImGui::DockBuilderSplitNode(docks[row][col], ImGuiDir_Left, col_width, &docks[row][col], &docks[row][col + 1]);
        }
    }

    // Dock the windows to correct node
    if (dock_signals != 0) {
        ImGui::DockBuilderDockWindow("Signals", dock_signals);
    }
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int visible_plot_idx = row * cols + col;
            ImGui::DockBuilderDockWindow(std::format("Plot {}", visible_plot_idx).c_str(), docks[row][col]);
        }
    }
    ImGui::DockBuilderFinish(main_dock);
}
