#include <catch2/catch_test_macros.hpp>

#include "scrolling_buffer.h"

TEST_CASE("Scrolling buffer retains chronological samples across wrapping and resizing") {
    ScrollingBuffer buffer(4);
    Scalar scalar{};
    scalar.src = ReadWriteFn([](std::optional<double>) { return 7.0; });
    buffer.startSampling(&scalar);
    REQUIRE(buffer.getTimeIndices(0, 100) == std::pair<int32_t, int32_t>{-1, -1});
    for (int i = 0; i < 10; ++i) {
        buffer.sample(i);
    }
    buffer.emptyTempBuffers();
    auto range = buffer.getTimeIndices(0, 100);
    REQUIRE(buffer.getTimeInRange(range) == std::vector<double>{6, 7, 8, 9});
    REQUIRE(buffer.getSamplesInRange(&scalar, range) == std::vector<double>{7, 7, 7, 7});
    REQUIRE(buffer.getValuesInRange(&scalar, range, ALL_SAMPLES).x == buffer.getTimeInRange(range));

    buffer.setCapacity(2);
    REQUIRE(buffer.getTimeInRange(buffer.getTimeIndices(0, 100)) == std::vector<double>{8, 9});
    buffer.setCapacity(8);
    buffer.sample(10);
    buffer.emptyTempBuffers();
    REQUIRE(buffer.getTimeInRange(buffer.getTimeIndices(0, 100)) == std::vector<double>{8, 9, 10});
    buffer.shiftTime(10);
    REQUIRE(buffer.getTimeInRange(buffer.getTimeIndices(19, 20)) == std::vector<double>{19, 20});
}

TEST_CASE("Scrolling buffer fills missing history for newly sampled channels") {
    ScrollingBuffer buffer(3);
    buffer.sample(0);
    buffer.emptyTempBuffers();
    buffer.sample(1);
    Scalar scalar{};
    scalar.src = ReadWriteFn([](std::optional<double>) { return 5.0; });
    buffer.startSampling(&scalar);
    buffer.sample(2);
    buffer.emptyTempBuffers();
    auto samples = buffer.getSamplesInRange(&scalar, buffer.getTimeIndices(0, 100));
    REQUIRE(samples.size() == 3);
    REQUIRE(std::isnan(samples[0]));
    REQUIRE(std::isnan(samples[1]));
    REQUIRE(samples[2] == 5);
}

TEST_CASE("Circular decimation matches contiguous decimation across bucket boundaries") {
    std::vector<double> time(500), values(500);
    std::iota(time.begin(), time.end(), 0.0);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = std::sin(double(i));
    }
    auto expected = decimateValues(time, values, 30, 490, 128, {.y_scale = -2, .y_offset = 3});
    std::rotate(time.begin(), time.begin() + 173, time.end());
    std::rotate(values.begin(), values.begin() + 173, values.end());
    auto actual = decimateCircularValues(time, values, 30, 490, 327, 128, {.y_scale = -2, .y_offset = 3});
    REQUIRE(actual.x == expected.x);
    REQUIRE(actual.y_min == expected.y_min);
    REQUIRE(actual.y_max == expected.y_max);
}

TEST_CASE("Scrolling buffer copies partially sampled vectors without creating source channels") {
    for (bool sample_x : {false, true}) {
        ScrollingBuffer buffer(3);
        Scalar source_x{}, source_y{}, target_x{}, target_y{};
        for (Scalar* scalar : {&source_x, &source_y, &target_x, &target_y}) {
            scalar->src = ReadWriteFn([](std::optional<double>) { return 7.0; });
        }
        Vector2D source{}, target{};
        source.x = &source_x;
        source.y = &source_y;
        target.x = &target_x;
        target.y = &target_y;
        Scalar* sampled_source = sample_x ? source.x : source.y;
        Scalar* missing_source = sample_x ? source.y : source.x;
        Scalar* sampled_target = sample_x ? target.x : target.y;
        Scalar* missing_target = sample_x ? target.y : target.x;

        buffer.startSampling(sampled_source);
        for (int i = 0; i < 5; ++i) {
            buffer.sample(i);
        }
        buffer.emptyTempBuffers();
        buffer.sample(5); // Copy pending samples as well as wrapped history.
        buffer.startSampling(&target);
        buffer.copySamples(source, target);
        REQUIRE_NOTHROW(buffer.emptyTempBuffers());
        REQUIRE_FALSE(buffer.isScalarSampled(missing_source));
        auto range = buffer.getTimeIndices(0, 100);
        REQUIRE(buffer.getTimeInRange(range) == std::vector<double>{3, 4, 5});
        REQUIRE(buffer.getSamplesInRange(sampled_target, range) == std::vector<double>{7, 7, 7});
        auto missing = buffer.getSamplesInRange(missing_target, range);
        REQUIRE(missing.size() == 3);
        REQUIRE(std::all_of(missing.begin(), missing.end(), [](double value) { return std::isnan(value); }));
        REQUIRE_NOTHROW(buffer.emptyTempBuffers()); // Paused frame with no pending samples.

        buffer.stopSampling(sampled_source);
        buffer.sample(6);
        buffer.emptyTempBuffers();
        range = buffer.getTimeIndices(0, 100);
        REQUIRE(buffer.getSamplesInRange(sampled_target, range) == std::vector<double>{7, 7, 7});
        REQUIRE(buffer.getSamplesInRange(missing_target, range).back() == 7);
    }
}
