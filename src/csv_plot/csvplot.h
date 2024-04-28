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
#pragma once

#include "themes.h"
#include "imgui.h"
#include "spectrum.h"
#include <string>
#include <filesystem>
#include <map>
#include <array>
#include <future>

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

inline constexpr int NOT_VISIBLE = -1;
inline constexpr ImVec4 NO_COLOR = {-1, -1, -1, -1};
inline constexpr MinMax AUTOFIT_AXIS{-1, 1};
inline constexpr int MAX_PLOTS = 10;

struct CsvSignal;
struct GLFWwindow;

struct CsvFileData {
    std::string name;
    std::string displayed_name;
    std::vector<CsvSignal> signals;
    std::filesystem::file_time_type write_time;
    double x_axis_shift = 0;
    int run_number = 0;

    bool operator==(CsvFileData const& other) {
        return name == other.name
            && displayed_name == other.displayed_name
            && write_time == other.write_time
            && run_number == other.run_number;
    }
};

struct CsvSignal {
    std::string name;
    std::vector<double> samples;
    int plot_idx = NOT_VISIBLE;
    ImVec4 color{NO_COLOR};
    CsvFileData const* file;
};

struct VectorPlot {
    std::vector<std::pair<CsvSignal*, CsvSignal*>> signals;
    bool autofit_next_frame = false;
};

struct SpectrumPlot {
    CsvSignal* real = nullptr;
    CsvSignal* imag = nullptr;
    Spectrum spectrum;
    SpectrumWindow window = SpectrumWindow::None;
    std::future<Spectrum> spectrum_calculation;
    bool logarithmic_y_axis = false;
    MinMax x_axis;
    MinMax y_axis;
    MinMax prev_x_range = {0, 0};
};

class CsvPlotter {
  public:
    CsvPlotter(std::vector<std::string> files = {},
               std::map<std::string, int> name_and_plot_idx = {},
               MinMax const& xlimits = AUTOFIT_AXIS,
               std::string const& image_filepath = "");

  private:
    void showSignalWindow();
    void showScalarPlots();
    void showVectorPlots();
    void showSpectrumPlots();

    std::vector<double> getVisibleSamples(CsvSignal const& signal);

    void updateSavedSettings();
    void loadPreviousSessionSettings();
    GLFWwindow* m_window;

    std::vector<std::unique_ptr<CsvFileData>> m_csv_data;
    std::map<std::string, double> m_signal_scales;
    int m_rows = 1;
    int m_cols = 1;
    int m_vector_plot_cnt = 0;
    int m_spectrum_plot_cnt = 0;
    int m_fit_plot_idx = -1;
    float m_signals_window_width = 0.15f;
    struct {
        bool first_signal_as_x = true;
        bool shift_samples_to_start_from_zero = true;
        bool link_axis = true;
        bool autofit_y_axis = true;
        bool fit_after_drag_and_drop = true;
        bool keep_old_signals_on_reload = true;
        bool cursor_measurements = false;
        Theme theme;
    } m_options;
    MinMax m_x_axis = AUTOFIT_AXIS;
    double m_drag_x1 = 0;
    double m_drag_x2 = 0;

    CsvSignal* m_selected_signals[2] = {};
    std::array<VectorPlot, MAX_PLOTS> m_vector_plots;
    std::array<SpectrumPlot, MAX_PLOTS> m_spectrum_plots;
};
