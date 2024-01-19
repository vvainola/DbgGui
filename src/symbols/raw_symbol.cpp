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
#include "symbol_helpers.h"
#include <fstream>
#include <format>
#include <functional>

RawSymbol::RawSymbol(SymbolInfo const& symbol)
    : info(symbol),
      tag(getSymbolTag(info)) {
    if (tag == SymTagBaseType) {
        basic_type = getBasicType(*this);
        if (basic_type == BasicType::btUInt
            || basic_type == BasicType::btInt
            || basic_type == BasicType::btBool) {
            bitfield_position = getBitPosition(*this);
        }
    } else if (tag == SymTagEnumerator) {
        basic_type = getBasicType(*this);
    }
}

RawSymbol::RawSymbol(nlohmann::json const& field) {
    static ModuleInfo module_info = getCurrentModuleInfo();

    info.Name = field["name"];
    info.Address = module_info.base_address + field["address"];
    info.Size = field["size"];
    info.Value = field["value"];

    tag = field["tag"];
    offset_to_parent = field["offset_to_parent"];
    array_element_count = field["array_element_count"];
    basic_type = field["basic_type"];
    bitfield_position = field["bitfield_position"];
    basic_type = field["basic_type"];

    if (field.contains("children")) {
        for (nlohmann::json const& child_data : field["children"]) {
            children.push_back(std::make_unique<RawSymbol>(child_data));
        }
    }
}

void to_json(nlohmann::json& field, RawSymbol const& sym) {
    field["name"] = sym.info.Name;
    if (sym.info.Address > 0) {
        field["address"] = sym.info.Address - sym.info.ModBase;
    } else {
        field["address"] = 0;
    }
    field["size"] = sym.info.Size;
    field["tag"] = sym.tag;
    field["offset_to_parent"] = sym.offset_to_parent;
    field["array_element_count"] = sym.array_element_count;
    field["basic_type"] = BasicType::btNoType;
    field["bitfield_position"] = NO_VALUE;
    field["value"] = sym.info.Value;
    if (sym.tag == SymTagBaseType) {
        field["basic_type"] = sym.basic_type;
        if (sym.basic_type == BasicType::btUInt
            || sym.basic_type == BasicType::btInt
            || sym.basic_type == BasicType::btBool) {
            field["bitfield_position"] = sym.bitfield_position;
        }
    } else if (sym.tag == SymTagEnumerator) {
        field["basic_type"] = sym.basic_type;
    }

    for (int i = 0; auto& child : sym.children) {
        if (sym.info.Name.starts_with("____")) {
            child->info.Name = std::format("____{}", std::to_string(i));
        }
        to_json(field["children"][std::to_string(i)], *child);
        ++i;
    }
}

void saveSymbolsToJson(std::string const& filename, std::vector<std::unique_ptr<RawSymbol>> const& symbols, bool omit_names) {
    ModuleInfo module_info = getCurrentModuleInfo();
    nlohmann::json symbols_json;
    symbols_json["write_time"] = module_info.write_time;
    for (int i = 0; auto& sym : symbols) {
        if (sym->info.ModBase != module_info.base_address) {
            return;
        }

        if (omit_names) {
            sym->info.Name = std::format("____{}", std::to_string(i));
        }
        symbols_json["symbols"][std::to_string(i)] = *sym;
        ++i;
    }

    std::ofstream(filename) << std::setw(4) << symbols_json;
}
