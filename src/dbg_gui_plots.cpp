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
#include "spectrum.h"
#include "imgui.h"
#include "implot.h"
#include "nfd.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <kissfft/kissfft.hh>
#include <future>

constexpr double LOG_AXIS_Y_MIN = 1e-12;
constexpr double PI = 3.1415926535897;
constexpr int SCALAR_PLOT_POINT_COUNT = 2000;

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

void DbgGui::showScalarPlots() {
    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        if (!scalar_plot.open) {
            continue;
        }
        Scalar* scalar_to_remove = nullptr;

        scalar_plot.focus.focused = ImGui::Begin(scalar_plot.title().c_str(), NULL, ImGuiWindowFlags_NoNavFocus);
        scalar_plot.closeOnMiddleClick();
        scalar_plot.contextMenu();
        if (!scalar_plot.focus.focused) {
            ImGui::End();
            continue;
        }

        // Menu
        if (ImGui::Button("Menu")) {
            ImGui::OpenPopup("##Menu");
        }
        if (ImGui::BeginPopup("##Menu")) {
            if (ImGui::Button("Save as csv")) {
                MinMax time_limits = m_options.link_scalar_x_axis ? m_linked_scalar_x_axis_limits : scalar_plot.x_axis;
                saveScalarsAsCsv(getFilenameToSave(), scalar_plot.scalars, time_limits);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Remove all")) {
                scalar_plot.scalars.clear();
                m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["signals"].clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Selection between common x-axis or separate
        MinMax& x_limits = m_options.link_scalar_x_axis ? m_linked_scalar_x_axis_limits : scalar_plot.x_axis;
        MinMax& y_limits = scalar_plot.y_axis;
        double& x_range = m_options.link_scalar_x_axis ? m_options.m_linked_scalar_x_axis_range : scalar_plot.x_range;

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
        ImPlotAxisFlags x_flags = ImPlotAxisFlags_None;
        ImPlotAxisFlags y_flags = scalar_plot.autofit_y ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None;
        ImGui::Checkbox("Autofit", &scalar_plot.autofit_y);

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
        if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
            // Initial axes values from settings
            ImPlot::SetupAxisLimits(ImAxis_Y1, y_limits.min, y_limits.max, ImGuiCond_Once);
            // Connect link values
            ImPlot::SetupAxisLinks(ImAxis_Y1, &y_limits.min, &y_limits.max);
            ImPlot::SetupAxisLinks(ImAxis_X1, &x_limits.min, &x_limits.max);
            // Autofit x-axis if running or the latest samples after pausing have not been drawn and x-axis fit to those
            // The x-axis can only be freely moved while paused.
            bool running = !m_paused;
            if (running || (scalar_plot.last_frame_timestamp < m_plot_timestamp)) {
                scalar_plot.last_frame_timestamp = m_plot_timestamp;
                ImPlot::SetupAxisLimits(ImAxis_X1, m_plot_timestamp - x_range, m_plot_timestamp, ImGuiCond_Always);
            } else if (time_range_changed) {
                double mid = 0.5 * (x_limits.max + x_limits.min);
                ImPlot::SetupAxisLimits(ImAxis_X1,
                                        mid - x_range / 2.0,
                                        mid + x_range / 2.0,
                                        ImGuiCond_Always);
            }
            ImPlot::SetupAxis(ImAxis_X1, NULL, x_flags);
            ImPlot::SetupAxis(ImAxis_Y1, NULL, y_flags);
            x_range = std::max(1e-6, x_range);

            auto time_idx = m_sampler.getTimeIndices(x_limits.min, x_limits.max);
            for (Scalar* scalar : scalar_plot.scalars) {
                ScrollingBuffer::DecimatedValues values = m_sampler.getValuesInRange(scalar,
                                                                                     time_idx,
                                                                                     SCALAR_PLOT_POINT_COUNT,
                                                                                     scalar->getScale(),
                                                                                     scalar->getOffset());
                std::string label_id = std::format("{}###{}", scalar->alias_and_group, scalar->name_and_group);
                ImPlot::PlotLine(label_id.c_str(),
                                 values.time.data(),
                                 values.y_min.data(),
                                 int(values.time.size()),
                                 ImPlotLineFlags_None);
                ImPlot::PlotLine(label_id.c_str(),
                                 values.time.data(),
                                 values.y_max.data(),
                                 int(values.time.size()),
                                 ImPlotLineFlags_None);
                ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.4f);
                ImPlot::PlotShaded(label_id.c_str(),
                                   values.time.data(),
                                   values.y_min.data(),
                                   values.y_max.data(),
                                   int(values.time.size()),
                                   ImPlotLineFlags_None);
                // Same scalar may be in multiple plots with different color so always
                // update color for tooltip
                scalar->color = ImPlot::GetLastItemColor();
                // Legend right-click
                if (ImPlot::BeginLegendPopup(label_id.c_str())) {
                    double current_value = scalar->getScaledValue();
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::Text(scalar->alias_and_group.c_str());
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::InputDouble("Trigger level", &current_value, 0, 0, "%g");
                    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        m_pause_triggers.push_back(PauseTrigger(scalar, current_value));
                        ImGui::CloseCurrentPopup();
                    }
                    addScalarScaleInput(scalar);
                    addScalarOffsetInput(scalar);
                    if (ImGui::Button("Copy name")) {
                        ImGui::SetClipboardText(scalar->alias.c_str());
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
                    std::pair<ScalarPlot*, Scalar*> plot_and_scalar = {&scalar_plot, scalar};
                    ImGui::SetDragDropPayload("PLOT_AND_SCALAR", &plot_and_scalar, sizeof(plot_and_scalar));
                    ImGui::Text("Drag to move another plot");
                    ImPlot::EndDragDropSource();
                }
            }

            if (ImPlot::BeginDragDropTargetPlot()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
                    uint64_t id = *(uint64_t*)payload->Data;
                    Scalar* scalar = getScalar(id);
                    m_sampler.startSampling(scalar);
                    scalar_plot.addScalarToPlot(scalar);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
                    VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
                    Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                    m_sampler.startSampling(scalar);
                    scalar_plot.addScalarToPlot(scalar);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PLOT_AND_SCALAR")) {
                    std::pair<ScalarPlot*, Scalar*> plot_and_scalar = *(std::pair<ScalarPlot*, Scalar*>*)payload->Data;
                    ScalarPlot* original_plot = plot_and_scalar.first;
                    Scalar* scalar = plot_and_scalar.second;
                    if (original_plot != &scalar_plot) {
                        remove(original_plot->scalars, scalar);
                        size_t signals_removed = m_settings["scalar_plots"][std::to_string(original_plot->id)]["signals"].erase(scalar->name_and_group);
                        assert(signals_removed > 0);
                        scalar_plot.addScalarToPlot(scalar);
                    }
                }
                ImPlot::EndDragDropTarget();
            }

            if (m_options.scalar_plot_tooltip && ImPlot::IsPlotHovered()) {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
                ImPlot::PlotInfLines("##", &mouse.x, 1);
                ImPlot::PopStyleColor(1);
                ImGui::BeginTooltip();
                auto mouse_time_idx = m_sampler.getTimeIndices(mouse.x, mouse.x);
                for (Scalar* scalar : scalar_plot.scalars) {
                    ScrollingBuffer::DecimatedValues value = m_sampler.getValuesInRange(scalar,
                                                                                        mouse_time_idx,
                                                                                        1,
                                                                                        scalar->getScale(),
                                                                                        scalar->getOffset());
                    double tooltip_value = value.y_min[0];
                    std::stringstream ss;
                    ss << scalar->alias_and_group << " : " << tooltip_value;
                    // Write enum value as string if not closing in destructor and it is not possible to write anymore
                    if (std::get_if<ReadWriteFnCustomStr>(&scalar->src) && !m_closing) {
                        // Retrieving the enum value on every iteration is slow so the values are cached.
                        static std::map<std::pair<Scalar*, double>, std::string> enum_str_cache;
                        std::pair<Scalar*, double> scalar_and_value{scalar, tooltip_value};
                        if (!enum_str_cache.contains(scalar_and_value)) {
                            bool paused = m_paused;
                            m_paused = true;
                            // Wait until main thread goes to pause state
                            while (m_next_sync_timestamp > 0) {
                            }
                            double current_value = getSourceValue(scalar->src);
                            // Write the tooltip value to the scalar to retrieve the enum value as str
                            setSourceValue(scalar->src, tooltip_value);
                            enum_str_cache[scalar_and_value] = getSourceValueStr(scalar->src);
                            // Write the original value back
                            setSourceValue(scalar->src, current_value);
                            m_paused = paused;
                        }
                        std::string enum_str = enum_str_cache[scalar_and_value];
                        ss << " (" << enum_str << ")";
                    }

                    ImGui::PushStyleColor(ImGuiCol_Text, scalar->color);
                    ImGui::Text(ss.str().c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::EndTooltip();
            }

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
        ImGui::End();

        if (scalar_to_remove) {
            remove(scalar_plot.scalars, scalar_to_remove);
            size_t signals_removed = m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["signals"].erase(scalar_to_remove->name_and_group);
            assert(signals_removed > 0);
        }
    }
}

void DbgGui::showVectorPlots() {
    for (VectorPlot& vector_plot : m_vector_plots) {
        if (!vector_plot.open) {
            continue;
        }
        vector_plot.focus.focused = ImGui::Begin(vector_plot.title().c_str(), NULL, ImGuiWindowFlags_NoNavFocus);
        vector_plot.closeOnMiddleClick();
        vector_plot.contextMenu();
        if (!vector_plot.focus.focused) {
            ImGui::End();
            continue;
        }

        // Fit first few frames because the initial fit does not seem to work sometimes with equal axes
        if (ImGui::GetFrameCount() < 5) {
            ImPlot::SetNextAxesToFit();
        }

        Vector2D* signal_to_remove = nullptr;

        float time_range_ms = vector_plot.time_range * 1e3f;
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::SliderFloat("Time range", &time_range_ms, 0, 100, "%.0f ms");
        vector_plot.time_range = time_range_ms * 1e-3f;

        static ImPlotAxisFlags flags = ImPlotAxisFlags_None;

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.1f, 0.1f));
        if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, ImGui::GetContentRegionAvail().y), ImPlotFlags_Equal)) {
            ImPlot::SetupAxes(NULL, NULL, flags, flags);

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

            // Use time range from the scalar plots
            double last_sample_time = m_linked_scalar_x_axis_limits.max;
            double first_sample_time = max(last_sample_time - vector_plot.time_range, m_linked_scalar_x_axis_limits.min);
            auto time_idx = m_sampler.getTimeIndices(first_sample_time, last_sample_time);

            // Collect rotation vectors to rotate samples to reference frame
            std::vector<XY<double>> frame_rotation_vectors;
            if (vector_plot.reference_frame_vector) {
                ScrollingBuffer::DecimatedValues values_x = m_sampler.getValuesInRange(vector_plot.reference_frame_vector->x, time_idx, ALL_SAMPLES);
                ScrollingBuffer::DecimatedValues values_y = m_sampler.getValuesInRange(vector_plot.reference_frame_vector->y, time_idx, ALL_SAMPLES);
                frame_rotation_vectors.reserve(values_x.time.size());
                for (size_t i = 0; i < values_x.y_max.size(); ++i) {
                    double angle = -atan2(values_y.y_min[i], values_x.y_min[i]);
                    frame_rotation_vectors.push_back({cos(angle), sin(angle)});
                }
            }

            // Plot vectors
            for (Vector2D* vector : vector_plot.vectors) {
                ScrollingBuffer::DecimatedValues values_x = m_sampler.getValuesInRange(vector->x,
                                                                                       time_idx,
                                                                                       ALL_SAMPLES,
                                                                                       vector->x->getScale(),
                                                                                       vector->x->getOffset());
                ScrollingBuffer::DecimatedValues values_y = m_sampler.getValuesInRange(vector->y,
                                                                                       time_idx,
                                                                                       ALL_SAMPLES,
                                                                                       vector->y->getScale(),
                                                                                       vector->y->getOffset());
                // Rotate samples
                if (frame_rotation_vectors.size() > 0) {
                    for (size_t i = 0; i < values_x.y_max.size(); ++i) {
                        double x_temp = values_x.y_min[i];
                        double y_temp = values_y.y_min[i];
                        values_x.y_min[i] = x_temp * frame_rotation_vectors[i].x - y_temp * frame_rotation_vectors[i].y;
                        values_y.y_min[i] = x_temp * frame_rotation_vectors[i].y + y_temp * frame_rotation_vectors[i].x;
                    }
                }
                size_t count = std::min(values_x.y_min.size(), values_y.y_min.size());
                ImPlot::PlotLine(vector->name_and_group.c_str(),
                                 values_x.y_min.data(),
                                 values_y.y_min.data(),
                                 int(count),
                                 ImPlotLineFlags_None);
                // Plot line from origin to latest sample
                double x_to_latest[2] = {0, values_x.y_min.back()};
                double y_to_latest[2] = {0, values_y.y_min.back()};
                ImPlot::PlotLine(vector->name_and_group.c_str(),
                                 x_to_latest,
                                 y_to_latest,
                                 2,
                                 ImPlotLineFlags_None);

                // Legend right-click
                if (ImPlot::BeginLegendPopup(vector->name_and_group.c_str())) {

                    if (vector == vector_plot.reference_frame_vector) {
                        if (ImGui::Button("Remove reference frame")) {
                            vector_plot.reference_frame_vector = nullptr;
                        }
                    } else {
                        if (ImGui::Button("Set as reference frame")) {
                            vector_plot.reference_frame_vector = vector;
                        };
                    }
                    if (ImGui::Button("Copy name")) {
                        ImGui::SetClipboardText(vector->name.c_str());
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Button("Remove")) {
                        signal_to_remove = vector;
                    };

                    ImPlot::EndLegendPopup();
                }

                // Legend items can be dragged to other plots to move the vector.
                if (ImPlot::BeginDragDropSourceItem(vector->name_and_group.c_str(), ImGuiDragDropFlags_None)) {
                    std::pair<VectorPlot*, Vector2D*> plot_and_vector = {&vector_plot, vector};
                    ImGui::SetDragDropPayload("PLOT_AND_VECTOR", &plot_and_vector, sizeof(plot_and_vector));
                    ImGui::Text("Drag to move another plot");
                    ImPlot::EndDragDropSource();
                }
            }

            if (ImPlot::BeginDragDropTargetPlot()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_ID")) {
                    uint64_t id = *(uint64_t*)payload->Data;
                    Vector2D* vector = getVector(id);
                    m_sampler.startSampling(vector);
                    vector_plot.addVectorToPlot(vector);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_SYMBOL")) {
                    VariantSymbol* symbol_x = *(VariantSymbol**)payload->Data;
                    VariantSymbol* symbol_y = *((VariantSymbol**)payload->Data + 1);
                    Vector2D* vector = addVectorSymbol(symbol_x, symbol_y, m_group_to_add_symbols);
                    m_sampler.startSampling(vector);
                    vector_plot.addVectorToPlot(vector);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PLOT_AND_VECTOR")) {
                    std::pair<VectorPlot*, Vector2D*> plot_and_vector = *(std::pair<VectorPlot*, Vector2D*>*)payload->Data;
                    VectorPlot* original_plot = plot_and_vector.first;
                    Vector2D* vector = plot_and_vector.second;
                    if (original_plot != &vector_plot) {
                        remove(original_plot->vectors, vector);
                        size_t signals_removed = m_settings["vector_plots"][std::to_string(original_plot->id)]["signals"].erase(vector->name_and_group);
                        assert(signals_removed > 0);
                        vector_plot.addVectorToPlot(vector);
                    }
                }
                ImPlot::EndDragDropTarget();
            }
            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
        ImGui::End();

        if (signal_to_remove) {
            remove(vector_plot.vectors, signal_to_remove);
            size_t signals_removed = m_settings["vector_plots"][std::to_string(vector_plot.id)]["signals"].erase(signal_to_remove->name_and_group);
            assert(signals_removed > 0);
        }
    }
}

void DbgGui::showSpectrumPlots() {
    for (SpectrumPlot& plot : m_spectrum_plots) {
        if (!plot.open) {
            continue;
        }

        plot.focus.focused = ImGui::Begin(plot.title().c_str(), NULL, ImGuiWindowFlags_NoNavFocus);
        plot.closeOnMiddleClick();
        plot.contextMenu();
        if (!plot.focus.focused) {
            ImGui::End();
            continue;
        }

        double time_range_ms = plot.time_range * 1e3;
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.6f);
        double min = 1;
        double max = 10000;
        ImGui::SliderScalar("Time range", ImGuiDataType_Double, &time_range_ms, &min, &max, "%.0f ms");
        plot.time_range = time_range_ms * 1e-3;

        ImGui::SameLine();
        ImGui::Checkbox("Logarithmic y-axis", &plot.logarithmic_y_axis);

        ImGui::SameLine();
        ImGui::PushItemWidth(80);
        ImGui::Combo("Window", reinterpret_cast<int*>(&plot.window), "None\0Hann\0Hamming\0Flat top\0\0");

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.1f, 0.1f));
        if (ImPlot::BeginPlot("Spectrum", ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
            // Connect link values
            ImPlot::SetupAxisLinks(ImAxis_Y1, &plot.y_axis.min, &plot.y_axis.max);
            ImPlot::SetupAxisLinks(ImAxis_X1, &plot.x_axis.min, &plot.x_axis.max);
            if (plot.logarithmic_y_axis) {
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                // Limit y-axis minimum with log axis because auto-zoom will otherwise always zoom out min to 1e-300
                if (plot.y_axis.min < LOG_AXIS_Y_MIN) {
                    ImPlot::SetupAxisLimits(ImAxis_Y1, LOG_AXIS_Y_MIN, plot.y_axis.max, ImPlotCond_Always);
                }
            }

            std::string text = "Drag signal to calculate spectrum";
            if (plot.vector) {
                text = plot.vector->name_and_group;
            } else if (plot.scalar) {
                text = plot.scalar->name_and_group;
            } else {
                plot.spectrum.freq.clear();
                plot.spectrum.mag.clear();
            }
            ImPlot::PlotStems(text.c_str(), plot.spectrum.freq.data(), plot.spectrum.mag.data(), int(plot.spectrum.mag.size()));

            if (ImPlot::BeginDragDropTargetPlot()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
                    uint64_t id = *(uint64_t*)payload->Data;
                    Scalar* scalar = getScalar(id);
                    m_sampler.startSampling(scalar);
                    plot.addScalarToPlot(scalar);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
                    VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
                    Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                    m_sampler.startSampling(scalar);
                    plot.addScalarToPlot(scalar);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_ID")) {
                    uint64_t id = *(uint64_t*)payload->Data;
                    Vector2D* vector = getVector(id);
                    m_sampler.startSampling(vector);
                    plot.addVectorToPlot(vector);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_SYMBOL")) {
                    VariantSymbol* symbol_x = *(VariantSymbol**)payload->Data;
                    VariantSymbol* symbol_y = *((VariantSymbol**)payload->Data + 1);
                    Vector2D* vector = addVectorSymbol(symbol_x, symbol_y, m_group_to_add_symbols);
                    m_sampler.startSampling(vector);
                    plot.addVectorToPlot(vector);
                }
                ImPlot::EndDragDropTarget();
            }

            if (ImPlot::IsPlotHovered()) {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                int idx = closestSpectralBin(plot.spectrum.freq, plot.spectrum.mag, mouse.x, mouse.y);
                if (idx != -1) {
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                    ImPlot::PlotStems("", &plot.spectrum.freq[idx], &plot.spectrum.mag[idx], 1);
                    ImGui::BeginTooltip();
                    ImGui::Text(std::format("x : {:10f}", plot.spectrum.freq[idx]).c_str());
                    ImGui::Text(std::format("y : {:10f}", plot.spectrum.mag[idx]).c_str());
                    ImGui::EndTooltip();
                }
            }

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();

        bool one_sided = plot.scalar != nullptr;
        auto time_idx = m_sampler.getTimeIndices(m_plot_timestamp - plot.time_range, m_plot_timestamp);
        if (plot.spectrum_calculation.valid() && plot.spectrum_calculation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            plot.spectrum = plot.spectrum_calculation.get();
        } else if (plot.vector && !plot.spectrum_calculation.valid()) {
            ScrollingBuffer::DecimatedValues samples_x = m_sampler.getValuesInRange(plot.vector->x,
                                                                                    time_idx,
                                                                                    ALL_SAMPLES,
                                                                                    plot.vector->x->getScale(),
                                                                                    plot.vector->x->getOffset());
            ScrollingBuffer::DecimatedValues samples_y = m_sampler.getValuesInRange(plot.vector->y,
                                                                                    time_idx,
                                                                                    ALL_SAMPLES,
                                                                                    plot.vector->y->getScale(),
                                                                                    plot.vector->y->getOffset());
            std::vector<std::complex<double>> samples = collectFftSamples(samples_x.time,
                                                                          samples_x.y_min,
                                                                          samples_y.y_min,
                                                                          m_sampling_time);
            plot.spectrum_calculation = std::async(std::launch::async,
                                                   calculateSpectrum,
                                                   samples,
                                                   m_sampling_time,
                                                   plot.window,
                                                   one_sided);
        } else if (plot.scalar && !plot.spectrum_calculation.valid()) {
            ScrollingBuffer::DecimatedValues values = m_sampler.getValuesInRange(plot.scalar,
                                                                                 time_idx,
                                                                                 ALL_SAMPLES,
                                                                                 plot.scalar->getScale(),
                                                                                 plot.scalar->getOffset());
            std::vector<double> zeros(values.time.size(), 0);
            std::vector<std::complex<double>> samples = collectFftSamples(values.time,
                                                                          values.y_min,
                                                                          zeros,
                                                                          m_sampling_time);
            plot.spectrum_calculation = std::async(std::launch::async,
                                                   calculateSpectrum,
                                                   samples,
                                                   m_sampling_time,
                                                   plot.window,
                                                   one_sided);
        }

        ImGui::End();
    }
}

void DbgGui::saveScalarsAsCsv(std::string filename, std::vector<Scalar*> const& scalars, MinMax time_limits) {
    if (filename.empty()) {
        return;
    }

    // Pause while saving CSV because the CSV saving can take a long time and the sampling
    // buffers would get filled and start hogging a lot of memory
    bool paused = m_paused;
    m_paused = true;
    // Wait until main thread goes to pause state
    while (m_next_sync_timestamp > 0) {
    }

    if (!filename.ends_with(".csv")) {
        filename.append(".csv");
    }
    std::ofstream csv(filename);

    csv << "time0,";
    csv << "time,";
    std::vector<ScrollingBuffer::DecimatedValues> values;
    auto time_idx = m_sampler.getTimeIndices(time_limits.min, time_limits.max);
    for (auto const& scalar : scalars) {
        if (!m_sampler.isScalarSampled(scalar)) {
            continue;
        }
        csv << scalar->name_and_group << ",";
        values.push_back(m_sampler.getValuesInRange(scalar,
                                                    time_idx,
                                                    ALL_SAMPLES,
                                                    scalar->getScale(),
                                                    scalar->getOffset()));
    }
    csv << "\n";
    if (values.size() == 0) {
        csv.close();
        m_paused = paused;
        return;
    }
    for (size_t i = 0; i < values[0].time.size(); ++i) {
        csv << std::format("{:g},{:g},", values[0].time[i] - values[0].time[0], values[0].time[i]);
        for (auto& value : values) {
            csv << std::format("{:g},", value.y_min[i]);
        }
        csv << "\n";
    }
    csv.close();

    m_paused = paused;
}
