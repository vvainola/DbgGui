#include <catch2/catch_test_macros.hpp>
#include "grid_window.h"

TEST_CASE("LED settings preserve the applied condition and color without saving drafts or runtime state") {
    GridWindow window("Grid", 1);
    GridCell widget;
    widget.type = GridCellType::LED;
    widget.led.condition = "return read('enabled') > 0";
    widget.led.editor.draft = "unfinished script";
    widget.led.color = {0.9f, 0.3f, 0.1f, 1};
    window.cells[0] = std::move(widget);
    nlohmann::json saved;
    window.updateJson(saved);
    GridWindow restored(saved);
    REQUIRE(restored.cells[0].led.condition == "return read('enabled') > 0");
    REQUIRE(restored.cells[0].led.color.x == 0.9f);
    REQUIRE(restored.cells[0].led.color.y == 0.3f);
    REQUIRE(restored.cells[0].led.editor.draft.empty());
    REQUIRE_FALSE(restored.cells[0].led.state);
}

TEST_CASE("Script drafts initialize on opening and apply changes only when closing") {
    GridScriptDraft editor;
    REQUIRE_FALSE(editor.update(true, "return false"));
    REQUIRE(editor.draft == "return false");
    editor.draft = "return true";
    REQUIRE_FALSE(editor.update(true, "return false"));
    REQUIRE(editor.draft == "return true");
    REQUIRE(editor.update(false, "return false"));
    REQUIRE_FALSE(editor.update(false, "return false"));
    REQUIRE_FALSE(editor.update(true, "return false"));
    REQUIRE(editor.draft == "return false");
    REQUIRE_FALSE(editor.update(false, "return false"));
}

TEST_CASE("Action editors retain invalid drafts across reopening without replacing applied scripts") {
    GridActionScript script;
    script.script = "return";
    script.updateEditor(true);
    script.editor.draft = "invalid [";
    script.updateEditor(false);
    REQUIRE(script.script == "return");
    REQUIRE_FALSE(script.syntax_error.empty());
    script.updateEditor(true);
    REQUIRE(script.editor.draft == "invalid [");
    script.editor.draft = "wait(1)";
    script.updateEditor(false);
    REQUIRE(script.script == "wait(1)");
    REQUIRE(script.syntax_error.empty());
    REQUIRE_FALSE(script.action);
}

TEST_CASE("Grid actions run only on sampling ticks and resume waits without repeating") {
    std::vector<double> writes;
    LuaScriptHost host;
    host.write = [&](std::string_view, double value) -> std::expected<void, std::string> {
        writes.push_back(value);
        return {};
    };
    GridActionState action("write('target', 1); wait(1); write('target', 2)", std::move(host));
    REQUIRE(writes.empty());
    REQUIRE(action.running.load());
    action.process(5);
    REQUIRE(writes == std::vector<double>{1});
    REQUIRE(action.running.load());
    action.process(5.5);
    REQUIRE(writes == std::vector<double>{1});
    action.runner.shiftSchedule(-4);
    action.process(2);
    REQUIRE(writes == std::vector<double>{1, 2});
    REQUIRE_FALSE(action.running.load());
    REQUIRE(action.error.empty());
    action.process(3);
    REQUIRE(writes == std::vector<double>{1, 2});
}

TEST_CASE("Grid action errors are published without executing drafts") {
    GridButtonCell cell;
    cell.script = "return";
    cell.editor.draft = "unfinished [";
    cell.action = std::make_shared<GridActionState>(cell.script, LuaScriptHost{});
    cell.action->process(0);
    REQUIRE(cell.action->error.empty());
    REQUIRE_FALSE(cell.action->running.load());
    cell.script = cell.editor.draft;
    cell.action = std::make_shared<GridActionState>(cell.script, LuaScriptHost{});
    cell.action->process(0);
    REQUIRE_FALSE(cell.action->error.empty());
    REQUIRE_FALSE(cell.action->running.load());
}

TEST_CASE("Grid actions stop on the sampling thread before starting or resuming") {
    std::vector<double> writes;
    LuaScriptHost host;
    host.write = [&](std::string_view, double value) -> std::expected<void, std::string> {
        writes.push_back(value);
        return {};
    };
    SECTION("Stop an action before its first sample") {
        GridActionState action("write('target', 1)", host);
        action.stop_requested.store(true);
        action.process(0);
        REQUIRE(writes.empty());
        REQUIRE_FALSE(action.running.load());
        REQUIRE(action.error.empty());
    }
    SECTION("Stop an action while it waits") {
        GridActionState action("write('target', 1); wait(1); write('target', 2)", host);
        action.process(0);
        REQUIRE(writes == std::vector<double>{1});
        action.stop_requested.store(true);
        REQUIRE(action.running.load());
        action.process(1);
        REQUIRE_FALSE(action.running.load());
        action.process(2);
        REQUIRE(writes == std::vector<double>{1});
        REQUIRE(action.error.empty());
    }
}

TEST_CASE("Applying a button script checks syntax and keeps the previous script on errors") {
    GridButtonCell cell;
    cell.script = "write('target', 1)";
    cell.editor.draft = "unfinished [";
    cell.applyScript();
    REQUIRE_FALSE(cell.syntax_error.empty());
    REQUIRE(cell.script == "write('target', 1)");
    REQUIRE_FALSE(cell.action);

    cell.action = std::make_shared<GridActionState>("invalid [", LuaScriptHost{});
    cell.action->process(0);
    REQUIRE_FALSE(cell.action->error.empty());
    cell.editor.draft = "write('target', 2)";
    cell.applyScript();
    REQUIRE(cell.syntax_error.empty());
    REQUIRE(cell.script == "write('target', 2)");
    REQUIRE_FALSE(cell.action);
}

TEST_CASE("Applying a valid button script does not stop or execute the running action") {
    GridButtonCell cell;
    cell.action = std::make_shared<GridActionState>("wait(1)", LuaScriptHost{});
    cell.action->process(0);
    auto action = cell.action;
    cell.editor.draft = "write('target', 2)";
    cell.applyScript();
    REQUIRE(cell.action == action);
    REQUIRE(cell.action->running.load());
    REQUIRE_FALSE(cell.action->stop_requested.load());
    REQUIRE(cell.script == cell.editor.draft);
}

TEST_CASE("Checkbox settings save transition scripts without runtime state or drafts") {
    GridWindow window("Grid", 1);
    GridCell widget;
    widget.type = GridCellType::Checkbox;
    widget.checkbox.label = "Enable output";
    widget.checkbox.checked = true;
    widget.checkbox.on_enable.script = "write('enabled', 1)";
    widget.checkbox.on_disable.script = "write('enabled', 0)";
    widget.checkbox.on_enable.editor.draft = "invalid [";
    window.cells[0] = std::move(widget);
    nlohmann::json saved;
    window.updateJson(saved);
    auto const& cell = saved["grid"]["cells"][0];
    REQUIRE(cell["on_enable"] == "write('enabled', 1)");
    REQUIRE(cell["on_disable"] == "write('enabled', 0)");
    REQUIRE_FALSE(cell.contains("checked"));
    REQUIRE_FALSE(cell.contains("draft"));
    REQUIRE_FALSE(cell.contains("action"));
    GridWindow restored(saved);
    REQUIRE(restored.cells[0].checkbox.label == "Enable output");
    auto const& checkbox = restored.cells[0].checkbox;
    REQUIRE_FALSE(checkbox.checked);
    REQUIRE(checkbox.on_enable.script == "write('enabled', 1)");
    REQUIRE(checkbox.on_disable.script == "write('enabled', 0)");
    REQUIRE(checkbox.on_enable.editor.draft.empty());
    REQUIRE_FALSE(checkbox.on_enable.action);
    REQUIRE_FALSE(checkbox.on_disable.action);
}

TEST_CASE("Checkbox transitions queue the matching script and cancel the previous action") {
    std::vector<double> writes;
    LuaScriptHost host;
    host.write = [&](std::string_view, double value) -> std::expected<void, std::string> {
        writes.push_back(value);
        return {};
    };
    GridCheckbox checkbox;
    checkbox.on_enable.script = "write('target', 1); wait(1); write('target', 9)";
    checkbox.on_disable.script = "write('target', 0)";
    REQUIRE_FALSE(checkbox.setChecked(false, host));
    auto enabled = checkbox.setChecked(true, host);
    REQUIRE(checkbox.checked);
    REQUIRE(enabled == checkbox.on_enable.action);
    REQUIRE(writes.empty());
    enabled->process(0);
    REQUIRE(writes == std::vector<double>{1});
    auto disabled = checkbox.setChecked(false, host);
    REQUIRE_FALSE(checkbox.checked);
    REQUIRE(disabled == checkbox.on_disable.action);
    REQUIRE(enabled->stop_requested.load());
    enabled->process(1);
    disabled->process(1);
    REQUIRE(writes == std::vector<double>{1, 0});
    REQUIRE_FALSE(enabled->running.load());
    REQUIRE_FALSE(disabled->running.load());
    REQUIRE_FALSE(checkbox.setChecked(false, host));
    auto reenabled = checkbox.setChecked(true, host);
    REQUIRE(reenabled != enabled);
    reenabled->process(2);
    REQUIRE(writes == std::vector<double>{1, 0, 1});
}

TEST_CASE("Checkbox transition editors apply and validate independently") {
    GridCheckbox checkbox;
    checkbox.on_enable.script = "return";
    checkbox.on_enable.editor.draft = "invalid [";
    checkbox.on_disable.editor.draft = "write('target', 0)";
    checkbox.on_enable.applyScript();
    checkbox.on_disable.applyScript();
    REQUIRE_FALSE(checkbox.on_enable.syntax_error.empty());
    REQUIRE(checkbox.on_enable.script == "return");
    REQUIRE(checkbox.on_disable.syntax_error.empty());
    REQUIRE(checkbox.on_disable.script == "write('target', 0)");
    REQUIRE_FALSE(checkbox.on_enable.action);
    REQUIRE_FALSE(checkbox.on_disable.action);
}

TEST_CASE("Grid windows save their text to value ratio and default to 30 percent") {
    GridWindow grid("Grid", 1);
    REQUIRE(grid.text_to_value_ratio == 0.3f);
    grid.text_to_value_ratio = 0.45f;
    grid.resize(1, 2);
    grid.setCellType(1, GridCellType::Slider);
    nlohmann::json saved;
    grid.updateJson(saved);
    REQUIRE(GridWindow(saved).text_to_value_ratio == 0.45f);
    for (auto const& cell : saved["grid"]["cells"]) {
        REQUIRE_FALSE(cell.contains("text_to_value_ratio"));
    }
    saved["grid"]["text_to_value_ratio"] = 2.0f;
    REQUIRE(GridWindow(saved).text_to_value_ratio == 0.95f);
}

TEST_CASE("Grid action error indicators include syntax and completed runtime errors") {
    GridActionScript script;
    REQUIRE_FALSE(script.hasError());
    script.editor.draft = "unfinished [";
    script.applyScript();
    REQUIRE(script.hasError());
    script.editor.draft = "return";
    script.applyScript();
    REQUIRE_FALSE(script.hasError());

    script.action = std::make_shared<GridActionState>("error('failed')", LuaScriptHost{});
    REQUIRE_FALSE(script.hasError());
    script.action->process(0);
    REQUIRE(script.hasError());
    script.applyScript();
    REQUIRE_FALSE(script.hasError());
}

TEST_CASE("Mixed grids preserve each cell's type and applied configuration") {
    GridWindow window("Controls", 42);
    window.resize(2, 3);
    constexpr std::array types = {GridCellType::Button, GridCellType::Value, GridCellType::Checkbox,
                                  GridCellType::Slider, GridCellType::LED};
    constexpr std::array<size_t, 5> positions = {0, 1, 2, 10, 11};
    for (size_t index = 0; index < types.size(); ++index) {
        window.setCellType(positions[index], types[index]);
    }
    auto& cells = window.cells;
    cells[0].button.label = "Start";
    cells[0].button.script = "write('enabled', 1)";
    cells[0].button.editor.draft = "unfinished [";
    window.text_to_value_ratio = 0.6f;
    cells[1].value.scalar_id = 123;
    cells[2].checkbox.label = "Enable";
    cells[2].checkbox.on_enable.script = "write('enabled', 1)";
    cells[2].checkbox.on_disable.script = "write('enabled', 0)";
    cells[10].value.scalar_id = 456;
    cells[10].value.minimum = -10;
    cells[10].value.maximum = 20;
    cells[11].led.label = "Running";
    cells[11].led.condition = "return read('enabled') == 1";
    cells[11].led.color = {0.1f, 0.2f, 0.3f, 1};
    nlohmann::json saved;
    window.updateJson(saved);
    REQUIRE(saved["grid"]["rows"] == 2);
    REQUIRE(saved["grid"]["columns"] == 3);
    REQUIRE(saved["grid"]["cells"].size() == window.cells.size());
    REQUIRE(saved["grid"]["cells"][0]["script"] == "write('enabled', 1)");
    REQUIRE(saved["grid"]["cells"][1]["scalar_id"] == 123);
    REQUIRE(saved["grid"]["cells"][2]["on_disable"] == "write('enabled', 0)");
    REQUIRE(saved["grid"]["cells"][10]["minimum"] == -10);
    REQUIRE(saved["grid"]["cells"][11]["label"] == "Running");
    GridWindow restored(saved);
    nlohmann::json roundtrip;
    restored.updateJson(roundtrip);
    REQUIRE(roundtrip == saved);
    REQUIRE(restored.title() == window.title());
    for (size_t index = 0; index < types.size(); ++index) {
        REQUIRE(restored.cells[positions[index]].type == types[index]);
    }
}

TEST_CASE("Mixed grid duplication copies all cells and resets their runtime state") {
    GridWindow window("Actions", 1);
    window.resize(1, 3);
    window.setCellType(1, GridCellType::Checkbox);
    window.setCellType(2, GridCellType::LED);
    window.setCellType(0, GridCellType::Button);
    auto& button = window.cells[0].button;
    button.script = "wait(1)";
    button.editor.draft = "invalid [";
    button.action = std::make_shared<GridActionState>(button.script, LuaScriptHost{});
    auto& checkbox = window.cells[1].checkbox;
    checkbox.on_enable.script = "wait(1)";
    checkbox.setChecked(true, LuaScriptHost{});
    window.cells[2].led.configure(LuaScriptHost{});
    auto copy = window.duplicate(2);
    REQUIRE(copy.id == 2);
    REQUIRE(copy.name == "Actions copy");
    REQUIRE(copy.columns == 3);
    REQUIRE(copy.cells[0].button.script == "wait(1)");
    REQUIRE(copy.cells[0].button.editor.draft.empty());
    REQUIRE_FALSE(copy.cells[0].button.action);
    REQUIRE_FALSE(copy.cells[1].checkbox.checked);
    REQUIRE_FALSE(copy.cells[1].checkbox.on_enable.action);
    REQUIRE_FALSE(copy.cells[2].led.state);
    copy.cells[0].button.label = "Changed";
    REQUIRE(button.label == "Button");
    REQUIRE(checkbox.checked);
    REQUIRE(checkbox.on_enable.action);
    REQUIRE(window.cells[2].led.state);
}

TEST_CASE("Changing a cell type keeps its configuration and stops hidden actions") {
    GridWindow window("Controls", 1);
    window.setCellType(0, GridCellType::Button);
    auto& cell = window.cells[0];
    cell.button.label = "Start";
    cell.button.script = "wait(1)";
    cell.button.action = std::make_shared<GridActionState>("wait(1)", LuaScriptHost{});
    window.setCellType(0, GridCellType::LED);
    REQUIRE(cell.button.action->stop_requested.load());
    cell.led.condition = "return true";
    window.setCellType(0, GridCellType::Checkbox);
    auto& checkbox = cell.checkbox;
    checkbox.on_enable.script = "wait(1)";
    checkbox.setChecked(true, LuaScriptHost{});
    window.setCellType(0, GridCellType::Button);
    REQUIRE(checkbox.on_enable.action->stop_requested.load());
    REQUIRE(cell.button.label == "Start");
    REQUIRE(cell.button.script == "wait(1)");
    REQUIRE(cell.led.condition == "return true");
    REQUIRE(cell.type == GridCellType::Button);
    window.setCellType(0, static_cast<GridCellType>(100));
    REQUIRE(cell.type == GridCellType::Value);
    window.setCellType(0, GridCellType::Button);
    window.setCellType(0, static_cast<GridCellType>(-1));
    REQUIRE(cell.type == GridCellType::Value);
}

TEST_CASE("Mixed grid resizing preserves cell coordinates and scalar bindings") {
    GridWindow window("Bindings", 5);
    REQUIRE(window.cells[0].type == GridCellType::Value);
    window.resize(2, 2);
    window.setCellType(10, GridCellType::Value);
    window.setCellType(11, GridCellType::Slider);
    window.cells[10].value.scalar_id = 123;
    window.cells[11].value.scalar_id = 123;
    window.resize(2, 3);
    REQUIRE(window.cells[10].type == GridCellType::Value);
    REQUIRE(window.cells[11].type == GridCellType::Slider);
    REQUIRE(window.cells[2].type == GridCellType::Value);
    window.remapScalarId(123, 456);
    nlohmann::json saved;
    window.updateJson(saved);
    GridWindow restored(saved);
    REQUIRE(restored.cells[10].value.scalar_id == 456);
    REQUIRE(restored.cells[11].value.scalar_id == 456);
    REQUIRE(restored.id == 5);
}

TEST_CASE("Value and slider cells share bindings and preserve ranges when switching types") {
    GridWindow window("Grid", 1);
    auto& value = window.cells[0].value;
    value.scalar_id = 42;
    value.minimum = -10;
    value.maximum = 20;
    window.setCellType(0, GridCellType::Slider);
    REQUIRE(window.cells[0].value.scalar_id == 42);
    REQUIRE(window.cells[0].value.minimum == -10);
    REQUIRE(window.cells[0].value.maximum == 20);

    window.cells[0].value.scalar_id = 99;
    window.setCellType(0, GridCellType::Value);
    REQUIRE(window.cells[0].value.scalar_id == 99);
    window.setCellType(0, GridCellType::Slider);
    REQUIRE(window.cells[0].value.minimum == -10);
    REQUIRE(window.cells[0].value.maximum == 20);

    window.setCellType(0, GridCellType::Value);
    nlohmann::json saved;
    window.updateJson(saved);
    GridWindow restored(saved);
    REQUIRE(restored.cells[0].type == GridCellType::Value);
    restored.setCellType(0, GridCellType::Slider);
    REQUIRE(restored.cells[0].value.minimum == -10);
    REQUIRE(restored.cells[0].value.maximum == 20);
    auto copy = window.duplicate(2);
    copy.setCellType(0, GridCellType::Slider);
    REQUIRE(copy.cells[0].value.scalar_id == 99);
    REQUIRE(copy.cells[0].value.minimum == -10);
    REQUIRE(copy.cells[0].value.maximum == 20);
    restored.setCellType(0, GridCellType::Value);
    REQUIRE(restored.cells[0].value.scalar_id == 99);
    restored.cells[0].value.scalar_id.reset();
    restored.setCellType(0, GridCellType::Slider);
    REQUIRE_FALSE(restored.cells[0].value.scalar_id);
}

TEST_CASE("Unsupported grid cell types restore as values and preserve scalar bindings") {
    for (int const type : {100, -1}) {
        nlohmann::json saved = {{"id", 1}, {"grid", {{"cells", nlohmann::json::array({{
            {"type", type}, {"scalar_id", 42}
        }})}}}};
        GridWindow restored(saved);
        REQUIRE(restored.cells[0].type == GridCellType::Value);
        REQUIRE(restored.cells[0].value.scalar_id == 42);
        restored.updateJson(saved);
        REQUIRE(saved["grid"]["cells"][0]["type"] == nlohmann::json(GridCellType::Value));
        REQUIRE(saved["grid"]["cells"][0]["scalar_id"] == 42);
    }
}

TEST_CASE("Flat value and slider cells preserve empty, zero and unresolved bindings") {
    for (auto type : {GridCellType::Value, GridCellType::Slider}) {
        for (auto binding : {std::optional<uint64_t>{}, std::optional<uint64_t>{0}, std::optional<uint64_t>{123}}) {
            GridCell cell;
            cell.type = type;
            auto& settings = cell.value;
            settings.scalar_id = binding;
            cell.value.minimum = -10;
            cell.value.maximum = 20;
            nlohmann::json saved;
            cell.updateJson(saved);
            REQUIRE_FALSE(saved.contains("cells"));
            REQUIRE_FALSE(saved.contains("rows"));
            REQUIRE_FALSE(saved.contains("columns"));
            REQUIRE(saved.contains("scalar_id") == binding.has_value());
            auto restored = GridCell::fromJson(saved);
            auto const& restored_settings = restored.value;
            REQUIRE(restored_settings.scalar_id == binding);
            if (type == GridCellType::Slider) {
                REQUIRE(restored.value.minimum == -10);
                REQUIRE(restored.value.maximum == 20);
            }
        }
    }
}

TEST_CASE("Mixed grid resizing preserves individual cell actions and LED runtimes") {
    GridWindow mixed("Grid", 1);
    mixed.resize(2, 2);
    mixed.setCellType(10, GridCellType::Button);
    mixed.cells[10].button.script = "wait(1)";
    mixed.cells[10].button.action = std::make_shared<GridActionState>("wait(1)", LuaScriptHost{});
    auto action = mixed.cells[10].button.action;
    mixed.setCellType(11, GridCellType::LED);
    mixed.cells[11].led.label = "Ready";
    mixed.cells[11].led.configure(LuaScriptHost{});
    auto state = mixed.cells[11].led.state;
    mixed.resize(2, 3);
    REQUIRE(mixed.cells[10].button.action == action);
    REQUIRE(mixed.cells[10].button.script == "wait(1)");
    REQUIRE(mixed.cells[11].led.state == state);
    REQUIRE(mixed.cells[11].led.label == "Ready");
    REQUIRE_FALSE(action->stop_requested.load());
    state->requested.store(true);
    mixed.resize(1, 1);
    REQUIRE(mixed.cells[10].button.action == action);
    REQUIRE(mixed.cells[11].led.state == state);
    REQUIRE(action->stop_requested.load());
    REQUIRE_FALSE(state->requested.load());
    nlohmann::json saved;
    mixed.updateJson(saved);
    GridWindow restored(saved);
    REQUIRE(restored.cells[10].button.script == "wait(1)");
    REQUIRE_FALSE(restored.cells[10].button.action);
    REQUIRE(restored.cells[11].led.label == "Ready");
    REQUIRE_FALSE(restored.cells[11].led.state);
}

TEST_CASE("Value cell arrow navigation follows grid coordinates and skips other controls") {
    GridWindow controls("Grid", 1);
    controls.resize(3, 3);
    for (size_t index = 0; index < controls.cells.size(); ++index) {
        controls.cells[index].value.scalar_id = 100 + index;
    }
    controls.setCellType(1, GridCellType::Button);
    controls.setCellType(10, GridCellType::Slider);
    controls.cells[21].value.scalar_id.reset();
    REQUIRE(controls.valueCellInDirection(0, 0, 1) == 2);
    REQUIRE(controls.valueCellInDirection(0, 1, 0) == 20);
    REQUIRE(controls.valueCellInDirection(22, 0, -1) == 20);
    REQUIRE(controls.valueCellInDirection(22, -1, 0) == 12);
    REQUIRE_FALSE(controls.valueCellInDirection(2, 0, 1));
    REQUIRE_FALSE(controls.valueCellInDirection(0, -1, 0));
    REQUIRE_FALSE(controls.valueCellInDirection(0, 0, 0));
    REQUIRE_FALSE(controls.valueCellInDirection(3, 0, -1));
    REQUIRE_FALSE(controls.valueCellInDirection(30, -1, 0));
    controls.focus_cell = 2;
    controls.setCellType(2, GridCellType::LED);
    REQUIRE_FALSE(controls.focus_cell);
    nlohmann::json saved;
    controls.focus_cell = 12;
    controls.updateJson(saved);
    REQUIRE_FALSE(GridWindow(saved).focus_cell);
}

TEST_CASE("Shrinking grids retains all cell types across resizing, saving, and duplication") {
    GridWindow window("Retained", 1);
    window.resize(3, 5);
    auto& cells = window.cells;
    window.setCellType(20, GridCellType::Button);
    cells[20].button.label = "Start";
    cells[20].button.script = "wait(1)";
    cells[21].value.scalar_id = 0;
    window.setCellType(22, GridCellType::Checkbox);
    cells[22].checkbox.label = "Power";
    cells[22].checkbox.on_enable.script = "write('power', 1)";
    cells[22].checkbox.on_disable.script = "write('power', 0)";
    window.setCellType(23, GridCellType::Slider);
    cells[23].value.scalar_id = 123;
    cells[23].value.minimum = -10;
    cells[23].value.maximum = 20;
    window.setCellType(24, GridCellType::LED);
    cells[24].led.label = "Ready";
    cells[24].led.condition = "return true";
    cells[24].led.color = {1, 0, 0, 1};
    auto* const original_cell = &cells[23];
    window.focus_cell = 23;
    window.resize(1, 1);
    REQUIRE_FALSE(window.focus_cell);
    REQUIRE(&window.cells[23] == original_cell);
    window.remapScalarId(123, 456);
    nlohmann::json saved;
    window.updateJson(saved);
    GridWindow restored(saved);
    auto copy = window.duplicate(2);
    for (auto* grid : {&window, &restored, &copy}) {
        REQUIRE(grid->rows == 1);
        REQUIRE(grid->columns == 1);
        grid->resize(4, 2);
        grid->resize(2, 5);
        grid->resize(3, 5);
        REQUIRE(grid->cells[20].type == GridCellType::Button);
        REQUIRE(grid->cells[20].button.label == "Start");
        REQUIRE(grid->cells[20].button.script == "wait(1)");
        REQUIRE(grid->cells[21].type == GridCellType::Value);
        REQUIRE(grid->cells[21].value.scalar_id == 0);
        REQUIRE(grid->cells[22].type == GridCellType::Checkbox);
        REQUIRE(grid->cells[22].checkbox.label == "Power");
        REQUIRE(grid->cells[22].checkbox.on_enable.script == "write('power', 1)");
        REQUIRE(grid->cells[22].checkbox.on_disable.script == "write('power', 0)");
        REQUIRE(grid->cells[23].type == GridCellType::Slider);
        REQUIRE(grid->cells[23].value.scalar_id == 456);
        REQUIRE(grid->cells[23].value.minimum == -10);
        REQUIRE(grid->cells[23].value.maximum == 20);
        REQUIRE(grid->cells[24].type == GridCellType::LED);
        REQUIRE(grid->cells[24].led.label == "Ready");
        REQUIRE(grid->cells[24].led.condition == "return true");
        REQUIRE(grid->cells[24].led.color.x == 1);
        REQUIRE(grid->cells[24].led.color.y == 0);
    }
    window.resize(-1, 100);
    REQUIRE(window.rows == 1);
    REQUIRE(window.columns == GridWindow::MAX_COLUMNS);
    window.resize(100, -1);
    REQUIRE(window.rows == GridWindow::MAX_ROWS);
    REQUIRE(window.columns == 1);
}

TEST_CASE("Resizing cancels newly hidden actions without restarting them on expansion") {
    std::vector<double> writes;
    LuaScriptHost host;
    host.write = [&](std::string_view, double value) -> std::expected<void, std::string> {
        writes.push_back(value);
        return {};
    };
    GridWindow window("Actions", 1);
    window.resize(2, 4);
    window.setCellType(0, GridCellType::Button);
    auto& visible = window.cells[0].button;
    visible.script = "wait(1); write('target', 1)";
    visible.action = std::make_shared<GridActionState>(visible.script, host);
    window.setCellType(11, GridCellType::Button);
    auto& button = window.cells[11].button;
    button.script = "wait(1); write('target', 2)";
    button.action = std::make_shared<GridActionState>(button.script, host);
    window.setCellType(12, GridCellType::Checkbox);
    auto& checkbox = window.cells[12].checkbox;
    checkbox.on_enable.script = "wait(1); write('target', 3)";
    checkbox.on_disable.script = "wait(1); write('target', 4)";
    auto const enabled_action = checkbox.setChecked(true, host);
    window.setCellType(13, GridCellType::LED);
    auto& led = window.cells[13].led;
    led.condition = "return true";
    led.configure(host);
    led.state->requested.store(true);
    visible.action->process(0);
    button.action->process(0);
    enabled_action->process(0);
    REQUIRE(writes.empty());
    SECTION("Reduce rows") { window.resize(1, 4); }
    SECTION("Reduce columns") { window.resize(2, 1); }
    REQUIRE_FALSE(visible.action->stop_requested.load());
    REQUIRE(button.action->stop_requested.load());
    REQUIRE(enabled_action->stop_requested.load());
    REQUIRE_FALSE(led.state->requested.load());
    visible.action->process(1);
    button.action->process(1);
    enabled_action->process(1);
    REQUIRE(writes == std::vector<double>{1});
    REQUIRE_FALSE(button.action->running.load());
    REQUIRE_FALSE(enabled_action->running.load());
    window.resize(2, 4);
    button.action->process(2);
    enabled_action->process(2);
    REQUIRE(writes == std::vector<double>{1});
    REQUIRE(button.script == "wait(1); write('target', 2)");
    REQUIRE(checkbox.checked);
    REQUIRE(checkbox.on_disable.script == "wait(1); write('target', 4)");
    REQUIRE(led.condition == "return true");
}
