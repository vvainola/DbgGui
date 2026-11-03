#include <catch2/catch_test_macros.hpp>

#include "csv_plot/csvplot.h"

TEST_CASE("Changing CSV plot type discards the previous comparison selection") {
    PlotBase plot;
    plot.settings.scalar_signals = {"saved"};
    plot.comparison_settings.scalar_signals = {"missing from current file"};

    SECTION("Keeping the type retains the snapshot") {
        plot.setPlotType(CsvPlotType::Scalar);
        CHECK(plot.comparison_settings.scalar_signals == std::vector<std::string>{"missing from current file"});
        CHECK(plot.settings.scalar_signals == std::vector<std::string>{"saved"});
    }
    SECTION("A different type clears incompatible bindings") {
        plot.setPlotType(CsvPlotType::Vector);
        CHECK(plot.comparison_settings.scalar_signals.empty());
        CHECK(plot.comparison_settings.signal_pairs.empty());
        CHECK(plot.settings.scalar_signals.empty());
    }
}

TEST_CASE("Clearing CSV runtime signals preserves the comparison snapshot for file cycling") {
    CsvSignal real{};
    CsvSignal imag{};
    for (CsvPlotType type : {CsvPlotType::Scalar, CsvPlotType::Vector, CsvPlotType::Spectrum}) {
        PlotBase plot;
        plot.setPlotType(type);
        if (type == CsvPlotType::Scalar) {
            std::get<ScalarPlot>(plot.variant).addSignal(&real);
            plot.comparison_settings.scalar_signals = {"real", "missing"};
        } else if (type == CsvPlotType::Vector) {
            std::get<VectorPlot>(plot.variant).addSignal({&real, &imag});
            plot.comparison_settings.signal_pairs = {{"real", "imag"}, {"missing", "imag"}};
        } else {
            std::get<SpectrumPlot>(plot.variant).addSignal(&real, nullptr);
            plot.comparison_settings.signal_pairs = {{"real", ""}, {"missing", ""}};
        }
        CsvPlotSignalSettings const snapshot = plot.comparison_settings;

        plot.clearPlot();

        CHECK(plot.comparison_settings.scalar_signals == snapshot.scalar_signals);
        CHECK(plot.comparison_settings.signal_pairs == snapshot.signal_pairs);
        if (type == CsvPlotType::Scalar) {
            CHECK(std::get<ScalarPlot>(plot.variant).signals.empty());
        } else if (type == CsvPlotType::Vector) {
            CHECK(std::get<VectorPlot>(plot.variant).signals.empty());
        } else {
            CHECK(std::get<SpectrumPlot>(plot.variant).spectrum.empty());
        }
    }
}
