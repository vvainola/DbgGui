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

#include "csv_plot/csv_lua_signal.h"

#include <string>
#include <vector>

TEST_CASE("CSV Lua signals read samples using relative indices") {
    std::vector<double> samples = {10, 20, 30};
    std::vector<CsvLuaSignalInput> inputs = {
      {.name = "input", .samples = samples},
    };

    auto result = evaluateCsvLuaSignal(
        "return read('input', -1) + read('input', 0) + read('input', 1)",
        "output",
        inputs,
        samples.size());

    REQUIRE(result);
    CHECK(*result == std::vector<double>{30, 60, 50});
}

TEST_CASE("CSV Lua signal reads default to the current sample") {
    std::vector<double> samples = {10, 20, 30};
    std::vector<CsvLuaSignalInput> inputs = {
      {.name = "input", .samples = samples},
    };

    auto result = evaluateCsvLuaSignal("return read('input')", "output", inputs, samples.size());

    REQUIRE(result);
    CHECK(*result == samples);
}

TEST_CASE("CSV Lua signals can read previously generated output samples") {
    std::vector<double> samples = {1, 2, 3};
    std::vector<CsvLuaSignalInput> inputs = {
      {.name = "input", .samples = samples},
    };

    auto result = evaluateCsvLuaSignal(
        "return read('filtered', -1) + read('input', 0)",
        "filtered",
        inputs,
        samples.size());

    REQUIRE(result);
    CHECK(*result == std::vector<double>{1, 3, 6});
}

TEST_CASE("CSV Lua signal reads of the current output sample return zero") {
    auto result = evaluateCsvLuaSignal("return read('custom', 0) + 1", "custom", {}, 3);

    REQUIRE(result);
    CHECK(*result == std::vector<double>{1, 1, 1});
}

TEST_CASE("CSV Lua signal errors identify the failing sample") {
    std::vector<double> samples = {1};
    std::vector<CsvLuaSignalInput> inputs = {
      {.name = "input", .samples = samples},
    };

    auto unknown = evaluateCsvLuaSignal("return read('missing', 0)", "output", inputs, samples.size());
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().find("Sample 0:") != std::string::npos);
    CHECK(unknown.error().find("unknown signal 'missing'") != std::string::npos);

    auto non_number = evaluateCsvLuaSignal("return 'text'", "output", inputs, samples.size());
    REQUIRE_FALSE(non_number);
    CHECK(non_number.error().find("expected a number, got string") != std::string::npos);
}

TEST_CASE("CSV Lua signals stop scripts that exceed the instruction limit") {
    auto result = evaluateCsvLuaSignal("while true do end", "output", {}, 1);

    REQUIRE_FALSE(result);
    CHECK(result.error().find("instruction limit exceeded") != std::string::npos);
}
