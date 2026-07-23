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

#include "data_structures.h"

TEST_CASE("Script windows persist their configured loop count") {
    ScriptWindow script(nullptr, nlohmann::json{{"name", "Script"}, {"id", 1}});
    script.loop_count = 5;

    nlohmann::json settings;
    script.updateJson(settings);

    CHECK(settings["loop_count"] == 5);
    CHECK_FALSE(settings.contains("loop"));

    ScriptWindow restored(nullptr, settings);
    CHECK(restored.loop_count == 5);
}

TEST_CASE("Script windows migrate the former loop checkbox") {
    nlohmann::json looping_settings = {
      {"name", "Looping"},
      {"id", 1},
      {"loop", true},
    };
    nlohmann::json single_run_settings = {
      {"name", "Once"},
      {"id", 2},
      {"loop", false},
    };

    ScriptWindow looping(nullptr, looping_settings);
    ScriptWindow single_run(nullptr, single_run_settings);

    CHECK(looping.loop_count == 0);
    CHECK(single_run.loop_count == 1);
}

TEST_CASE("Script windows clamp negative loop counts to infinite") {
    nlohmann::json settings = {
      {"name", "Infinite"},
      {"id", 1},
      {"loop_count", -5},
    };

    ScriptWindow script(nullptr, settings);
    CHECK(script.loop_count == 0);

    script.loop_count = -2;
    script.updateJson(settings);
    CHECK(settings["loop_count"] == 0);
}
