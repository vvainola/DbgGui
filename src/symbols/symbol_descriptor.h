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

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using MemoryAddress = uint64_t;
inline constexpr int NO_VALUE = -1;

enum class SymbolKind {
    Unknown,
    Scalar,
    Pointer,
    Array,
    Object,
    Enum,
    EnumValue,
    Function
};

enum class ScalarType {
    None,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Boolean,
    WChar,
    Char16,
    Char32
};

struct SymbolDescriptor {
    std::string name;
    MemoryAddress address = 0;
    uint32_t size = 0;
    SymbolKind kind = SymbolKind::Unknown;
    uint32_t offset_to_parent = 0;
    uint32_t array_element_count = 0;
    ScalarType scalar_type = ScalarType::None;
    int bitfield_position = -1;
    int64_t enum_value = 0;
    std::vector<std::unique_ptr<SymbolDescriptor>> children;

    static SymbolDescriptor fromJson(nlohmann::json const& j);
    std::unique_ptr<SymbolDescriptor> clone() const;
};

void to_json(nlohmann::json& j, SymbolDescriptor const& symbol);
void saveSymbolDescriptorsToJson(std::string const& filename,
                                 std::vector<std::unique_ptr<SymbolDescriptor>> const& symbols,
                                 bool omit_names);
