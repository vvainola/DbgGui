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

#define _CRT_SECURE_NO_WARNINGS

#include "csvplot.h"
#include "csv_helpers.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <format>
#include <span>

constexpr double PI = 3.1415926535897;

std::pair<int32_t, int32_t> getTimeIndices(std::span<double const> time, double start_time, double end_time);

template <typename T>
struct XY {
    T x;
    T y;
};

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

void CsvPlotter::showVectorPlots() {
    for (int i = 0; i < m_vector_plot_cnt; ++i) {
        auto& plot = m_vector_plots[i];
        ImGui::Begin(std::format("Vector plot {}", i).c_str(), NULL, ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking);

        if (plot.autofit_next_frame) {
            ImPlot::SetNextAxesToFit();
            plot.autofit_next_frame = false;
        }

        if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, ImGui::GetContentRegionAvail().y), ImPlotFlags_Equal)) {
            // Plot unit circle
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.2f));
            ImPlot::PlotLine("##Unit circle",
                             &UNIT_CIRCLE.data()->x,
                             &UNIT_CIRCLE.data()->y,
                             int(UNIT_CIRCLE.size()),
                             ImPlotLineFlags_None,
                             0,
                             2 * sizeof(double));
            ImPlot::PlotLine("##Half unit circle",
                             &HALF_UNIT_CIRCLE.data()->x,
                             &HALF_UNIT_CIRCLE.data()->y,
                             int(HALF_UNIT_CIRCLE.size()),
                             ImPlotLineFlags_None,
                             0,
                             2 * sizeof(double));
            ImPlot::PopStyleColor();

            std::pair<CsvSignal*, CsvSignal*>* signal_to_remove = nullptr;
            // Plot signals
            for (auto& signals : plot.signals) {
                std::vector<double> const& all_x_values = signals.first->samples;
                std::vector<double> const& all_y_values = signals.second->samples;

                std::pair<int32_t, int32_t> x_indices;
                std::pair<int32_t, int32_t> y_indices;
                if (!m_options.first_signal_as_x) {
                    x_indices = {std::max(0, (int)std::floor(m_x_axis.min)),
                                 std::min((int)all_x_values.size(), (int)std::ceil(m_x_axis.max))};
                    y_indices = {std::max(0, (int)std::floor(m_x_axis.min)),
                                 std::min((int)all_y_values.size(), (int)std::ceil(m_x_axis.max))};
                } else {
                    x_indices = getTimeIndices(signals.first->file->signals[0].samples, m_x_axis.min, m_x_axis.max);
                    y_indices = getTimeIndices(signals.second->file->signals[0].samples, m_x_axis.min, m_x_axis.max);
                }

                std::vector<double> plotted_x(all_x_values.begin() + x_indices.first, all_x_values.begin() + x_indices.second);
                std::vector<double> plotted_y(all_y_values.begin() + y_indices.first, all_y_values.begin() + y_indices.second);
                // Scale samples. Set default scale if signal has no scale
                double& x_scale = m_signal_scales[signals.first->name];
                double& y_scale = m_signal_scales[signals.second->name];
                if (x_scale == 0) {
                    x_scale = 1;
                }
                if (y_scale == 0) {
                    y_scale = 1;
                }
                for (double& x_sample : plotted_x) {
                    x_sample *= x_scale;
                }
                for (double& y_sample : plotted_y) {
                    y_sample *= y_scale;
                }

                std::string displayed_signal_name = std::format("{} | {}", signals.first->name, signals.first->file->displayed_name);
                ImPlot::PlotLine(displayed_signal_name.c_str(),
                                 plotted_x.data(),
                                 plotted_y.data(),
                                 int(plotted_y.size()),
                                 ImPlotLineFlags_None);
                // Plot line from origin to latest sample
                double x_to_latest[2] = {0, plotted_x.back()};
                double y_to_latest[2] = {0, plotted_y.back()};
                ImPlot::PlotLine(displayed_signal_name.c_str(),
                                 x_to_latest,
                                 y_to_latest,
                                 2,
                                 ImPlotLineFlags_None);

                // Legend right-click
                if (ImPlot::BeginLegendPopup(displayed_signal_name.c_str())) {
                    if (ImGui::Button("Remove")) {
                        signal_to_remove = &signals;
                    };
                    ImPlot::EndLegendPopup();
                }

                // Fit the data with both double click & mouse middle button
                if (ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                    plot.autofit_next_frame = true;
                }
            }
            if (signal_to_remove) {
                remove(plot.signals, *signal_to_remove);
            }

            ImPlot::EndPlot();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CSV_Vector")) {
                assert(m_selected_signals[0] != nullptr);
                assert(m_selected_signals[1] != nullptr);
                CsvSignal* signal_x = m_selected_signals[0];
                CsvSignal* signal_y = m_selected_signals[1];
                plot.signals.push_back({signal_x, signal_y});
                m_selected_signals[0] = nullptr;
                m_selected_signals[1] = nullptr;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::End();
    }
}
