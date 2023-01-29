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
#include <Windows.h>
#include <DbgHelp.h>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

using MemoryAddress = uint64_t;
inline constexpr int NO_VALUE = -1;

struct SymbolInfo {
    SymbolInfo(){};
    SymbolInfo(SYMBOL_INFO* symbol)
        : TypeIndex(symbol->TypeIndex),
          Index(symbol->Index),
          Size(symbol->Size),
          ModBase(symbol->ModBase),
          Value(symbol->Value),
          Address(symbol->Address),
          PdbTag((SymTagEnum)symbol->Tag),
          Name(symbol->Name) {
    }
    ULONG TypeIndex = 0; // Type Index of symbol
    ULONG Index = 0;
    ULONG Size = 0;
    ULONG64 ModBase = 0;            // Base Address of module containing this symbol
    ULONG64 Value = 0;              // Value of symbol, ValuePresent should be 1
    ULONG64 Address = 0;            // Address of symbol including base address of module
    SymTagEnum PdbTag = SymTagNull; // pdb classification
    std::string Name = "";
};

SymTagEnum getSymbolTag(SymbolInfo const& sym);
struct RawSymbol {
    RawSymbol() {}
    RawSymbol(SymbolInfo const& symbol);

    // Copy from another symbol
    RawSymbol(RawSymbol const& other)
        : info(other.info),
          tag(other.tag),
          offset_to_parent(other.offset_to_parent),
          array_element_count(other.array_element_count),
          basic_type(other.basic_type),
          bitfield_position(other.bitfield_position) {
    }

    SymbolInfo info;
    SymTagEnum tag = SymTagNull;
    DWORD offset_to_parent = 0;
    uint32_t array_element_count = 0;
    BasicType basic_type = BasicType::btNoType;
    int bitfield_position = -1;
    // Children/members of the symbol
    std::vector<std::unique_ptr<RawSymbol>> children;
};

void to_json(nlohmann::ordered_json& j, RawSymbol const& sym);
void from_json(nlohmann::ordered_json const& j, RawSymbol& sym);
void saveSymbolsToFile(std::string const& filename,
                       std::vector<std::unique_ptr<RawSymbol>>& symbols,
                       bool omit_names);
