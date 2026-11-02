#include <catch2/catch_test_macros.hpp>
#include "panel_window.h"
#include "panel_file.h"
#include "panel_clipboard.h"

TEST_CASE("Panel widget geometry preserves saved control settings") {
    nlohmann::json saved = {
        {"id", 1}, {"name", "Panel"}, {"zoom", 1.5}, {"pan_x", -20}, {"pan_y", 30},
        {"widgets", nlohmann::json::array({{
            {"id", 2}, {"type", PanelWidgetType::ButtonGrid}, {"label", "Controls"},
            {"x", 10}, {"y", 20}, {"width", 320}, {"height", 180},
            {"cells", nlohmann::json::array({{{"script", "write('enabled', 1)"}}})}
        }})}
    };
    auto const cells = saved["widgets"][0]["cells"];
    PanelWindow panel(saved);
    REQUIRE(panel.widgets.size() == 1);
    REQUIRE(panel.widgets[0].type == PanelWidgetType::ButtonGrid);
    REQUIRE(panel.zoom == 1.5f);
    REQUIRE(panel.pan.x == -20);
    panel.widgets[0].position = {50, -10};
    panel.widgets[0].size = {400, 200};
    panel.updateJson(saved);
    REQUIRE(saved["widgets"][0]["cells"][0]["script"] == cells[0]["script"]);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].position.x == 50);
    REQUIRE(restored.widgets[0].position.y == -10);
    REQUIRE(restored.widgets[0].size.x == 400);
    REQUIRE(restored.widgets[0].size.y == 200);
}

TEST_CASE("Panel widget loading bounds geometry and skips unknown types") {
    PanelWindow panel(nlohmann::json{
        {"widgets", nlohmann::json::array({
            {{"type", PanelWidgetType::SliderGrid}, {"width", -1}, {"height", 0}},
            {{"type", 100}}
        })}
    });
    REQUIRE(panel.widgets.size() == 1);
    REQUIRE(panel.widgets[0].size.x == 100);
    REQUIRE(panel.widgets[0].size.y == 96);
}

TEST_CASE("Panel box selection and group movement use canvas coordinates") {
    PanelWindow panel("Panel", 1);
    panel.widgets = {
        {1, PanelWidgetType::LedGrid, "LED", {10, 10}, {20, 20}},
        {2, PanelWidgetType::CheckboxGrid, "Check", {50, 10}, {60, 20}},
        {3, PanelWidgetType::Plot, "Plot", {200, 200}, {320, 180}}
    };
    panel.selectWidgetsInBox({120, 40}, {0, 0}, false);
    REQUIRE(panel.selected_widgets == std::vector<uint64_t>{1, 2});
    panel.moveSelectedWidgets({5, -3});
    REQUIRE(panel.widgets[0].position.x == 15);
    REQUIRE(panel.widgets[1].position.y == 7);
    REQUIRE(panel.widgets[2].position.x == 200);
    panel.selectWidget(1, true);
    REQUIRE(panel.selected_widgets == std::vector<uint64_t>{2});
    panel.selectWidget(2, false);
    panel.selectWidgetsInBox({190, 190}, {210, 210}, true);
    REQUIRE(panel.selected_widgets == std::vector<uint64_t>{2, 3});
    panel.selectWidgetsInBox({-20, -20}, {-10, -10}, false);
    REQUIRE(panel.selected_widgets.empty());
    nlohmann::json saved;
    panel.selectWidget(1, false);
    panel.updateJson(saved);
    PanelWindow restored(saved);
    REQUIRE(restored.selected_widgets.empty());
}

TEST_CASE("LED grids allow rectangular layouts") {
    PanelWidget grid;
    grid.type = PanelWidgetType::LedGrid;
    grid.setSize({320, 160});
    REQUIRE(grid.size.x == 320);
    REQUIRE(grid.size.y == 160);
    grid.setSize({10, 30});
    REQUIRE(grid.size.x == 160);
    REQUIRE(grid.size.y == 100);
}

TEST_CASE("Panel grid snapping handles negative coordinates and widget minimum sizes") {
    REQUIRE(snapToPanelGrid(15) == 0);
    REQUIRE(snapToPanelGrid(17) == 32);
    REQUIRE(snapToPanelGrid(-17) == -32);
    PanelWidget slider;
    slider.type = PanelWidgetType::SliderGrid;
    slider.resizeOnGrid({240, 30}, {-200, -100});
    REQUIRE(slider.size.x == 128);
    REQUIRE(slider.size.y == 96);
    PanelWidget led;
    led.type = PanelWidgetType::LedGrid;
    led.resizeOnGrid({40, 40}, {30, 0});
    REQUIRE(led.size.x == 160);
    REQUIRE(led.size.y == 128);
    led.resizeOnGrid({40, 40}, {-100, 0});
    REQUIRE(led.size.x == 160);
    REQUIRE(led.size.y == 128);
}

TEST_CASE("LED settings preserve the applied condition and color without saving drafts or runtime state") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.id = 2;
    widget.type = PanelWidgetType::LedGrid;
    widget.setSize({40, 40});
    widget.leds.cells[0].condition = "return read('enabled') > 0";
    widget.leds.cells[0].draft = "unfinished script";
    widget.leds.cells[0].color = {0.9f, 0.3f, 0.1f, 1};
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].leds.cells[0].condition == "return read('enabled') > 0");
    REQUIRE(restored.widgets[0].leds.cells[0].color.x == 0.9f);
    REQUIRE(restored.widgets[0].leds.cells[0].color.y == 0.3f);
    REQUIRE(restored.widgets[0].leds.cells[0].draft.empty());
    REQUIRE_FALSE(restored.widgets[0].leds.cells[0].state);
}

TEST_CASE("Panel plots preserve layout, styles, and unresolved placements") {
    nlohmann::json saved = {
        {"id", 1}, {"name", "Panel"},
        {"widgets", nlohmann::json::array({{
            {"id", 2}, {"type", PanelWidgetType::Plot}, {"label", "Measurements"},
            {"plot", {
                {"rows", 1}, {"cols", 3}, {"vector_time_range", 0.05},
                {"spectrum_time_range", 0.5}, {"spectrum_logarithmic_y_axis", true},
                {"subplots", nlohmann::json::array({
                    {{"type", PlotType::Scalar}, {"signals", {{"pending", 99}}}},
                    {{"type", PlotType::Vector}, {"vectors", {{1, 2}}}},
                    {{"type", PlotType::Spectrum}, {"spectrums", {{3, 0}}}}
                })}
            }}
        }})}
    };
    PanelWindow panel(saved);
    auto* plot = panel.widgets[0].plot.get();
    REQUIRE(plot != nullptr);
    REQUIRE(plot->subplotCount() == 3);
    REQUIRE(plot->subplots[1].type == PlotType::Vector);
    REQUIRE(plot->subplots[2].type == PlotType::Spectrum);
    REQUIRE(plot->vector_time_range == 0.05f);
    REQUIRE(plot->spectrum.logarithmic_y_axis);
    panel.widgets.reserve(100);
    REQUIRE(panel.widgets[0].plot.get() == plot);
    plot->vector_time_range = 0.1;
    panel.updateJson(saved);
    auto const& plot_data = saved["widgets"][0]["plot"];
    REQUIRE(plot_data["subplots"][0]["signals"]["pending"] == 99);
    REQUIRE(plot_data["subplots"][1]["vectors"][0] == nlohmann::json::array({1, 2}));
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].plot->vector_time_range == 0.1f);
    REQUIRE(restored.widgets[0].plot->subplots[2].type == PlotType::Spectrum);
}

TEST_CASE("Button grids preserve cell coordinates when resized and save applied scripts") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.type = PanelWidgetType::ButtonGrid;
    widget.id = 2;
    widget.buttons.resize(2, 2);
    widget.buttons.cells[2].label = "Enable";
    widget.buttons.cells[2].script = "write('enabled', 1)";
    widget.buttons.cells[2].draft = "unfinished script [";
    widget.buttons.resize(3, 3);
    REQUIRE(widget.buttons.cells[3].label == "Enable");
    REQUIRE(widget.buttons.cells[3].script == "write('enabled', 1)");
    REQUIRE(widget.buttons.cells[2].script.empty());
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    REQUIRE(saved["widgets"][0]["rows"] == 3);
    REQUIRE(saved["widgets"][0]["columns"] == 3);
    REQUIRE(saved["widgets"][0]["cells"].size() == 9);
    REQUIRE_FALSE(saved["widgets"][0]["cells"][3].contains("draft"));
    REQUIRE_FALSE(saved["widgets"][0]["cells"][3].contains("action"));
    PanelWindow restored(saved);
    auto& grid = restored.widgets[0].buttons;
    REQUIRE(grid.cells[3].label == "Enable");
    REQUIRE(grid.cells[3].script == "write('enabled', 1)");
    REQUIRE(grid.cells[3].draft.empty());
    REQUIRE_FALSE(grid.cells[3].action);
    grid.resize(2, 2);
    REQUIRE(grid.cells[2].label == "Enable");
    grid.resize(-1, 100);
    REQUIRE(grid.rows == 1);
    REQUIRE(grid.columns == 10);
    REQUIRE(grid.cells.size() == 10);
}

TEST_CASE("Panel actions run only on sampling ticks and resume waits without repeating") {
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

TEST_CASE("Panel action errors are published without executing drafts") {
    GridButtonCell cell;
    cell.script = "return";
    cell.draft = "unfinished [";
    cell.action = std::make_shared<GridActionState>(cell.script, LuaScriptHost{});
    cell.action->process(0);
    REQUIRE(cell.action->error.empty());
    REQUIRE_FALSE(cell.action->running.load());
    cell.script = cell.draft;
    cell.action = std::make_shared<GridActionState>(cell.script, LuaScriptHost{});
    cell.action->process(0);
    REQUIRE_FALSE(cell.action->error.empty());
    REQUIRE_FALSE(cell.action->running.load());
}

TEST_CASE("Panel actions stop on the sampling thread before starting or resuming") {
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
    cell.draft = "unfinished [";
    cell.applyScript();
    REQUIRE_FALSE(cell.syntax_error.empty());
    REQUIRE(cell.script == "write('target', 1)");
    REQUIRE_FALSE(cell.action);

    cell.action = std::make_shared<GridActionState>("invalid [", LuaScriptHost{});
    cell.action->process(0);
    REQUIRE_FALSE(cell.action->error.empty());
    cell.draft = "write('target', 2)";
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
    cell.draft = "write('target', 2)";
    cell.applyScript();
    REQUIRE(cell.action == action);
    REQUIRE(cell.action->running.load());
    REQUIRE_FALSE(cell.action->stop_requested.load());
    REQUIRE(cell.script == cell.draft);
}

TEST_CASE("Checkbox settings save transition scripts without runtime state or drafts") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.type = PanelWidgetType::CheckboxGrid;
    widget.id = 2;
    widget.label = "Enable output";
    widget.checkboxes.cells[0].checked = true;
    widget.checkboxes.cells[0].on_enable.script = "write('enabled', 1)";
    widget.checkboxes.cells[0].on_disable.script = "write('enabled', 0)";
    widget.checkboxes.cells[0].on_enable.draft = "invalid [";
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    auto const& cell = saved["widgets"][0]["cells"][0];
    REQUIRE(cell["on_enable"] == "write('enabled', 1)");
    REQUIRE(cell["on_disable"] == "write('enabled', 0)");
    REQUIRE_FALSE(cell.contains("checked"));
    REQUIRE_FALSE(cell.contains("draft"));
    REQUIRE_FALSE(cell.contains("action"));
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].label == "Enable output");
    auto const& checkbox = restored.widgets[0].checkboxes.cells[0];
    REQUIRE_FALSE(checkbox.checked);
    REQUIRE(checkbox.on_enable.script == "write('enabled', 1)");
    REQUIRE(checkbox.on_disable.script == "write('enabled', 0)");
    REQUIRE(checkbox.on_enable.draft.empty());
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
    checkbox.on_enable.draft = "invalid [";
    checkbox.on_disable.draft = "write('target', 0)";
    checkbox.on_enable.applyScript();
    checkbox.on_disable.applyScript();
    REQUIRE_FALSE(checkbox.on_enable.syntax_error.empty());
    REQUIRE(checkbox.on_enable.script == "return");
    REQUIRE(checkbox.on_disable.syntax_error.empty());
    REQUIRE(checkbox.on_disable.script == "write('target', 0)");
    REQUIRE_FALSE(checkbox.on_enable.action);
    REQUIRE_FALSE(checkbox.on_disable.action);
}

TEST_CASE("Checkbox grids preserve cell state and scripts in their coordinates when resized") {
    GridCheckboxGrid grid;
    grid.resize(2, 2);
    auto& cell = grid.cells[2];
    cell.label = "Output";
    cell.on_enable.script = "wait(1)";
    auto action = cell.setChecked(true, LuaScriptHost{});
    grid.resize(3, 3);
    REQUIRE(grid.cells[3].label == "Output");
    REQUIRE(grid.cells[3].checked);
    REQUIRE(grid.cells[3].on_enable.script == "wait(1)");
    REQUIRE(grid.cells[3].on_enable.action == action);
    REQUIRE_FALSE(grid.cells[2].checked);
    auto other = grid.cells[0].setChecked(true, LuaScriptHost{});
    REQUIRE(other != action);
    REQUIRE_FALSE(action->stop_requested.load());
    grid.resize(2, 2);
    REQUIRE(grid.cells[2].label == "Output");
    REQUIRE(grid.cells[2].checked);
    grid.resize(1, 1);
    REQUIRE(grid.cells.size() == 1);
    REQUIRE(grid.cells[0].checked);
}

TEST_CASE("Checkbox grids round trip multiple cell labels and transition scripts") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.id = 2;
    widget.type = PanelWidgetType::CheckboxGrid;
    widget.checkboxes.resize(2, 2);
    widget.checkboxes.cells[3].label = "Power";
    widget.checkboxes.cells[3].on_enable.script = "write('power', 1)";
    widget.checkboxes.cells[3].on_disable.script = "write('power', 0)";
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    REQUIRE(saved["widgets"][0]["rows"] == 2);
    REQUIRE(saved["widgets"][0]["columns"] == 2);
    REQUIRE(saved["widgets"][0]["cells"].size() == 4);
    PanelWindow restored(saved);
    auto const& cell = restored.widgets[0].checkboxes.cells[3];
    REQUIRE(cell.label == "Power");
    REQUIRE(cell.on_enable.script == "write('power', 1)");
    REQUIRE(cell.on_disable.script == "write('power', 0)");
    REQUIRE_FALSE(cell.checked);
}

TEST_CASE("Panel value grids preserve unresolved bindings and empty cells") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.id = 2;
    widget.type = PanelWidgetType::ValueGrid;
    widget.values.resize(2, 2);
    widget.values.cells[0].scalar_id = 0;
    widget.values.cells[3].scalar_id = 1234;
    panel.widgets.push_back(std::move(widget));

    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    auto& grid = restored.widgets[0].values;
    REQUIRE(grid.rows == 2);
    REQUIRE(grid.columns == 2);
    REQUIRE(grid.cells[0].scalar_id == 0);
    REQUIRE_FALSE(grid.cells[1].scalar_id.has_value());
    REQUIRE(grid.cells[3].scalar_id == 1234);

    grid.resize(2, 3);
    REQUIRE(grid.cells[4].scalar_id == 1234);
    REQUIRE_FALSE(grid.cells[3].scalar_id.has_value());
    grid.resize(1, 1);
    restored.updateJson(saved);
    REQUIRE(saved["widgets"][0]["cells"].size() == 1);
    REQUIRE(saved["widgets"][0]["cells"][0]["scalar_id"] == 0);
}

TEST_CASE("Panel value grids save their text to value ratio and default to 30 percent") {
    nlohmann::json saved = {{"widgets", nlohmann::json::array({{
        {"id", 2}, {"type", PanelWidgetType::ValueGrid}
    }})}};
    PanelWindow panel(saved);
    REQUIRE(panel.widgets[0].values.text_to_value_ratio == 0.3f);
    panel.widgets[0].values.text_to_value_ratio = 0.45f;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].values.text_to_value_ratio == 0.45f);
    saved["widgets"][0]["text_to_value_ratio"] = 2.0f;
    REQUIRE(PanelWindow(saved).widgets[0].values.text_to_value_ratio == 0.95f);
}

TEST_CASE("Panel action error indicators include syntax and completed runtime errors") {
    GridActionScript script;
    REQUIRE_FALSE(script.hasError());
    script.draft = "unfinished [";
    script.applyScript();
    REQUIRE(script.hasError());
    script.draft = "return";
    script.applyScript();
    REQUIRE_FALSE(script.hasError());

    script.action = std::make_shared<GridActionState>("error('failed')", LuaScriptHost{});
    REQUIRE_FALSE(script.hasError());
    script.action->process(0);
    REQUIRE(script.hasError());
    script.applyScript();
    REQUIRE_FALSE(script.hasError());
}

TEST_CASE("Panel sliders round trip ranges and unresolved bindings") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.id = 2;
    widget.type = PanelWidgetType::SliderGrid;
    widget.sliders.resize(2, 2);
    widget.sliders.text_to_value_ratio = 0.4f;
    widget.sliders.cells[3].scalar_id = 5678;
    widget.sliders.cells[3].maximum = 100;
    widget.sliders.cells[0].scalar_id = 1234;
    widget.sliders.cells[0].minimum = -10;
    widget.sliders.cells[0].maximum = 20;
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].sliders.text_to_value_ratio == 0.4f);
    REQUIRE(restored.widgets[0].sliders.rows == 2);
    REQUIRE(restored.widgets[0].sliders.columns == 2);
    REQUIRE(restored.widgets[0].sliders.cells[3].scalar_id == 5678);
    REQUIRE(restored.widgets[0].sliders.cells[3].maximum == 100);
    REQUIRE(restored.widgets[0].sliders.cells[0].scalar_id == 1234);
    REQUIRE(restored.widgets[0].sliders.cells[0].minimum == -10);
    REQUIRE(restored.widgets[0].sliders.cells[0].maximum == 20);
    restored.widgets[0].sliders.cells[0].scalar_id.reset();
    restored.updateJson(saved);
    REQUIRE_FALSE(saved["widgets"][0]["cells"][0].contains("scalar_id"));
}

TEST_CASE("Slider grid resizing preserves per-cell bindings and ranges") {
    PanelControlGrid<GridSlider> grid;
    grid.resize(2, 2);
    grid.cells[3].scalar_id = 123;
    grid.cells[3].minimum = -5;
    grid.cells[3].maximum = 15;
    grid.resize(2, 3);
    REQUIRE(grid.cells[4].scalar_id == 123);
    REQUIRE(grid.cells[4].minimum == -5);
    REQUIRE(grid.cells[4].maximum == 15);
    REQUIRE_FALSE(grid.cells[3].scalar_id.has_value());
    REQUIRE(grid.cells[3].maximum == 1);
}

TEST_CASE("LED grids preserve independent cells and runtimes when resized") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.type = PanelWidgetType::LedGrid;
    widget.leds.resize(2, 2);
    auto& cell = widget.leds.cells[3];
    cell.label = "Ready";
    cell.condition = "return true";
    cell.color = {1, 0, 0, 1};
    cell.configure(LuaScriptHost{});
    auto state = cell.state;
    widget.leds.resize(2, 3);
    REQUIRE(widget.leds.cells[4].state == state);
    REQUIRE(widget.leds.cells[4].label == "Ready");
    REQUIRE_FALSE(widget.leds.cells[3].state);
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    auto const& grid = restored.widgets[0].leds;
    REQUIRE(grid.rows == 2);
    REQUIRE(grid.columns == 3);
    REQUIRE(grid.cells[4].label == "Ready");
    REQUIRE(grid.cells[4].condition == "return true");
    REQUIRE(grid.cells[4].color.x == 1);
    REQUIRE(grid.cells[4].color.y == 0);
    REQUIRE_FALSE(grid.cells[4].state);
    REQUIRE(grid.cells[0].condition == "return false");
}


TEST_CASE("Panel files preserve widgets and bindings while assigning new identities") {
    PanelWindow panel("Controls", 10);
    PanelWidget widget;
    widget.id = 900;
    widget.type = PanelWidgetType::SliderGrid;
    widget.sliders.resize(2, 2);
    widget.sliders.cells[3].scalar_id = 123;
    widget.sliders.cells[3].minimum = -10;
    panel.widgets.push_back(std::move(widget));
    PanelWidget button;
    button.id = 900; // Local IDs are rebuilt even when the input has duplicates.
    button.buttons.cells[0].script = "write('enabled', 1)";
    panel.widgets.push_back(std::move(button));
    nlohmann::json saved;
    panel.updateJson(saved);
    auto const file = panel_file::encode(saved);
    auto const decoded = panel_file::decode(nlohmann::json::parse(file.dump()), 20);
    PanelWindow restored(decoded);
    REQUIRE(restored.id == 20);
    REQUIRE(restored.name == "Controls");
    REQUIRE(restored.widgets[0].id == 1);
    REQUIRE(restored.widgets[1].id == 2);
    REQUIRE(restored.widgets[0].sliders.cells[3].scalar_id == 123);
    REQUIRE(restored.widgets[0].sliders.cells[3].minimum == -10);
    REQUIRE(restored.widgets[1].buttons.cells[0].script == "write('enabled', 1)");
    REQUIRE_FALSE(restored.widgets[1].buttons.cells[0].action);
    REQUIRE(saved["id"] == 10);
}

TEST_CASE("Panel files reject unsupported formats and malformed panels") {
    REQUIRE_THROWS(panel_file::decode(nlohmann::json::object(), 1));
    auto file = panel_file::encode({{"widgets", nlohmann::json::array()}});
    file["version"] = 2;
    REQUIRE_THROWS(panel_file::decode(file, 1));
    file["version"] = 1;
    file["panel"]["widgets"] = 42;
    REQUIRE_THROWS(panel_file::decode(file, 1));
    file["panel"]["widgets"] = nlohmann::json::array({42});
    REQUIRE_THROWS(panel_file::decode(file, 1));
}

TEST_CASE("Panel files retain unresolved plot placements") {
    nlohmann::json panel = {{"name", "Plots"}, {"widgets", nlohmann::json::array({{
        {"id", 4}, {"type", PanelWidgetType::Plot}, {"label", "Scope"},
        {"plot", {{"subplots", nlohmann::json::array({{
            {"signals", {{"missing signal", 123}}},
            {"vectors", nlohmann::json::array({{123, 456}})}
        }})}}}
    }})}};
    auto const decoded = panel_file::decode(panel_file::encode(panel), 10);
    REQUIRE(decoded["widgets"][0]["plot"] == panel["widgets"][0]["plot"]);
    PanelWindow restored(decoded);
    REQUIRE(restored.widgets[0].plot->id == restored.widgets[0].id);
}

TEST_CASE("Panel clipboard preserves relative positions and assigns destination IDs") {
    PanelWindow source("Source", 1);
    source.widgets = {{7, PanelWidgetType::ButtonGrid, "First", {64, 96}},
                      {8, PanelWidgetType::ValueGrid, "Second", {128, 32}}};
    source.widgets[0].buttons.cells[0].script = "write('enabled', 1)";
    source.widgets[1].values.cells[0].scalar_id = 123;
    nlohmann::json saved;
    source.updateJson(saved);
    auto clipboard = panel_clipboard::copy(saved, {7, 8});
    PanelWindow destination("Destination", 2);
    destination.widgets.emplace_back();
    destination.widgets[0].id = 50;
    auto pasted = panel_clipboard::place(clipboard, destination, {17, 80});
    PanelWindow restored(nlohmann::json{{"widgets", pasted}});
    REQUIRE(restored.widgets[0].id == 51);
    REQUIRE(restored.widgets[1].id == 52);
    REQUIRE(restored.widgets[0].position.x == 32);
    REQUIRE(restored.widgets[0].position.y == 160);
    REQUIRE(restored.widgets[1].position.x == 96);
    REQUIRE(restored.widgets[1].position.y == 96);
    REQUIRE(restored.widgets[0].buttons.cells[0].script == "write('enabled', 1)");
    REQUIRE(restored.widgets[1].values.cells[0].scalar_id == 123);
    REQUIRE(clipboard[0]["id"] == 7);
    REQUIRE(panel_clipboard::copy(saved, {8}).size() == 1);
    REQUIRE(panel_clipboard::copy(saved, {}).empty());
}

TEST_CASE("Pasted widgets omit drafts and script runtimes") {
    PanelWindow panel("Panel", 1);
    panel.widgets.emplace_back();
    auto& widget = panel.widgets[0];
    widget.id = 2;
    widget.buttons.cells[0].script = "return";
    widget.buttons.cells[0].draft = "unsaved";
    widget.buttons.cells[0].action = std::make_shared<GridActionState>("return", LuaScriptHost{});
    nlohmann::json saved;
    panel.updateJson(saved);
    auto clipboard = panel_clipboard::copy(saved, {2});
    PanelWindow pasted(nlohmann::json{{"widgets", panel_clipboard::place(clipboard, panel, {0, 0})}});
    REQUIRE_FALSE(pasted.widgets[0].buttons.cells[0].action);
    REQUIRE(pasted.widgets[0].buttons.cells[0].draft.empty());
    REQUIRE(pasted.widgets[0].buttons.cells[0].script == "return");
}

TEST_CASE("Deleting a selected panel widget removes the whole selection") {
    PanelWindow panel("Panel", 1);
    panel.widgets.resize(3);
    for (size_t index = 0; index < panel.widgets.size(); ++index) {
        panel.widgets[index].id = index + 1;
    }
    panel.selected_widgets = {1, 3};
    panel.removeWidgets(3);
    REQUIRE(panel.widgets.size() == 1);
    REQUIRE(panel.widgets[0].id == 2);
    REQUIRE(panel.selected_widgets.empty());
}

TEST_CASE("Deleting an unselected panel widget preserves the selection") {
    PanelWindow panel("Panel", 1);
    panel.widgets.resize(2);
    panel.widgets[0].id = 1;
    panel.widgets[1].id = 2;
    panel.selected_widgets = {1};
    panel.removeWidgets(2);
    REQUIRE(panel.widgets.size() == 1);
    REQUIRE(panel.widgets[0].id == 1);
    REQUIRE(panel.selected_widgets == std::vector<uint64_t>{1});
}

TEST_CASE("Panel text notes preserve multiline text and appearance through files and clipboard") {
    PanelWindow panel("Notes", 1);
    PanelWidget widget;
    widget.id = 2;
    widget.type = PanelWidgetType::TextNote;
    widget.note.text = "Controls\nVoltage [V]";
    widget.note.font_size = 32;
    widget.note.color = {0.2f, 0.4f, 0.8f, 1};
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(panel_file::decode(panel_file::encode(saved), 3));
    REQUIRE(restored.widgets[0].note.text == "Controls\nVoltage [V]");
    REQUIRE(restored.widgets[0].note.font_size == 32);
    REQUIRE(restored.widgets[0].note.color.z == 0.8f);
    auto clipboard = panel_clipboard::copy(saved, {2});
    PanelWindow pasted(nlohmann::json{{"widgets", panel_clipboard::place(clipboard, panel, {64, 96})}});
    REQUIRE(pasted.widgets[0].type == PanelWidgetType::TextNote);
    REQUIRE(pasted.widgets[0].note.text == restored.widgets[0].note.text);
    REQUIRE(pasted.widgets[0].note.font_size == 32);
}

TEST_CASE("Panel text notes bound font sizes and support small rectangular geometry") {
    PanelWindow panel(nlohmann::json{{"widgets", nlohmann::json::array({{
        {"type", PanelWidgetType::TextNote}, {"font_size", -1}, {"width", 0}, {"height", 0}
    }})}});
    REQUIRE(panel.widgets[0].note.font_size == 8);
    REQUIRE(panel.widgets[0].size.x == 64);
    REQUIRE(panel.widgets[0].size.y == 32);
}

TEST_CASE("Panel backgrounds do not couple widget selection or movement") {
    PanelWindow panel("Panel", 1);
    panel.widgets = {{1, PanelWidgetType::ButtonGrid, "One", {64, 64}, {160, 100}},
                     {2, PanelWidgetType::ValueGrid, "Two", {256, 64}, {160, 100}},
                     {3, PanelWidgetType::TextNote, "Other", {600, 64}, {64, 32}}};
    panel.selected_widgets = {1, 2};
    panel.groupSelected();
    auto group = panel.widgets[0].group;
    REQUIRE(group == panel.widgets[1].group);
    REQUIRE_FALSE(panel.widgets[2].group);
    panel.selected_widgets.clear();
    panel.selectWidget(2, false);
    REQUIRE(panel.selected_widgets == std::vector<uint64_t>{2});
    auto bounds = panel.groupBounds(*group);
    REQUIRE(bounds.x == 56);
    REQUIRE(bounds.y == 44);
    REQUIRE(bounds.z == 424);
    panel.moveSelectedWidgets({32, 64});
    REQUIRE(panel.widgets[0].position.x == 64);
    REQUIRE(panel.widgets[0].position.y == 64);
    REQUIRE(panel.widgets[1].position.x == 288);
    REQUIRE(panel.widgets[1].position.y == 128);
    REQUIRE(panel.widgets[2].position.x == 600);
    panel.selectWidget(2, true);
    REQUIRE(panel.selected_widgets.empty());
    panel.selectWidgetsInBox({96, 128}, {97, 129}, false);
    REQUIRE(panel.selected_widgets == std::vector<uint64_t>{1});
    panel.removeWidgets(1);
    REQUIRE(panel.widgets.size() == 2);
    REQUIRE(panel.widgets[0].id == 2);
    REQUIRE(panel.widgets[0].group == group);
    panel.selectWidget(2, false);
    panel.ungroupSelected();
    REQUIRE_FALSE(panel.widgets[0].group);
}

TEST_CASE("Panel placement finds the topmost background at the canvas position") {
    PanelWindow panel("Panel", 1);
    panel.widgets = {{1, PanelWidgetType::ButtonGrid, "One", {64, 64}, {160, 100}},
                     {2, PanelWidgetType::ButtonGrid, "Two", {128, 64}, {160, 100}},
                     {3, PanelWidgetType::ButtonGrid, "Three", {64, 64}, {160, 100}}};
    panel.selected_widgets = {1, 3};
    panel.groupSelected();
    auto const first = panel.widgets[0].group;
    panel.widgets[1].group = std::make_shared<PanelGroup>();
    panel.widgets[1].group->id = first->id + 1;
    REQUIRE(panel.groupAt({64, 64}) == first);
    REQUIRE(panel.groupAt({160, 96}) == panel.widgets[1].group);
    REQUIRE_FALSE(panel.groupAt({0, 0}));
    PanelWidget added;
    added.position = {160, 96};
    added.group = panel.groupAt(added.position);
    panel.widgets.push_back(std::move(added));
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets.back().group == restored.widgets[1].group);
}

TEST_CASE("Grouping one widget joins the background at its position") {
    PanelWindow panel("Panel", 1);
    panel.widgets = {{1, PanelWidgetType::ButtonGrid, "One", {64, 64}, {160, 100}},
                     {2, PanelWidgetType::ButtonGrid, "Two", {256, 64}, {160, 100}},
                     {3, PanelWidgetType::ButtonGrid, "Inside", {128, 64}, {64, 64}},
                     {4, PanelWidgetType::ButtonGrid, "Outside", {640, 64}, {64, 64}}};
    panel.selected_widgets = {1, 2};
    panel.groupSelected();
    auto const group = panel.widgets[0].group;
    panel.selected_widgets = {3};
    panel.groupSelected();
    REQUIRE(panel.widgets[2].group == group);
    REQUIRE(panel.widgets[0].group == group);
    REQUIRE(panel.widgets[1].group == group);
    panel.selected_widgets = {4};
    panel.groupSelected();
    REQUIRE_FALSE(panel.widgets[3].group);
}

TEST_CASE("Groups round trip and pasted groups are independent of existing groups") {
    PanelWindow panel("Panel", 1);
    panel.widgets.resize(2);
    panel.widgets[0].id = 1;
    panel.widgets[1].id = 2;
    panel.selected_widgets = {1, 2};
    panel.groupSelected();
    auto group = panel.widgets[0].group;
    group->title = "Controls";
    group->padding = 24;
    group->color = {0.1f, 0.2f, 0.3f, 0.4f};
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(panel_file::decode(panel_file::encode(saved), 10));
    REQUIRE(restored.widgets[0].group == restored.widgets[1].group);
    REQUIRE(restored.widgets[0].group != group);
    REQUIRE(restored.widgets[0].group->title == "Controls");
    REQUIRE(restored.widgets[0].group->padding == 24);
    REQUIRE(restored.widgets[0].group->color.w == 0.4f);
    auto single = panel_clipboard::copy(saved, {1});
    REQUIRE(single.size() == 1);
    REQUIRE_FALSE(single[0].contains("group"));
    PanelWindow pasted_single(nlohmann::json{{"widgets", panel_clipboard::place(single, panel, {640, 0})}});
    REQUIRE_FALSE(pasted_single.widgets[0].group);
    auto clipboard = panel_clipboard::copy(saved, {1, 2});
    PanelWindow pasted(nlohmann::json{{"widgets", panel_clipboard::place(clipboard, panel, {0, 0})}});
    REQUIRE(pasted.widgets[0].group == pasted.widgets[1].group);
    REQUIRE(pasted.widgets[0].group->id != group->id);
    pasted.widgets[0].group->title = "Copy";
    REQUIRE(group->title == "Controls");
}

TEST_CASE("Control grids save their text to value ratio and default to 30 percent") {
    Grid grid;
    REQUIRE(grid.text_to_value_ratio == 0.3f);
    grid.text_to_value_ratio = 0.45f;
    grid.resize(1, 2);
    grid.setCellType(1, ControlCellType::Slider);
    nlohmann::json saved;
    grid.updateJson(saved);
    REQUIRE(Grid(saved).text_to_value_ratio == 0.45f);
    for (auto const& cell : saved["cells"]) {
        REQUIRE_FALSE(cell.contains("text_to_value_ratio"));
    }
    saved["text_to_value_ratio"] = 2.0f;
    REQUIRE(Grid(saved).text_to_value_ratio == 0.95f);
}

TEST_CASE("Mixed control grids preserve each cell's type and applied configuration") {
    Grid grid;
    grid.resize(2, 3);
    constexpr std::array types = {ControlCellType::Button, ControlCellType::Value, ControlCellType::Checkbox,
                                  ControlCellType::Slider, ControlCellType::Led};
    for (size_t index = 0; index < types.size(); ++index) {
        grid.setCellType(index, types[index]);
    }
    auto& cells = grid.cells;
    cells[0].button.label = "Start";
    cells[0].button.script = "write('enabled', 1)";
    cells[0].button.draft = "unfinished [";
    grid.text_to_value_ratio = 0.6f;
    cells[1].value.scalar_id = 123;
    cells[2].checkbox.label = "Enable";
    cells[2].checkbox.on_enable.script = "write('enabled', 1)";
    cells[2].checkbox.on_disable.script = "write('enabled', 0)";
    cells[3].slider.scalar_id = 456;
    cells[3].slider.minimum = -10;
    cells[3].slider.maximum = 20;
    cells[4].led.label = "Running";
    cells[4].led.condition = "return read('enabled') == 1";
    cells[4].led.color = {0.1f, 0.2f, 0.3f, 1};
    nlohmann::json saved;
    grid.updateJson(saved);
    REQUIRE(saved["rows"] == 2);
    REQUIRE(saved["columns"] == 3);
    REQUIRE(saved["cells"].size() == 6);
    REQUIRE(saved["cells"][0]["script"] == "write('enabled', 1)");
    REQUIRE(saved["cells"][1]["scalar_id"] == 123);
    REQUIRE(saved["cells"][2]["on_disable"] == "write('enabled', 0)");
    REQUIRE(saved["cells"][3]["minimum"] == -10);
    REQUIRE(saved["cells"][4]["label"] == "Running");
    Grid restored(saved);
    nlohmann::json roundtrip;
    restored.updateJson(roundtrip);
    REQUIRE(roundtrip == saved);
    for (size_t index = 0; index < types.size(); ++index) {
        REQUIRE(restored.cells[index].type == types[index]);
    }
}

TEST_CASE("Changing a cell type keeps its configuration and stops hidden actions") {
    Grid grid;
    grid.setCellType(0, ControlCellType::Button);
    auto& cell = grid.cells[0];
    cell.button.label = "Start";
    cell.button.script = "wait(1)";
    cell.button.action = std::make_shared<GridActionState>("wait(1)", LuaScriptHost{});
    grid.setCellType(0, ControlCellType::Led);
    REQUIRE(cell.button.action->stop_requested.load());
    cell.led.condition = "return true";
    grid.setCellType(0, ControlCellType::Checkbox);
    auto& checkbox = cell.checkbox;
    checkbox.on_enable.script = "wait(1)";
    checkbox.setChecked(true, LuaScriptHost{});
    grid.setCellType(0, ControlCellType::Button);
    REQUIRE(checkbox.on_enable.action->stop_requested.load());
    REQUIRE(cell.button.label == "Start");
    REQUIRE(cell.button.script == "wait(1)");
    REQUIRE(cell.led.condition == "return true");
    REQUIRE(cell.type == ControlCellType::Button);
    REQUIRE_THROWS(grid.setCellType(0, static_cast<ControlCellType>(100)));
    REQUIRE_THROWS(grid.setCellType(0, static_cast<ControlCellType>(-1)));
    REQUIRE(cell.type == ControlCellType::Button);
}

TEST_CASE("Mixed grid resizing preserves cell coordinates and scalar bindings") {
    Grid grid;
    REQUIRE(grid.cells[0].type == ControlCellType::Value);
    grid.resize(2, 2);
    grid.setCellType(2, ControlCellType::Value);
    grid.setCellType(3, ControlCellType::Slider);
    grid.cells[2].value.scalar_id = 123;
    grid.cells[3].slider.scalar_id = 123;
    grid.resize(2, 3);
    REQUIRE(grid.cells[3].type == ControlCellType::Value);
    REQUIRE(grid.cells[4].type == ControlCellType::Slider);
    REQUIRE(grid.cells[2].type == ControlCellType::Value);
    grid.remapScalarId(123, 456);
    nlohmann::json saved;
    grid.updateJson(saved);
    Grid restored(saved);
    REQUIRE(restored.cells[3].value.scalar_id == 456);
    REQUIRE(restored.cells[4].slider.scalar_id == 456);
}

TEST_CASE("Mixed grids reject unsupported cell types when loading") {
    nlohmann::json saved = {{"cells", nlohmann::json::array({{{"type", 100}}})}};
    REQUIRE_THROWS(Grid(saved));
}

TEST_CASE("Flat value and slider cells preserve empty, zero and unresolved bindings") {
    for (auto type : {ControlCellType::Value, ControlCellType::Slider}) {
        for (auto binding : {std::optional<uint64_t>{}, std::optional<uint64_t>{0}, std::optional<uint64_t>{123}}) {
            ControlCell cell;
            cell.type = type;
            auto& settings = type == ControlCellType::Value ? cell.value : cell.slider;
            settings.scalar_id = binding;
            cell.slider.minimum = -10;
            cell.slider.maximum = 20;
            nlohmann::json saved;
            cell.updateJson(saved);
            REQUIRE_FALSE(saved.contains("cells"));
            REQUIRE_FALSE(saved.contains("rows"));
            REQUIRE_FALSE(saved.contains("columns"));
            REQUIRE(saved.contains("scalar_id") == binding.has_value());
            auto restored = ControlCell::fromJson(saved);
            auto const& restored_settings = type == ControlCellType::Value ? restored.value : restored.slider;
            REQUIRE(restored_settings.scalar_id == binding);
            if (type == ControlCellType::Slider) {
                REQUIRE(restored.slider.minimum == -10);
                REQUIRE(restored.slider.maximum == 20);
            }
        }
    }
}

TEST_CASE("Mixed grid resizing preserves individual cell actions and LED runtimes") {
    Grid mixed;
    mixed.resize(2, 2);
    mixed.setCellType(2, ControlCellType::Button);
    mixed.cells[2].button.script = "wait(1)";
    mixed.cells[2].button.action = std::make_shared<GridActionState>("wait(1)", LuaScriptHost{});
    auto action = mixed.cells[2].button.action;
    mixed.setCellType(3, ControlCellType::Led);
    mixed.cells[3].led.label = "Ready";
    mixed.cells[3].led.configure(LuaScriptHost{});
    auto state = mixed.cells[3].led.state;
    mixed.resize(2, 3);
    REQUIRE(mixed.cells[3].button.action == action);
    REQUIRE(mixed.cells[3].button.script == "wait(1)");
    REQUIRE(mixed.cells[4].led.state == state);
    REQUIRE(mixed.cells[4].led.label == "Ready");
    REQUIRE_FALSE(mixed.cells[2].button.action);
    REQUIRE_FALSE(action->stop_requested.load());
    nlohmann::json saved;
    mixed.updateJson(saved);
    Grid restored(saved);
    REQUIRE(restored.cells[3].button.script == "wait(1)");
    REQUIRE_FALSE(restored.cells[3].button.action);
    REQUIRE(restored.cells[4].led.label == "Ready");
    REQUIRE_FALSE(restored.cells[4].led.state);
}

TEST_CASE("Value cell arrow navigation follows grid coordinates and skips other controls") {
    Grid controls;
    controls.resize(3, 3);
    for (size_t index = 0; index < controls.cells.size(); ++index) {
        controls.cells[index].value.scalar_id = 100 + index;
    }
    controls.setCellType(1, ControlCellType::Button);
    controls.setCellType(3, ControlCellType::Slider);
    controls.cells[7].value.scalar_id.reset();
    REQUIRE(controls.valueCellInDirection(0, 0, 1) == 2);
    REQUIRE(controls.valueCellInDirection(0, 1, 0) == 6);
    REQUIRE(controls.valueCellInDirection(8, 0, -1) == 6);
    REQUIRE(controls.valueCellInDirection(8, -1, 0) == 5);
    REQUIRE_FALSE(controls.valueCellInDirection(2, 0, 1));
    REQUIRE_FALSE(controls.valueCellInDirection(0, -1, 0));
    REQUIRE_FALSE(controls.valueCellInDirection(0, 0, 0));
    controls.focus_cell = 2;
    controls.setCellType(2, ControlCellType::Led);
    REQUIRE_FALSE(controls.focus_cell);
    nlohmann::json saved;
    controls.focus_cell = 5;
    controls.updateJson(saved);
    REQUIRE_FALSE(Grid(saved).focus_cell);
}
