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
#include "minmax.h"
#include "spectrum.h"
#include "imgui.h"
#include "implot.h"
#include "nfd.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <kissfft/kissfft.hh>
#include <future>
#include <span>

std::string formatCsvColumns(std::vector<std::string> const& header,
                             std::vector<std::vector<double>> const& data);

constexpr double LOG_AXIS_Y_MIN = 1e-12;
constexpr double PI = 3.1415926535897;

constexpr std::array<XY<double>, 1000> unitCirclePoints(double radius) {
    std::array<XY<double>, 1000> points;
    double interval = (PI * 2 + 0.01) / points.size();
    for (int i = 0; i < points.size(); ++i) {
        points[i].x = radius * cos(i * interval);
        points[i].y = radius * sin(i * interval);
    }
    return points;
}
const std::array<XY<double>, 1000> UNIT_CIRCLE = unitCirclePoints(1.0);
const std::array<XY<double>, 1000> HALF_UNIT_CIRCLE = unitCirclePoints(0.5);

void addScalarAliasInput(Scalar& scalar) {
    if (ImGui::InputText("Alias", &scalar.alias)) {
        if (scalar.alias.empty()) {
            scalar.alias = scalar.name;
        }
        scalar.updateDisplayNames();
    }
}

namespace {
struct VectorSubplotDragDropPayload {
    PlotWindow::Subplot* subplot;
    VectorPair vector;
};

template <typename Plots, typename Render>
void renderPlotWindows(Plots& plots, double timestamp, Render render) {
    for (auto& plot : plots) {
        if (!plot.open) {
            continue;
        }
        plot.focus.focused = ImGui::Begin(plot.title().c_str(), nullptr, ImGuiWindowFlags_NoNavFocus);
        plot.closeOnMiddleClick();
        plot.contextMenu();
        if (plot.focus.focused) {
            render(plot);
        } else if constexpr (requires { plot.last_frame_timestamp; }) {
            plot.last_frame_timestamp = timestamp;
        }
        ImGui::End();
    }
}

template <typename Render>
void renderPlotGrid(int rows, int cols, const char* id, Render render) {
    if (rows * cols == 1) {
        render(0);
    } else if (ImPlot::BeginSubplots(id, rows, cols, ImVec2(-1, ImGui::GetContentRegionAvail().y), ImPlotSubplotFlags_NoTitle)) {
        for (int index = 0; index < rows * cols; ++index) {
            render(index);
        }
        ImPlot::EndSubplots();
    }
}
std::vector<XY<double>> getFrameRotationVectors(ScrollingBuffer& sampler,
                                                VectorPair const& reference,
                                                std::pair<int32_t, int32_t> time_idx) {
    DecimatedValues values_x = sampler.getValuesInRange(reference.x, time_idx, ALL_SAMPLES);
    DecimatedValues values_y = sampler.getValuesInRange(reference.y, time_idx, ALL_SAMPLES);
    std::vector<XY<double>> rotations;
    rotations.reserve(values_x.x.size());
    for (size_t i = 0; i < values_x.y_max.size(); ++i) {
        double angle = -atan2(values_y.y_min[i], values_x.y_min[i]);
        rotations.push_back({cos(angle), sin(angle)});
    }
    return rotations;
}

void renderVectorPair(ScrollingBuffer& sampler,
                      VectorPair const& vector,
                      const char* label,
                      std::pair<int32_t, int32_t> time_idx,
                      std::vector<XY<double>> const& frame_rotation_vectors) {
    DecimatedValues values_x = sampler.getValuesInRange(vector.x,
                                                        time_idx,
                                                        ALL_SAMPLES,
                                                        vector.x->getScale(),
                                                        vector.x->getOffset());
    DecimatedValues values_y = sampler.getValuesInRange(vector.y,
                                                        time_idx,
                                                        ALL_SAMPLES,
                                                        vector.y->getScale(),
                                                        vector.y->getOffset());
    // Rotate samples
    if (frame_rotation_vectors.size() > 0) {
        for (size_t i = 0; i < values_x.y_max.size(); ++i) {
            double x_temp = values_x.y_min[i];
            double y_temp = values_y.y_min[i];
            values_x.y_min[i] = x_temp * frame_rotation_vectors[i].x - y_temp * frame_rotation_vectors[i].y;
            values_y.y_min[i] = x_temp * frame_rotation_vectors[i].y + y_temp * frame_rotation_vectors[i].x;
        }
    }
    size_t count = MIN(values_x.y_min.size(), values_y.y_min.size());
    ImPlot::PlotLine(label,
                     values_x.y_min.data(),
                     values_y.y_min.data(),
                     int(count),
                     ImPlotLineFlags_None);
    // Plot line from origin to latest sample
    double x_to_latest[2] = {0, values_x.y_min.back()};
    double y_to_latest[2] = {0, values_y.y_min.back()};
    ImPlot::PlotLine(label,
                     x_to_latest,
                     y_to_latest,
                     2,
                     ImPlotLineFlags_None);
}

} // namespace

void DbgGui::showPlots() {
    // Show vertical line at same time in all plots if mouse is hovered in any plot
    static double vertical_line_time_next = 0;
    double vertical_line_time = vertical_line_time_next;
    vertical_line_time_next = NAN;
    renderPlotWindows(m_plot_windows, m_plot_timestamp, [&](PlotWindow& plot_window) {
        // Menu
        if (ImGui::Button("Menu")) {
            ImGui::OpenPopup("##Menu");
        }
        if (ImGui::BeginPopup("##Menu")) {
            if (ImGui::Button("Save as csv")) {
                MinMax time_limits = m_options.link_scalar_x_axis ? m_linked_scalar_x_axis_limits : plot_window.x_axis;
                saveScalarsAsCsv(getFilenameToSave(), plot_window.allScalars(), time_limits);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Remove all")) {
                plot_window.clearSignals();
                ImGui::CloseCurrentPopup();
            }
            int rows = plot_window.rows();
            int cols = plot_window.cols();
            if (ImGui::InputInt("Rows", &rows)) {
                plot_window.setSubplotGrid(rows, plot_window.cols());
            }
            if (ImGui::InputInt("Columns", &cols)) {
                plot_window.setSubplotGrid(plot_window.rows(), cols);
            }
            float type_width = std::max({ImGui::CalcTextSize("Scalar").x, ImGui::CalcTextSize("Vector").x, ImGui::CalcTextSize("Spectrum").x})
                             + ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().FramePadding.x;
            if (ImGui::BeginTable("##SubplotTypes", plot_window.cols(), ImGuiTableFlags_Borders)) {
                for (int row = 0; row < plot_window.rows(); ++row) {
                    ImGui::TableNextRow();
                    for (int col = 0; col < plot_window.cols(); ++col) {
                        ImGui::TableSetColumnIndex(col);
                        int index = row * plot_window.cols() + col;
                        auto& subplot = plot_window.subplots[index];
                        std::string id = std::format("##Type{}", index);
                        ImGui::SetNextItemWidth(type_width);
                        if (ImGui::BeginCombo(id.c_str(), subplot.type == PlotType::Vector ? "Vector" : subplot.type == PlotType::Spectrum ? "Spectrum" :
                                                                                                                                             "Scalar")) {
                            if (ImGui::Selectable("Scalar", subplot.type == PlotType::Scalar)) {
                                subplot.type = PlotType::Scalar;
                            }
                            if (ImGui::Selectable("Vector", subplot.type == PlotType::Vector)) {
                                subplot.type = PlotType::Vector;
                            }
                            if (ImGui::Selectable("Spectrum", subplot.type == PlotType::Spectrum)) {
                                subplot.type = PlotType::Spectrum;
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        std::string autofit_id = std::format("##Autofit{}", index);
                        ImGui::Checkbox(autofit_id.c_str(), &subplot.autofit_y);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Autofit");
                        }
                    }
                }
                ImGui::EndTable();
            }
            bool has_vectors = std::ranges::any_of(plot_window.subplots, [](auto const& subplot) {
                return subplot.type == PlotType::Vector;
            });
            if (has_vectors) {
                float time_range_ms = plot_window.vector_time_range * 1e3f;
                if (ImGui::SliderFloat("Vector time range", &time_range_ms, 0, 100, "%.0f ms")) {
                    plot_window.vector_time_range = time_range_ms * 1e-3f;
                }
            }
            bool has_spectrums = std::ranges::any_of(plot_window.subplots, [](auto const& subplot) {
                return subplot.type == PlotType::Spectrum;
            });
            if (has_spectrums) {
                ImGui::Checkbox("Spectrum logarithmic Y-axis", &plot_window.spectrum.logarithmic_y_axis);
                double time_range_ms = plot_window.spectrum.time_range * 1e3;
                double min = 1, max = 10000;
                if (ImGui::SliderScalar("Spectrum time range", ImGuiDataType_Double, &time_range_ms, &min, &max, "%.0f ms")) {
                    plot_window.spectrum.time_range = time_range_ms * 1e-3;
                }
                ImGui::InputDouble("Bin Threshold [%]", &m_options.spectrum_plot_threshold, 0, 0, "%.3f");
                ImGui::SameLine();
                HelpMarker("Minimum bin magnitude as a percentage of the largest bin. Zero includes all bins.");
                ImGui::Combo("FFT window", reinterpret_cast<int*>(&plot_window.spectrum.window), "None\0Hann\0Hamming\0Flat top\0\0");
            }
            ImGui::EndPopup();
        }

        // Selection between common x-axis or separate
        MinMax& x_limits = m_options.link_scalar_x_axis ? m_linked_scalar_x_axis_limits : plot_window.x_axis;
        double& x_range = m_options.link_scalar_x_axis ? m_options.m_linked_scalar_x_axis_range : plot_window.x_range;

        // Time range slider
        ImGui::SameLine();
        float time_range = static_cast<float>((x_limits.max - x_limits.min) * 1000);
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
        bool time_range_changed = ImGui::SliderFloat("Time range", &time_range, 1, 1000, "%.1f ms");
        if (time_range_changed) {
            x_range = time_range * 1e-3;
        }

        // Auto fit button
        ImGui::SameLine();
        if (plot_window.subplotCount() == 1) {
            ImGui::Checkbox("Autofit", &plot_window.subplots[0].autofit_y);
        } else {
            int autofit_count = 0;
            for (PlotWindow::Subplot const& subplot : plot_window.subplots) {
                if (subplot.autofit_y) {
                    ++autofit_count;
                }
            }
            std::string autofit_label = std::format("Autofit ({}/{})", autofit_count, plot_window.subplotCount());
            ImGui::PushItemWidth(150);
            if (ImGui::BeginCombo("##Autofit", autofit_label.c_str())) {
                // Match the checkbox layout to the actual subplot grid so row/col
                // labels map directly to the plots the user sees.
                if (ImGui::BeginTable("##AutofitGrid", plot_window.cols())) {
                    for (int row = 0; row < plot_window.rows(); ++row) {
                        ImGui::TableNextRow();
                        for (int col = 0; col < plot_window.cols(); ++col) {
                            ImGui::TableSetColumnIndex(col);
                            int subplot_idx = row * plot_window.cols() + col;
                            std::string label = std::format("R{} C{}##Autofit{}", row + 1, col + 1, subplot_idx);
                            ImGui::Checkbox(label.c_str(), &plot_window.subplots[subplot_idx].autofit_y);
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
        }
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
        bool running = !m_paused;
        bool fit_x_to_latest = running || (plot_window.last_frame_timestamp < m_plot_timestamp);
        renderPlotGrid(plot_window.rows(), plot_window.cols(), "##ScalarSubplots", [&](int subplot_idx) {
            auto& subplot = plot_window.subplots[subplot_idx];
            switch (subplot.type) {
                case PlotType::Vector: {
                    double last_sample_time = m_linked_scalar_x_axis_limits.max;
                    double first_sample_time = MAX(last_sample_time - plot_window.vector_time_range, m_linked_scalar_x_axis_limits.min);
                    auto time_idx = m_sampler.getTimeIndices(first_sample_time, last_sample_time);
                    renderVectorSubplot(subplot, subplot_idx, time_idx);
                    break;
                }
                case PlotType::Spectrum: {
                    renderSpectrumSubplot(subplot, subplot_idx, plot_window.spectrum.time_range, plot_window.spectrum.window, plot_window.spectrum.logarithmic_y_axis);
                    break;
                }
                case PlotType::Scalar: {
                    ImPlot::SetNextAxisLinks(ImAxis_X1, &x_limits.min, &x_limits.max);
                    // Follow the latest samples while running; allow free movement while paused.
                    if (fit_x_to_latest) {
                        ImPlot::SetNextAxisLimits(ImAxis_X1, m_plot_timestamp - x_range, m_plot_timestamp, ImGuiCond_Always);
                    } else if (time_range_changed) {
                        double mid = 0.5 * (x_limits.max + x_limits.min);
                        ImPlot::SetNextAxisLimits(ImAxis_X1, mid - x_range / 2.0, mid + x_range / 2.0, ImGuiCond_Always);
                    }
                    x_range = MAX(1e-6, x_range);
                    renderScalarSubplot(subplot, subplot_idx, vertical_line_time, vertical_line_time_next);
                    break;
                }
            }
        });
        if (fit_x_to_latest) {
            plot_window.last_frame_timestamp = m_plot_timestamp;
        }
        ImPlot::PopStyleVar();
    });
}

void DbgGui::renderScalarSubplot(PlotWindow::Subplot& subplot,
                                 int subplot_idx,
                                 double vertical_line_time,
                                 double& vertical_line_time_next) {
    struct PlotScalarDragDropPayload {
        PlotWindow::Subplot* subplot;
        Scalar* scalar;
    };
    Scalar* scalar_to_remove = nullptr;
    MinMax& y_limits = subplot.y_axis;
    ImPlotAxisFlags y_flags = subplot.autofit_y ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None;
    if (!ImPlot::BeginPlot(std::format("##Scrolling{}", subplot_idx).c_str(), ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
        return;
    }
    // Initial axes values from settings
    ImPlot::SetupAxisLimits(ImAxis_Y1, y_limits.min, y_limits.max, ImGuiCond_Once);
    // Connect link values
    ImPlot::SetupAxisLinks(ImAxis_Y1, &y_limits.min, &y_limits.max);
    ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y1, NULL, y_flags);
    ImPlotRange x_limits = ImPlot::GetPlotLimits().X;

    auto time_idx = m_sampler.getTimeIndices(x_limits.Min, x_limits.Max);
    int point_count = int(2.0f * ImPlot::GetPlotSize().x);
    std::unordered_map<Scalar*, bool> scalar_visible;
    for (Scalar* scalar : subplot.scalars) {
        DecimatedValues values = m_sampler.getValuesInRange(scalar,
                                                            time_idx,
                                                            point_count,
                                                            scalar->getScale(),
                                                            scalar->getOffset());
        std::string label_id = std::format("{}###{}", scalar->alias_and_group, scalar->name_and_group);
        bool visible = ImPlot::PlotLine(label_id.c_str(),
                                        values.x.data(),
                                        values.y_min.data(),
                                        int(values.x.size()),
                                        ImPlotLineFlags_None);
        scalar_visible[scalar] = visible;
        ImPlot::PlotLine(label_id.c_str(),
                         values.x.data(),
                         values.y_max.data(),
                         int(values.x.size()),
                         ImPlotLineFlags_None);
        ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.4f);
        ImPlot::PlotShaded(label_id.c_str(),
                           values.x.data(),
                           values.y_min.data(),
                           values.y_max.data(),
                           int(values.x.size()),
                           ImPlotLineFlags_None);
        // Same scalar may be in multiple plots with different color so always
        // update color for tooltip
        scalar->color = ImPlot::GetLastItemColor();
        // Legend right-click
        if (ImPlot::BeginLegendPopup(label_id.c_str())) {
            double current_value = scalar->getScaledValue();
            ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
            ImGui::TextUnformatted(scalar->alias_and_group.c_str());
            addScalarAliasInput(*scalar);
            ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
            if (std::optional<std::string> error = addScalarScaleInput(scalar, m_selected_scalars)) {
                logMessage(*error);
            }
            if (std::optional<std::string> error = addScalarOffsetInput(scalar, m_selected_scalars)) {
                logMessage(*error);
            }
            if (ImGui::InputDouble("Trigger level", &current_value, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_pause_triggers.push_back(PauseTrigger(scalar, current_value));
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Copy name")) {
                ImGui::SetClipboardText(scalar->name.c_str());
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Copy alias")) {
                ImGui::SetClipboardText(scalar->alias.c_str());
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Remove")) {
                scalar_to_remove = scalar;
            };
            ImPlot::EndLegendPopup();
        }

        // Legend items can be dragged to other plots to move the scalar.
        if (ImPlot::BeginDragDropSourceItem(label_id.c_str(), ImGuiDragDropFlags_None)) {
            bool const move_all_signals = ImGui::GetIO().KeyShift;
            std::vector<PlotScalarDragDropPayload> dragged_scalars;
            if (move_all_signals) {
                dragged_scalars.reserve(subplot.scalars.size());
                for (Scalar* source_scalar : subplot.scalars) {
                    dragged_scalars.push_back({&subplot, source_scalar});
                }
            } else {
                dragged_scalars.push_back({&subplot, scalar});
            }
            ImGui::SetDragDropPayload("PLOT_AND_SCALAR",
                                      dragged_scalars.data(),
                                      dragged_scalars.size() * sizeof(PlotScalarDragDropPayload));
            ImGui::TextUnformatted(move_all_signals ? "Drag to move all signals" : "Drag to move signal");
            for (PlotScalarDragDropPayload const& dragged_scalar : dragged_scalars) {
                ImGui::Text("  %s", dragged_scalar.scalar->alias_and_group.c_str());
            }
            ImPlot::EndDragDropSource();
        }
    }

    if (ImPlot::BeginDragDropTargetPlot()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
            std::span<uint64_t> ids(reinterpret_cast<uint64_t*>(payload->Data),
                                    payload->DataSize / sizeof(uint64_t));
            for (uint64_t id : ids) {
                Scalar* scalar = findScalar(m_scalars, id);
                if (scalar) {
                    m_sampler.startSampling(scalar);
                    subplot.addScalar(scalar);
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL_MULTI")) {
            std::span<VariantSymbol*> symbols(reinterpret_cast<VariantSymbol**>(payload->Data),
                                              payload->DataSize / sizeof(VariantSymbol*));
            for (VariantSymbol* symbol : symbols) {
                Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                m_sampler.startSampling(scalar);
                subplot.addScalar(scalar);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PLOT_AND_SCALAR")) {
            std::span<PlotScalarDragDropPayload const> dragged_scalars(
              static_cast<PlotScalarDragDropPayload const*>(payload->Data),
              payload->DataSize / sizeof(PlotScalarDragDropPayload));
            for (PlotScalarDragDropPayload const& dragged_scalar : dragged_scalars) {
                if (dragged_scalar.subplot != &subplot) {
                    dragged_scalar.subplot->removeScalar(dragged_scalar.scalar);
                    subplot.addScalar(dragged_scalar.scalar);
                }
            }
        }
        ImPlot::EndDragDropTarget();
    }

    if (m_options.scalar_plot_tooltip && ImPlot::IsPlotHovered()) {
        ImPlotPoint mouse = ImPlot::GetPlotMousePos();
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        ImPlot::PlotInfLines("##", &mouse.x, 1);
        ImPlot::PopStyleColor(1);
        vertical_line_time_next = mouse.x;

        // Add small point to the sample location
        std::vector<DecimatedValues> scalar_values_in_tooltip;
        auto mouse_time_idx = m_sampler.getTimeIndices(mouse.x, mouse.x);
        for (Scalar* scalar : subplot.scalars) {
            DecimatedValues& value = scalar_values_in_tooltip.emplace_back(
              m_sampler.getValuesInRange(scalar,
                                         mouse_time_idx,
                                         1,
                                         scalar->getScale(),
                                         scalar->getOffset()));
            if (scalar_visible[scalar]) {
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, scalar->color, IMPLOT_AUTO, scalar->color);
                ImPlot::PlotScatter(("##Point" + scalar->name_and_group).c_str(), &value.x.front(), &value.y_min.front(), 1);
            }
        }

        // Show tooltip with values of all scalars at the hovered time
        ImGui::BeginTooltip();
        for (size_t i = 0; i < subplot.scalars.size(); ++i) {
            Scalar* scalar = subplot.scalars[i];
            DecimatedValues const& value = scalar_values_in_tooltip[i];
            double tooltip_value = value.y_min[0];
            std::stringstream ss;
            ss << scalar->alias_and_group << " : " << tooltip_value;
            // Retrieve the name for enum values from the unscaled sample value.
            // During destruction, enum sources can no longer be safely written.
            if (std::get_if<ReadWriteFnCustomStr>(&scalar->src) && !m_closing) {
                // Retrieving the enum value on every iteration is slow so the values are cached.
                static std::map<std::pair<Scalar*, double>, std::string> enum_str_cache;
                double source_value = (tooltip_value - scalar->getOffset()) / scalar->getScale();
                if (std::isfinite(source_value)) {
                    std::pair<Scalar*, double> scalar_and_value{scalar, source_value};
                    if (!enum_str_cache.contains(scalar_and_value)) {
                        bool paused = m_paused;
                        m_paused = true;
                        // Wait until main thread goes to pause state
                        while (m_next_sync_timestamp > 0) {
                        }
                        double current_value = getSourceValue(scalar->src);
                        // Temporarily write the unscaled sample value to retrieve its enum name.
                        setSourceValue(scalar->src, source_value);
                        enum_str_cache[scalar_and_value] = getSourceValueStr(scalar->src);
                        // Write the original value back
                        setSourceValue(scalar->src, current_value);
                        m_paused = paused;
                    }
                    std::string const& enum_str = enum_str_cache[scalar_and_value];
                    if (!enum_str.empty()) {
                        ss << " (" << enum_str << ")";
                    }
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, scalar->color);
            ImGui::TextUnformatted(ss.str().c_str());
            ImGui::PopStyleColor();
        }

        ImGui::EndTooltip();
    } else if (m_options.show_vertical_line_in_all_plots && !std::isnan(vertical_line_time)) {
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        ImPlot::PlotInfLines("##", &vertical_line_time, 1);
        ImPlot::PopStyleColor(1);
    }

    ImPlot::EndPlot();
    if (scalar_to_remove) {
        subplot.removeScalar(scalar_to_remove);
    }
}

void DbgGui::renderVectorSubplot(PlotWindow::Subplot& subplot, int subplot_idx, std::pair<int32_t, int32_t> time_idx) {
    if (ImGui::GetFrameCount() < 5) {
        ImPlot::SetNextAxesToFit();
    }
    if (!ImPlot::BeginPlot(std::format("##Vectors{}", subplot_idx).c_str(),
                           ImVec2(-1, ImGui::GetContentRegionAvail().y),
                           ImPlotFlags_Equal)) {
        return;
    }
    ImPlotAxisFlags axis_flags = subplot.autofit_y ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None;
    ImPlot::SetupAxes(nullptr, nullptr, axis_flags, axis_flags);
    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.2f));
    for (auto const* circle : {&UNIT_CIRCLE, &HALF_UNIT_CIRCLE}) {
        ImPlot::PlotLine(circle == &UNIT_CIRCLE ? "##Unit circle" : "##Half unit circle",
                         &circle->front().x,
                         &circle->front().y,
                         int(circle->size()),
                         ImPlotLineFlags_None,
                         0,
                         sizeof(XY<double>));
    }
    ImPlot::PopStyleColor();

    std::vector<XY<double>> rotations;
    if (subplot.reference_frame_vector) {
        rotations = getFrameRotationVectors(m_sampler, *subplot.reference_frame_vector, time_idx);
    }
    std::optional<VectorPair> vector_to_remove;
    for (VectorPair const& vector : subplot.vectors) {
        std::string label = std::format("{} / {}###{}:{}", vector.x->alias_and_group, vector.y->alias_and_group, vector.x->id, vector.y->id);
        renderVectorPair(m_sampler, vector, label.c_str(), time_idx, rotations);
        if (ImPlot::BeginLegendPopup(label.c_str())) {
            if (subplot.reference_frame_vector == vector) {
                if (ImGui::Button("Remove reference frame")) {
                    subplot.reference_frame_vector.reset();
                }
            } else if (ImGui::Button("Set as reference frame")) {
                subplot.reference_frame_vector = vector;
            }
            if (ImGui::Button("Remove")) {
                vector_to_remove = vector;
            }
            ImPlot::EndLegendPopup();
        }
        if (ImPlot::BeginDragDropSourceItem(label.c_str())) {
            VectorSubplotDragDropPayload payload{&subplot, vector};
            ImGui::SetDragDropPayload("SUBPLOT_VECTOR", &payload, sizeof(payload));
            ImGui::Text("Drag to move vector\n  %s / %s", vector.x->alias_and_group.c_str(), vector.y->alias_and_group.c_str());
            ImPlot::EndDragDropSource();
        }
    }
    if (ImPlot::BeginDragDropTargetPlot()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SUBPLOT_VECTOR")) {
            auto const& dragged = *static_cast<VectorSubplotDragDropPayload const*>(payload->Data);
            if (dragged.subplot != &subplot) {
                dragged.subplot->removeVector(dragged.vector);
                subplot.addVector(dragged.vector);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
            std::span<uint64_t const> ids(static_cast<uint64_t const*>(payload->Data), payload->DataSize / sizeof(uint64_t));
            if (ids.size() == 2) {
                Scalar* x = findScalar(m_scalars, ids[0]);
                Scalar* y = findScalar(m_scalars, ids[1]);
                if (x && y) {
                    m_sampler.startSampling(x);
                    m_sampler.startSampling(y);
                    subplot.addVector({x, y});
                }
            } else {
                logMessage("Vector plots require exactly two scalars.");
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL_MULTI")) {
            std::span<VariantSymbol* const> symbols(static_cast<VariantSymbol* const*>(payload->Data),
                                                    payload->DataSize / sizeof(VariantSymbol*));
            if (symbols.size() == 2) {
                Scalar* x = addScalarSymbol(symbols[0], m_group_to_add_symbols);
                Scalar* y = addScalarSymbol(symbols[1], m_group_to_add_symbols);
                m_sampler.startSampling(x);
                m_sampler.startSampling(y);
                subplot.addVector({x, y});
            } else {
                logMessage("Vector plots require exactly two symbols.");
            }
        }
        ImPlot::EndDragDropTarget();
    }
    ImPlot::EndPlot();
    if (vector_to_remove) {
        subplot.removeVector(*vector_to_remove);
    }
}

void DbgGui::renderSpectrumSubplot(PlotWindow::Subplot& subplot,
                                   int subplot_idx,
                                   double time_range,
                                   SpectrumWindow window,
                                   bool logarithmic_y_axis) {
    std::optional<VectorPair> spectrum_to_remove;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.1f, 0.1f));
    if (ImPlot::BeginPlot(std::format("##Spectrum{}", subplot_idx).c_str(), ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, subplot.autofit_y ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None);
        // Connect link values
        ImPlot::SetupAxisLinks(ImAxis_Y1, &subplot.spectrum_y_axis.min, &subplot.spectrum_y_axis.max);
        ImPlot::SetupAxisLinks(ImAxis_X1, &subplot.spectrum_x_axis.min, &subplot.spectrum_x_axis.max);
        if (logarithmic_y_axis) {
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
            // Limit y-axis minimum with log axis because auto-zoom will otherwise always zoom out min to 1e-300
            if (subplot.spectrum_y_axis.min < LOG_AXIS_Y_MIN) {
                ImPlot::SetupAxisLimits(ImAxis_Y1, LOG_AXIS_Y_MIN, subplot.spectrum_y_axis.max, ImPlotCond_Always);
            }
        }

        for (auto& spec : subplot.spectrums) {
            std::string const label_id = std::format("{}###Spectrum{}:{}", spec.real->alias_and_group, spec.real->id, spec.imag ? spec.imag->id : 0);
            ImPlot::PlotStems(label_id.c_str(), spec.data.freq.data(), spec.data.mag.data(), int(spec.data.mag.size()));
            // Legend right-click
            if (ImPlot::BeginLegendPopup(label_id.c_str())) {
                addScalarAliasInput(*spec.real);
                if (ImGui::Button("Remove")) {
                    spectrum_to_remove = VectorPair{spec.real, spec.imag};
                };
                ImPlot::EndLegendPopup();
            }
        }

        if (ImPlot::BeginDragDropTargetPlot()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SUBPLOT_VECTOR")) {
                auto const& dragged = *static_cast<VectorSubplotDragDropPayload const*>(payload->Data);
                m_sampler.startSampling(dragged.vector);
                subplot.addSpectrum(dragged.vector.x, dragged.vector.y);
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID_MULTI")) {
                std::span<uint64_t> ids(reinterpret_cast<uint64_t*>(payload->Data),
                                        payload->DataSize / sizeof(uint64_t));
                for (uint64_t id : ids) {
                    Scalar* scalar = findScalar(m_scalars, id);
                    if (scalar) {
                        m_sampler.startSampling(scalar);
                        subplot.addSpectrum(scalar, nullptr);
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL_MULTI")) {
                std::span<VariantSymbol*> symbols(reinterpret_cast<VariantSymbol**>(payload->Data),
                                                  payload->DataSize / sizeof(VariantSymbol*));
                for (VariantSymbol* symbol : symbols) {
                    Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                    m_sampler.startSampling(scalar);
                    subplot.addSpectrum(scalar, nullptr);
                }
            }
            ImPlot::EndDragDropTarget();
        }

        if (ImPlot::IsPlotHovered()) {
            ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            for (int i = 0; i < subplot.spectrums.size(); ++i) {
                auto& spec = subplot.spectrums[i];
                int idx = closestSpectralBin(spec.data.freq, spec.data.mag, mouse.x, mouse.y);
                if (idx != -1) {
                    ImVec4 color = ImPlot::GetColormapColor(i);
                    ImPlot::PushStyleColor(ImPlotCol_Line, color);
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, IMPLOT_AUTO, color, IMPLOT_AUTO, color);
                    ImPlot::PlotStems("", &spec.data.freq[idx], &spec.data.mag[idx], 1);
                    ImPlot::PopStyleColor();
                    ImGui::BeginTooltip();
                    ImGui::TextColored(color, "%s", std::format("{} x : {:10f}", spec.real->alias, spec.data.freq[idx]).c_str());
                    ImGui::TextColored(color, "%s", std::format("{} y : {:10f}", spec.real->alias, spec.data.mag[idx]).c_str());
                    ImGui::EndTooltip();
                }
            }
        }

        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    if (spectrum_to_remove) {
        subplot.removeSpectrum(spectrum_to_remove->x, spectrum_to_remove->y);
    }

    for (auto& spec : subplot.spectrums) {
        bool one_sided = spec.imag == nullptr;
        auto time_idx = m_sampler.getTimeIndices(m_plot_timestamp - time_range, m_plot_timestamp);
        if (spec.calculation.valid() && spec.calculation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            spec.data = spec.calculation.get();
        } else if (!one_sided && !spec.calculation.valid()) {
            DecimatedValues samples_x = m_sampler.getValuesInRange(spec.real,
                                                                   time_idx,
                                                                   ALL_SAMPLES,
                                                                   spec.real->getScale(),
                                                                   spec.real->getOffset());
            DecimatedValues samples_y = m_sampler.getValuesInRange(spec.imag,
                                                                   time_idx,
                                                                   ALL_SAMPLES,
                                                                   spec.imag->getScale(),
                                                                   spec.imag->getOffset());
            std::vector<std::complex<double>> samples = collectFftSamples(samples_x.x,
                                                                          samples_x.y_min,
                                                                          samples_y.y_min,
                                                                          m_sampling_time);
            spec.calculation = std::async(std::launch::async,
                                          calculateSpectrum,
                                          samples,
                                          m_sampling_time,
                                          window,
                                          one_sided,
                                          m_options.spectrum_plot_threshold / 100.0);
        } else if (one_sided && !spec.calculation.valid()) {
            DecimatedValues values = m_sampler.getValuesInRange(spec.real,
                                                                time_idx,
                                                                ALL_SAMPLES,
                                                                spec.real->getScale(),
                                                                spec.real->getOffset());
            std::vector<double> zeros(values.x.size(), 0);
            std::vector<std::complex<double>> samples = collectFftSamples(values.x,
                                                                          values.y_min,
                                                                          zeros,
                                                                          m_sampling_time);
            spec.calculation = std::async(std::launch::async,
                                          calculateSpectrum,
                                          samples,
                                          m_sampling_time,
                                          window,
                                          one_sided,
                                          m_options.spectrum_plot_threshold / 100.0);
        }
    }
}

SampleClipboardData DbgGui::collectScalarSamples(std::vector<Scalar*> const& scalars, MinMax time_limits) {
    SampleClipboardData samples;

    // Pause while collecting samples because export can take a long time and the sampling
    // buffers would get filled and start hogging a lot of memory
    bool paused = m_paused;
    m_paused = true;
    // Wait until main thread goes to pause state
    while (m_next_sync_timestamp > 0) {
    }

    auto time_idx = m_sampler.getTimeIndices(time_limits.min, time_limits.max);
    if (time_idx.first < 0 || time_idx.second < 0) {
        m_paused = paused;
        return samples;
    }

    std::vector<double> time = m_sampler.getTimeInRange(time_idx);
    if (time.empty()) {
        m_paused = paused;
        return samples;
    }

    std::vector<double> time_from_zero;
    time_from_zero.reserve(time.size());
    for (double t : time) {
        time_from_zero.push_back(t - time.front());
    }

    samples.header = {"time0", "time"};
    samples.data.reserve(scalars.size() + 2);
    samples.data.push_back(std::move(time_from_zero));
    samples.data.push_back(std::move(time));

    for (auto const& scalar : scalars) {
        if (!m_sampler.isScalarSampled(scalar)) {
            continue;
        }
        std::vector<double> scalar_samples = m_sampler.getSamplesInRange(scalar,
                                                                         time_idx,
                                                                         scalar->getScale(),
                                                                         scalar->getOffset());
        if (scalar_samples.empty()) {
            continue;
        }
        samples.header.push_back(scalar->name_and_group);
        samples.data.push_back(std::move(scalar_samples));
    }

    if (samples.data.size() <= 2) {
        samples = {};
        m_paused = paused;
        return samples;
    }

    m_paused = paused;
    return samples;
}

void DbgGui::copyAllScalarSamplesToClipboard() {
    std::vector<Scalar*> scalars;
    scalars.reserve(m_scalars.size());
    for (auto const& scalar : m_scalars) {
        scalars.push_back(scalar.get());
    }

    SampleClipboardData samples = collectScalarSamples(scalars, m_linked_scalar_x_axis_limits);
    if (!copySamplesToClipboard(samples)) {
        logMessage("No sampled scalar data in the visible range");
    }
}

void DbgGui::saveScalarsAsCsv(std::string filename, std::vector<Scalar*> const& scalars, MinMax time_limits) {
    if (filename.empty()) {
        return;
    }

    SampleClipboardData samples = collectScalarSamples(scalars, time_limits);
    if (samples.data.empty()) {
        logMessage("No sampled scalar data in the visible range");
        return;
    }
    std::string csv_text = formatCsvColumns(samples.header, samples.data);

    if (!filename.ends_with(".csv")) {
        filename.append(".csv");
    }
    std::ofstream csv(filename);
    csv << csv_text;
    csv.close();
}
