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

#include "csv_lua_signal.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <format>
#include <limits>
#include <unordered_map>

namespace {

inline constexpr int LuaInstructionLimit = 100000;

struct EvaluationContext {
    std::string_view output_name;
    std::unordered_map<std::string_view, std::span<double const>> const* inputs;
    std::vector<double>* output;
    size_t current_index = 0;
};

EvaluationContext* getContext(lua_State* state) {
    return *static_cast<EvaluationContext**>(lua_getextraspace(state));
}

int readSignal(lua_State* state) {
    EvaluationContext const& context = *getContext(state);
    size_t name_length = 0;
    char const* name_data = luaL_checklstring(state, 1, &name_length);
    std::string_view name{name_data, name_length};
    lua_Integer offset = luaL_optinteger(state, 2, 0);

    if (offset > 0 && size_t(offset) > std::numeric_limits<size_t>::max() - context.current_index) {
        lua_pushnumber(state, 0);
        return 1;
    }
    if (offset < 0 && size_t(-(offset + 1)) + 1 > context.current_index) {
        lua_pushnumber(state, 0);
        return 1;
    }
    size_t index = offset < 0 ? context.current_index - size_t(-(offset + 1)) - 1
                              : context.current_index + size_t(offset);

    if (name == context.output_name) {
        lua_pushnumber(state, index < context.output->size() ? (*context.output)[index] : 0);
        return 1;
    }

    auto input = context.inputs->find(name);
    if (input != context.inputs->end()) {
        std::span<double const> samples = input->second;
        lua_pushnumber(state, index < samples.size() ? samples[index] : 0);
        return 1;
    }

    std::string error = std::format("unknown signal '{}'", name);
    return luaL_error(state, "%s", error.c_str());
}

void instructionHook(lua_State* state, lua_Debug*) {
    luaL_error(state, "instruction limit exceeded");
}

std::string luaError(lua_State* state) {
    char const* error = lua_tostring(state, -1);
    return error != nullptr ? error : "unknown Lua error";
}

} // namespace

std::expected<std::vector<double>, std::string> evaluateCsvLuaSignal(
    std::string_view source,
    std::string_view output_name,
    std::span<CsvLuaSignalInput const> inputs,
    size_t sample_count) {
    lua_State* state = luaL_newstate();
    if (state == nullptr) {
        return std::unexpected("Failed to create Lua state");
    }

    luaL_openlibs(state);
    lua_pushcfunction(state, readSignal);
    lua_setglobal(state, "read");

    if (luaL_loadbuffer(state, source.data(), source.size(), "Custom signal") != LUA_OK) {
        std::string error = luaError(state);
        lua_close(state);
        return std::unexpected(std::move(error));
    }
    int function_ref = luaL_ref(state, LUA_REGISTRYINDEX);

    std::vector<double> output;
    output.reserve(sample_count);
    std::unordered_map<std::string_view, std::span<double const>> inputs_by_name;
    inputs_by_name.reserve(inputs.size());
    for (CsvLuaSignalInput const& input : inputs) {
        inputs_by_name.emplace(input.name, input.samples);
    }
    EvaluationContext context{
      .output_name = output_name,
      .inputs = &inputs_by_name,
      .output = &output,
    };
    *static_cast<EvaluationContext**>(lua_getextraspace(state)) = &context;

    for (size_t i = 0; i < sample_count; ++i) {
        context.current_index = i;
        lua_rawgeti(state, LUA_REGISTRYINDEX, function_ref);
        lua_sethook(state, instructionHook, LUA_MASKCOUNT, LuaInstructionLimit);
        if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
            std::string error = std::format("Sample {}: {}", i, luaError(state));
            lua_close(state);
            return std::unexpected(std::move(error));
        }
        if (lua_type(state, -1) != LUA_TNUMBER) {
            std::string type = luaL_typename(state, -1);
            lua_close(state);
            return std::unexpected(std::format("Sample {}: expected a number, got {}", i, type));
        }
        output.push_back(lua_tonumber(state, -1));
        lua_pop(state, 1);
    }

    lua_close(state);
    return output;
}
