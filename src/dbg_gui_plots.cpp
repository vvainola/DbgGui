// Dear ImGui: standalone example application for GLFW + OpenGL 3, using
// programmable pipeline (GLFW is a cross-platform general purpose library for
// handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation,
// etc.) If you are new to Dear ImGui, read documentation from the docs/ folder
// + read the top of imgui.cpp. Read online:
// https://github.com/ocornut/imgui/tree/master/docs

#include "dbg_gui.h"
#include "imgui.h"
#include "implot.h"
#include "nfd.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <kissfft/kissfft.hh>
#include <future>

constexpr double PI = 3.1415926535897;

void savePlotAsCsv(ScalarPlot const& plot);
SpectrumPlot::Spectrum calculateSpectrum(std::vector<std::complex<double>> samples,
                                         double sampling_time,
                                         SpectrumPlot::Window window,
                                         bool one_sided);

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
        Scalar* signal_to_remove = nullptr;

        if (!ImGui::Begin(scalar_plot.name.c_str(), &scalar_plot.open)) {
            ImGui::End();
            continue;
        }

        // Menu
        if (ImGui::Button("Menu")) {
            ImGui::OpenPopup("##Menu");
        }
        if (ImGui::BeginPopup("##Menu")) {
            if (ImGui::Button("Save as csv")) {
                savePlotAsCsv(scalar_plot);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Remove all")) {
                scalar_plot.signals.clear();
                m_settings["scalar_plots"][scalar_plot.name]["signals"].clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Time range slider
        ImGui::SameLine();
        float time_range = static_cast<float>(scalar_plot.x_range * 1000);
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
        bool time_range_changed = ImGui::SliderFloat("Time range", &time_range, 1, 1000, "%.1f ms");
        scalar_plot.x_range = time_range * 1e-3f;

        // Auto fit button
        ImGui::SameLine();
        ImPlotAxisFlags x_flags = ImPlotAxisFlags_None;
        ImPlotAxisFlags y_flags = ImPlotAxisFlags_None;
        ImGui::Checkbox("Autofit", &scalar_plot.autofit_y);
        if (scalar_plot.autofit_y) {
            y_flags |= ImPlotAxisFlags_AutoFit;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Show tooltip", &scalar_plot.show_tooltip);

        ImPlot::PushStyleColor(ImPlotCol_LegendBg, {0, 0, 0, 0});
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
        if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
            // Initial axes values from settings
            ImPlot::SetupAxisLimits(ImAxis_Y1, scalar_plot.y_axis_min, scalar_plot.y_axis_max, ImGuiCond_Once);
            // Connect link values
            ImPlot::SetupAxisLinks(ImAxis_Y1, &scalar_plot.y_axis_min, &scalar_plot.y_axis_max);
            ImPlot::SetupAxisLinks(ImAxis_X1, &scalar_plot.x_axis_min, &scalar_plot.x_axis_max);
            // Allow adjusting settings while paused
            if (!m_paused) {
                ImPlot::SetupAxisLimits(ImAxis_X1, m_timestamp - scalar_plot.x_range, m_timestamp, ImGuiCond_Always);
                x_flags |= ImPlotAxisFlags_NoTickLabels;
            } else if (time_range_changed) {
                double mid = 0.5 * (scalar_plot.x_axis_max + scalar_plot.x_axis_min);
                ImPlot::SetupAxisLimits(ImAxis_X1,
                                        mid - scalar_plot.x_range / 2.0,
                                        mid + scalar_plot.x_range / 2.0,
                                        ImGuiCond_Always);
            } else {
                scalar_plot.x_range = scalar_plot.x_axis_max - scalar_plot.x_axis_min;
            }
            ImPlot::SetupAxis(ImAxis_X1, NULL, x_flags);
            ImPlot::SetupAxis(ImAxis_Y1, NULL, y_flags);
            scalar_plot.x_range = std::max(1e-6, scalar_plot.x_range);

            for (Scalar* signal : scalar_plot.signals) {
                ScrollingBuffer::DecimatedValues values = signal->buffer->getValuesInRange(scalar_plot.x_axis_min,
                                                                                           scalar_plot.x_axis_max,
                                                                                           1000,
                                                                                           signal->scale,
                                                                                           signal->offset);
                ImPlot::PlotLine(signal->alias_and_group.c_str(),
                                 values.time.data(),
                                 values.y_min.data(),
                                 int(values.time.size()),
                                 ImPlotLineFlags_None);
                ImPlot::PlotLine(signal->alias_and_group.c_str(),
                                 values.time.data(),
                                 values.y_max.data(),
                                 int(values.time.size()),
                                 ImPlotLineFlags_None);
                ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.4f);
                ImPlot::PlotShaded(signal->alias_and_group.c_str(),
                                   values.time.data(),
                                   values.y_min.data(),
                                   values.y_max.data(),
                                   int(values.time.size()),
                                   ImPlotLineFlags_None);
                // Same signal may be in multiple plots with different color so always
                // update color for tooltip
                signal->color = ImPlot::GetLastItemColor();
                // Legend right-click
                if (ImPlot::BeginLegendPopup(signal->alias_and_group.c_str())) {
                    double current_value = getSourceValue(signal->src);
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::InputDouble("Value", &current_value, 0, 0, "%.3f");
                    if (ImGui::IsItemEdited() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        setSourceValue(signal->src, current_value);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::InputDouble("Trigger level", &current_value, 0, 0, "%.3f");
                    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        signal->addTrigger(current_value);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::InputDouble("Scale", &signal->scale, 0, 0, "%.3f");
                    ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
                    ImGui::InputDouble("Offset", &signal->offset, 0, 0, "%.3f");
                    if (ImGui::Button("Remove")) {
                        signal_to_remove = signal;
                    };
                    ImPlot::EndLegendPopup();
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
                    size_t id = *(size_t*)payload->Data;
                    Scalar* signal = m_scalars[id].get();
                    scalar_plot.addSignalToPlot(signal);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
                    VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
                    Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                    scalar_plot.addSignalToPlot(scalar);
                }
                ImGui::EndDragDropTarget();
            }

            if (scalar_plot.show_tooltip && ImPlot::IsPlotHovered()) {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                ImPlot::PushStyleColor(ImPlotCol_Line, {255, 255, 255, 0.25});
                ImPlot::PlotInfLines("##", &mouse.x, 1);
                ImPlot::PopStyleColor(1);
                ImGui::BeginTooltip();
                for (Scalar* signal : scalar_plot.signals) {
                    ScrollingBuffer::DecimatedValues value = signal->buffer->getValuesInRange(mouse.x,
                                                                                              mouse.x,
                                                                                              1,
                                                                                              signal->scale,
                                                                                              signal->offset);
                    std::stringstream ss;
                    ss << signal->alias_and_group << " : " << value.y_min[0];
                    ImGui::PushStyleColor(ImGuiCol_Text, signal->color);
                    ImGui::Text(ss.str().c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::EndTooltip();
            }

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
        ImPlot::PopStyleColor();
        ImGui::End();

        if (signal_to_remove) {
            remove(scalar_plot.signals, signal_to_remove);
            m_settings["scalar_plots"][scalar_plot.name]["signals"].erase(signal_to_remove->name_and_group);
        }
    }
}

void savePlotAsCsv(ScalarPlot const& plot) {
    nfdchar_t* out_path = NULL;
    auto cwd = std::filesystem::current_path();
    nfdresult_t result = NFD_SaveDialog("csv", cwd.string().c_str(), &out_path);

    if (result == NFD_OKAY) {
        std::string out(out_path);
        free(out_path);
        if (!out.ends_with(".csv")) {
            out.append(".csv");
        }
        std::ofstream csv(out);

        csv << "time0,";
        csv << "time,";
        std::vector<ScrollingBuffer::DecimatedValues> values;
        for (Scalar* signal : plot.signals) {
            csv << signal->name_and_group << ",";
            values.push_back(signal->buffer->getValuesInRange(plot.x_axis_min,
                                                              plot.x_axis_max,
                                                              INT_MAX,
                                                              signal->scale,
                                                              signal->offset));
        }
        csv << "\n";
        if (values.size() == 0) {
            csv.close();
            return;
        }
        for (size_t i = 0; i < values[0].time.size(); ++i) {
            csv << values[0].time[i] - values[0].time[0] << ",";
            csv << values[0].time[i] << ",";
            for (auto& value : values) {
                csv << value.y_min[i] << ",";
            }
            csv << "\n";
        }
        csv.close();
    }
}

void DbgGui::showVectorPlots() {
    for (VectorPlot& vector_plot : m_vector_plots) {
        if (!vector_plot.open) {
            continue;
        }
        if (!ImGui::Begin(vector_plot.name.c_str(), &vector_plot.open)) {
            ImGui::End();
            continue;
        }
        Vector2D* signal_to_remove = nullptr;

        float time_range_ms = vector_plot.time_range * 1e3f;
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::SliderFloat("Time range", &time_range_ms, 0, 100, "%.0f ms");
        vector_plot.time_range = time_range_ms * 1e-3f;

        ImGui::SameLine();
        static float time_offset = 0;
        float time_offset_ms = time_offset * 1e3f;
        ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::SliderFloat("Offset", &time_offset_ms, 0, 100, "%.0f ms");
        time_offset = time_offset_ms * 1e-3f;

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

            if (!m_paused) {
                time_offset = 0;
            }
            double last_sample_time = m_timestamp - time_offset;

            // Collect rotation vectors to rotate samples to reference frame
            std::vector<XY<double>> frame_rotation_vectors;
            if (vector_plot.reference_frame_vector) {
                ScrollingBuffer::DecimatedValues values_x = vector_plot.reference_frame_vector->x->buffer->getValuesInRange(last_sample_time - vector_plot.time_range, last_sample_time, INT_MAX);
                ScrollingBuffer::DecimatedValues values_y = vector_plot.reference_frame_vector->y->buffer->getValuesInRange(last_sample_time - vector_plot.time_range, last_sample_time, INT_MAX);
                frame_rotation_vectors.reserve(values_x.time.size());
                for (size_t i = 0; i < values_x.y_max.size(); ++i) {
                    double angle = -atan2(values_y.y_min[i], values_x.y_min[i]);
                    frame_rotation_vectors.push_back({cos(angle), sin(angle)});
                }
            }

            // Plot vectors
            for (Vector2D* signal : vector_plot.signals) {
                ScrollingBuffer::DecimatedValues values_x = signal->x->buffer->getValuesInRange(last_sample_time - vector_plot.time_range, last_sample_time, INT_MAX);
                ScrollingBuffer::DecimatedValues values_y = signal->y->buffer->getValuesInRange(last_sample_time - vector_plot.time_range, last_sample_time, INT_MAX);
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
                ImPlot::PlotLine(signal->name_and_group.c_str(),
                                 values_x.y_min.data(),
                                 values_y.y_min.data(),
                                 int(count),
                                 ImPlotLineFlags_None);
                // Plot line from origin to latest sample
                double x_to_latest[2] = {0, values_x.y_min.back()};
                double y_to_latest[2] = {0, values_y.y_min.back()};
                ImPlot::PlotLine(signal->name_and_group.c_str(),
                                 x_to_latest,
                                 y_to_latest,
                                 2,
                                 ImPlotLineFlags_None);

                // Legend right-click
                if (ImPlot::BeginLegendPopup(signal->name_and_group.c_str())) {

                    if (signal == vector_plot.reference_frame_vector) {
                        if (ImGui::Button("Remove reference frame")) {
                            vector_plot.reference_frame_vector = nullptr;
                        }
                    } else {
                        if (ImGui::Button("Set as reference frame")) {
                            vector_plot.reference_frame_vector = signal;
                        };
                    }
                    if (ImGui::Button("Remove")) {
                        signal_to_remove = signal;
                    };

                    ImPlot::EndLegendPopup();
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_ID")) {
                    size_t id = *(size_t*)payload->Data;
                    Vector2D* signal = m_vectors[id].get();
                    vector_plot.addSignalToPlot(signal);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_SYMBOL")) {
                    VariantSymbol* symbol_x = *(VariantSymbol**)payload->Data;
                    VariantSymbol* symbol_y = *((VariantSymbol**)payload->Data + 1);
                    Vector2D* vector = addVectorSymbol(symbol_x, symbol_y, m_group_to_add_symbols);
                    vector_plot.addSignalToPlot(vector);
                }
                ImGui::EndDragDropTarget();
            }
            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
        ImGui::End();

        if (signal_to_remove) {
            remove(vector_plot.signals, signal_to_remove);
            m_settings["vector_plots"][vector_plot.name]["signals"].erase(signal_to_remove->name_and_group);
        }
    }
}

template <typename T>
int closestSpectralBin(T vec_x, T vec_y, double x, double y) {
    if (vec_x.size() == 0) {
        return -1;
    }
    auto const it = std::lower_bound(vec_x.begin(), vec_x.end(), x);
    if (it == vec_x.begin()) {
        return 0;
    } else if (it == vec_x.end()) {
        return -1;
    }

    int idx = int(std::distance(vec_x.begin(), it));
    bool y_close = std::abs(vec_y[idx] - y) / y < 0.1;
    bool prev_y_close = std::abs(vec_y[idx - 1] - y) / y < 0.1;

    double err_x = std::abs(vec_x[idx] - x);
    double err_x_prev = std::abs(vec_x[idx - 1] - x);
    if (err_x_prev < err_x && prev_y_close) {
        return idx - 1;
    } else if (y_close) {
        return idx;
    }
    return -1;
}

void DbgGui::showSpectrumPlots() {
    for (SpectrumPlot& plot : m_spectrum_plots) {
        if (!plot.open) {
            continue;
        }
        if (!ImGui::Begin(plot.name.c_str(), &plot.open)) {
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
        ImGui::PushItemWidth(80);
        ImGui::Combo("Window", reinterpret_cast<int*>(&plot.window), "None\0Hann\0Hamming\0Flat top\0\0");

        ImPlot::PushStyleColor(ImPlotCol_LegendBg, {0, 0, 0, 0});
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.1f, 0.1f));
        if (ImPlot::BeginPlot("Spectrum", ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
            // Connect link values
            ImPlot::SetupAxisLinks(ImAxis_Y1, &plot.y_axis_min, &plot.y_axis_max);
            ImPlot::SetupAxisLinks(ImAxis_X1, &plot.x_axis_min, &plot.x_axis_max);

            std::string text = "Drag signal to calculate spectrum";
            if (plot.vector) {
                text = plot.vector->name_and_group;
            } else if (plot.scalar) {
                text = plot.scalar->name_and_group;
            }
            ImPlot::PlotStems(text.c_str(), plot.spectrum.freq.data(), plot.spectrum.mag.data(), int(plot.spectrum.mag.size()));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_ID")) {
                    size_t id = *(size_t*)payload->Data;
                    Scalar* scalar = m_scalars[id].get();
                    plot.addSignalToPlot(scalar);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCALAR_SYMBOL")) {
                    VariantSymbol* symbol = *(VariantSymbol**)payload->Data;
                    Scalar* scalar = addScalarSymbol(symbol, m_group_to_add_symbols);
                    plot.addSignalToPlot(scalar);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_ID")) {
                    size_t id = *(size_t*)payload->Data;
                    Vector2D* vector = m_vectors[id].get();
                    plot.addSignalToPlot(vector);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VECTOR_SYMBOL")) {
                    VariantSymbol* symbol_x = *(VariantSymbol**)payload->Data;
                    VariantSymbol* symbol_y = *((VariantSymbol**)payload->Data + 1);
                    Vector2D* vector = addVectorSymbol(symbol_x, symbol_y, m_group_to_add_symbols);
                    plot.addSignalToPlot(vector);
                }
                ImGui::EndDragDropTarget();
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
        ImPlot::PopStyleColor();

        bool one_sided = plot.scalar != nullptr;
        if (plot.spectrum_calculation.valid() && plot.spectrum_calculation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            plot.spectrum = plot.spectrum_calculation.get();

        } else if (plot.vector && !plot.spectrum_calculation.valid()) {
            ScrollingBuffer::DecimatedValues values_x = plot.vector->x->buffer->getValuesInRange(m_timestamp - plot.time_range, m_timestamp, INT_MAX);
            ScrollingBuffer::DecimatedValues values_y = plot.vector->y->buffer->getValuesInRange(m_timestamp - plot.time_range, m_timestamp, INT_MAX);
            size_t sample_cnt = std::min(values_x.y_min.size(), values_y.y_min.size());
            std::vector<std::complex<double>> samples;
            samples.reserve(sample_cnt);
            for (size_t i = 0; i < sample_cnt; ++i) {
                samples.push_back({values_x.y_min[i], values_y.y_min[i]});
            }
            plot.spectrum_calculation = std::async(std::launch::async,
                                                   calculateSpectrum,
                                                   samples,
                                                   m_sampling_time,
                                                   plot.window,
                                                   one_sided);
        } else if (plot.scalar && !plot.spectrum_calculation.valid()) {
            ScrollingBuffer::DecimatedValues values = plot.scalar->buffer->getValuesInRange(m_timestamp - plot.time_range, m_timestamp, INT_MAX);
            size_t sample_cnt = values.y_min.size();
            std::vector<std::complex<double>> samples;
            samples.reserve(sample_cnt);
            for (size_t i = 0; i < sample_cnt; ++i) {
                samples.push_back({values.y_min[i], 0});
            }
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

/// <summary>
///  Reduce sample count to be a multiple of 2, 3 and 5 so that kf_bfly_generic does not need to be used since it is much slower
/// </summary>
/// <param name="n">Original sample count</param>
/// <returns>Largest sample count that can be expressed as 2^a * 3^b * 5^c where a, b and c are integers</returns>
size_t reduceSampleCountForFFT(size_t n) {
    if (n == 0) {
        n = 1;
    }

    size_t original_n = n;
    while (n % 2 == 0) {
        n = n / 2;
    }
    while (n % 3 == 0) {
        n = n / 3;
    }
    while (n % 5 == 0) {
        n = n / 5;
    }
    if (n == 1) {
        return original_n;
    } else {
        return reduceSampleCountForFFT(original_n - 1);
    }
}

SpectrumPlot::Spectrum calculateSpectrum(std::vector<std::complex<double>> samples,
                                         double sampling_time,
                                         SpectrumPlot::Window window,
                                         bool one_sided) {
    // Push one zero if odd number of samples so that 1 second sampling time does not get
    // truncated down due to floating point inaccuracies when collecting samples (1 sample is missing)
    if (samples.size() % 2 == 1) {
        samples.push_back(0);
    }

    size_t sample_cnt = reduceSampleCountForFFT(samples.size());
    samples.resize(sample_cnt, 0);
    kissfft<double> fft(sample_cnt, false);
    std::vector<std::complex<double>> cplx_spec(sample_cnt, 0);

    // Apply window
    if (window == SpectrumPlot::Window::Hann) {
        for (size_t n = 0; n < sample_cnt; ++n) {
            double amplitude_correction = 2.0;
            samples[n] *= amplitude_correction * (0.5 - 0.5 * cos(2 * PI * n / sample_cnt));
        }
    } else if (window == SpectrumPlot::Window::Hamming) {
        for (size_t n = 0; n < sample_cnt; ++n) {
            double amplitude_correction = 1.8534;
            samples[n] *= amplitude_correction * (0.53836 - 0.46164 * cos(2 * PI * n / sample_cnt));
        }
    } else if (window == SpectrumPlot::Window::FlatTop) {
        for (size_t n = 0; n < sample_cnt; ++n) {
            double a0 = 0.21557895;
            double a1 = 0.41663158;
            double a2 = 0.277263158;
            double a3 = 0.083578947;
            double a4 = 0.006947368;
            double amplitude_correction = 4.6432;
            samples[n] *= amplitude_correction
                        * (a0
                           - a1 * cos(2 * PI * n / (sample_cnt - 1))
                           + a2 * cos(4 * PI * n / (sample_cnt - 1))
                           - a3 * cos(6 * PI * n / (sample_cnt - 1))
                           + a4 * cos(8 * PI * n / (sample_cnt - 1)));
        }
    }
    fft.transform(samples.data(), cplx_spec.data());

    // Calculate magnitude spectrum with Hz on x-axis
    SpectrumPlot::Spectrum spec;
    double abs_max = 0;
    double amplitude_inv = 1.0 / sample_cnt;
    for (std::complex<double> x : cplx_spec) {
        abs_max = std::max(abs_max, amplitude_inv * std::abs(x));
    }
    int mid = int(cplx_spec.size() / 2);
    double resolution = 1.0 / (sampling_time * cplx_spec.size());
    double mag_coeff = one_sided ? 2 : 1;
    if (!one_sided) {
        // Negative side
        for (int i = 0; i < mid; ++i) {
            double mag = mag_coeff * std::abs(cplx_spec[mid + i]) * amplitude_inv;
            if (mag > abs_max * 2e-3) {
                spec.freq.push_back((-mid + i) * resolution);
                spec.mag.push_back(mag);
            }
        }
    }
    // DC
    spec.freq.push_back(0);
    spec.mag.push_back(std::abs(cplx_spec[0]) * amplitude_inv);

    // Positive side
    for (int i = 1; i < mid; ++i) {
        double mag = mag_coeff * std::abs(cplx_spec[i]) * amplitude_inv;
        if (mag > abs_max * 2e-3) {
            spec.freq.push_back(i * resolution);
            spec.mag.push_back(mag);
        }
    }

    return spec;
}
