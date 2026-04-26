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

RawSymbol RawSymbol::fromJson(nlohmann::json const& field) {
    RawSymbol sym;
    static ModuleInfo module_info = getCurrentModuleInfo();

    sym.name = field["name"].get<std::string>();
    sym.address = module_info.base_address + static_cast<size_t>(field["address"].get<uint64_t>());
    sym.size = field["size"].get<uint32_t>();

    sym.tag = field["tag"].get<SymTagEnum>();
    sym.offset_to_parent = field["offset_to_parent"].get<uint32_t>();
    sym.array_element_count = field["array_element_count"].get<uint32_t>();
    sym.basic_type = field["basic_type"].get<BasicType>();
    sym.bitfield_position = field["bitfield_position"].get<int>();
    sym.basic_type = field["basic_type"].get<BasicType>();
    if (field.contains("enum_value")) {
        sym.enum_value = field["enum_value"].get<int32_t>();
    }

    if (field.contains("children")) {
        for (auto const& child_data : field["children"]) {
            sym.children.push_back(std::make_unique<RawSymbol>(fromJson(child_data)));
        }
    }

    return sym;
}

std::unique_ptr<RawSymbol> RawSymbol::clone() const {
    auto sym = std::make_unique<RawSymbol>();
    sym->name = name;
    sym->address = address;
    sym->size = size;
    sym->tag = tag;
    sym->offset_to_parent = offset_to_parent;
    sym->array_element_count = array_element_count;
    sym->basic_type = basic_type;
    sym->bitfield_position = bitfield_position;
    sym->enum_value = enum_value;
#if WINDOWS
    sym->mod_base = mod_base;
    sym->type_index = type_index;
    sym->index = index;
    sym->pdb_tag = pdb_tag;
#endif
    return sym;
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
