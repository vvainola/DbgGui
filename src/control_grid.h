#pragma once

#include "lua_script.h"
#include "imgui.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <array>
#include <stdexcept>

enum class ControlCellType {
    Button,
    Value,
    Checkbox,
    Slider,
    Led
};

struct GridLedResult {
    bool lit = false;
    std::string error;
};

struct GridLedState {
    GridLedState(std::string condition, LuaScriptHost host)
        : runner(std::move(condition), std::move(host), "LED condition") {
    }
    // The runner is used only by the sampling thread. GUI reads published results.
    LuaScriptRunner runner;
    std::atomic<bool> requested = false;
    std::atomic<std::shared_ptr<GridLedResult const>> result{std::make_shared<GridLedResult>()};
};

// One invocation of a grid action. Execution belongs to the sampling thread;
// the GUI only reads the published status and error.
struct GridActionState {
    GridActionState(std::string source, LuaScriptHost host)
        : runner(std::move(source), std::move(host), "Grid action") {
    }
    void process(double timestamp) {
        if (!running.load()) {
            return;
        }
        if (stop_requested.load()) {
            runner.stop();
            running.store(false);
            return;
        }
        std::expected<void, std::string> result;
        if (!started) {
            started = true;
            result = runner.start(timestamp, 1);
        }
        if (result) {
            result = runner.process(timestamp);
        }
        if (!result) {
            error = result.error();
        }
        running.store(runner.running());
    }
    LuaScriptRunner runner;
    bool started = false;
    std::atomic<bool> running = true;
    std::atomic<bool> stop_requested = false;
    // Written before running becomes false; GUI reads it only after completion.
    std::string error;
};

struct GridActionScript {
    std::string script;
    std::string draft;
    std::shared_ptr<GridActionState> action;
    std::string syntax_error;

    bool hasError() const {
        return !syntax_error.empty() || (action && !action->running.load() && !action->error.empty());
    }

    void applyScript() {
        if (auto result = validateLuaSyntax(draft, "Grid action"); !result) {
            syntax_error = result.error();
            return;
        }
        syntax_error.clear();
        script = draft;
        if (action && !action->running.load()) {
            action.reset();
        }
    }
};

struct GridButtonCell : GridActionScript {
    bool editor_open = false;
    std::string label = "Button";
};

struct GridCheckbox {
    bool editor_open = false;
    std::string label = "Checkbox";
    bool checked = false;
    GridActionScript on_enable;
    GridActionScript on_disable;

    std::shared_ptr<GridActionState> setChecked(bool value, LuaScriptHost host) {
        if (checked == value) {
            return nullptr;
        }
        checked = value;
        // A delayed action from the previous state must not undo this transition.
        for (auto* script : {&on_enable, &on_disable}) {
            if (script->action && script->action->running.load()) {
                script->action->stop_requested.store(true);
            }
        }
        auto& script = checked ? on_enable : on_disable;
        script.action = std::make_shared<GridActionState>(script.script, std::move(host));
        return script.action;
    }
};

struct GridValueCell {
    // Keep the ID even when the scalar is unavailable so it can resolve later.
    std::optional<uint64_t> scalar_id;
};

struct GridSlider : GridValueCell {
    double minimum = 0;
    double maximum = 1;
};

struct GridLedCell {
    std::string label = "LED";
    bool editor_open = false;
    std::string condition = "return false";
    std::string draft;
    ImVec4 color = {0.2f, 0.9f, 0.3f, 1};
    std::shared_ptr<GridLedState> state;

    void configure(LuaScriptHost host) {
        state = std::make_shared<GridLedState>(condition, std::move(host));
    }
};

struct ControlCell {
    static ControlCell fromJson(nlohmann::json const& data);
    void updateJson(nlohmann::json& data) const;
    void remapScalarId(uint64_t old_id, uint64_t new_id);
    ControlCellType type = ControlCellType::Value;
    GridButtonCell button;
    GridCheckbox checkbox;
    GridValueCell value;
    GridSlider slider;
    GridLedCell led;
};

struct ControlCellKind {
    ControlCellType type;
    const char* label;
};

inline constexpr std::array CONTROL_CELL_KINDS = {
    ControlCellKind{ControlCellType::Value, "Value"},
    ControlCellKind{ControlCellType::Button, "Button"},
    ControlCellKind{ControlCellType::Checkbox, "Checkbox"},
    ControlCellKind{ControlCellType::Slider, "Slider"},
    ControlCellKind{ControlCellType::Led, "LED"}
};

struct Grid {
    Grid() = default;
    explicit Grid(nlohmann::json const& saved) {
        text_to_value_ratio = std::clamp(saved.value("text_to_value_ratio", 0.3f), 0.05f, 0.95f);
        resize(saved.value("rows", 1), saved.value("columns", 1));
        auto const saved_cells = saved.value("cells", nlohmann::json::array());
        for (size_t index = 0; index < saved_cells.size() && index < cells.size(); ++index) {
            validateType(saved_cells[index].value("type", ControlCellType::Value));
            cells[index] = ControlCell::fromJson(saved_cells[index]);
        }
    }
    void updateJson(nlohmann::json& data) const {
        auto saved_cells = nlohmann::json::array();
        for (auto const& cell : cells) {
            nlohmann::json saved;
            cell.updateJson(saved);
            saved_cells.push_back(std::move(saved));
        }
        data = {{"rows", rows}, {"columns", columns}, {"cells", std::move(saved_cells)},
                {"text_to_value_ratio", text_to_value_ratio}};
    }
    void resize(int new_rows, int new_columns) {
        new_rows = std::clamp(new_rows, 1, 20);
        new_columns = std::clamp(new_columns, 1, 10);
        std::vector<ControlCell> resized(new_rows * new_columns);
        for (int row = 0; row < std::min(rows, new_rows); ++row) {
            for (int col = 0; col < std::min(columns, new_columns); ++col) {
                resized[row * new_columns + col] = std::move(cells[row * columns + col]);
            }
        }
        rows = new_rows;
        columns = new_columns;
        cells = std::move(resized);
        focus_cell.reset();
    }

    void setCellType(size_t index, ControlCellType type) {
        validateType(type);
        auto& cell = cells.at(index);
        if (cell.type == type) {
            return;
        }
        // Hidden controls must not continue delayed simulation writes.
        for (auto* script : {static_cast<GridActionScript*>(&cell.button),
                             &cell.checkbox.on_enable, &cell.checkbox.on_disable}) {
            if (script->action && script->action->running.load()) {
                script->action->stop_requested.store(true);
            }
        }
        cell.type = type;
        if (focus_cell == index && type != ControlCellType::Value) {
            focus_cell.reset();
        }
    }
    std::optional<size_t> valueCellInDirection(size_t from, int row_delta, int column_delta) const {
        if (from >= cells.size() || (row_delta == 0 && column_delta == 0)) {
            return std::nullopt;
        }
        int row = static_cast<int>(from) / columns + row_delta;
        int column = static_cast<int>(from) % columns + column_delta;
        for (; row >= 0 && row < rows && column >= 0 && column < columns;
             row += row_delta, column += column_delta) {
            size_t const index = row * columns + column;
            if (cells[index].type == ControlCellType::Value && cells[index].value.scalar_id) {
                return index;
            }
        }
        return std::nullopt;
    }
    void remapScalarId(uint64_t old_id, uint64_t new_id) {
        for (auto& cell : cells) {
            cell.remapScalarId(old_id, new_id);
        }
    }

    int rows = 1;
    int columns = 1;
    std::vector<ControlCell> cells{1};
    float text_to_value_ratio = 0.3f;
    std::optional<size_t> focus_cell;

  private:
    static void validateType(ControlCellType type) {
        if (!std::ranges::any_of(CONTROL_CELL_KINDS, [type](auto const& kind) { return kind.type == type; })) {
            throw std::runtime_error("Unsupported control cell type");
        }
    }
};
