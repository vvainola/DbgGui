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

#include "symbol_descriptor.h"
#include "symbol_helpers.h"
#include "magic_enum.hpp"
#include <fstream>
#include <format>

namespace {

bool shouldAnonymizeChildren(std::string const& name, bool anonymize) {
    return anonymize || name.starts_with("____");
}

void writeSymbolDescriptor(nlohmann::json& field,
                           SymbolDescriptor const& symbol,
                           std::string const& name,
                           bool anonymize) {
    static ModuleInfo module_info = getCurrentModuleInfo();
    field["name"] = name;
    if (symbol.address > 0) {
        field["address"] = symbol.address - module_info.base_address;
    } else {
        field["address"] = 0;
    }
    field["size"] = symbol.size;
    field["kind"] = magic_enum::enum_name(symbol.kind);
    field["offset_to_parent"] = symbol.offset_to_parent;
    field["array_element_count"] = symbol.array_element_count;
    field["scalar_type"] = magic_enum::enum_name(ScalarType::None);
    field["bitfield_position"] = NO_VALUE;
    field["is_const"] = symbol.is_const;
    if (symbol.kind == SymbolKind::Scalar) {
        field["scalar_type"] = magic_enum::enum_name(symbol.scalar_type);
        if (symbol.scalar_type == ScalarType::UnsignedInteger
            || symbol.scalar_type == ScalarType::SignedInteger
            || symbol.scalar_type == ScalarType::Boolean) {
            field["bitfield_position"] = symbol.bitfield_position;
        }
    } else if (symbol.kind == SymbolKind::Enum) {
        field["scalar_type"] = magic_enum::enum_name(symbol.scalar_type);
    }

    if (symbol.kind == SymbolKind::EnumValue && symbol.enum_value != 0) {
        field["enum_value"] = symbol.enum_value;
    }

    bool const anonymize_children = shouldAnonymizeChildren(name, anonymize);
    for (int i = 0; auto const& child : symbol.children) {
        std::string const child_name = anonymize_children ? std::format("____{}", i) : child->name;
        writeSymbolDescriptor(field["children"][std::to_string(i)], *child, child_name, anonymize_children);
        ++i;
    }
}

} // namespace

SymbolDescriptor SymbolDescriptor::fromJson(nlohmann::json const& field) {
    SymbolDescriptor sym;
    static ModuleInfo module_info = getCurrentModuleInfo();

    sym.name = field["name"].get<std::string>();
    sym.address = module_info.base_address + static_cast<size_t>(field["address"].get<uint64_t>());
    sym.size = field["size"].get<uint32_t>();
    sym.offset_to_parent = field["offset_to_parent"].get<uint32_t>();
    sym.array_element_count = field["array_element_count"].get<uint32_t>();
    sym.bitfield_position = field["bitfield_position"].get<int>();
    sym.is_const = field.value("is_const", false);
    sym.kind = magic_enum::enum_cast<SymbolKind>(field["kind"].get<std::string>()).value_or(SymbolKind::Unknown);
    sym.scalar_type = magic_enum::enum_cast<ScalarType>(field["scalar_type"].get<std::string>()).value_or(ScalarType::None);

    if (field.contains("enum_value")) {
        sym.enum_value = field["enum_value"].get<int32_t>();
    }

    if (field.contains("children")) {
        for (auto const& child_data : field["children"]) {
            sym.children.push_back(std::make_shared<SymbolDescriptor>(fromJson(child_data)));
        }
    }

    return sym;
}

std::unique_ptr<SymbolDescriptor> SymbolDescriptor::clone() const {
    auto sym = std::make_unique<SymbolDescriptor>();
    sym->name = name;
    sym->address = address;
    sym->size = size;
    sym->kind = kind;
    sym->offset_to_parent = offset_to_parent;
    sym->array_element_count = array_element_count;
    sym->scalar_type = scalar_type;
    sym->bitfield_position = bitfield_position;
    sym->enum_value = enum_value;
    sym->is_const = is_const;
    sym->children = children;
    return sym;
}

void to_json(nlohmann::json& field, SymbolDescriptor const& symbol) {
    writeSymbolDescriptor(field, symbol, symbol.name, false);
}

void saveSymbolDescriptorsToJson(std::string const& filename,
                                 std::vector<std::unique_ptr<SymbolDescriptor>> const& symbols,
                                 bool omit_names) {
    ModuleInfo module_info = getCurrentModuleInfo();
    nlohmann::json symbols_json;
    symbols_json["write_time"] = module_info.write_time;
    for (int i = 0; auto& sym : symbols) {
        std::string const name = omit_names ? std::format("____{}", i) : sym->name;
        writeSymbolDescriptor(symbols_json["symbols"][std::to_string(i)], *sym, name, omit_names);
        ++i;
    }

    std::ofstream(filename) << std::setw(4) << symbols_json;
}
