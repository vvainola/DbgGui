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
#include "str_helpers.h"
#include "nlohmann/json.hpp"

#include <numeric>
#include <vector>
#include <cmath>
#include <future>
#include <string>
#include <format>
#include <map>

#define TRY(expression)                       \
    try {                                     \
        expression                            \
    } catch (nlohmann::json::exception err) { \
        std::cerr << err.what() << std::endl; \
    } catch (std::exception err) {            \
        std::cerr << err.what() << std::endl; \
    }

inline constexpr unsigned MAX_NAME_LENGTH = 255;

uint64_t hash(const std::string& str);
uint64_t hashWithTime(const std::string& str);
std::string getFilenameToSave(std::string const& filter = "csv", std::string default_path = "");
std::string getFilenameToOpen(std::string const& filter, std::string default_path = "");

template <typename T>
T inline min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
T inline max(T a, T b) {
    return a > b ? a : b;
}

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
        alias_and_group = alias + " (" + group + ")";
    }

    nlohmann::json updateJson(nlohmann::json& j) const {
        j["id"] = id;
        j["scale"] = getScaleStr();
        j["offset"] = getOffsetStr();
        j["alias"] = alias;
        return j;
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
    Window(std::string const& name_, uint64_t id_)
        : name(name_), id(id_) {
    }
    Window(nlohmann::json const& j) {
        name = j.value("name", name);
        id = j.value("id", id);
        focus.initial_focus = j.value("initial_focus", focus.initial_focus);
    }
    nlohmann::json updateJson(nlohmann::json& j) const {
        j["name"] = name;
        j["id"] = id;
        j["initial_focus"] = focus.focused;
        return j;
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
    ScalarPlot(std::string const& name, uint64_t id)
        : Window(name, id) {
    }
    ScalarPlot(nlohmann::json const& j)
        : Window(j) {
        x_range = j.value("x_range", x_range);
        x_axis.min = 0;
        x_axis.max = x_range;
        autofit_y = j.value("autofit_y", autofit_y);
        if (!autofit_y) {
            y_axis.min = j.value("y_min", y_axis.min);
            y_axis.max = j.value("y_max", y_axis.max);
        }
    }
    nlohmann::json updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        // Update range only if autofit is not on because otherwise the file
        // could be continously rewritten when autofit range changes
        if (!autofit_y) {
            j["y_min"] = y_axis.min;
            j["y_max"] = y_axis.max;
        }
        j["x_range"] = x_range;
        j["autofit_y"] = autofit_y;
        return j;
    }

    std::vector<Scalar*> scalars;
    MinMax y_axis = {-1, 1};
    MinMax x_axis = {0, 1};
    double x_range = 1; // Range is stored separately so that x-axis can be zoomed while paused but original range is restored on continue
    double last_frame_timestamp = 0;
    bool autofit_y = true;

    void addScalarToPlot(Scalar* new_scalar) {
        // Add scalar if it is not already in the plot
        auto it = std::find_if(scalars.begin(), scalars.end(), [=](Scalar* sig) {
            return sig->id == new_scalar->id;
        });
        if (it == scalars.end()) {
            scalars.push_back(new_scalar);
        }
    }
};

struct VectorPlot : Window {
    VectorPlot(std::string const& name, uint64_t id)
        : Window(name, id) {
    }
    VectorPlot(nlohmann::json const& j)
        : Window(j) {
        time_range = j.value("time_range", time_range);
    }
    nlohmann::json updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["time_range"] = time_range;
        return j;
    }

    std::vector<Vector2D*> vectors;
    Vector2D* reference_frame_vector = nullptr;
    float time_range = 20e-3f;

    void addVectorToPlot(Vector2D* new_vector) {
        // Add vector if it is not already in the plot
        auto it = std::find_if(vectors.begin(), vectors.end(), [=](Vector2D* sig) {
            return sig->id == new_vector->id;
        });
        if (it == vectors.end()) {
            vectors.push_back(new_vector);
        }
    }
};

struct SpectrumPlot : Window {
    SpectrumPlot(std::string const& name, uint64_t id)
        : Window(name, id) {
    }
    SpectrumPlot(nlohmann::json const& j)
        : Window(j) {
        time_range = j.value("time_range", time_range);
        logarithmic_y_axis = j.value("logarithmic_y_axis", logarithmic_y_axis);
        y_axis.min = j.value("y_axis_min", y_axis.min);
        y_axis.max = j.value("y_axis_max", y_axis.max);
        x_axis.min = j.value("x_axis_min", x_axis.min);
        x_axis.max = j.value("x_axis_max", x_axis.max);
    }
    nlohmann::json updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["time_range"] = time_range;
        j["logarithmic_y_axis"] = logarithmic_y_axis;
        j["window"] = static_cast<int>(window);
        j["x_axis_min"] = x_axis.min;
        j["x_axis_max"] = x_axis.max;
        j["y_axis_min"] = y_axis.min;
        j["y_axis_max"] = y_axis.max;
        return j;
    }

    // Source is either scalar or vector
    Scalar* scalar = nullptr;
    Vector2D* vector = nullptr;

    double time_range = 1;
    bool logarithmic_y_axis = false;
    MinMax y_axis = {-0.1, 1.1};
    MinMax x_axis = {-1000, 1000};

    Spectrum spectrum;
    SpectrumWindow window = SpectrumWindow::None;
    std::future<Spectrum> spectrum_calculation;

    void addVectorToPlot(Vector2D* new_vector) {
        vector = new_vector;
        scalar = nullptr;
    }

    void addScalarToPlot(Scalar* new_scalar) {
        scalar = new_scalar;
        vector = nullptr;
    }
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

  private:
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
    nlohmann::json updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["rows"] = rows;
        j["columns"] = columns;
        j["text_to_value_ratio"] = text_to_value_ratio;
        return j;
    }

    static const int MAX_ROWS = 20;
    static const int MAX_COLUMNS = 10;
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
            name.reserve(MAX_NAME_LENGTH);
            if (ImGui::InputText("Name##window_context_menu",
                                 name.data(),
                                 MAX_NAME_LENGTH)) {
                name = std::string(name.data());
            }
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
    nlohmann::json updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["even_split"] = even_split;
        return j;
    }

    void contextMenu() {
        if (ImGui::BeginPopupContextItem((title() + "_context_menu").c_str())) {
            name.reserve(MAX_NAME_LENGTH);
            if (ImGui::InputText("Name##window_context_menu",
                                 name.data(),
                                 MAX_NAME_LENGTH)) {
                name = std::string(name.data());
            }
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
struct ScriptWindow : Window {
  public:
    ScriptWindow(DbgGui* gui, std::string const& name_, uint64_t id_);
    ScriptWindow(DbgGui* gui, nlohmann::json const& j)
        : Window(j), m_gui(gui) {
        std::string json_text = j.value("text", "");
        std::memcpy((void*)text, (void*)json_text.data(), json_text.size());
        text[json_text.size()] = '\0';
        loop = j.value("loop", loop);
    }
    nlohmann::json updateJson(nlohmann::json& j) const {
        Window::updateJson(j);
        j["text"] = text;
        j["loop"] = loop;
        return j;
    }

    char text[1024 * 16];
    bool loop = false;
    bool text_edit_open = false;

    std::string startScript(double current_time, std::vector<std::unique_ptr<Scalar>> const& scalars);
    void processScript(double timestamp);
    void stopScript();
    int currentLine();
    bool running();
    double getTime(double timestamp);

  private:
    struct Operation {
        double time;
        std::function<void(double)> action;
        int line;
    };
    std::vector<Operation> m_operations;
    int m_idx = -1;
    double m_start_time = 0;
    DbgGui* m_gui;

    std::expected<Operation, std::string> parseSpecialOperation(std::string const& operation_name, std::string line, int line_number);
};
