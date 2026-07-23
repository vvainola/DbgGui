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

#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>

struct lua_State;
struct lua_Debug;

enum class LuaSymbolAccess {
    Read,
    Write,
};

struct LuaScriptHost {
    std::function<std::string(std::string_view, std::string_view)> add_scalar;
    std::function<bool(std::string_view)> exists;
    std::function<std::expected<double, std::string>(std::string_view)> read;
    std::function<std::expected<void, std::string>(std::string_view, double)> write;
    std::function<std::expected<void, std::string>(std::string_view, LuaSymbolAccess)> validate_symbol;
    std::function<void()> pause;
    std::function<void(std::string)> save_csv;
};

class LuaScriptRunner {
  public:
    LuaScriptRunner(std::string source, LuaScriptHost host, std::string chunk_name = "Lua script");
    ~LuaScriptRunner();

    LuaScriptRunner(LuaScriptRunner const&) = delete;
    LuaScriptRunner& operator=(LuaScriptRunner const&) = delete;

    std::expected<void, std::string> start(double timestamp, bool loop);
    std::expected<void, std::string> process(double timestamp);
    void shiftSchedule(double time_offset);
    void stop();

    bool running() const;
    int currentLine() const;
    double startTime() const;

  private:
    std::expected<void, std::string> initialize(double timestamp);
    std::expected<void, std::string> validateLiteralSymbolTargets() const;
    std::expected<void, std::string> resume(double timestamp);
    std::expected<void, std::string> luaError();
    void updateCurrentLine(lua_State* state);

    static int read(lua_State* state);
    static int addScalar(lua_State* state);
    static int readUnchecked(lua_State* state);
    static int write(lua_State* state);
    static int writeUnchecked(lua_State* state);
    static int exists(lua_State* state);
    static int wait(lua_State* state);
    static int pause(lua_State* state);
    static int saveCsv(lua_State* state);
    static void instructionHook(lua_State* state, lua_Debug* debug);

    std::string m_source;
    std::string m_chunk_name;
    LuaScriptHost m_host;
    lua_State* m_state = nullptr;
    lua_State* m_thread = nullptr;
    int m_thread_ref = -1;
    double m_start_time = 0;
    double m_process_timestamp = 0;
    double m_resume_time = 0;
    double m_next_resume_time = 0;
    int m_current_line = 0;
    bool m_loop = false;
    bool m_running = false;
    bool m_waiting = false;
    // Used to reject looping scripts that would otherwise restart without
    // yielding to the sampling loop.
    bool m_has_waited = false;
};
