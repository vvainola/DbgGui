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
    std::vector<Scalar*> scalars;
    MinMax y_axis = {-1, 1};
    MinMax x_axis = {0, 1};
    double x_range = 1; // Range is stored separately so that x-axis can be zoomed while paused but original range is restored on continue
    double last_frame_timestamp;
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
    std::vector<Vector2D*> vectors;
    Vector2D* reference_frame_vector;
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

    bool hasVisibleItems(std::string const& filter);

  private:
    std::string m_filter_prev;
    bool m_has_visible_items = true;
};

enum FontSelection {
    COUSINE_REGULAR,
    CALIBRI
};

struct ScriptWindow : Window {
  public:
    ScriptWindow(std::string const& name_, uint64_t id_) {
        text[0] = '\0';
        name = name_;
        id = id_;
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
        Scalar* scalar = nullptr;
        double value;
        int line;
    };
    std::vector<Operation> m_operations;
    int m_idx = -1;
    double m_start_time = 0;
};
