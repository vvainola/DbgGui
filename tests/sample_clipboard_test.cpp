// MIT License
//
// Copyright (c) 2026 vvainola
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <catch2/catch_test_macros.hpp>

#include "sample_clipboard.h"

#include <cmath>
#include <limits>

TEST_CASE("Sample clipboard native format round-trips columns") {
    SampleClipboardData samples;
    samples.header = {"time0", "time", "signal (group)"};
    samples.data = {
      {0.0, 0.1, 0.2},
      {10.0, 10.1, 10.2},
      {1.0, std::numeric_limits<double>::quiet_NaN(), -2.5},
    };

    if (!copySamplesToClipboard(samples)) {
        SKIP("Native sample clipboard is not available");
    }

    REQUIRE(hasSampleClipboardData());
    std::expected<SampleClipboardData, std::string> parsed = readSamplesFromClipboard();

    REQUIRE(parsed.has_value());
    CHECK(parsed->header == samples.header);
    REQUIRE(parsed->data.size() == samples.data.size());
    CHECK(parsed->data[0] == samples.data[0]);
    CHECK(parsed->data[1] == samples.data[1]);
    REQUIRE(parsed->data[2].size() == samples.data[2].size());
    CHECK(parsed->data[2][0] == samples.data[2][0]);
    CHECK(std::isnan(parsed->data[2][1]));
    CHECK(parsed->data[2][2] == samples.data[2][2]);
}

TEST_CASE("Sample clipboard rejects empty data") {
    SampleClipboardData samples;

    CHECK_FALSE(copySamplesToClipboard(samples));
}

TEST_CASE("Sample clipboard rejects non-rectangular data") {
    SampleClipboardData samples;
    samples.header = {"time", "signal"};
    samples.data = {{0.0, 0.1}, {1.0}};

    CHECK_FALSE(copySamplesToClipboard(samples));
}

TEST_CASE("Sample clipboard rejects header and data size mismatch") {
    SampleClipboardData samples;
    samples.header = {"time"};
    samples.data = {{0.0, 0.1}, {1.0, 2.0}};

    CHECK_FALSE(copySamplesToClipboard(samples));
}
