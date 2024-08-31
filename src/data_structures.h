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
#include "symbols/arithmetic_symbol.h"
#include "imgui.h"
#include "spectrum.h"

#include <numeric>
#include <vector>
#include <cmath>
#include <future>
#include <string>
#include <format>
#include <map>

inline constexpr unsigned MAX_NAME_LENGTH = 255;

uint64_t hash(const std::string& str);
uint64_t hashWithTime(const std::string& str);

template <typename T>
T inline min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
T inline max(T a, T b) {
    return a > b ? a : b;
}

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
    Scalar* replacement = nullptr;
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
};

class PauseTrigger {
  public:
    PauseTrigger(Scalar const* src, double pause_level)
        : src(src) {
        double current_value = src->getScaledValue();
        m_initial_value = current_value,
        m_previous_sample = current_value,
        m_pause_level = pause_level;
    }

    bool check() {
        double current_value = src->getScaledValue();
        // Pause when value changes if trigger value is same as initial value
        bool zero_crossed = (current_value - m_pause_level) * (m_previous_sample - m_pause_level) <= 0;
        if (current_value != m_initial_value && zero_crossed) {
            return true;
        }
        m_previous_sample = current_value;
        return false;
    }

    bool operator==(PauseTrigger const& r) {
        return m_initial_value == r.m_initial_value
            && m_previous_sample == r.m_previous_sample;
    }

  private:
    Scalar const* src;
    double m_initial_value;
    double m_previous_sample;
    double m_pause_level;
};

struct Vector2D {
    uint64_t id;
    std::string group;
    std::string name;
    std::string name_and_group;
    Scalar* x;
    Scalar* y;
    bool deleted = false;
    Vector2D* replacement = nullptr;
};

struct Window {
    std::string name;
    uint64_t id;
    Focus focus;
    bool open = true;

    std::string title() const {
        return std::format("{}###{}", name, id);
    }

    bool operator==(Window const& other) {
        return id == other.id;
    }

    void closeOnMiddleClick() {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            open = false;
        }
    }

    void contextMenu() {
        if (ImGui::BeginPopupContextItem((title() + "_context_menu").c_str())) {
            name.reserve(MAX_NAME_LENGTH);
            if (ImGui::InputText("Name##window_context_menu",
                                 name.data(),
                                 MAX_NAME_LENGTH)) {
                name = std::string(name.data());
            }
            ImGui::EndPopup();
        }
    }
};

struct ScalarPlot : Window {
    std::vector<Scalar*> signals;
    MinMax y_axis = {-1, 1};
    MinMax x_axis = {0, 1};
    double x_range = 1; // Range is stored separately so that x-axis can be zoomed while paused but original range is restored on continue
    double last_frame_timestamp;
    bool autofit_y = true;

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

struct VectorPlot : Window {
    std::vector<Vector2D*> signals;
    Vector2D* reference_frame_vector;
    float time_range = 20e-3f;

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

struct SpectrumPlot : Window {
    // Source is either scalar or vector
    Scalar* scalar;
    Vector2D* vector;

    double time_range = 1;
    bool logarithmic_y_axis = false;
    MinMax y_axis = {-0.1, 1.1};
    MinMax x_axis = {-1000, 1000};

    Spectrum spectrum;
    SpectrumWindow window;
    std::future<Spectrum> spectrum_calculation;

    void addSignalToPlot(Vector2D* new_signal) {
        vector = new_signal;
        scalar = nullptr;
    }

    void addSignalToPlot(Scalar* new_signal) {
        scalar = new_signal;
        vector = nullptr;
    }
};

struct CustomWindow : Window {
    std::vector<Scalar*> scalars;
};

struct DockSpace : Window {
    unsigned int dock_id = 0;
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

enum FontSelection {
    COUSINE_REGULAR,
    CALIBRI
};

struct ScriptWindow : Window {
  public:
    ScriptWindow() {
        text[0] = '\0';
    }
    ScriptWindow(std::string const& name_, uint64_t id_) {
        text[0] = '\0';
        name = name_;
        id = id_;
    }

    char text[1024 * 16];
    bool loop = false;

    std::string startScript(double current_time, std::vector<std::unique_ptr<Scalar>> const& scalars);
    void processScript(double timestamp);
    void stopScript();
    bool running();
    double getTime(double timestamp);

  private:
    struct Operation {
        double time;
        Scalar* scalar = nullptr;
        double value;
    };
    std::vector<Operation> m_operations;
    int m_idx = -1;
    double m_start_time;
};
