// MIT License
//
// Copyright (c) 2026 vvainola
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

#include "plot_base.h"

CsvPlotType PlotBase::plotType() const {
    if (std::holds_alternative<VectorPlot>(variant)) {
        return CsvPlotType::Vector;
    }
    if (std::holds_alternative<SpectrumPlot>(variant)) {
        return CsvPlotType::Spectrum;
    }
    return CsvPlotType::Scalar;
}

void PlotBase::setPlotType(CsvPlotType type) {
    if (plotType() == type) {
        return;
    }

    // A type change invalidates the old signal selection because scalar plots
    // store single signals while vector/spectrum plots store signal pairs.
    switch (type) {
        case CsvPlotType::Scalar:
            variant = ScalarPlot{};
            break;
        case CsvPlotType::Vector:
            variant = VectorPlot{};
            break;
        case CsvPlotType::Spectrum:
            variant = SpectrumPlot{};
            break;
    }
    settings = {};
}

void PlotBase::clearPlot() {
    // Clear both the live plot data and the persisted signal names so the plot
    // remains empty after settings are saved and restored.
    if (auto* scalar_plot = std::get_if<ScalarPlot>(&variant)) {
        scalar_plot->clear();
    } else if (auto* vector_plot = std::get_if<VectorPlot>(&variant)) {
        vector_plot->signals.clear();
    } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&variant)) {
        spectrum_plot->spectrum.clear();
        spectrum_plot->prev_x_range = {0, 0};
    }
    settings = {};
}

void PlotBase::removeSignal(CsvSignal* signal) {
    // File removal/reload works with live CsvSignal pointers, so remove the
    // pointer from whichever concrete plot type is currently active.
    if (auto* scalar_plot = std::get_if<ScalarPlot>(&variant)) {
        scalar_plot->removeSignal(signal);
    } else if (auto* vector_plot = std::get_if<VectorPlot>(&variant)) {
        vector_plot->removeSignal(signal);
    } else if (auto* spectrum_plot = std::get_if<SpectrumPlot>(&variant)) {
        spectrum_plot->removeSignal(signal);
    }
}
