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

#include "symbols/dbghelp_symbols_lookup.h"
#include <imgui.h>

#include <numeric>
#include <vector>
#include <cmath>
#include <future>
#include <string>

inline double getSourceValue(ValueSource src) {
    return std::visit(
        [=](auto&& src) {
            using T = std::decay_t<decltype(src)>;
            if constexpr (std::is_same_v<T, ReadWriteFn>) {
                return src(std::nullopt);
            } else if constexpr (std::is_same_v<T, ReadWriteFnCustomStr>) {
                return src(std::nullopt).second;
            } else {
                return static_cast<double>(*src);
            }
        },
        src);
}

inline void setSourceValue(ValueSource dst, double value) {
    std::visit(
        [=](auto&& dst) {
            using T = std::decay_t<decltype(dst)>;
            if constexpr (std::is_same_v<T, ReadWriteFn> || std::is_same_v<T, ReadWriteFnCustomStr>) {
                dst(value);
            } else {
                *dst = static_cast<std::remove_pointer<T>::type>(value);
            }
        },
        dst);
}

template <typename T>
struct XY {
    T x;
    T y;
};

struct Focus {
    bool focused = false;
    bool initial_focus = false;
};

struct MinMax {
    double min;
    double max;
};

struct Trigger {
    double initial_value;
    double previous_sample;
    double pause_level;

    bool operator==(Trigger const& r) {
        return initial_value == r.initial_value
            && previous_sample == r.previous_sample;
    }
};

struct Scalar {
    uint64_t id;
    std::string name;
    std::string group;
    std::string name_and_group;
    std::string alias;
    std::string alias_and_group;
    ImVec4 color = {-1, -1, -1, -1};
    ValueSource src;
    bool hide_from_scalars_window = false;
    bool deleted = false;
    double scale = 1;
    double offset = 0;

    double getValue() const {
        return getSourceValue(src);
    }

    void setValue(double value) {
        setSourceValue(src, value);
    }

    double getScaledValue() const {
        return getValue() * scale + offset;
    }

    void setScaledValue(double value) {
        setValue((value - offset) / scale);
    }

    std::vector<Trigger> m_pause_triggers;
    void addTrigger(double pause_level);
    bool checkTriggers(double value);
};

struct Vector2D {
    uint64_t id;
    std::string group;
    std::string name;
    std::string name_and_group;
    Scalar* x;
    Scalar* y;
    bool deleted = false;
};

struct ScalarPlot {
    std::string name;
    Focus focus;
    std::vector<Scalar*> signals;
    MinMax y_axis = {-1, 1};
    MinMax x_axis = {0, 1};
    double x_range = 1; // Range is stored separately so that x-axis can be zoomed while paused but original range is restored on continue
    double last_frame_timestamp;
    bool autofit_y = true;
    bool show_tooltip = true;
    bool open = true;

    void addSignalToPlot(Scalar* new_signal) {
        // Add signal if it is not already in the plot
        auto it = std::find_if(signals.begin(), signals.end(), [=](Scalar* sig) {
            return sig->id == new_signal->id;
        });
        if (it == signals.end()) {
            signals.push_back(new_signal);
        }
    }
};

struct VectorPlot {
    std::string name;
    Focus focus;
    std::vector<Vector2D*> signals;
    Vector2D* reference_frame_vector;
    float time_range = 20e-3f;
    bool open = true;

    void addSignalToPlot(Vector2D* new_signal) {
        // Add signal if it is not already in the plot
        auto it = std::find_if(signals.begin(), signals.end(), [=](Vector2D* sig) {
            return sig->id == new_signal->id;
        });
        if (it == signals.end()) {
            signals.push_back(new_signal);
        }
    }
};

struct SpectrumPlot {
    std::string name;
    Focus focus;

    // Source is either scalar or vector
    Scalar* scalar;
    Vector2D* vector;

    double time_range = 1;
    bool open = true;
    bool logarithmic_y_axis = false;
    MinMax y_axis = {-0.1, 1.1};
    MinMax x_axis = {-1000, 1000};

    struct Spectrum {
        std::vector<double> freq;
        std::vector<double> mag;
    };
    Spectrum spectrum;
    std::future<Spectrum> spectrum_calculation;

    enum Window {
        None,
        Hann,
        Hamming,
        FlatTop
    };
    Window window;

    void addSignalToPlot(Vector2D* new_signal) {
        vector = new_signal;
        scalar = nullptr;
    }

    void addSignalToPlot(Scalar* new_signal) {
        scalar = new_signal;
        vector = nullptr;
    }
};

struct CustomWindow {
    std::string name;
    Focus focus;
    std::vector<Scalar*> scalars;
    bool open = true;
};

template <typename T>
struct SignalGroup {
    // For e.g. "abc|xyz" name is only "xyz" and full name is "abc|xyz"
    std::string name;
    std::string full_name;
    std::vector<T*> signals;
    std::map<std::string, SignalGroup<T>> subgroups;
    bool opened_manually = false;
};
