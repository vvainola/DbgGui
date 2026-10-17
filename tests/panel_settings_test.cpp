#include <catch2/catch_test_macros.hpp>
#include "panel_window.h"

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
    REQUIRE(saved["widgets"][0]["cells"] == cells);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].position.x == 50);
    REQUIRE(restored.widgets[0].position.y == -10);
    REQUIRE(restored.widgets[0].size.x == 400);
    REQUIRE(restored.widgets[0].size.y == 200);
}

TEST_CASE("Panel widget loading bounds geometry and skips unknown types") {
    PanelWindow panel(nlohmann::json{
        {"widgets", nlohmann::json::array({
            {{"type", PanelWidgetType::Slider}, {"width", -1}, {"height", 0}},
            {{"type", 100}}
        })}
    });
    REQUIRE(panel.widgets.size() == 1);
    REQUIRE(panel.widgets[0].size.x == 100);
    REQUIRE(panel.widgets[0].size.y == 20);
}

TEST_CASE("Panel box selection and group movement use canvas coordinates") {
    PanelWindow panel("Panel", 1);
    panel.widgets = {
        {1, PanelWidgetType::Led, "LED", {10, 10}, {20, 20}},
        {2, PanelWidgetType::Checkbox, "Check", {50, 10}, {60, 20}},
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

TEST_CASE("LED geometry is square and contains only the indicator") {
    nlohmann::json saved = {{"widgets", nlohmann::json::array({{
        {"id", 1}, {"type", PanelWidgetType::Led}, {"width", 100}, {"height", 40}
    }})}};
    PanelWindow panel(saved);
    auto& led = panel.widgets[0];
    REQUIRE(led.size.x == 40);
    REQUIRE(led.size.y == 40);
    led.setSize({10, 30});
    REQUIRE(led.size.x == 20);
    REQUIRE(led.size.y == 20);
    panel.updateJson(saved);
    REQUIRE(saved["widgets"][0]["width"] == saved["widgets"][0]["height"]);
}

TEST_CASE("Panel grid snapping handles negative coordinates and widget minimum sizes") {
    REQUIRE(snapToPanelGrid(15) == 0);
    REQUIRE(snapToPanelGrid(17) == 32);
    REQUIRE(snapToPanelGrid(-17) == -32);
    PanelWidget slider;
    slider.type = PanelWidgetType::Slider;
    slider.resizeOnGrid({240, 30}, {-200, -100});
    REQUIRE(slider.size.x == 128);
    REQUIRE(slider.size.y == 32);
    PanelWidget led;
    led.type = PanelWidgetType::Led;
    led.resizeOnGrid({40, 40}, {30, 0});
    REQUIRE(led.size.x == 64);
    REQUIRE(led.size.y == 64);
    led.resizeOnGrid({40, 40}, {-100, 0});
    REQUIRE(led.size.x == 32);
    REQUIRE(led.size.y == 32);
}

TEST_CASE("LED settings preserve the applied condition and color without saving drafts or runtime state") {
    PanelWindow panel("Panel", 1);
    PanelWidget widget;
    widget.id = 2;
    widget.type = PanelWidgetType::Led;
    widget.setSize({40, 40});
    widget.led.condition = "return read('enabled') > 0";
    widget.led.draft = "unfinished script";
    widget.led.color = {0.9f, 0.3f, 0.1f, 1};
    panel.widgets.push_back(std::move(widget));
    nlohmann::json saved;
    panel.updateJson(saved);
    PanelWindow restored(saved);
    REQUIRE(restored.widgets[0].led.condition == "return read('enabled') > 0");
    REQUIRE(restored.widgets[0].led.color.x == 0.9f);
    REQUIRE(restored.widgets[0].led.color.y == 0.3f);
    REQUIRE(restored.widgets[0].led.draft.empty());
    REQUIRE_FALSE(restored.widgets[0].led.state);
}
