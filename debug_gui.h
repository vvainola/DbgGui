#pragma once

#include "symbols/dbghelp_symbols_lookup.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <thread>
#include "scrolling_buffer.h"

struct GLFWwindow;

inline constexpr unsigned MAX_NAME_LENGTH = 255;

template <typename T>
struct XY {
    T x;
    T y;
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
    size_t id;
    std::string group;
    std::string name;
    std::string name_and_group;
    std::string group_and_name;
    ValueSource src;
    std::unique_ptr<ScrollingBuffer> buffer;
    bool hide_from_scalars_window = false;
    double scale = 1;
    double offset = 0;

    void startBuffering() {
        if (buffer == nullptr) {
            buffer = std::make_unique<ScrollingBuffer>(size_t(1e6));
        }
    }

    std::vector<Trigger> m_pause_triggers;
    void addTrigger(double pause_level);
    bool checkTriggers(double value);
};

struct Vector {
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
    std::vector<Vector*> signals;
    float time_range = 20e-3f;
    bool open = true;

    void addSignalToPlot(Vector* new_signal) {
        new_signal->x->startBuffering();
        new_signal->y->startBuffering();
        // Add signal if it is not already in the plot
        auto it = std::find_if(signals.begin(), signals.end(), [=](Vector* sig) {
            return sig->id == new_signal->id;
        });
        if (it == signals.end()) {
            signals.push_back(new_signal);
        }
    }
};

class DbgGui {
  public:
    DbgGui();
    ~DbgGui();
    void startUpdateLoop();

    void sample(double timestamp);

    bool isClosed();

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
    void loadPreviousSessionSettings();
    void updateSavedSettings();

    Scalar* addScalarSymbol(VariantSymbol* scalar, std::string const& group);
    Vector* addVectorSymbol(VariantSymbol* x, VariantSymbol* y, std::string const& group);

    GLFWwindow* m_window = nullptr;

    DbgHelpSymbols m_dbghelp_symbols;
    std::vector<VariantSymbol*> m_symbol_search_results;
    char m_group_to_add_symbols[MAX_NAME_LENGTH]{};

    std::map<size_t, std::unique_ptr<Scalar>> m_scalars;
    std::map<std::string, std::vector<Scalar*>> m_scalar_groups;

    std::map<size_t, std::unique_ptr<Vector>> m_vectors;
    std::map<std::string, std::vector<Vector*>> m_vector_groups;
    std::vector<Scalar*> m_custom_window_scalars;

    std::vector<ScalarPlot> m_scalar_plots;
    std::vector<VectorPlot> m_vector_plots;

    double m_timestamp = 0;
    double m_last_sleep_timestamp = 0;

    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_paused = true;
    float m_simulation_speed = 1;

    std::thread m_gui_thread;
    std::mutex m_sampling_mutex;

    nlohmann::json m_saved_settings;
    bool m_manual_save_settings = false;
};


template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

inline double getSourceValue(ValueSource src) {
    return std::visit(
        [=](auto&& src) {
            using T = std::decay_t<decltype(src)>;
            if constexpr (std::is_same_v<T, ReadWriteFn>) {
                return src(std::nullopt);
            } else if constexpr (std::is_same_v<T, ReadWriteFnCustomStr>) {
                return src(std::nullopt).value;
            } else {
                return static_cast<double>(*src);
            }
        },
        src);
}
