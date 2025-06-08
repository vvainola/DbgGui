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
#include "imgui.h"
#include "nlohmann/json.hpp"
#include "themes.h"
#include "str_helpers.h"

#include <mutex>
#include <thread>
#include <unordered_map>
#include <set>
#include <deque>

struct GLFWwindow;

inline constexpr int32_t ALL_SAMPLES = 1000'000'000;
inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.6f, 0.6f, 0.6f, 1);
inline constexpr ImVec4 COLOR_TEAL = ImVec4(0.0f, 1.0f, 1.0f, 1);
inline constexpr ImVec4 COLOR_WHITE = ImVec4(1, 1, 1, 1);
inline constexpr int32_t MIN_FONT_SIZE = 1;
inline constexpr int32_t MAX_FONT_SIZE = 100;

namespace str {
inline constexpr const char* ADD_SCALAR_PLOT = "Add scalar plot";
inline constexpr const char* ADD_VECTOR_PLOT = "Add vector plot";
inline constexpr const char* ADD_SPECTRUM_PLOT = "Add spectrum plot";
inline constexpr const char* ADD_CUSTOM_WINDOW = "Add custom window";
inline constexpr const char* ADD_SCRIPT_WINDOW = "Add script window";
inline constexpr const char* ADD_GRID_WINDOW = "Add grid window";
inline constexpr const char* ADD_DOCKSPACE = "Add dockspace";
inline constexpr const char* PAUSE_AFTER = "Pause after";
inline constexpr const char* PAUSE_AT = "Pause at";
} // namespace str

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
    void logMessage(const char* msg);

  private:
    void updateLoop();
    void showDockSpaces();
    void showErrorModal();
    void showMainMenuBar();
    void showLogWindow();
    void showScalarWindow();
    void showSymbolsWindow();
    void showVectorWindow();
    void showCustomWindow();
    void showScriptWindow();
    void showGridWindow();
    void addCustomWindowDragAndDrop(CustomWindow& custom_window);
    void showScalarPlots();
    void showVectorPlots();
    void showSpectrumPlots();
    void showCustomSignalCreator();
    void loadPreviousSessionSettings();
    void updateSavedSettings();
    void setInitialFocus();
    void synchronizeSpeed();
    void saveScalarsAsCsv(std::string filename, std::vector<Scalar*> const& scalars, MinMax time_limits);
    void addScalarContextMenu(Scalar* scalar);
    void addScalarScaleInput(Scalar* scalar);
    void addScalarOffsetInput(Scalar* scalar);
    void addSymbolContextMenu(VariantSymbol& sym);
    void restoreScalarSettings(Scalar* scalar);
    void addPopupModal(std::string const& modal_name);
    void addGridWindowDragAndDrop(GridWindow& grid_window, int row, int col);
    void saveSnapshot();
    void loadSnapshot();

    Scalar* addScalarSymbol(VariantSymbol* scalar, std::string const& group);
    Vector2D* addVectorSymbol(VariantSymbol* x, VariantSymbol* y, std::string const& group);

    Scalar* getScalar(uint64_t id) {
        for (auto& scalar : m_scalars) {
            if (scalar->id == id) {
                return scalar.get();
            }
        }
        return nullptr;
    }

    Vector2D* getVector(uint64_t id) {
        for (auto& vector : m_vectors) {
            if (vector->id == id) {
                return vector.get();
            }
        }
        return nullptr;
    }

    DbgHelpSymbols const& m_dbghelp_symbols;
    std::vector<VariantSymbol*> m_symbol_search_results;
    char m_group_to_add_symbols[MAX_NAME_LENGTH]{"dbg"};
    std::set<std::string> m_hidden_symbols;
    std::vector<SymbolValue> m_saved_snapshot;
    std::vector<VariantSymbol*> m_selected_symbols;
    bool m_show_custom_signal_creator = false;

    ScrollingBuffer m_sampler{int(1e6)};
    std::vector<std::unique_ptr<Scalar>> m_scalars;
    std::map<std::string, SignalGroup<Scalar>> m_scalar_groups;
    std::vector<std::unique_ptr<Vector2D>> m_vectors;
    std::map<std::string, SignalGroup<Vector2D>> m_vector_groups;
    MinMax m_linked_scalar_x_axis_limits = {0, 1};

    GLFWwindow* m_window = nullptr;
    std::vector<CustomWindow> m_custom_windows;
    std::vector<GridWindow> m_grid_windows;
    std::vector<ScriptWindow> m_script_windows;
    std::vector<ScalarPlot> m_scalar_plots;
    std::vector<VectorPlot> m_vector_plots;
    std::vector<SpectrumPlot> m_spectrum_plots;
    std::vector<DockSpace> m_dockspaces;
    std::vector<PauseTrigger> m_pause_triggers;
    struct {
        Focus scalars;
        Focus vectors;
        Focus symbols;
        Focus log;
    } m_window_focus;

    double m_sampling_time;
    double m_plot_timestamp = 0;
    double m_sample_timestamp = 0;
    std::atomic<double> m_next_sync_timestamp = 0;

    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_paused = true;
    std::atomic<bool> m_closing = false;
    bool m_initial_focus_set = false;
    float m_simulation_speed = 1;
    double m_pause_at_time = 0;

    std::deque<std::string> m_message_queue;
    std::string m_all_messages;
    std::string m_error_message;
    std::string m_info_message;

    struct OptionalSettings {
        bool pause_on_close = false;
        bool link_scalar_x_axis = false;
        bool scalar_plot_tooltip = true;
        bool show_latest_message_on_main_menu_bar = true;
        Theme theme = Theme::DefaultDark;
        int sampling_buffer_size = (int)1e6;
        int font_size = 13;
        double m_linked_scalar_x_axis_range = 1;

        nlohmann::json toJson() {
            nlohmann::json j;
            j["pause_on_close"] = pause_on_close;
            j["link_scalar_x_axis"] = link_scalar_x_axis;
            j["scalar_plot_tooltip"] = scalar_plot_tooltip;
            j["show_latest_message_on_main_menu_bar"] = show_latest_message_on_main_menu_bar;
            j["theme"] = theme;
            j["sampling_buffer_size"] = sampling_buffer_size;
            j["font_size"] = font_size;
            j["linked_scalar_x_axis_range"] = m_linked_scalar_x_axis_range;
            return j;
        }

        void fromJson(nlohmann::json const& j) {
            pause_on_close = j.value("pause_on_close", pause_on_close);
            link_scalar_x_axis = j.value("link_scalar_x_axis", link_scalar_x_axis);
            scalar_plot_tooltip = j.value("scalar_plot_tooltip", scalar_plot_tooltip);
            show_latest_message_on_main_menu_bar = j.value("show_latest_message_on_main_menu_bar", show_latest_message_on_main_menu_bar);
            theme = j.value("theme", theme);
            sampling_buffer_size = j.value("sampling_buffer_size", sampling_buffer_size);
            font_size = j.value("font_size", font_size);
            m_linked_scalar_x_axis_range = j.value("linked_scalar_x_axis_range", m_linked_scalar_x_axis_range);
        }
    } m_options;

    std::jthread m_gui_thread;
    std::mutex m_sampling_mutex;

    nlohmann::json m_settings;
    nlohmann::json m_settings_saved;
    std::filesystem::file_time_type m_last_settings_write_time;
    bool m_clear_saved_settings = false;

    friend struct ScriptWindow;
};

template <typename T>
inline std::string numberAsStr(T number) {
    return std::format("{:g}", double(number));
}

std::string getSourceValueStr(ValueSource src);
void HelpMarker(const char* desc);
