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
#pragma once

#include "csv_helpers.h"
#include "spectrum.h"

#include <string>
#include <utility>
#include <variant>
#include <vector>

struct CsvSignal;

struct MinMax {
    double min;
    double max;

    bool operator==(MinMax const& other) const {
        return min == other.min && max == other.max;
    }
    bool operator!=(MinMax const& other) const {
        return !(*this == other);
    }
};

enum class CsvPlotType {
    Scalar,
    Vector,
    Spectrum,
};

struct CsvPlotSignalSettings {
    std::vector<std::string> scalar_signals;
    std::vector<std::pair<std::string, std::string>> signal_pairs;
};

struct ScalarPlot {
    std::vector<CsvSignal*> signals;
    bool autofit_next_frame = false;

    void addSignal(CsvSignal* signal) {
        if (contains(signals, signal)) {
            return;
        }
        signals.push_back(signal);
    }

    void removeSignal(CsvSignal* signal) {
        if (!contains(signals, signal)) {
            return;
        }
        remove(signals, signal);
    }

    void clear() {
        for (int i = (int)signals.size() - 1; i >= 0; --i) {
            removeSignal(signals[i]);
        }
    }
};

struct VectorPlot {
    std::vector<std::pair<CsvSignal*, CsvSignal*>> signals;
    bool autofit_next_frame = false;

    void addSignal(std::pair<CsvSignal*, CsvSignal*> signal) {
        if (contains(signals, signal)) {
            return;
        }
        signals.push_back(signal);
    }

    void removeSignal(CsvSignal* signal) {
        for (int i = (int)signals.size() - 1; i >= 0; --i) {
            if (signals[i].first == signal || signals[i].second == signal) {
                signals.erase(signals.begin() + i);
            }
        }
    }
};

struct SpectrumPlot {
    std::vector<Spectrum<CsvSignal>> spectrum;
    SpectrumWindow window = SpectrumWindow::None;
    bool logarithmic_y_axis = false;
    MinMax x_axis;
    MinMax y_axis;
    MinMax prev_x_range = {0, 0};

    void addSignal(CsvSignal* real, CsvSignal* imag) {
        for (auto& spec : spectrum) {
            if (spec.real == real && spec.imag == imag) {
                return;
            }
        }
        spectrum.push_back(Spectrum<CsvSignal>(real, imag));
    }

    void removeSignal(CsvSignal* signal) {
        for (int i = (int)spectrum.size() - 1; i >= 0; --i) {
            if (spectrum[i].real == signal || spectrum[i].imag == signal) {
                spectrum.erase(spectrum.begin() + i);
            }
        }
    }
};

// PlotBase owns both the persistable signal-name lists (settings) and the live plot
// data (variant). Bundling them removes the need for a parallel settings array and
// lets callers use plot.settings / plot.variant directly instead of going through an
// index-based accessor.
class PlotBase {
  public:
    CsvPlotSignalSettings settings;
    std::variant<ScalarPlot, VectorPlot, SpectrumPlot> variant = ScalarPlot{};

    PlotBase() = default;
    PlotBase(ScalarPlot p) : variant(std::move(p)) {}
    PlotBase(VectorPlot p) : variant(std::move(p)) {}
    PlotBase(SpectrumPlot p) : variant(std::move(p)) {}

    CsvPlotType plotType() const;
    void setPlotType(CsvPlotType type);
    void clearPlot();
    void removeSignal(CsvSignal* signal);
};
