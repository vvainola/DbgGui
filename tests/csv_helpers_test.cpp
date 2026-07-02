#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "csv_plot/csv_helpers.h"

#include <cmath>

using Approx = Catch::Approx;

TEST_CASE("Plot value lookup follows selected plot style") {
    std::vector<double> x = {0, 1, 2};
    std::vector<double> y = {10, 20, 30};

    SECTION("linear interpolation remains optional") {
        CHECK(getPlotValueAtX(CsvPlotStyle::Linear, x, y, 0.5, true) == Approx(15));
        CHECK(getPlotValueAtX(CsvPlotStyle::Linear, x, y, 0.5, false) == Approx(10));
    }

    SECTION("lagging stairs hold the previous value") {
        CHECK(getPlotValueAtX(CsvPlotStyle::LaggingStairs, x, y, 0.5, true) == Approx(10));
        CHECK(getPlotValueAtX(CsvPlotStyle::LaggingStairs, x, y, 1.0, true) == Approx(20));
    }

    SECTION("leading stairs hold the next value") {
        CHECK(getPlotValueAtX(CsvPlotStyle::LeadingStairs, x, y, 0.5, true) == Approx(20));
        CHECK(getPlotValueAtX(CsvPlotStyle::LeadingStairs, x, y, 1.0, true) == Approx(20));
    }

    SECTION("lookups clamp outside the sample range") {
        CHECK(getPlotValueAtX(CsvPlotStyle::LaggingStairs, x, y, -1.0, true) == Approx(10));
        CHECK(getPlotValueAtX(CsvPlotStyle::LeadingStairs, x, y, 3.0, true) == Approx(30));
    }
}

TEST_CASE("Plot value lookup returns NaN for empty inputs") {
    std::vector<double> x;
    std::vector<double> y;

    CHECK(std::isnan(getPlotValueAtX(CsvPlotStyle::Linear, x, y, 0, true)));
}

TEST_CASE("CSV column formatter writes columns") {
    std::vector<std::string> header = {"time0", "sig"};
    std::vector<std::vector<double>> data = {{0, 1}, {1.25, 2.5}};

    std::string csv = formatCsvColumns(header, data);

    CHECK(csv == "time0,sig,\n0,1.25,\n1,2.5,\n");
}

TEST_CASE("CSV signal names can be made unique") {
    std::vector<std::string> names = {"time", "sig", "sig", "other", "sig"};

    CHECK(makeUniqueCsvSignalNames(names) == std::vector<std::string>{"time", "sig#0", "sig#1", "other", "sig#2"});
}
