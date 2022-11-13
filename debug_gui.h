#pragma once

#include "symbols/dbghelp_symbols_lookup.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <thread>

struct GLFWwindow;

inline constexpr unsigned MAX_NAME_LENGTH = 255;

template <typename T>
struct XY {
    T x;
    T y;
};

// utility structure for realtime plot
struct ScrollingBuffer {
    size_t current_idx = 0;
    size_t buffer_size;

    std::vector<double> time;
    std::vector<double> data;
    struct DecimatedValues {
        std::vector<double> time;
        std::vector<double> y_min;
        std::vector<double> y_max;
    };
    bool full_buffer_looped = false;

    ScrollingBuffer(size_t max_size)
        : buffer_size(max_size),
          time(buffer_size * 2),
          data(buffer_size * 2) {
    }

    void AddPoint(double x, double y) {
        // y += 0.1 * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5);
        time[current_idx] = x;
        time[current_idx + buffer_size] = x;
        data[current_idx] = y;
        data[current_idx + buffer_size] = y;
        current_idx = (current_idx + 1) % buffer_size;
        if (current_idx == 0) {
            full_buffer_looped = true;
        }
    }

    // TODO: check implot implementation
    size_t binarySearch(double t, size_t start, size_t end) {
        size_t mid = std::midpoint(start, end);
        while (start < end) {
            mid = std::midpoint(start, end);
            double val = time[mid];
            if (start == mid) {
                return mid;
            } else if (val < t) {
                start = mid + 1;
            } else if (val > t) {
                end = mid - 1;
            } else {
                return mid;
            }
        }
        return mid;
    }

    DecimatedValues getValuesInRange(double x_min, double x_max, int n_points, double scale = 1, double offset = 0) {
        DecimatedValues decimated_values;
        x_max = std::min(time[current_idx - 1], x_max);
        size_t start_idx = current_idx - 1;
        size_t end_idx = current_idx - 1 + buffer_size;
        if (!full_buffer_looped) {
            // Nothing sampled yet
            if (current_idx == 0) {
                decimated_values.time.push_back(0);
                decimated_values.y_min.push_back(0);
                decimated_values.y_max.push_back(0);
                return decimated_values;
            }
            x_min = std::max(0.0, x_min);
            start_idx = binarySearch(x_min, 0, current_idx - 1);
            end_idx = binarySearch(x_max, start_idx, current_idx - 1);
        } else {
            start_idx = binarySearch(x_min, start_idx, end_idx);
            end_idx = binarySearch(x_max, start_idx, end_idx);
        }
        end_idx = std::max(end_idx, start_idx + 2);

        size_t decimation = static_cast<size_t>(std::max(std::floor(double(end_idx - start_idx) / n_points) - 1, 0.0));

        decimated_values.time.reserve(end_idx - start_idx);
        decimated_values.y_min.reserve(end_idx - start_idx);
        decimated_values.y_max.reserve(end_idx - start_idx);

        double current_min = INFINITY;
        double current_max = -INFINITY;
        int64_t counter = 0;
        for (size_t i = start_idx; i < end_idx; i++) {
            if (counter < 0) {
                decimated_values.time.push_back(time[i]);
                decimated_values.y_min.push_back(scale * current_min + offset);
                decimated_values.y_max.push_back(scale * current_max + offset);

                current_min = INFINITY;
                current_max = -INFINITY;
                counter = decimation;
            }
            current_min = std::min(data[i], current_min);
            current_max = std::max(data[i], current_max);
            counter--;
        }
        // Update leftover so that blanks are not left at the end
        if (counter > 0) {
            decimated_values.time.push_back(time[end_idx]);
            decimated_values.y_min.push_back(scale * current_min + offset);
            decimated_values.y_max.push_back(scale * current_max + offset);
        }
        return decimated_values;
    }
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
