// MIT License
//
// Copyright (c) 2022 vvainola
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

#include "cvconst.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

using MemoryAddress = uint64_t;
inline constexpr int NO_VALUE = -1;

struct RawSymbol {
    std::string name;
    MemoryAddress address = 0;
    uint32_t size = 0;
    SymTagEnum tag = SymTagNull;
    uint32_t offset_to_parent = 0;
    uint32_t array_element_count = 0;
    BasicType basic_type = BasicType::btNoType;
    int bitfield_position = -1;
    int64_t enum_value = 0;
    std::vector<std::unique_ptr<RawSymbol>> children;

#if WINDOWS
    MemoryAddress mod_base = 0;
    uint32_t type_index = 0;
    uint32_t index = 0;
    SymTagEnum pdb_tag = SymTagNull;
#endif

    static RawSymbol fromJson(nlohmann::json const& j);
    std::unique_ptr<RawSymbol> clone() const;
};

void to_json(nlohmann::json& j, RawSymbol const& sym);
void saveSymbolsToJson(std::string const& filename,
                       std::vector<std::unique_ptr<RawSymbol>> const& symbols,
                       bool omit_names);
