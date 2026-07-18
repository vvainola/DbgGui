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

#include "lua_script.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

namespace {
inline constexpr int LuaInstructionLimit = 100000;

LuaScriptRunner* getRunner(lua_State* state) {
    return *static_cast<LuaScriptRunner**>(lua_getextraspace(state));
}

void openLibrary(lua_State* state, char const* name, lua_CFunction open) {
    luaL_requiref(state, name, open, 1);
    lua_pop(state, 1);
}

void addFunction(lua_State* state, char const* name, lua_CFunction function) {
    lua_pushcfunction(state, function);
    lua_setglobal(state, name);
}

std::string formatLuaError(std::string_view chunk_name, std::string_view error, int current_line) {
    size_t const source_end = error.find("]:");
    if (error.starts_with("[string \"") && source_end != std::string_view::npos) {
        return std::format("{}:{}", chunk_name, error.substr(source_end + 2));
    }
    if (error.starts_with(chunk_name) && error.size() > chunk_name.size() && error[chunk_name.size()] == ':') {
        return std::string(error);
    }
    return std::format("{}:{}: {}", chunk_name, current_line + 1, error);
}

struct LiteralSymbolTarget {
    std::string operation;
    std::string name;
    int line = 1;
};

std::vector<LiteralSymbolTarget> findLiteralSymbolTargets(std::string const& source) {
    static std::regex const SymbolOperationRegex(R"(^\s*(read|write)\s*\(\s*[\"']([^\"']+)[\"'])");

    std::vector<LiteralSymbolTarget> targets;
    std::istringstream lines(source);
    std::string line;
    for (int line_number = 1; std::getline(lines, line); ++line_number) {
        std::smatch match;
        if (std::regex_search(line, match, SymbolOperationRegex)) {
            targets.push_back(LiteralSymbolTarget{match[1].str(), match[2].str(), line_number});
        }
    }
    return targets;
}

} // namespace

LuaScriptRunner::LuaScriptRunner(std::string source, LuaScriptHost host, std::string chunk_name)
    : m_source(std::move(source)), m_chunk_name(std::move(chunk_name)), m_host(std::move(host)) {}

LuaScriptRunner::~LuaScriptRunner() {
    stop();
}

std::expected<void, std::string> LuaScriptRunner::start(double timestamp, bool loop) {
    m_loop = loop;
    return initialize(timestamp);
}

std::expected<void, std::string> LuaScriptRunner::initialize(double timestamp) {
    stop();
    m_start_time = timestamp;
    m_resume_time = timestamp;
    m_next_resume_time = timestamp;
    m_current_line = 0;
    m_waiting = false;
    m_has_waited = false;

    if (std::expected<void, std::string> result = validateLiteralSymbolTargets(); !result.has_value()) {
        return result;
    }

    m_state = luaL_newstate();
    if (m_state == nullptr) {
        return std::unexpected("Could not create Lua state");
    }

    openLibrary(m_state, LUA_GNAME, luaopen_base);
    openLibrary(m_state, LUA_MATHLIBNAME, luaopen_math);
    openLibrary(m_state, LUA_STRLIBNAME, luaopen_string);
    openLibrary(m_state, LUA_TABLIBNAME, luaopen_table);
    openLibrary(m_state, LUA_UTF8LIBNAME, luaopen_utf8);
    openLibrary(m_state, LUA_IOLIBNAME, luaopen_io);
    openLibrary(m_state, LUA_OSLIBNAME, luaopen_os);

    addFunction(m_state, "read", &LuaScriptRunner::read);
    addFunction(m_state, "read_u", &LuaScriptRunner::readUnchecked);
    addFunction(m_state, "write", &LuaScriptRunner::write);
    addFunction(m_state, "write_u", &LuaScriptRunner::writeUnchecked);
    addFunction(m_state, "exists", &LuaScriptRunner::exists);
    addFunction(m_state, "wait", &LuaScriptRunner::wait);
    addFunction(m_state, "pause", &LuaScriptRunner::pause);
    addFunction(m_state, "save_csv", &LuaScriptRunner::saveCsv);

    // Keep the coroutine alive in Lua's registry. lua_newthread leaves it on
    // the stack, where it could otherwise be garbage collected.
    m_thread = lua_newthread(m_state);
    m_thread_ref = luaL_ref(m_state, LUA_REGISTRYINDEX);
    *static_cast<LuaScriptRunner**>(lua_getextraspace(m_thread)) = this;
    lua_sethook(m_thread, &LuaScriptRunner::instructionHook, LUA_MASKCOUNT, LuaInstructionLimit);

    int const load_status = luaL_loadbufferx(m_thread, m_source.data(), m_source.size(), m_chunk_name.c_str(), "t");
    if (load_status != LUA_OK) {
        return luaError();
    }

    // The sampler owns execution. Arming the script here keeps UI actions from
    // writing values between two sampling instants. m_waiting makes the first
    // call to process() resume the newly created coroutine.
    m_running = true;
    m_waiting = true;
    return {};
}

std::expected<void, std::string> LuaScriptRunner::validateLiteralSymbolTargets() const {
    for (LiteralSymbolTarget const& target : findLiteralSymbolTargets(m_source)) {
        if (m_host.validate_symbol) {
            LuaSymbolAccess const access = target.operation == "read" ? LuaSymbolAccess::Read : LuaSymbolAccess::Write;
            if (std::expected<void, std::string> result = m_host.validate_symbol(target.name, access); !result.has_value()) {
                return std::unexpected(std::format("{}:{}: invalid {} target '{}': {}", m_chunk_name, target.line, target.operation, target.name, result.error()));
            }
        }
    }
    return {};
}

std::expected<void, std::string> LuaScriptRunner::process(double timestamp) {
    if (!m_running) {
        return {};
    }
    // A delayed sampling call may pass several scheduled wake-up times. Run
    // through those waits now so waits are measured on the sampling clock,
    // rather than being stretched by GUI frame timing.
    while (m_running && m_waiting && timestamp >= m_next_resume_time) {
        // wait(seconds) is relative to the previous scheduled wake-up, not
        // necessarily this process() call. This prevents timing drift.
        m_resume_time = m_next_resume_time;
        if (std::expected<void, std::string> result = resume(timestamp); !result.has_value()) {
            return result;
        }
    }
    return {};
}

void LuaScriptRunner::shiftSchedule(double time_offset) {
    // sampleWithTimestamp() can jump backwards when a snapshot restores the
    // simulation clock. Keep wait() deadlines in that same time frame, while
    // leaving m_start_time unchanged so the displayed script time follows the
    // restored simulation timestamp.
    m_resume_time += time_offset;
    m_next_resume_time += time_offset;
}

std::expected<void, std::string> LuaScriptRunner::resume(double timestamp) {
    m_waiting = false;
    // Lua preserves the count-hook budget across coroutine yields. Reinstall it
    // before every resume so the limit applies to one execution slice, not to
    // the lifetime of a well-behaved script that calls wait().
    lua_sethook(m_thread, &LuaScriptRunner::instructionHook, LUA_MASKCOUNT, LuaInstructionLimit);
    int result_count = 0;
    int const status = lua_resume(m_thread, nullptr, 0, &result_count);
    if (status == LUA_YIELD) {
        if (!m_waiting) {
            stop();
            return std::unexpected("Lua scripts must yield through wait(seconds)");
        }
        return {};
    }
    if (status != LUA_OK) {
        return luaError();
    }

    m_running = false;
    if (m_loop) {
        // Restart only scripts that yielded at least once. Otherwise a looped
        // script would repeatedly execute to completion in one sample.
        if (!m_has_waited) {
            stop();
            return std::unexpected("Looping Lua scripts must call wait(seconds)");
        }
        return initialize(timestamp);
    }
    return {};
}

std::expected<void, std::string> LuaScriptRunner::luaError() {
    char const* message = lua_tostring(m_thread, -1);
    std::string error = formatLuaError(m_chunk_name, message ? message : "Unknown Lua error", m_current_line);
    stop();
    return std::unexpected(error);
}

void LuaScriptRunner::stop() {
    if (m_state != nullptr) {
        lua_close(m_state);
    }
    m_state = nullptr;
    m_thread = nullptr;
    m_thread_ref = LUA_NOREF;
    m_running = false;
    m_waiting = false;
}

bool LuaScriptRunner::running() const {
    return m_running;
}

int LuaScriptRunner::currentLine() const {
    return m_current_line;
}

double LuaScriptRunner::startTime() const {
    return m_start_time;
}

int LuaScriptRunner::read(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    runner->updateCurrentLine(state);
    size_t name_length = 0;
    char const* name = luaL_checklstring(state, 1, &name_length);
    {
        std::expected<double, std::string> result = runner->m_host.read(std::string_view(name, name_length));
        if (result.has_value()) {
            lua_pushnumber(state, result.value());
            return 1;
        }
        lua_pushlstring(state, result.error().data(), result.error().size());
    }
    return lua_error(state);
}

int LuaScriptRunner::readUnchecked(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    size_t name_length = 0;
    char const* name = luaL_checklstring(state, 1, &name_length);
    if (!runner->m_host.exists(std::string_view(name, name_length))) {
        lua_pushnumber(state, 0);
        return 1;
    }
    return read(state);
}

int LuaScriptRunner::write(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    runner->updateCurrentLine(state);
    size_t name_length = 0;
    char const* name = luaL_checklstring(state, 1, &name_length);
    double const value = luaL_checknumber(state, 2);
    {
        std::expected<void, std::string> result = runner->m_host.write(std::string_view(name, name_length), value);
        if (result.has_value()) {
            return 0;
        }
        lua_pushlstring(state, result.error().data(), result.error().size());
    }
    return lua_error(state);
}

int LuaScriptRunner::writeUnchecked(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    size_t name_length = 0;
    char const* name = luaL_checklstring(state, 1, &name_length);
    if (!runner->m_host.exists(std::string_view(name, name_length))) {
        return 0;
    }
    return write(state);
}

int LuaScriptRunner::exists(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    size_t name_length = 0;
    char const* name = luaL_checklstring(state, 1, &name_length);
    lua_pushboolean(state, runner->m_host.exists(std::string_view(name, name_length)));
    return 1;
}

int LuaScriptRunner::wait(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    double const seconds = luaL_checknumber(state, 1);
    if (!std::isfinite(seconds) || seconds < 0) {
        lua_pushliteral(state, "wait(seconds) requires a non-negative finite number");
        return lua_error(state);
    }

    runner->updateCurrentLine(state);
    // Base the next wake-up on the scheduled resume time, rather than the
    // current wall/GUI time. This preserves the script's requested cadence.
    runner->m_next_resume_time = runner->m_resume_time + seconds;
    if (seconds == 0) {
        // A zero wait must still advance the deadline past this timestamp;
        // otherwise process() would resume it forever in one sample. nextafter
        // is used instead of a fixed epsilon because that epsilon can round
        // back to zero when simulation timestamps are large.
        runner->m_next_resume_time = std::nextafter(runner->m_resume_time, std::numeric_limits<double>::infinity());
    }
    runner->m_waiting = true;
    runner->m_has_waited = true;
    return lua_yield(state, 0);
}

int LuaScriptRunner::pause(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    runner->m_host.pause();
    return 0;
}

int LuaScriptRunner::saveCsv(lua_State* state) {
    LuaScriptRunner* runner = getRunner(state);
    size_t filename_length = 0;
    char const* filename = luaL_checklstring(state, 1, &filename_length);
    runner->m_host.save_csv(std::string(filename, filename_length));
    return 0;
}

void LuaScriptRunner::instructionHook(lua_State* state, lua_Debug*) {
    lua_pushliteral(state, "Lua script exceeded the 100000 instruction limit; call wait(seconds) to yield");
    lua_error(state);
}

void LuaScriptRunner::updateCurrentLine(lua_State* state) {
    lua_Debug debug{};
    if (lua_getstack(state, 1, &debug) != 0 && lua_getinfo(state, "l", &debug) != 0) {
        m_current_line = std::max(debug.currentline - 1, 0);
    }
}
