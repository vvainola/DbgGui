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

#include "dbghelp_symbols_lookup.h"
#include "symbols/variant_symbol.h"
#include "scrolling_buffer.h"
#include <mutex>
#include <thread>
#include <map>
#include <complex>
#include <future>
#include <imgui.h>
#include <nlohmann/json.hpp>

struct GLFWwindow;

inline constexpr unsigned MAX_NAME_LENGTH = 255;
inline constexpr int32_t ALL_SAMPLES = 1000'000'000;
inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.7f, 0.7f, 0.7f, 1);

template <typename T>
struct XY {
    T x;
    T y;
};

struct MinMax {
    double min;
    double max;
};

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
    size_t id;
    std::string name;
    std::string group;
    std::string name_and_group;
    std::string alias;
    std::string alias_and_group;
    ImVec4 color = {-1, -1, -1, -1};
    ValueSource src;
    std::unique_ptr<ScrollingBuffer> buffer;
    bool hide_from_scalars_window = false;
    bool deleted = false;
    double scale = 1;
    double offset = 0;

    void startBuffering() {
        if (buffer == nullptr) {
            buffer = std::make_unique<ScrollingBuffer>(int32_t(1e6));
        }
    }

    double getValue() {
        return getSourceValue(src);
    }

    void setValue(double value) {
        setSourceValue(src, value);
    }

    double getScaledValue() {
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
    size_t id;
    std::string group;
    std::string name;
    std::string name_and_group;
    Scalar* x;
    Scalar* y;
    bool hide_from_vector_window = false;
};

struct ScalarPlot {
    std::string name;
    std::vector<Scalar*> signals;
    MinMax y_axis = {-1, 1};
    MinMax x_axis = {0, 1};
    double x_range = 1; // Range is stored separately so that x-axis can be zoomed while paused but original range is restored on continue
    double last_frame_timestamp;
    bool autofit_y = true;
    bool show_tooltip = true;
    bool open = true;

    void addSignalToPlot(Scalar* new_signal) {
        new_signal->startBuffering();

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
    std::vector<Vector2D*> signals;
    Vector2D* reference_frame_vector;
    float time_range = 20e-3f;
    bool open = true;

    void addSignalToPlot(Vector2D* new_signal) {
        new_signal->x->startBuffering();
        new_signal->y->startBuffering();
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

    // Source is either scalar or vector
    Scalar* scalar;
    Vector2D* vector;

    double time_range = 1;
    bool open = true;
    bool logarithmic_y_axis = false;
    double y_axis_min = -0.1;
    double y_axis_max = 1.1;
    double x_axis_min = -1000;
    double x_axis_max = 1000;

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
        new_signal->x->startBuffering();
        new_signal->y->startBuffering();
        vector = new_signal;
        scalar = nullptr;
    }

    void addSignalToPlot(Scalar* new_signal) {
        new_signal->startBuffering();
        scalar = new_signal;
        vector = nullptr;
    }
};

struct CustomWindow {
    std::string name;
    std::vector<Scalar*> scalars;
    bool open = true;
};

class DbgGui {
  public:
    DbgGui(double sampling_time);
    ~DbgGui();
    void startUpdateLoop();

    void sample();
    void sampleWithTimestamp(double timestamp);

    bool isClosed();
    void close();

    Scalar* addScalar(ValueSource const& src, std::string group, std::string const& name);
    Vector2D* addVector(ValueSource const& x, ValueSource const& y, std::string group, std::string const& name);

  private:
    void updateLoop();
    void showConfigurationWindow();
    void showScalarWindow();
    void showSymbolsWindow();
    void showVectorWindow();
    void showCustomWindow();
    void addCustomWindowDragAndDrop(CustomWindow& custom_window);
    void showScalarPlots();
    void showVectorPlots();
    void showSpectrumPlots();
    void loadPreviousSessionSettings();
    void updateSavedSettings();
    void synchronizeSpeed();

    Scalar* addScalarSymbol(VariantSymbol* scalar, std::string const& group);
    Vector2D* addVectorSymbol(VariantSymbol* x, VariantSymbol* y, std::string const& group);

    GLFWwindow* m_window = nullptr;

    DbgHelpSymbols m_dbghelp_symbols;
    std::vector<VariantSymbol*> m_symbol_search_results;
    char m_group_to_add_symbols[MAX_NAME_LENGTH]{"dbg"};

    Scalar* getScalar(size_t id) {
        for (auto& scalar : m_scalars) {
            if (scalar->id == id) {
                return scalar.get();
            }
        }
        return nullptr;
    }
    std::vector<std::unique_ptr<Scalar>> m_scalars;
    std::map<std::string, std::vector<Scalar*>> m_scalar_groups;
    MinMax m_linked_scalar_x_axis_limits = {0, 1};
    double m_linked_scalar_x_axis_range = 1;
    

    Vector2D* getVector(size_t id) {
        for (auto& vector : m_vectors) {
            if (vector->id == id) {
                return vector.get();
            }
        }
        return nullptr;
    }
    std::vector<std::unique_ptr<Vector2D>> m_vectors;
    std::map<std::string, std::vector<Vector2D*>> m_vector_groups;

    std::vector<CustomWindow> m_custom_windows;
    std::vector<ScalarPlot> m_scalar_plots;
    std::vector<VectorPlot> m_vector_plots;
    std::vector<SpectrumPlot> m_spectrum_plots;

    double m_sampling_time;
    double m_timestamp = 0;
    double m_next_sync_timestamp = 0;

    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_paused = true;
    float m_simulation_speed = 1;
    double m_time_until_pause = 0;
    
    struct OptionalSettings {
        bool x_tick_labels = true;
        bool pause_on_close = false;
        bool link_scalar_x_axis = false;
    } m_options;

    std::jthread m_gui_thread;
    std::mutex m_sampling_mutex;

    nlohmann::json m_settings;
};

template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}
