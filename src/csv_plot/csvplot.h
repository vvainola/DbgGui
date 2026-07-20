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

#include "plot_base.h"
#include "themes.h"
#include "imgui.h"
#include "imgui_helpers.h"
#include <expected>
#include <string>
#include <filesystem>
#include <map>
#include <array>
#include <span>
#include <vector>
#include <optional>
#include "csv_helpers.h"

inline constexpr int NOT_VISIBLE = -1;
inline constexpr ImVec4 NO_COLOR = {-1, -1, -1, -1};
inline constexpr MinMax AUTOFIT_AXIS{-1, 1};
inline constexpr int MAX_PLOTS = 10;
inline constexpr int MAX_PLOT_WINDOWS = MAX_PLOTS * MAX_PLOTS;
inline constexpr int MAX_UNDOCKED_PLOTS = 10;
inline constexpr int MAX_RECENT_CUSTOM_EQUATIONS = 10;

struct CsvSignal;
struct GLFWwindow;

struct CsvFileData {
    std::string name;
    std::string displayed_name;
    std::vector<CsvSignal> signals;
    std::filesystem::file_time_type write_time;
    double x_axis_shift = 0;
    int run_number = 0;
    bool enabled = true;
    bool in_memory = false;

    bool operator==(CsvFileData const& other) {
        return name == other.name
            && displayed_name == other.displayed_name
            && write_time == other.write_time
            && run_number == other.run_number;
    }
};

struct CsvSignalTransform {
    std::string scale_expression = "1";
    double scale = 1;
    std::string offset_expression = "0";
    double offset = 0;

    bool isDefault() const {
        return scale == 1 && offset == 0;
    }
};

struct CsvSignal {
    std::string name;
    std::vector<double> samples;
    CsvFileData* file;
    CsvSignalTransform transform;
};

struct RecentCustomEquation {
    std::string name;
    std::string equation;
};

class CsvPlotter {
  public:
    CsvPlotter(std::vector<std::string> files = {},
               std::map<std::string, int> name_and_plot_idx = {},
               MinMax const& xlimits = AUTOFIT_AXIS,
               int rows = 0,
               int cols = 0,
               std::string const& image_filepath = "");

  private:
    void showErrorModal();
    void showCommandPalette();
    std::vector<CommandPaletteCommand> commandPaletteCommands(bool enable_hotkeys = true);
    void showCustomSignalCreator();
    void showSignalWindow();
    void showPlots();
    void showScalarPlot(PlotBase& plot_base, int visible_plot_idx, double& vertical_line_time, double& vertical_line_time_next);
    void showVectorPlot(PlotBase& plot_base, int visible_plot_idx);
    void showSpectrumPlot(PlotBase& plot_base, int visible_plot_idx);
    void showXSignalCombo();
    bool showPlotContextMenu(PlotBase& plot);

    std::vector<double> getVisibleSamples(CsvSignal const& signal);
    std::span<double const> getXSignalSamples(CsvFileData const& file);
    double getScalarPlotXOrigin(ScalarPlot const& plot);
    void applySignalTransforms(CsvFileData& file);
    void applyPlottedSignals(CsvFileData& file);
    void updatePlottedSignalSettings();
    void updateComparisonMode();
    void snapshotComparisonPlotSettings();
    void showComparisonFile(CsvFileData& file);
    int dockedPlotCount() const;
    int activePlotCount() const;
    PlotBase& plotAt(int visible_plot_idx);
    PlotBase const& plotAt(int visible_plot_idx) const;
    std::string plotWindowName(int visible_plot_idx) const;
    template <typename Fn>
    void forEachPlot(Fn&& fn);
    bool isSignalPlotted(CsvSignal* signal) const;
    void removeSignalFromAllPlots(CsvSignal* signal);
    void replaceReloadedFileSignals(CsvFileData& old_file, CsvFileData& new_file);
    void setSignalTransform(std::string const& signal_name, CsvSignalTransform const& transform);
    std::expected<void, std::string> setSignalScale(std::vector<CsvSignal*> const& signals, std::string const& scale_expression);
    std::expected<void, std::string> setSignalOffset(std::vector<CsvSignal*> const& signals, std::string const& offset_expression);
    void setSignalPlotStyle(std::vector<CsvSignal*> const& signals, std::optional<CsvPlotStyle> plot_style);
    CsvPlotStyle getSignalPlotStyle(CsvSignal const& signal) const;
    void showSignalPlotStyleCombo(CsvSignal const& signal, std::vector<CsvSignal*> const& signals_to_update);
    std::vector<CsvSignal*> sameNamedSignalsFromOpenFiles(std::vector<CsvSignal*> const& signals);

    void updateSavedSettings(bool force = false);
    void loadPreviousSessionSettings();
    void saveSettings();
    void loadSettings();
    void removeFileFromPlots(CsvFileData& file);
    void removeAllFiles();
    void openFilesFromDialog();
    void clearPlots();
    void copyPlottedSignalArgumentsToClipboard();
    void addClipboardFileFromClipboard();
    GLFWwindow* m_window;

    std::vector<std::unique_ptr<CsvFileData>> m_csv_data;
    std::map<std::string, CsvSignalTransform> m_signal_transform_settings;
    std::map<std::string, CsvPlotStyle> m_signal_plot_style_settings;
    CommandHotkeyOverrides m_hotkey_overrides;
    std::vector<RecentCustomEquation> m_recent_custom_equations;
    // When signals are given on the command line they override the persisted selection at
    // startup. Only normal GUI sessions restore saved plotted signal assignments.
    bool m_use_saved_plotted_signals = true;
    bool m_save_settings = true;
    int m_rows = 1;
    int m_cols = 1;
    // Docked plots are stored in stable row/column slots: row * MAX_PLOTS + col.
    // plotAt() still receives a visible row-major index from render loops, but maps
    // it through the current grid dimensions so adding columns does not move lower rows.
    int m_undocked_plot_count = 0;
    float m_signals_window_width = 0.15f;
    struct {
        std::string x_signal_name;
        bool shift_samples_to_start_from_zero = true;
        bool link_axis = true;
        bool autofit_y_axis = true;
        bool keep_old_signals_on_reload = true;
        bool cursor_measurements = false;
        bool show_vertical_line_in_all_plots = true;
        bool interpolate_tooltip = false;
        CsvPlotStyle plot_style = CsvPlotStyle::Linear;
        Theme theme;
        int font_size = 13;
    } m_options;
    MinMax m_x_axis = AUTOFIT_AXIS;
    MinMax m_x_shift_slider_range = AUTOFIT_AXIS;
    bool m_x_shift_slider_active = false;
    double m_drag_x1 = 0;
    double m_drag_x2 = 0;
    std::string m_error_message;
    int m_clipboard_file_count = 0;
    struct {
        bool reset_colors;
    } m_flags;

    // Comparison selection persists between Alt holds, while the per-plot name
    // snapshots are refreshed for each new hold and never store signal pointers.
    struct {
        bool active = false;
        bool reset_colors = false;
        float alt_hold_duration = 0;
        // Retained after Alt is released so the next comparison starts here.
        int selected_file_index = 0;
        std::array<CsvPlotSignalSettings, MAX_PLOT_WINDOWS> docked_plot_settings;
        std::array<CsvPlotSignalSettings, MAX_UNDOCKED_PLOTS> undocked_plot_settings;
    } m_comparison;

    std::vector<CsvSignal*> m_selected_signals;
    // Flattened list of signal pointers actually submitted this frame (i.e. inside expanded
    // file tree nodes and matching the filter). Built during the submission loop and used to
    // map multi-select SetRange/SetAll indices back to CsvSignal* in applyMultiSelectRequests().
    // Must outlive the BeginMultiSelect/EndMultiSelect scope (i.e. be a member, not a
    // frame-local), because EndMultiSelect's requests are applied after the loop.
    std::vector<CsvSignal*> m_visible_signals;
    std::array<PlotBase, MAX_PLOT_WINDOWS> m_docked_plots;
    std::array<PlotBase, MAX_UNDOCKED_PLOTS> m_undocked_plots;
};
