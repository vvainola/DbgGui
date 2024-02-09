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
#include "symbols/variant_symbol.h"
#include "scrolling_buffer.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <thread>
#include <unordered_map>
#include <set>
#include <deque>

struct GLFWwindow;

inline constexpr int32_t ALL_SAMPLES = 1000'000'000;
inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.7f, 0.7f, 0.7f, 1);
inline constexpr ImVec4 COLOR_TEAL = ImVec4(0.0f, 1.0f, 1.0f, 1);
inline constexpr ImVec4 COLOR_WHITE = ImVec4(1, 1, 1, 1);

class DbgGui {
  public:
    DbgGui(double sampling_time);
    ~DbgGui();
    void startUpdateLoop();

    void sample();
    void sampleWithTimestamp(double timestamp);

    bool isClosed();
    void close();
    void pause();

    Scalar* addSymbol(std::string const& symbol_name,
                      std::string group,
                      std::string const& alias,
                      double scale = 1.0,
                      double offset = 0.0);
    Scalar* addScalar(ValueSource const& src,
                      std::string group,
                      std::string const& name,
                      double scale = 1.0,
                      double offset = 0.0);
    Vector2D* addVector(ValueSource const& x,
                        ValueSource const& y,
                        std::string group,
                        std::string const& name_x,
                        std::string const& name_y,
                        double scale = 1.0,
                        double offset = 0.0);
    void displayMessage(const char* msg);

  private:
    void updateLoop();
    void showDockSpaces();
    void showMainMenuBar();
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
    void setInitialFocus();
    void synchronizeSpeed();
    void saveSignalsAsCsv(std::vector<Scalar*> const& signals, MinMax time_limits);
    void addScalarContextMenu(Scalar* scalar);
    void addSymbolContextMenu(VariantSymbol const& sym);
    void restoreScalarSettings(Scalar* scalar);

    Scalar* addScalarSymbol(VariantSymbol* scalar, std::string const& group);
    Vector2D* addVectorSymbol(VariantSymbol* x, VariantSymbol* y, std::string const& group);

    GLFWwindow* m_window = nullptr;

    DbgHelpSymbols const& m_dbghelp_symbols;
    std::vector<VariantSymbol*> m_symbol_search_results;
    char m_group_to_add_symbols[MAX_NAME_LENGTH]{"dbg"};
    std::set<std::string> m_hidden_symbols;

    Scalar* getScalar(uint64_t id) {
        for (auto& scalar : m_scalars) {
            if (scalar->id == id) {
                return scalar.get();
            }
        }
        return nullptr;
    }
    ScrollingBuffer m_sampler{0};
    std::vector<std::unique_ptr<Scalar>> m_scalars;
    std::map<std::string, SignalGroup<Scalar>> m_scalar_groups;
    std::vector<std::unique_ptr<Vector2D>> m_vectors;
    std::map<std::string, SignalGroup<Vector2D>> m_vector_groups;
    MinMax m_linked_scalar_x_axis_limits = {0, 1};
    double m_linked_scalar_x_axis_range = 1;

    Vector2D* getVector(uint64_t id) {
        for (auto& vector : m_vectors) {
            if (vector->id == id) {
                return vector.get();
            }
        }
        return nullptr;
    }

    std::vector<CustomWindow> m_custom_windows;
    std::vector<ScalarPlot> m_scalar_plots;
    std::vector<VectorPlot> m_vector_plots;
    std::vector<SpectrumPlot> m_spectrum_plots;
    std::vector<DockSpace> m_dockspaces;
    std::vector<PauseTrigger> m_pause_triggers;
    Focus m_vector_window_focus;
    Focus m_scalar_window_focus;

    double m_sampling_time;
    double m_plot_timestamp = 0;
    double m_sample_timestamp = 0;
    double m_next_sync_timestamp = 0;

    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_paused = true;
    bool m_initial_focus_set = false;
    float m_simulation_speed = 1;
    double m_pause_at_time = 0;
    std::deque<std::string> m_messages;

    struct OptionalSettings {
        bool x_tick_labels = true;
        bool pause_on_close = false;
        bool link_scalar_x_axis = false;
        bool scalar_plot_tooltip = true;
        bool clear_saved_settings = false;
        FontSelection font_selection = COUSINE_REGULAR;
        int sampling_buffer_size = (int)1e6;
    } m_options;

    std::jthread m_gui_thread;
    std::mutex m_sampling_mutex;

    nlohmann::json m_settings;
    nlohmann::json m_settings_saved;
};

template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

template <typename T>
inline bool contains(std::vector<T>& v, const T& item_to_search) {
    for (auto const& item : v) {
        if (item == item_to_search) {
            return true;
        }
    }
    return false;
}
