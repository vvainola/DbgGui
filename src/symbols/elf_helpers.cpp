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

#include "dbghelp_helpers.h"

#include <fstream>
#include <sstream>

ModuleInfo getCurrentModuleInfo() {
    return ModuleInfo{
        .base_address = 0,
        .size = 0,
        .write_time = "",
        .path = ""};
}

std::string getModuleName(ModuleBase module_base) {
    return "";
}

std::string getUndecoratedSymbolName(std::string const& name) {
    return name;
}

std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address) {
    return nullptr;
}

int getBitPosition(RawSymbol const& sym) {
    return NO_VALUE;
}

DataKind getDataKind(RawSymbol const& sym) {
    return DataKind::DataIsUnknown;
}

BasicType getBasicType(RawSymbol const& sym) {
    return BasicType::btNoType;
}

SymTagEnum getSymbolTag(SymbolInfo const& sym) {
    return SymTagEnum::SymTagNull;
}

std::string readFile(std::string const& filename) {
    return (std::stringstream() << std::ifstream(filename).rdbuf()).str();
}

void printLastError() {
}

void copyChildrenFromSymbol(RawSymbol const& from, RawSymbol& parent) {
    parent.children.reserve(from.children.size());
    parent.array_element_count = from.array_element_count;
    for (size_t i = 0; i < from.children.size(); ++i) {
        std::unique_ptr<RawSymbol>& new_child = parent.children.emplace_back(
            std::make_unique<RawSymbol>(*from.children[i]));
        copyChildrenFromSymbol(*from.children[i], *new_child);
    }
}

void addChildrenToSymbol(RawSymbol& parent_symbol, std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*>& reference_symbols) {
}
