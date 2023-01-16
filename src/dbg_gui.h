#pragma once

#include "symbols/dbghelp_symbols_lookup.h"
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
inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.7f, 0.7f, 0.7f, 1);

template <typename T>
struct XY {
    T x;
    T y;
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
    double y_axis_min;
    double y_axis_max;
    double x_axis_min;
    double x_axis_max;
    double x_range;
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

    bool isClosed();
    void close();

    size_t addScalar(ValueSource const& src, std::string const& group, std::string const& name);
    size_t addVector(ValueSource const& x, ValueSource const& y, std::string const& group, std::string const& name);

  private:
    void updateLoop();
    void showConfigurationWindow();
    void showScalarWindow();
    void showSymbolsWindow();
    void showVectorWindow();
    void showCustomWindow();
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

    std::map<size_t, std::unique_ptr<Scalar>> m_scalars;
    std::map<std::string, std::vector<Scalar*>> m_scalar_groups;

    std::map<size_t, std::unique_ptr<Vector2D>> m_vectors;
    std::map<std::string, std::vector<Vector2D*>> m_vector_groups;

    std::vector<CustomWindow> m_custom_windows;
    std::vector<ScalarPlot> m_scalar_plots;
    std::vector<VectorPlot> m_vector_plots;
    std::vector<SpectrumPlot> m_spectrum_plots;

    double m_sampling_time;
    double m_timestamp = 0;
    double m_next_sync_timestamp = 0;

    bool m_sample_all = false;
    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_paused = true;
    float m_simulation_speed = 1;
    double m_time_until_pause = 0;

    std::jthread m_gui_thread;
    std::mutex m_sampling_mutex;

    nlohmann::json m_settings;
};

template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}
