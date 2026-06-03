#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "csv_plot/csv_helpers.h"

#include <cmath>

using Approx = Catch::Approx;

TEST_CASE("Stair values expand lagging stairs") {
    std::vector<double> x = {0, 1, 2};
    std::vector<double> y = {10, 20, 30};

    StairValues stairs = makeStairValues(x, y, CsvPlotStyle::LaggingStairs);

    CHECK(stairs.x == std::vector<double>{0, 1, 1, 2, 2});
    CHECK(stairs.y == std::vector<double>{10, 10, 20, 20, 30});
}

TEST_CASE("Stair values expand leading stairs") {
    std::vector<double> x = {0, 1, 2};
    std::vector<double> y = {10, 20, 30};

    StairValues stairs = makeStairValues(x, y, CsvPlotStyle::LeadingStairs);

    CHECK(stairs.x == std::vector<double>{0, 0, 1, 1, 2});
    CHECK(stairs.y == std::vector<double>{10, 20, 20, 30, 30});
}

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
