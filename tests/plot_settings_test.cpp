#include <catch2/catch_test_macros.hpp>

#include "plot_settings.h"

namespace {
std::unique_ptr<Scalar> makeSettingsScalar(uint64_t id) {
    auto scalar = std::make_unique<Scalar>();
    scalar->id = id;
    return scalar;
}
}

TEST_CASE("Plot pair restoration handles load and late registration without duplicates") {
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(1));
    scalars.push_back(makeSettingsScalar(2));
    nlohmann::json saved = {
        {"vectors", {{1, 2}, {1, 99}}},
        {"spectrums", {{1, 0}, {1, 2}, {1, 99}}},
        {"reference_frame", {1, 2}}
    };
    PlotWindow::Subplot subplot;
    auto sampled = plot_settings::restoreVectors(subplot, saved, scalars);
    auto spectrum_samples = plot_settings::restoreSpectra(subplot, saved, scalars);
    REQUIRE(sampled.size() == 2);
    REQUIRE(spectrum_samples.size() == 2);
    REQUIRE(subplot.vectors.size() == 1);
    REQUIRE(subplot.spectrums.size() == 2);
    REQUIRE(subplot.spectrums[0].imag == nullptr);
    REQUIRE(subplot.reference_frame_vector == subplot.vectors[0]);

    plot_settings::restoreVectors(subplot, saved, scalars, 2);
    plot_settings::restoreSpectra(subplot, saved, scalars, 2);
    REQUIRE(subplot.vectors.size() == 1);
    REQUIRE(subplot.spectrums.size() == 2);
    REQUIRE(plot_settings::restoreVectors(subplot, saved, scalars, 500).empty());
    REQUIRE(plot_settings::restoreSpectra(subplot, saved, scalars, 500).empty());

    scalars.push_back(makeSettingsScalar(99));
    sampled = plot_settings::restoreVectors(subplot, saved, scalars, 99);
    spectrum_samples = plot_settings::restoreSpectra(subplot, saved, scalars, 99);
    REQUIRE(sampled.size() == 2);
    REQUIRE(subplot.vectors.size() == 2);
    REQUIRE(subplot.spectrums.size() == 3);
    REQUIRE(subplot.spectrums.back().imag == scalars.back().get());
}

TEST_CASE("Plot settings reconciliation preserves unresolved pairs and recognizes deleted IDs") {
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(1));
    scalars.push_back(makeSettingsScalar(2));
    scalars[1]->deleted = true;
    nlohmann::json saved = {{1, 2}, {1, 99}, {99, 2}, {1, 0}, {1}, {"bad", 2}};
    PlotWindow::Subplot subplot;
    nlohmann::json saved_subplot = {{"vectors", saved}, {"spectrums", saved}, {"reference_frame", {1, 99}}};
    plot_settings::reconcileVectors(subplot, saved_subplot, scalars);
    plot_settings::reconcileSpectra(subplot, saved_subplot, scalars);
    REQUIRE(saved_subplot["vectors"] == nlohmann::json::array({{1, 99}, {99, 2}, {1, 0}}));
    REQUIRE(saved_subplot["spectrums"] == nlohmann::json::array({{1, 99}, {99, 2}}));
    REQUIRE(saved_subplot.contains("reference_frame"));
    saved_subplot["reference_frame"] = {1, 2};
    plot_settings::reconcileVectors(subplot, saved_subplot, scalars);
    REQUIRE_FALSE(saved_subplot.contains("reference_frame"));
    saved_subplot["vectors"] = nullptr;
    plot_settings::reconcileVectors(subplot, saved_subplot, scalars);
    REQUIRE(saved_subplot["vectors"].empty());
}

TEST_CASE("Plot settings restoration ignores malformed and deleted components") {
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(1));
    scalars.push_back(makeSettingsScalar(2));
    scalars[1]->deleted = true;
    nlohmann::json saved = {
        {"vectors", {{1, 2}, {1, 0}, {1}, {"bad", 2}}},
        {"spectrums", {{1, 2}, {1, 0}}}
    };
    PlotWindow::Subplot subplot;
    REQUIRE(plot_settings::restoreVectors(subplot, saved, scalars).empty());
    auto sampled = plot_settings::restoreSpectra(subplot, saved, scalars);
    REQUIRE(subplot.vectors.empty());
    REQUIRE(subplot.spectrums.size() == 1);
    REQUIRE(subplot.spectrums[0].imag == nullptr);
    REQUIRE(sampled.size() == 1);
    REQUIRE(sampled[0] == scalars[0].get());
}

TEST_CASE("Explicit vector and spectrum reconciliation writes runtime placements") {
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(1));
    scalars.push_back(makeSettingsScalar(2));
    VectorPair pair{scalars[0].get(), scalars[1].get()};
    PlotWindow::Subplot subplot;
    subplot.addVector(pair);
    subplot.reference_frame_vector = pair;
    subplot.addSpectrum(pair.x);
    subplot.addSpectrum(pair.x, pair.y);
    nlohmann::json saved;
    plot_settings::reconcileVectors(subplot, saved, scalars);
    plot_settings::reconcileSpectra(subplot, saved, scalars);
    REQUIRE(saved["vectors"] == nlohmann::json::array({{1, 2}}));
    REQUIRE(saved["reference_frame"] == nlohmann::json::array({1, 2}));
    REQUIRE(saved["spectrums"] == nlohmann::json::array({{1, 0}, {1, 2}}));
    subplot.removeVector(pair);
    subplot.removeSpectrum(pair.x);
    plot_settings::reconcileVectors(subplot, saved, scalars);
    plot_settings::reconcileSpectra(subplot, saved, scalars);
    REQUIRE(saved["vectors"].empty());
    REQUIRE_FALSE(saved.contains("reference_frame"));
    REQUIRE(saved["spectrums"] == nlohmann::json::array({{1, 2}}));
}

TEST_CASE("Scalar settings restore at load and on late registration") {
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(1));
    scalars.push_back(makeSettingsScalar(2));
    scalars[1]->deleted = true;
    nlohmann::json saved;
    SECTION("Named signal map") {
        saved["signals"] = {{"live", 1}, {"deleted", 2}, {"pending", 99}, {"invalid", "bad"}};
    }
    SECTION("Legacy signal array") {
        saved["signals"] = {1, 1, 2, 99, "bad"};
    }
    PlotWindow::Subplot subplot;
    auto sampled = plot_settings::restoreScalars(subplot, saved, scalars);
    REQUIRE(sampled.size() == 1);
    REQUIRE(subplot.scalars.size() == 1);
    REQUIRE(subplot.scalars[0] == scalars[0].get());
    REQUIRE(plot_settings::restoreScalars(subplot, saved, scalars, 500).empty());
    scalars.push_back(makeSettingsScalar(99));
    sampled = plot_settings::restoreScalars(subplot, saved, scalars, 99);
    REQUIRE(sampled.size() == 1);
    REQUIRE(sampled[0] == scalars.back().get());
    REQUIRE(subplot.scalars.size() == 2);
    plot_settings::restoreScalars(subplot, saved, scalars, 99);
    REQUIRE(subplot.scalars.size() == 2);
}

TEST_CASE("Scalar settings reconciliation rebuilds placements and keeps unresolved IDs") {
    std::vector<std::unique_ptr<Scalar>> scalars;
    scalars.push_back(makeSettingsScalar(1));
    scalars.push_back(makeSettingsScalar(2));
    scalars[0]->name_and_group = "old";
    scalars[0]->deleted = true;
    scalars[1]->name_and_group = "live";
    PlotWindow::Subplot subplot;
    subplot.addScalar(scalars[1].get());
    nlohmann::json saved = {{"signals", {{"old", 1}, {"removed", 2}, {"pending", 99}, {"invalid", "bad"}}}};
    plot_settings::reconcileScalars(subplot, saved, scalars);
    REQUIRE(saved["signals"] == nlohmann::json({{"live", 2}, {"pending", 99}}));
    subplot.removeScalar(scalars[1].get());
    plot_settings::reconcileScalars(subplot, saved, scalars);
    REQUIRE(saved["signals"] == nlohmann::json({{"pending", 99}}));
}
