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
constexpr double LOG_AXIS_Y_MIN = 1e-12;
constexpr double RAD_TO_DEG = 180.0 / PI;

std::vector<std::complex<double>> realImagToComplex(std::vector<double> const& real, std::vector<double> const& imag) {
    if (real.size() != imag.size()) {
        return {};
    }
    std::vector<std::complex<double>> cvec(real.size());
    std::transform(real.begin(), real.end(), imag.begin(), cvec.begin(), [](double x, double y) {
        return std::complex<double>(x, y);
    });
    return cvec;
}

void CsvPlotter::showSpectrumPlots() {
    for (int i = 0; i < m_spectrum_plot_cnt; ++i) {
        SpectrumPlot& plot = m_spectrum_plots[i];
        ImGui::Begin(std::format("Spectrum plot {}", i).c_str(), NULL, ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking);

        ImGui::Checkbox("Logarithmic y-axis", &plot.logarithmic_y_axis);

        ImGui::SameLine();
        ImGui::PushItemWidth(80);
        ImGui::Combo("Window", reinterpret_cast<int*>(&plot.window), "None\0Hann\0Hamming\0Flat top\0\0");

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.1f, 0.1f));
        if (ImPlot::BeginPlot("##Spectrum", ImVec2(-1, ImGui::GetContentRegionAvail().y))) {
            ImPlot::SetupAxisLinks(ImAxis_Y1, &plot.y_axis.min, &plot.y_axis.max);
            ImPlot::SetupAxisLinks(ImAxis_X1, &plot.x_axis.min, &plot.x_axis.max);

            if (plot.logarithmic_y_axis) {
                // Limit y-axis minimum with log axis because auto-zoom will otherwise always zoom out min to 1e-300
                if (plot.y_axis.min < LOG_AXIS_Y_MIN) {
                    ImPlot::SetupAxisLimits(ImAxis_Y1, LOG_AXIS_Y_MIN, plot.y_axis.max, ImPlotCond_Always);
                }
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
            }
            std::string text = "Drag signal to calculate spectrum";
            if (plot.real) {
                text = plot.real->name;
            }
            ImPlot::PlotStems(text.c_str(), plot.spectrum.freq.data(), plot.spectrum.mag.data(), int(plot.spectrum.mag.size()));

            if (ImPlot::IsPlotHovered()) {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                int idx = closestSpectralBin(plot.spectrum.freq, plot.spectrum.mag, mouse.x, mouse.y);
                if (idx != -1) {
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
                    ImPlot::PlotStems("", &plot.spectrum.freq[idx], &plot.spectrum.mag[idx], 1);
                    ImGui::BeginTooltip();
                    ImGui::Text(std::format("x : {:10f}", plot.spectrum.freq[idx]).c_str());
                    ImGui::Text(std::format("y : {:10f}", plot.spectrum.mag[idx]).c_str());
                    ImGui::Text(std::format("< : {:10.2f}", plot.spectrum.angle[idx] * RAD_TO_DEG).c_str());
                    ImGui::EndTooltip();
                }
            }

            ImPlot::EndPlot();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CSV")) {
                CsvSignal* sig = *(CsvSignal**)payload->Data;
                plot.real = sig;
                plot.imag = nullptr;
                plot.prev_x_range = {0, 0};
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CSV_Vector")) {
                assert(m_selected_signals[0] != nullptr);
                assert(m_selected_signals[1] != nullptr);
                CsvSignal* signal_x = m_selected_signals[0];
                CsvSignal* signal_y = m_selected_signals[1];
                plot.real = signal_x;
                plot.imag = signal_y;
                m_selected_signals[0] = nullptr;
                m_selected_signals[1] = nullptr;
                plot.prev_x_range = {0, 0};
            }
            ImGui::EndDragDropTarget();
        }

        bool one_sided = (plot.imag == nullptr);
        if (plot.spectrum_calculation.valid()
            && plot.spectrum_calculation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            plot.spectrum = plot.spectrum_calculation.get();
        } else if (m_x_axis != plot.prev_x_range
                   && plot.real
                   && plot.imag
                   && !plot.spectrum_calculation.valid()) {
            double sampling_time = plot.real->file->signals[0].samples[1] - plot.real->file->signals[0].samples[0];
            std::vector<double> real = getVisibleSamples(*plot.real);
            std::vector<double> imag = getVisibleSamples(*plot.imag);
            std::vector<std::complex<double>> samples = realImagToComplex(real, imag);
            // Store used x-range so that spectrum is not recalculated over and over if samples are not changing
            plot.prev_x_range = m_x_axis;
            plot.spectrum_calculation = std::async(std::launch::async,
                                                   calculateSpectrum,
                                                   samples,
                                                   sampling_time,
                                                   plot.window,
                                                   one_sided);
        } else if (m_x_axis != plot.prev_x_range
                   && plot.real
                   && !plot.spectrum_calculation.valid()) {
            double sampling_time = plot.real->file->signals[0].samples[1] - plot.real->file->signals[0].samples[0];
            std::vector<double> real = getVisibleSamples(*plot.real);
            std::vector<double> imag(real.size(), 0);
            std::vector<std::complex<double>> samples = realImagToComplex(real, imag);
            // Store used x-range so that spectrum is not recalculated over and over if samples are not changing
            plot.prev_x_range = m_x_axis;
            plot.spectrum_calculation = std::async(std::launch::async,
                                                   calculateSpectrum,
                                                   samples,
                                                   sampling_time,
                                                   plot.window,
                                                   one_sided);
        }
        ImGui::End();
    }
}
