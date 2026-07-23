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

#include "lua_script.h"

#include <cmath>
#include <limits>
#include <vector>

namespace {
LuaScriptHost makeHost(std::vector<double>& writes) {
    LuaScriptHost host;
    host.add_scalar = [](std::string_view name, std::string_view) {
        return std::string(name);
    };
    host.exists = [](std::string_view name) {
        return name == "source" || name == "target";
    };
    host.read = [](std::string_view name) -> std::expected<double, std::string> {
        if (name == "source") {
            return 2.0;
        }
        return std::unexpected("unknown name");
    };
    host.validate_symbol = [](std::string_view name, LuaSymbolAccess access) -> std::expected<void, std::string> {
        if (access == LuaSymbolAccess::Read && name == "source") {
            return {};
        }
        if (access == LuaSymbolAccess::Write && name == "target") {
            return {};
        }
        return std::unexpected(access == LuaSymbolAccess::Write ? "unknown target" : "unknown name");
    };
    host.write = [&writes](std::string_view name, double value) -> std::expected<void, std::string> {
        if (name != "target") {
            return std::unexpected("unknown target");
        }
        writes.push_back(value);
        return {};
    };
    host.pause = [] {};
    host.save_csv = [](std::string) {};
    return host;
}
} // namespace

TEST_CASE("Lua scripts execute on the sampling clock and resume after wait") {
    std::vector<double> writes;
    LuaScriptRunner runner("write('target', read('source') + 1)\nwait(1)\nwrite('target', 5)", makeHost(writes));

    REQUIRE(runner.start(10.0, false));
    REQUIRE(runner.running());
    REQUIRE(writes.empty());
    REQUIRE(runner.process(10.0));
    REQUIRE(writes == std::vector<double>{3.0});
    CHECK(runner.currentLine() == 1);

    REQUIRE(runner.process(10.5));
    CHECK(writes == std::vector<double>{3.0});
    REQUIRE(runner.process(11.0));
    CHECK(writes == std::vector<double>{3.0, 5.0});
    CHECK_FALSE(runner.running());
}

TEST_CASE("Lua unchecked symbol access ignores missing names") {
    std::vector<double> writes;
    LuaScriptRunner runner("local lua_only = 1\n"
                           "write('target', exists('source') and 1 or 0)\n"
                           "write('target', exists('missing') and 1 or 0)\n"
                           "write('target', exists('lua_only') and 1 or 0)\n"
                           "write_u('missing', 123)\n"
                           "write('target', read_u('missing'))",
                           makeHost(writes));

    REQUIRE(runner.start(0.0, false));
    REQUIRE(runner.process(0.0));
    CHECK(writes == std::vector<double>{1.0, 0.0, 0.0, 0.0});
}

TEST_CASE("Lua scripts rebase waits after a backwards sampling timestamp jump") {
    std::vector<double> writes;
    LuaScriptRunner runner("write('target', 1)\nwait(1)\nwrite('target', 2)", makeHost(writes));

    REQUIRE(runner.start(10.0, false));
    REQUIRE(runner.process(10.0));
    REQUIRE(writes == std::vector<double>{1.0});

    runner.shiftSchedule(-10.0);
    REQUIRE(runner.process(0.5));
    CHECK(writes == std::vector<double>{1.0});
    REQUIRE(runner.process(1.0));
    CHECK(writes == std::vector<double>{1.0, 2.0});
}

TEST_CASE("Lua wait zero defers execution until the next sampling timestamp") {
    std::vector<double> writes;
    LuaScriptRunner runner("write('target', 1)\nwait(0)\nwrite('target', 2)", makeHost(writes));

    REQUIRE(runner.start(1.0, false));
    REQUIRE(runner.process(1.0));
    CHECK(writes == std::vector<double>{1.0});
    REQUIRE(runner.process(std::nextafter(1.0, std::numeric_limits<double>::infinity())));
    CHECK(writes == std::vector<double>{1.0, 2.0});
}

TEST_CASE("Lua wait zero yields once per sampling timestamp") {
    std::vector<double> writes;
    LuaScriptRunner runner("while true do\n    write('target', 1)\n    wait(0)\nend", makeHost(writes));

    REQUIRE(runner.start(0.0, false));
    REQUIRE(runner.process(0.0));
    REQUIRE(runner.process(1.0));
    REQUIRE(runner.process(2.0));
    CHECK(writes == std::vector<double>{1.0, 1.0, 1.0});
}

TEST_CASE("Lua loop restarts from a fresh state after waiting") {
    std::vector<double> writes;
    LuaScriptRunner runner("write('target', 1)\nwait(1)", makeHost(writes));

    REQUIRE(runner.start(0.0, true));
    REQUIRE(runner.process(1.0));
    CHECK(writes == std::vector<double>{1.0, 1.0});
    CHECK(runner.running());
}

TEST_CASE("Lua while loops can yield repeatedly without exhausting their instruction budget") {
    std::vector<double> writes;
    LuaScriptRunner runner("while true do\n    write('target', 1)\n    wait(0.01)\nend", makeHost(writes));

    REQUIRE(runner.start(0.0, false));
    for (int i = 1; i <= 10001; ++i) {
        REQUIRE(runner.process(i * 0.01));
    }
    CHECK(runner.running());
    CHECK(writes.size() >= 10000);
}

TEST_CASE("Lua scripts expose trusted libraries and reject runaway loops") {
    std::vector<double> writes;
    LuaScriptRunner sandbox_runner("if os == nil or io == nil or package ~= nil or debug ~= nil then error('unexpected library configuration') end\n"
                                 "local script = assert(load(\"write('target', 1)\"))\n"
                                 "script()\n"
                                 "wait(1)",
                                 makeHost(writes));
    REQUIRE(sandbox_runner.start(0.0, false));
    REQUIRE(sandbox_runner.process(0.0));
    CHECK(sandbox_runner.running());
    CHECK(writes == std::vector<double>{1.0});

    LuaScriptRunner runaway_runner("while true do end", makeHost(writes));
    REQUIRE(runaway_runner.start(0.0, false));
    std::expected<void, std::string> result = runaway_runner.process(0.0);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("instruction limit") != std::string::npos);
}

TEST_CASE("Lua validates literal write targets before arming the script") {
    std::vector<double> writes;
    LuaScriptRunner runner("write('missing', 1)", makeHost(writes), "Sine generator");

    std::expected<void, std::string> result = runner.start(0.0, false);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().starts_with("Sine generator:1:"));
    CHECK(result.error().find("unknown target") != std::string::npos);
}

TEST_CASE("Lua validates dynamic write targets at runtime") {
    std::vector<double> writes;
    LuaScriptRunner runner("local target_name = 'missing'\nwrite(target_name, 1)", makeHost(writes));

    REQUIRE(runner.start(0.0, false));
    std::expected<void, std::string> result = runner.process(0.0);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().starts_with("Lua script:2:"));
    CHECK(result.error().find("unknown target") != std::string::npos);
}

TEST_CASE("Lua validates literal read targets before arming the script") {
    std::vector<double> writes;
    LuaScriptRunner runner("read('missing')", makeHost(writes));

    std::expected<void, std::string> result = runner.start(0.0, false);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("invalid read target 'missing'") != std::string::npos);
}

TEST_CASE("Lua add_scalar returns its name and defaults the group") {
    std::vector<double> writes;
    std::vector<std::pair<std::string, std::string>> added_scalars;
    LuaScriptHost host = makeHost(writes);
    host.add_scalar = [&](std::string_view name, std::string_view group) {
        added_scalars.emplace_back(name, group);
        return std::string(name);
    };
    LuaScriptRunner runner("local first = add_scalar('hello1', 'testing')\n"
                           "local second = add_scalar('hello2')\n"
                           "write('target', first == 'hello1' and second == 'hello2' and 1 or 0)",
                           std::move(host));

    REQUIRE(runner.start(0.0, false));
    REQUIRE(runner.process(0.0));
    CHECK(added_scalars == std::vector<std::pair<std::string, std::string>>{
                             {"hello1", "testing"},
                             {"hello2", "Scripts"},
                           });
    CHECK(writes == std::vector<double>{1.0});
}
