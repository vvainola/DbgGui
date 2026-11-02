#pragma once

#include "data_structures.h"
#include "lua_script.h"
#include "imgui.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

struct GridWindow : Window {
    GridWindow(std::string const& name, uint64_t id);
    explicit GridWindow(nlohmann::json const& data);
    void updateJson(nlohmann::json& data) const;
    GridWindow duplicate(uint64_t new_id) const;
    void resize(int new_rows, int new_columns);
    void setCellType(size_t index, ControlCellType type);
    std::optional<size_t> valueCellInDirection(size_t from, int row_delta, int column_delta) const;
    void remapScalarId(uint64_t old_id, uint64_t new_id);

    int rows = 1;
    int columns = 1;
    std::vector<ControlCell> cells{1};
    float text_to_value_ratio = 0.3f;
    std::optional<size_t> focus_cell;

  private:
    static void validateType(ControlCellType type);
};
