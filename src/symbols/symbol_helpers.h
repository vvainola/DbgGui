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
#include "raw_symbol.h"
#include <optional>
#include <memory>
#include <map>

using ModuleBase = ULONG64;
using TypeIndex = ULONG;

void printLastError();

int getBitPosition(RawSymbol const& sym);
SymTagEnum getSymbolTag(SymbolInfo const& sym);
BasicType getBasicType(RawSymbol const& sym);
void addChildrenToSymbol(RawSymbol& parent_symbol, std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*>& reference_symbols);
std::string getUndecoratedSymbolName(std::string const& name);
std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address);

inline bool startsWith(std::string const& s, std::string const& w) {
    return s.rfind(w, 0) == 0;
}

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

struct ModuleInfo {
    MemoryAddress base_address;
    MemoryAddress size;
    std::string md5_hash;
};
ModuleInfo getCurrentModuleInfo();

std::string readFile(std::string const& filename);

// Currently unused helpers
DataKind getDataKind(RawSymbol const& sym);
