#include <catch2/catch_test_macros.hpp>

#include "imgui/imgui.h"
#include "settings_migration.h"
#include "plot_settings.h"

namespace {
std::unique_ptr<Scalar> makeSettingsScalar(uint64_t id) {
    auto scalar = std::make_unique<Scalar>();
    scalar->id = id;
    return scalar;
}
} // namespace

TEST_CASE("ImGui layout migration updates old triple-hash dock window references") {
    std::string layout =
      "[Window][###123]\n"
      "Pos=1,2\n"
      "DockId=0x731539FF,0\n"
      "\n"
      "[Window][Docked in root]\n"
      "DockId=0xCD6C9E75,0\n"
      "\n"
      "[Docking][Data]\n"
      "DockSpace ID=0xCD6C9E75 Window=0x731539FF Pos=0,0 Size=100,100 Split=X Selected=0x731539FF\n"
      "DockNode  ID=0x00000002 Parent=0xCD6C9E75 SizeRef=50,100 Selected=0xDEADBEEF\n";

#if IMGUI_VERSION_NUM >= 19260
    CHECK(settings_migration::migrateLayoutIniHashes(layout));
    CHECK(layout.find("Window=0x884863D2") != std::string::npos);
    CHECK(layout.find("Selected=0x884863D2") != std::string::npos);
    CHECK(layout.find("DockSpace ID=0xB4F4E0DE") != std::string::npos);
    CHECK(layout.find("Parent=0xB4F4E0DE") != std::string::npos);
    CHECK(layout.find("DockId=0xB4F4E0DE,0") != std::string::npos);
    CHECK(layout.find("DockId=0x731539FF,0") != std::string::npos);
    CHECK(layout.find("Window=0x731539FF") == std::string::npos);
    CHECK(layout.find("Selected=0x731539FF") == std::string::npos);
    CHECK(layout.find("ID=0xCD6C9E75") == std::string::npos);
    CHECK(layout.find("Parent=0xCD6C9E75") == std::string::npos);
    CHECK(layout.find("DockId=0xCD6C9E75") == std::string::npos);
    CHECK_FALSE(settings_migration::migrateLayoutIniHashes(layout));
#else
    CHECK_FALSE(settings_migration::migrateLayoutIniHashes(layout));
    CHECK(layout.find("Window=0x731539FF") != std::string::npos);
    CHECK(layout.find("Selected=0x731539FF") != std::string::npos);
    CHECK(layout.find("DockSpace ID=0xCD6C9E75") != std::string::npos);
    CHECK(layout.find("Parent=0xCD6C9E75") != std::string::npos);
    CHECK(layout.find("DockId=0xCD6C9E75,0") != std::string::npos);
#endif
}

TEST_CASE("ImGui layout migration leaves layouts without triple-hash windows untouched") {
    std::string layout =
      "[Window][Scalars]\n"
      "Pos=1,2\n"
      "\n"
      "[Docking][Data]\n"
      "DockSpace ID=0x00000001 Window=0x731539FF Pos=0,0 Size=100,100 Selected=0x731539FF\n";
    std::string original = layout;

    CHECK_FALSE(settings_migration::migrateLayoutIniHashes(layout));
    CHECK(layout == original);
}

TEST_CASE("Version zero settings migrate scalar placements once") {
    nlohmann::json settings = {
        {"scalar_plots", {
            {"1", {{"id", 1}, {"name", "Old"}, {"signals", {7, 7, 99}}, {"autofit_y", false}, {"y_min", -3}}},
            {"2", {{"id", 2}, {"subplots", nlohmann::json::array({{{"signals", {{"named", 8}, {"missing", 99}}}}})}}}
        }},
        {"custom_windows", {{"3", {{"signals", {7, 99}}}}}},
        {"layout", "unchanged"}
    };
    REQUIRE(settings_migration::migrate(settings));
    REQUIRE(settings["format_version"] == 1);
    auto const& plot = settings["scalar_plots"]["1"];
    REQUIRE_FALSE(plot.contains("signals"));
    REQUIRE(plot["subplots"][0]["signals"] == nlohmann::json({{"7", 7}, {"99", 99}}));
    REQUIRE(plot["subplots"][0]["autofit_y"] == false);
    REQUIRE(plot["subplots"][0]["y_min"] == -3);
    REQUIRE(settings["scalar_plots"]["2"]["subplots"][0]["signals"]["missing"] == 99);
    REQUIRE(settings["custom_windows"]["3"]["signals"]["99"] == 99);
    REQUIRE(settings["layout"] == "unchanged");
    auto const migrated = settings;
    REQUIRE_FALSE(settings_migration::migrate(settings));
    REQUIRE(settings == migrated);
}

TEST_CASE("Old vector windows migrate to component pairs without live symbols") {
    auto const x_id = signalId("x", "group");
    auto const y_id = signalId("y", "group");
    nlohmann::json settings = {
        {"vector_symbols", {{"x (group)", {{"name", "x"}, {"group", "group"}, {"x", "x"}, {"y", "y"}}}}},
        {"vector_plots", {{"12", {
            {"id", 12}, {"name", "Vectors"}, {"initial_focus", true}, {"time_range", 0.05},
            {"signals", {{"x", x_id}, {"from scalars", signalId("x+y", "group")}}}
        }}}}
    };
    REQUIRE(settings_migration::migrate(settings));
    auto const& plot = settings["scalar_plots"]["12"];
    REQUIRE(plot["id"] == 12);
    REQUIRE(plot["name"] == "Vectors");
    REQUIRE(plot["initial_focus"] == true);
    REQUIRE(plot["vector_time_range"] == 0.05);
    REQUIRE(plot["rows"] == 1);
    REQUIRE(plot["cols"] == 1);
    REQUIRE(plot["subplots"][0]["type"] == nlohmann::json(PlotType::Vector));
    REQUIRE(plot["subplots"][0]["autofit_y"] == false);
    REQUIRE(plot["subplots"][0]["vectors"] == nlohmann::json::array({{x_id, y_id}}));
    REQUIRE(settings["scalar_symbols"]["x (group)"]["name"] == "x");
    REQUIRE(settings["scalar_symbols"]["y (group)"]["group"] == "group");
    REQUIRE_FALSE(settings.contains("vector_symbols"));
    REQUIRE_FALSE(settings.contains("vector_plots"));

    PlotWindow window(plot);
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(x_id));
    REQUIRE(plot_settings::restoreVectors(window.subplots[0], plot["subplots"][0], scalars).empty());
    plot_settings::reconcileVectors(window.subplots[0], settings["scalar_plots"]["12"]["subplots"][0], scalars);
    scalars.push_back(makeSettingsScalar(y_id));
    REQUIRE(plot_settings::restoreVectors(window.subplots[0], plot["subplots"][0], scalars, y_id).size() == 2);
    REQUIRE(window.subplots[0].vectors.size() == 1);
}

TEST_CASE("Old spectrum windows migrate axes and spectrum options") {
    nlohmann::json settings = {{"spec_plots", {{"4", {
        {"id", 4}, {"name", "Spectrum"}, {"signals", {{7, 0}, {7, 99}}},
        {"time_range", 0.5}, {"window", SpectrumWindow::Hann}, {"logarithmic_y_axis", true},
        {"x_axis_min", 10}, {"x_axis_max", 100}, {"y_axis_min", 0.01}, {"y_axis_max", 10}
    }}}}};
    REQUIRE(settings_migration::migrate(settings));
    auto const& plot = settings["scalar_plots"]["4"];
    PlotWindow window(plot);
    REQUIRE(window.subplots[0].type == PlotType::Spectrum);
    REQUIRE(window.spectrum.time_range == 0.5);
    REQUIRE(window.spectrum.window == SpectrumWindow::Hann);
    REQUIRE(window.spectrum.logarithmic_y_axis);
    REQUIRE(window.subplots[0].spectrum_x_axis.min == 10);
    REQUIRE(window.subplots[0].spectrum_y_axis.min == 0.01);
    REQUIRE(plot["subplots"][0]["spectrums"] == nlohmann::json::array({{7, 0}, {7, 99}}));
    REQUIRE_FALSE(settings.contains("spec_plots"));
}

TEST_CASE("Migration rejects conflicting window IDs and unsupported versions") {
    nlohmann::json settings;
    SECTION("Conflicting window IDs") {
        settings["scalar_plots"]["1"] = {{"id", 1}, {"signals", nlohmann::json::object()}};
        settings["vector_plots"]["1"] = {{"id", 1}, {"signals", nlohmann::json::object()}};
    }
    SECTION("Unsupported future version") {
        settings["format_version"] = 2;
    }
    REQUIRE_THROWS_AS(settings_migration::migrate(settings), std::runtime_error);
    REQUIRE(settings.value("format_version", 0) != 1);
}

TEST_CASE("Missing vector definitions do not prevent settings migration") {
    auto const x_id = signalId("x", "group");
    auto const y_id = signalId("y", "group");
    nlohmann::json settings = {
        {"vector_symbols", {{"x (group)", {{"x", "x"}, {"y", "y"}, {"group", "group"}}}}},
        {"vector_plots", {
            {"1", {{"id", 1}, {"signals", {123, x_id}}}},
            {"2", {{"id", 2}, {"signals", {123}}}}
        }},
        {"scalar_plots", {{"3", {{"id", 3}, {"signals", {{"pending", 99}}}}}}}
    };
    REQUIRE(settings_migration::migrate(settings));
    REQUIRE(settings["format_version"] == 1);
    REQUIRE(settings["scalar_plots"]["1"]["subplots"][0]["vectors"] == nlohmann::json::array({{x_id, y_id}}));
    REQUIRE(settings["scalar_plots"]["2"]["subplots"][0]["vectors"].empty());
    REQUIRE(settings["scalar_plots"]["3"]["subplots"][0]["signals"]["pending"] == 99);
}

TEST_CASE("Malformed vector entries do not prevent valid settings from migrating") {
    auto const x_id = signalId("x", "group");
    auto const y_id = signalId("y", "group");
    nlohmann::json settings;
    settings["vector_symbols"] = {
        {"null", nullptr},
        {"primitive", "bad"},
        {"missing x", {{"y", "y"}}},
        {"missing y", {{"x", "x"}}},
        {"bad x", {{"x", 1}, {"y", "y"}}},
        {"bad y", {{"x", "x"}, {"y", false}}},
        {"bad group", {{"x", "x"}, {"y", "y"}, {"group", 1}}},
        {"bad name", {{"x", "x"}, {"y", "y"}, {"name", nullptr}}},
        {"valid", {{"x", "x"}, {"y", "y"}, {"group", "group"}}}
    };
    settings["vector_plots"]["1"] = {
        {"id", 1}, {"signals", nlohmann::json::array({"bad", nullptr, false, 1.5, -1, nlohmann::json::object(), nlohmann::json::array({1, 2}), x_id})}
    };
    settings["scalar_plots"]["2"] = {{"id", 2}, {"signals", {{"pending", 99}}}};

    REQUIRE(settings_migration::migrate(settings));
    REQUIRE(settings["format_version"] == 1);
    REQUIRE(settings["scalar_plots"]["1"]["subplots"][0]["vectors"] == nlohmann::json::array({{x_id, y_id}}));
    REQUIRE(settings["scalar_plots"]["2"]["subplots"][0]["signals"]["pending"] == 99);
    REQUIRE(settings["scalar_symbols"].size() == 2);
}
