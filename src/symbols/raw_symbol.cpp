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

#include "raw_symbol.h"
#include "dbghelp_helpers.h"
#include <fstream>
#include <format>
#include <functional>

RawSymbol::RawSymbol(std::string const& in_name,
                     MemoryAddress in_address,
                     uint32_t in_size,
                     SymTagEnum in_tag)
    : name(in_name),
      address(in_address),
      size(in_size),
      tag(in_tag) {
}

#if WINDOWS
RawSymbol::RawSymbol(SymbolInfo const& symbol_info)
    : info(symbol_info),
      name(symbol_info.Name),
      address(symbol_info.Address),
      size(symbol_info.Size),
      tag(getSymbolTag(symbol_info)) {
    if (tag == SymTagBaseType) {
        basic_type = getBasicType(symbol_info);
        if (basic_type == BasicType::btUInt
            || basic_type == BasicType::btInt
            || basic_type == BasicType::btLong
            || basic_type == BasicType::btULong
            || basic_type == BasicType::btBool) {
            bitfield_position = getBitPosition(symbol_info);
        }
    } else if (tag == SymTagEnumerator) {
        basic_type = getBasicType(symbol_info);
    }
}
#endif

RawSymbol::RawSymbol(nlohmann::json const& field) {
    static ModuleInfo module_info = getCurrentModuleInfo();

    name = field["name"].get<std::string>();
    address = module_info.base_address + static_cast<size_t>(field["address"].get<uint64_t>());
    size = field["size"].get<uint32_t>();

    tag = field["tag"].get<SymTagEnum>();
    offset_to_parent = field["offset_to_parent"].get<uint32_t>();
    array_element_count = field["array_element_count"].get<uint32_t>();
    basic_type = field["basic_type"].get<BasicType>();
    bitfield_position = field["bitfield_position"].get<int>();
    basic_type = field["basic_type"].get<BasicType>();
    if (field.contains("enum_value")) {
        enum_value = field["enum_value"].get<int32_t>();
    }

    if (field.contains("children")) {
        for (auto const& child_data : field["children"]) {
            children.push_back(std::make_unique<RawSymbol>(child_data));
        }
    }
}

void to_json(nlohmann::json& field, RawSymbol const& sym) {
    static ModuleInfo module_info = getCurrentModuleInfo();
    field["name"] = sym.name;
    if (sym.address > 0) {
        field["address"] = sym.address - module_info.base_address;
    } else {
        field["address"] = 0;
    }
    field["size"] = sym.size;
    field["tag"] = sym.tag;
    field["offset_to_parent"] = sym.offset_to_parent;
    field["array_element_count"] = sym.array_element_count;
    field["basic_type"] = BasicType::btNoType;
    field["bitfield_position"] = NO_VALUE;
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

    if (sym.tag == SymTagEnumerator && sym.enum_value != 0) {
        field["enum_value"] = sym.enum_value;
    }

    for (int i = 0; auto& child : sym.children) {
        if (sym.name.starts_with("____")) {
            child->name = std::format("____{}", std::to_string(i));
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
        if (omit_names) {
            sym->name = std::format("____{}", std::to_string(i));
        }
        symbols_json["symbols"][std::to_string(i)] = *sym;
        ++i;
    }

    std::ofstream(filename) << std::setw(4) << symbols_json;
}
