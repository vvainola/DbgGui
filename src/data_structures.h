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

#include "symbols/dbg_symbols.hpp"
#include "symbols/arithmetic_symbol.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "lua_script.h"
#include "minmax.h"
#include "spectrum.h"
#include "str_helpers.h"
#include "nlohmann/json.hpp"

#include <numeric>
#include <optional>
#include <vector>
#include <cmath>
#include <future>
#include <string>
#include <format>
#include <map>
#include <algorithm>

#define TRY(expression)                         \
    try {                                       \
        expression                              \
    } catch (nlohmann::json::exception & err) { \
        std::cerr << err.what() << std::endl;   \
    } catch (std::exception & err) {            \
        std::cerr << err.what() << std::endl;   \
    }

inline constexpr unsigned MAX_NAME_LENGTH = 255;

inline uint64_t hash(const std::string& str) {
    uint64_t hash = 5381;
    for (size_t i = 0; i < str.size(); ++i) {
        hash = 33 * hash + (uint64_t)str[i];
    }
    return hash;
}
uint64_t hashWithTime(const std::string& str);
// Compute the signal id from name and group, matching the formula used by
// addScalar/addVector: hash(name + " (" + group + ")").
inline uint64_t signalId(std::string const& name, std::string const& group) {
    return hash(name + " (" + group + ")");
}
std::string getFilenameToSave(std::string const& filter = "csv", std::string default_path = "");
std::string getFilenameToOpen(std::string const& filter, std::string default_path = "");

template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

template <typename T>
inline bool contains(std::vector<T> const& v, const T& item_to_search) {
    for (auto const& item : v) {
        if (item == item_to_search) {
            return true;
        }
    }
    return false;
}

inline double getSourceValue(ValueSource const& src) {
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

inline void setSourceValue(ValueSource const& dst, double value) {
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
    bool read_only = false;
    bool deleted = false;

    void updateDisplayNames() {
        name_and_group = name + " (" + group + ")";
        alias_and_group = alias + " (" + group + ")";
    }

    void fromJson(nlohmann::json const& j) {
        std::string scale = "1";
        if (j["scale"].is_number()) {
            scale = std::format("{:g}", double(j["scale"]));
        } else {
            scale = j.value("scale", scale);
        }
        setScaleStr(scale);
        std::string offset = "0";
        if (j["offset"].is_number()) {
            offset = std::format("{:g}", double(j["offset"]));
        } else {
            offset = j.value("offset", offset);
        }
        setOffsetStr(offset);
        alias = j.value("alias", alias);
        updateDisplayNames();
    }

    void updateJson(nlohmann::json& j) const {
        j["id"] = id;
        j["scale"] = getScaleStr();
        j["offset"] = getOffsetStr();
        j["alias"] = alias;
    }

    double getValue() const {
        return getSourceValue(src);
    }

    void setValue(double value) {
        setSourceValue(src, value);
    }

    double getScaledValue() const {
        return getValue() * m_scale + m_offset;
    }

    void setScaledValue(double value) {
        setValue((value - m_offset) / m_scale);
    }

    void setScaleStr(std::string const& scale) {
        auto scale_v = str::evaluateExpression(scale);
        if (!scale_v.has_value()) {
            throw std::runtime_error(std::format("Scale expression '{}' could not be evaluated", scale));
        }
        m_scale = scale_v.value();
        m_scale_str = scale;
    }

    double getScale() const {
        return m_scale;
    }

    std::string const& getScaleStr() const {
        return m_scale_str;
    }

    void setOffsetStr(std::string const& offset) {
        auto offset_v = str::evaluateExpression(offset);
        if (!offset_v.has_value()) {
            throw std::runtime_error(std::format("Offset expression '{}' could not be evaluated", offset));
        }
        m_offset = offset_v.value();
        m_offset_str = offset;
    }

    double getOffset() const {
        return m_offset;
    }

    std::string const& getOffsetStr() const {
        return m_offset_str;
    }

    bool customScaleOrOffset() const {
        return m_scale != 1 || m_offset != 0;
    }

  private:
    std::string m_scale_str = "1";
    double m_scale = 1;
    std::string m_offset_str = "0";
    double m_offset = 0;
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

struct VectorPair {
    Scalar* x;
    Scalar* y;

    bool operator==(VectorPair const&) const = default;
};

struct Window {
    Window(std::string const& name_, uint64_t id_)
        : name(name_), id(id_) {
    }
    Window(nlohmann::json const& j) {
        name = j.value("name", name);
        id = j.value("id", id);
        focus.initial_focus = j.value("initial_focus", focus.initial_focus);
    }
    void updateJson(nlohmann::json& j) const {
        j["name"] = name;
        j["id"] = id;
        j["initial_focus"] = focus.focused;
    }

    std::string name = "";
    uint64_t id = 0;
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
            ImGui::InputText("Name##window_context_menu", &name);
            ImGui::EndPopup();
        }
    }
};

enum class PlotType {
    Scalar,
    Vector,
    Spectrum
};

struct PlotWindow : Window {
    struct Subplot {
        PlotType type = PlotType::Scalar;
        std::vector<Scalar*> scalars;
        std::vector<VectorPair> vectors;
        std::vector<Spectrum<Scalar>> spectrums;
        MinMax spectrum_x_axis = {-1000, 1000};
        MinMax spectrum_y_axis = {-0.1, 1.1};
        std::optional<VectorPair> reference_frame_vector;
        MinMax y_axis = {-1, 1};
        bool autofit_y = true;

        void addSpectrum(Scalar* real, Scalar* imag = nullptr) {
            for (auto const& spec : spectrums) {
                if (spec.real == real && spec.imag == imag) {
                    return;
                }
            }
            spectrums.emplace_back(real, imag);
        }

        void removeSpectrum(Scalar* real, Scalar* imag = nullptr) {
            std::erase_if(spectrums, [=](auto const& spec) {
                return spec.real == real && spec.imag == imag;
            });
        }

        void addScalar(Scalar* scalar) {
            if (!contains(scalars, scalar)) {
                scalars.push_back(scalar);
            }
        }

        void removeScalar(Scalar* scalar) {
            remove(scalars, scalar);
        }

        void addVector(VectorPair vector) {
            if (!contains(vectors, vector)) {
                vectors.push_back(vector);
            }
        }

        void removeVector(VectorPair vector) {
            remove(vectors, vector);
            if (reference_frame_vector == vector) {
                reference_frame_vector.reset();
            }
        }
    };

    inline static constexpr int MAX_SUBPLOT_ROWS = 10;
    inline static constexpr int MAX_SUBPLOT_COLS = 10;

    PlotWindow(std::string const& name, uint64_t id)
        : Window(name, id) {
        subplots.resize(subplotCount());
    }
    PlotWindow(nlohmann::json const& j)
        : Window(j) {
        x_range = j.value("x_range", x_range);
        vector_time_range = j.value("vector_time_range", vector_time_range);
        spectrum.time_range = j.value("spectrum_time_range", spectrum.time_range);
        spectrum.window = j.value("spectrum_window", spectrum.window);
        spectrum.logarithmic_y_axis = j.value("spectrum_logarithmic_y_axis", spectrum.logarithmic_y_axis);
        x_axis.min = 0;
        x_axis.max = x_range;
        m_rows = std::clamp(j.value("rows", m_rows), 1, MAX_SUBPLOT_ROWS);
        m_cols = std::clamp(j.value("cols", m_cols), 1, MAX_SUBPLOT_COLS);
        subplots.resize(subplotCount());
        if (j.contains("subplots")) {
            int subplot_idx = 0;
            for (auto const& subplot_data : j["subplots"]) {
                if (subplot_idx >= subplotCount()) {
                    break;
                }
                Subplot& subplot = subplots[subplot_idx];
                subplot.type = subplot_data.value("type", subplot.type);
                subplot.spectrum_x_axis = {subplot_data.value("spectrum_x_min", -1000.0), subplot_data.value("spectrum_x_max", 1000.0)};
                subplot.spectrum_y_axis = {subplot_data.value("spectrum_y_min", -0.1), subplot_data.value("spectrum_y_max", 1.1)};
                subplot.autofit_y = subplot_data.value("autofit_y", subplot.autofit_y);
                if (!subplot.autofit_y) {
                    subplot.y_axis.min = subplot_data.value("y_min", subplot.y_axis.min);
                    subplot.y_axis.max = subplot_data.value("y_max", subplot.y_axis.max);
                }
                ++subplot_idx;
            }
        }
    }
    void updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["x_range"] = x_range;
        j["vector_time_range"] = vector_time_range;
        j["spectrum_time_range"] = spectrum.time_range;
        j["spectrum_window"] = spectrum.window;
        j["spectrum_logarithmic_y_axis"] = spectrum.logarithmic_y_axis;
        j["rows"] = m_rows;
        j["cols"] = m_cols;
        // Preserve signal placement until updateSavedSettings() reconciles it
        // after deleted and late-added scalars have been resolved.
        if (!j["subplots"].is_array()) {
            j["subplots"] = nlohmann::json::array();
        }
        while (j["subplots"].size() < subplots.size()) {
            j["subplots"].push_back(nlohmann::json::object());
        }
        while (j["subplots"].size() > subplots.size()) {
            j["subplots"].erase(j["subplots"].end() - 1);
        }
        for (size_t subplot_idx = 0; subplot_idx < subplots.size(); ++subplot_idx) {
            Subplot const& subplot = subplots[subplot_idx];
            nlohmann::json& subplot_data = j["subplots"][subplot_idx];
            subplot_data["type"] = subplot.type;
            subplot_data["spectrum_x_min"] = subplot.spectrum_x_axis.min;
            subplot_data["spectrum_x_max"] = subplot.spectrum_x_axis.max;
            subplot_data["spectrum_y_min"] = subplot.spectrum_y_axis.min;
            subplot_data["spectrum_y_max"] = subplot.spectrum_y_axis.max;
            subplot_data["autofit_y"] = subplot.autofit_y;
            if (!subplot.autofit_y) {
                subplot_data["y_min"] = subplot.y_axis.min;
                subplot_data["y_max"] = subplot.y_axis.max;
            } else {
                subplot_data.erase("y_min");
                subplot_data.erase("y_max");
            }
        }
    }

    std::vector<Subplot> subplots;
    MinMax x_axis = {0, 1};
    float vector_time_range = 20e-3f;
    struct {
        double time_range = 1;
        SpectrumWindow window = SpectrumWindow::None;
        bool logarithmic_y_axis = false;
    } spectrum;
    double x_range = 1; // Range is stored separately so that x-axis can be zoomed while paused but original range is restored on continue
    double last_frame_timestamp = 0;

    int rows() const {
        return m_rows;
    }

    int cols() const {
        return m_cols;
    }

    int subplotCount() const {
        return m_rows * m_cols;
    }

    void setSubplotGrid(int new_rows, int new_cols) {
        new_rows = std::clamp(new_rows, 1, MAX_SUBPLOT_ROWS);
        new_cols = std::clamp(new_cols, 1, MAX_SUBPLOT_COLS);
        int new_count = new_rows * new_cols;
        std::vector<Subplot> old_subplots = std::move(subplots);
        m_rows = new_rows;
        m_cols = new_cols;
        subplots.resize(new_count);
        int copied_count = std::min<int>(static_cast<int>(old_subplots.size()), static_cast<int>(subplots.size()));
        for (int i = 0; i < copied_count; ++i) {
            subplots[i] = std::move(old_subplots[i]);
        }
        // When the grid shrinks, keep signals from removed cells instead of
        // silently dropping them.
        for (int i = new_count; i < static_cast<int>(old_subplots.size()); ++i) {
            for (Scalar* scalar : old_subplots[i].scalars) {
                addScalarToPlot(scalar, 0);
            }
            for (VectorPair vector : old_subplots[i].vectors) {
                subplots[0].addVector(vector);
            }
            for (auto const& spec : old_subplots[i].spectrums) {
                subplots[0].addSpectrum(spec.real, spec.imag);
            }
        }
    }

    void addScalarToPlot(Scalar* new_scalar, int subplot_idx = 0) {
        subplot_idx = std::clamp(subplot_idx, 0, subplotCount() - 1);
        subplots[subplot_idx].addScalar(new_scalar);
    }

    std::vector<Scalar*> allScalars() const {
        std::vector<Scalar*> all_scalars;
        for (Subplot const& subplot : subplots) {
            for (Scalar* scalar : subplot.scalars) {
                if (!contains(all_scalars, scalar)) {
                    all_scalars.push_back(scalar);
                }
            }
        }
        return all_scalars;
    }

    void clearSignals() {
        for (Subplot& subplot : subplots) {
            subplot.scalars.clear();
            subplot.vectors.clear();
            subplot.spectrums.clear();
            subplot.reference_frame_vector.reset();
        }
    }

    void contextMenu() {
        if (ImGui::BeginPopupContextItem((title() + "_context_menu").c_str())) {
            ImGui::InputText("Name##window_context_menu", &name);
            int new_rows = m_rows;
            int new_cols = m_cols;
            if (ImGui::InputInt("Rows", &new_rows)) {
                setSubplotGrid(new_rows, m_cols);
            }
            if (ImGui::InputInt("Columns", &new_cols)) {
                setSubplotGrid(m_rows, new_cols);
            }
            ImGui::EndPopup();
        }
    }

  private:
    int m_rows = 1;
    int m_cols = 1;
};

struct CustomWindow : Window {
    CustomWindow(std::string const& name, uint64_t id)
        : Window(name, id) {
    }
    CustomWindow(nlohmann::json const& j)
        : Window(j) {
    }

    std::vector<Scalar*> scalars;

    void addScalar(Scalar* scalar) {
        if (!contains(scalars, scalar)) {
            scalars.push_back(scalar);
            sortSignals();
        }
    }

    void sortSignals() {
        std::sort(scalars.begin(), scalars.end(), [](Scalar* l, Scalar* r) {
            if (l->group == r->group) {
                return l->name < r->name;
            } else {
                return l->group < r->group;
            }
        });
    }
};

struct GridWindow : Window {
    GridWindow(std::string const& name, uint64_t id)
        : Window(name, id) {
    }
    GridWindow(nlohmann::json const& j)
        : Window(j) {
        rows = j.value("rows", rows);
        columns = j.value("columns", columns);
        text_to_value_ratio = j.value("text_to_value_ratio", text_to_value_ratio);
    }
    void updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["rows"] = rows;
        j["columns"] = columns;
        j["text_to_value_ratio"] = text_to_value_ratio;
    }

    inline static const int MAX_ROWS = 20;
    inline static const int MAX_COLUMNS = 10;
    std::array<std::array<uint64_t, MAX_COLUMNS>, MAX_ROWS> scalars = {};
    int rows = 1;
    int columns = 1;
    float text_to_value_ratio = 0.3f;

    using Cell = std::pair<int, int>;

    void focusCell(Cell cell) {
        m_focus_cell = cell;
    }

    bool isCellFocused(Cell cell) {
        if (m_focus_cell) {
            bool is_focused = m_focus_cell->first == cell.first && m_focus_cell->second == cell.second;
            if (is_focused) {
                m_focus_cell = std::nullopt;
                return true;
            }
        }
        return false;
    }

    void contextMenu() {
        rows = std::clamp(rows, 1, MAX_ROWS);
        columns = std::clamp(columns, 1, MAX_COLUMNS);
        if (ImGui::BeginPopupContextItem((title() + "_context_menu").c_str())) {
            ImGui::InputText("Name##window_context_menu", &name);
            ImGui::InputInt("Rows", &rows, 0, 0);
            ImGui::InputInt("Columms", &columns, 0, 0);
            ImGui::InputFloat("Text to value ratio", &text_to_value_ratio, 0, 0, "%.2f");
            ImGui::EndPopup();
        }
    }

  private:
    std::optional<Cell> m_focus_cell = std::nullopt;
};

struct DockSpace : Window {
    DockSpace(std::string const& name, uint64_t id)
        : Window(name, id) {
    }
    DockSpace(nlohmann::json const& j)
        : Window(j) {
        even_split = j.value("even_split", even_split);
    }
    void updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["even_split"] = even_split;
    }

    void contextMenu() {
        if (ImGui::BeginPopupContextItem((title() + "_context_menu").c_str())) {
            ImGui::InputText("Name##window_context_menu", &name);
            ImGui::Checkbox("Even split", &even_split);
            ImGui::EndPopup();
        }
    }

    unsigned int dock_id = 0;
    bool even_split = false;
};

template <typename T>
struct SignalGroup {
    // For e.g. "abc|xyz" name is only "xyz" and full name is "abc|xyz"
    std::string name;
    std::string full_name;
    std::vector<T*> signals;
    std::map<std::string, SignalGroup<T>> subgroups;
    bool opened_manually = false;

    bool hasVisibleItems(std::string const& filter);

  private:
    std::string m_filter_prev;
    bool m_has_visible_items = true;
};

class DbgGui;
enum class ScriptLanguage {
    Lua,
    Legacy,
};

struct ScriptWindow : Window {
  public:
    ScriptWindow(DbgGui* gui, std::string const& name_, uint64_t id_);
    ScriptWindow(DbgGui* gui, nlohmann::json const& j)
        : Window(j), m_gui(gui) {
        text = j.value("text", "");
        loop_count = std::max(j.value("loop_count", loop_count), 0);
        language = j.value("language", std::string{"legacy"}) == "lua" ? ScriptLanguage::Lua : ScriptLanguage::Legacy;
        run_on_startup = j.value("run_on_startup", false);
    }
    void updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["text"] = text;
        j["loop_count"] = std::max(loop_count, 0);
        j.erase("loop");
        j["language"] = language == ScriptLanguage::Lua ? "lua" : "legacy";
        j["run_on_startup"] = run_on_startup;
    }

    std::string text;
    ScriptLanguage language = ScriptLanguage::Lua;
    int loop_count = 1;
    bool run_on_startup = false;

    std::string startScript(double current_time, std::vector<std::unique_ptr<Scalar>> const& scalars);
    std::string processScript(double timestamp);
    void shiftScriptSchedule(double time_offset);
    void stopScript();
    int currentLine();
    bool running();
    int loopsRemaining();
    double getTime(double timestamp);

  private:
    struct Operation {
        double time;
        std::function<void(double)> action;
        int line;
    };
    std::vector<Operation> m_operations;
    std::unique_ptr<LuaScriptRunner> m_lua_script;
    int m_idx = -1;
    int m_loops_remaining = 0;
    double m_start_time = 0;
    DbgGui* m_gui;

    std::expected<Operation, std::string> parseSpecialOperation(std::string const& operation_name, std::string line, int line_number);
};
