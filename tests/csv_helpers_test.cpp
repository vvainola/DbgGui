#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "csv_plot/csv_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

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

TEST_CASE("Decimation clamps requested point count when decimating") {
    std::vector<double> x(MIN_PLOT_SAMPLE_COUNT + 2);
    std::iota(x.begin(), x.end(), 0.0);
    std::vector<double> y = x;

    DecimatedValues min_clamped = decimateValues(x, y, 1);
    CHECK(min_clamped.x.size() == MIN_PLOT_SAMPLE_COUNT);

    DecimatedValues zero_clamped = decimateValues(x, y, 0);
    CHECK(zero_clamped.x.size() == MIN_PLOT_SAMPLE_COUNT);

    std::vector<double> large_x(MAX_PLOT_SAMPLE_COUNT + 512);
    std::iota(large_x.begin(), large_x.end(), 0.0);
    std::vector<double> large_y = large_x;

    DecimatedValues max_clamped = decimateValues(large_x, large_y, MAX_PLOT_SAMPLE_COUNT + 256);
    CHECK(max_clamped.x.size() == MAX_PLOT_SAMPLE_COUNT);
}

TEST_CASE("Decimation keeps raw samples when under the point budget") {
    std::vector<double> x = {0, 1, 2};
    std::vector<double> y = {10, 20, 30};

    DecimatedValues values = decimateValues(x, y, 10);

    CHECK(values.x == x);
    CHECK(values.y_min == y);
    CHECK(values.y_max == y);
}

TEST_CASE("Decimation keeps all samples for full sample requests") {
    std::vector<double> x(MAX_PLOT_SAMPLE_COUNT + 512);
    std::iota(x.begin(), x.end(), 0.0);
    std::vector<double> y = x;

    DecimatedValues values = decimateValues(x, y, ALL_SAMPLES);

    CHECK(values.x.size() == x.size());
    CHECK(values.y_min == y);
    CHECK(values.y_max == y);
}

TEST_CASE("Decimation preserves extrema inside each bucket") {
    std::vector<double> x(MIN_PLOT_SAMPLE_COUNT * 2);
    std::iota(x.begin(), x.end(), 0.0);
    std::vector<double> y(x.size(), 0);
    y[3] = 100;
    y[200] = -50;

    DecimatedValues values = decimateValues(x, y, 2);

    REQUIRE(values.x.size() == MIN_PLOT_SAMPLE_COUNT);
    CHECK(*std::max_element(values.y_max.begin(), values.y_max.end()) == Approx(100));
    CHECK(*std::min_element(values.y_min.begin(), values.y_min.end()) == Approx(-50));
}

TEST_CASE("Decimation applies x and y transforms after min max aggregation") {
    std::vector<double> x(MIN_PLOT_SAMPLE_COUNT * 2);
    std::iota(x.begin(), x.end(), 0.0);
    std::vector<double> y(x.size(), 0);
    y[0] = -1;
    y[1] = 5;

    DecimatedValues values = decimateValues(x,
                                            y,
                                            MIN_PLOT_SAMPLE_COUNT,
                                            {.x_offset = 10, .y_scale = -2, .y_offset = 1});

    REQUIRE(values.x.size() == MIN_PLOT_SAMPLE_COUNT);
    CHECK(values.x[0] == Approx(10.5));
    CHECK(values.y_min[0] == Approx(-9));
    CHECK(values.y_max[0] == Approx(3));
}

TEST_CASE("Decimation ignores NaNs unless the whole bucket is NaN") {
    double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> x(MIN_PLOT_SAMPLE_COUNT * 2);
    std::iota(x.begin(), x.end(), 0.0);
    std::vector<double> y(x.size(), 0);
    y[0] = nan;
    y[1] = 2;
    y[2] = nan;
    y[3] = nan;

    DecimatedValues values = decimateValues(x, y, MIN_PLOT_SAMPLE_COUNT);

    REQUIRE(values.y_min.size() == MIN_PLOT_SAMPLE_COUNT);
    CHECK(values.y_min[0] == Approx(2));
    CHECK(values.y_max[0] == Approx(2));
    CHECK(std::isnan(values.y_min[1]));
    CHECK(std::isnan(values.y_max[1]));
}
