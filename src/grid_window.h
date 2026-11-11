#pragma once

#include "data_structures.h"
#include "lua_script.h"
#include "imgui.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Numeric values are persisted in grid cell settings.
enum class GridCellType {
    Value,
    Button,
    Checkbox,
    Slider,
    LED
};

struct GridLedState {
    using Result = std::expected<LuaLedResult, std::string>;

    GridLedState(std::string condition, LuaScriptHost host)
        : runner(std::move(condition), std::move(host), "LED condition") {
    }
    // The runner is used only by the sampling thread. GUI reads published results.
    LuaScriptRunner runner;
    std::atomic<bool> requested = false;
    std::atomic<std::shared_ptr<Result const>> result{std::make_shared<Result>()};
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

struct GridScriptDraft {
    std::string draft;
    bool editor_open = false;

    // Return true when closing should apply the draft.
    // Preserve invalid drafts on reopening and revalidate them on close.
    bool update(bool open, std::string const& applied, bool preserve_draft = false) {
        if (open && !editor_open && !preserve_draft) {
            draft = applied;
        }
        bool const apply = editor_open && !open && (draft != applied || preserve_draft);
        editor_open = open;
        return apply;
    }
};

struct GridActionScript {
    std::string script;
    GridScriptDraft editor;
    std::shared_ptr<GridActionState> action;
    std::string syntax_error;

    bool hasError() const {
        return !syntax_error.empty() || (action && !action->running.load() && !action->error.empty());
    }

    void updateEditor(bool open) {
        if (editor.update(open, script, !syntax_error.empty())) {
            applyScript();
        }
    }

    void applyScript() {
        if (auto result = validateLuaSyntax(editor.draft, "Grid action"); !result) {
            syntax_error = result.error();
            return;
        }
        syntax_error.clear();
        script = editor.draft;
        if (action && !action->running.load()) {
            action.reset();
        }
    }
};

struct GridButtonCell {
    std::string label = "Button";
    GridActionScript on_click;
};

struct GridCheckbox {
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
    double minimum = 0;
    double maximum = 1;
};

struct GridLedCell {
    std::string label = "LED";
    std::string condition = "return false";
    GridScriptDraft editor;
    ImVec4 color = {0.2f, 0.9f, 0.3f, 1};
    std::shared_ptr<GridLedState> state;

    void configure(LuaScriptHost host) {
        state = std::make_shared<GridLedState>(condition, std::move(host));
    }
};

struct GridCell {
    static GridCell fromJson(nlohmann::json const& data);
    void updateJson(nlohmann::json& data) const;
    GridCellType type = GridCellType::Value;
    GridButtonCell button;
    GridCheckbox checkbox;
    GridValueCell value;
    GridLedCell led;
};

struct GridWindow : Window {
    static constexpr int MAX_ROWS = 20;
    static constexpr int MAX_COLUMNS = 10;

    GridWindow(std::string const& name, uint64_t id);
    explicit GridWindow(nlohmann::json const& data);
    void updateJson(nlohmann::json& data) const;
    GridWindow duplicate(uint64_t new_id) const;
    void resize(int new_rows, int new_columns);
    void setCellType(size_t index, GridCellType type);
    std::optional<size_t> valueCellInDirection(size_t from, int row_delta, int column_delta) const;
    void remapScalarId(uint64_t old_id, uint64_t new_id);

    int rows = 1;
    int columns = 1;
    // Storage and saved cells use a fixed MAX_COLUMNS stride; rows/columns only control visibility.
    std::array<GridCell, MAX_ROWS * MAX_COLUMNS> cells;
    float text_to_value_ratio = 0.3f;
    std::optional<size_t> focus_cell;

};
