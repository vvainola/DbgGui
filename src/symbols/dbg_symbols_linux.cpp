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

// Linux-specific symbol loading using DWARF debug information from ELF binaries.
// Unlike Windows which uses RawPDB with PDB files, Linux uses libdwarf
// to read DWARF debug info directly from the executable's .debug_* sections.
// Also supports reading symbols from loaded shared libraries via dl_iterate_phdr.

#include "dbg_symbols.hpp"
#include "str_helpers.h"
#include "variant_symbol.h"

#include <cassert>
#include "symbol_helpers.h"

#include <fcntl.h>
#include <link.h>
#include <dwarf.h>
#include <libdwarf.h>
#include <dlfcn.h>
#include <elf.h>
#include <unistd.h>
#include <climits>
#include <cstring>
#include <cxxabi.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

static void appendMembers(SymbolDescriptor& symbol,
                          SymbolDescriptor const& member_container,
                          uint32_t container_offset) {
    for (auto const& child : member_container.children) {
        auto appended_child = std::make_shared<SymbolDescriptor>(*child);
        appended_child->offset_to_parent += container_offset;
        symbol.children.push_back(std::move(appended_child));
    }
}

// ============================================================================
// Address computation helpers
// ============================================================================

// Determines where the current executable was loaded in memory.
static MemoryAddress getLoadBase() {
    static MemoryAddress const load_base = [] {
        MemoryAddress base = 0;
        dl_iterate_phdr(
          [](dl_phdr_info* info, size_t, void* data) {
              // The main executable is represented by the entry with an empty
              // name. dlpi_addr is its ELF load bias: zero for ET_EXEC and the
              // relocation base for PIE executables.
              if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
                  *static_cast<MemoryAddress*>(data) = info->dlpi_addr;
                  return 1;
              }
              return 0;
          },
          &base);
        return base;
    }();
    return load_base;
}

// ============================================================================
// DWARF type resolution
// ============================================================================

// Map DWARF base type encoding to the backend-neutral scalar type.
static ScalarType encodingToScalarType(Dwarf_Unsigned encoding) {
    switch (encoding) {
        case DW_ATE_signed:
        case DW_ATE_signed_char:
            return ScalarType::SignedInteger;
        case DW_ATE_unsigned:
        case DW_ATE_unsigned_char:
            return ScalarType::UnsignedInteger;
        case DW_ATE_float:
            return ScalarType::FloatingPoint;
        case DW_ATE_boolean:
            return ScalarType::Boolean;
        default:
            return ScalarType::None;
    }
}

// Follow typedef/const/volatile/restrict chains to find the underlying type offset.
static Dwarf_Off followTypeChain(Dwarf_Debug dbg, Dwarf_Off type_offset) {
    Dwarf_Error err = nullptr;

    for (int depth = 0; depth < 50; ++depth) {
        Dwarf_Die type_die = nullptr;
        if (dwarf_offdie_b(dbg, type_offset, 1, &type_die, &err) != DW_DLV_OK) {
            return type_offset;
        }

        Dwarf_Half tag;
        dwarf_tag(type_die, &tag, &err);

        if (tag != DW_TAG_typedef
            && tag != DW_TAG_const_type
            && tag != DW_TAG_volatile_type
            && tag != DW_TAG_restrict_type) {
            dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
            return type_offset;
        }

        Dwarf_Attribute attr = nullptr;
        if (dwarf_attr(type_die, DW_AT_type, &attr, &err) != DW_DLV_OK) {
            // No type attribute (e.g., const void)
            dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
            return type_offset;
        }

        Dwarf_Off next_offset;
        dwarf_global_formref(attr, &next_offset, &err);
        dwarf_dealloc(dbg, attr, DW_DLA_ATTR);
        dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
        type_offset = next_offset;
    }

    return type_offset;
}

// followTypeChain strips typedef/const/volatile/restrict wrappers before
// resolving the underlying layout. Walk the same wrapper chain first so the
// const qualifier can be preserved on the SymbolDescriptor before the type
// offset is replaced with the unqualified type.
static bool isConstQualifiedType(Dwarf_Debug dbg, Dwarf_Off type_offset) {
    Dwarf_Error err = nullptr;

    for (int depth = 0; depth < 50; ++depth) {
        Dwarf_Die type_die = nullptr;
        if (dwarf_offdie_b(dbg, type_offset, 1, &type_die, &err) != DW_DLV_OK) {
            return false;
        }

        Dwarf_Half tag;
        dwarf_tag(type_die, &tag, &err);
        bool const is_const = tag == DW_TAG_const_type;
        if (tag != DW_TAG_typedef
            && tag != DW_TAG_const_type
            && tag != DW_TAG_volatile_type
            && tag != DW_TAG_restrict_type) {
            dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
            return false;
        }

        Dwarf_Attribute attr = nullptr;
        if (dwarf_attr(type_die, DW_AT_type, &attr, &err) != DW_DLV_OK) {
            dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
            return is_const;
        }

        Dwarf_Off next_offset;
        dwarf_global_formref(attr, &next_offset, &err);
        dwarf_dealloc(dbg, attr, DW_DLA_ATTR);
        dwarf_dealloc(dbg, type_die, DW_DLA_DIE);

        if (is_const) {
            return true;
        }
        type_offset = next_offset;
    }

    return false;
}

static uint32_t getDataMemberLocationOffset(Dwarf_Debug dbg, Dwarf_Die die) {
    Dwarf_Error err = nullptr;
    uint32_t offset = 0;

    Dwarf_Attribute loc_attr = nullptr;
    if (dwarf_attr(die, DW_AT_data_member_location, &loc_attr, &err) == DW_DLV_OK) {
        Dwarf_Unsigned uval;
        if (dwarf_formudata(loc_attr, &uval, &err) == DW_DLV_OK) {
            offset = (uint32_t)uval;
        } else {
            Dwarf_Signed sval;
            if (dwarf_formsdata(loc_attr, &sval, &err) == DW_DLV_OK && sval >= 0) {
                offset = (uint32_t)sval;
            }
        }
        dwarf_dealloc(dbg, loc_attr, DW_DLA_ATTR);
    }

    return offset;
}

static std::string getTypeName(Dwarf_Debug dbg, Dwarf_Off type_offset) {
    Dwarf_Error err = nullptr;
    type_offset = followTypeChain(dbg, type_offset);

    Dwarf_Die type_die = nullptr;
    if (dwarf_offdie_b(dbg, type_offset, 1, &type_die, &err) != DW_DLV_OK) {
        return "";
    }

    std::string name;
    char* type_name = nullptr;
    if (dwarf_diename(type_die, &type_name, &err) == DW_DLV_OK && type_name != nullptr) {
        name = type_name;
        dwarf_dealloc(dbg, type_name, DW_DLA_STRING);
    }
    dwarf_dealloc(dbg, type_die, DW_DLA_DIE);

    return name;
}

// Resolve a DWARF type (given by offset) and populate the SymbolDescriptor's
// kind, size, scalar_type, children, and array_element_count.
// The name, address, and offset_to_parent should already be set by the caller.
// Returns false if the type DIE could not be resolved.
//
// full_type_defs is a cross-CU index of full class/struct/union definitions by
// unqualified name. When the type DIE at type_offset turns out to be a forward
// declaration (DW_AT_declaration=1, no DW_AT_byte_size), we look the name up in
// this map and re-resolve against the full definition's DIE.
static bool resolveType(Dwarf_Debug dbg,
                        Dwarf_Off type_offset,
                        SymbolDescriptor& symbol,
                        DbgSymbols::FullTypeDefs const& full_type_defs,
                        DbgSymbols::TypeCache& type_cache) {
    Dwarf_Error err = nullptr;
    Dwarf_Off const original_type_offset = type_offset;

    if (auto const cached = type_cache.find(original_type_offset); cached != type_cache.end()) {
        std::string name = std::move(symbol.name);
        MemoryAddress const address = symbol.address;
        uint32_t const offset_to_parent = symbol.offset_to_parent;
        bool const was_const = symbol.is_const;
        symbol = cached->second;
        symbol.name = std::move(name);
        symbol.address = address;
        symbol.offset_to_parent = offset_to_parent;
        symbol.is_const = symbol.is_const || was_const;
        return true;
    }

    bool const type_is_const = isConstQualifiedType(dbg, original_type_offset);
    // Cache only the reusable type layout. `symbol` also carries state from the
    // current global/member, including inherited constness; storing that state
    // would make later symbols of the same type inherit the first symbol's
    // name, address, parent offset, or qualifiers.
    auto cacheResolvedType = [&] {
        SymbolDescriptor cached_symbol = symbol;
        cached_symbol.name.clear();
        cached_symbol.address = 0;
        cached_symbol.offset_to_parent = 0;
        cached_symbol.is_const = type_is_const;
        type_cache.emplace(original_type_offset, std::move(cached_symbol));
    };

    // Follow typedef/const/volatile chains
    symbol.is_const = symbol.is_const || type_is_const;
    type_offset = followTypeChain(dbg, type_offset);

    Dwarf_Die type_die = nullptr;
    if (dwarf_offdie_b(dbg, type_offset, 1, &type_die, &err) != DW_DLV_OK) {
        return false;
    }

    Dwarf_Half tag;
    dwarf_tag(type_die, &tag, &err);

    Dwarf_Unsigned type_size = 0;
    dwarf_bytesize(type_die, &type_size, &err);

    // If this is a forward-declared class/struct/union/enum, find the full
    // definition by name in another CU and resolve against that instead.
    // Note: type_size is not checked here because a forward-declared enum with
    // an explicit underlying type (e.g. `enum class E : int;`) carries
    // DW_AT_byte_size from the underlying type even though it has no
    // enumerator children. DW_AT_declaration is the reliable indicator.
    if (tag == DW_TAG_structure_type
        || tag == DW_TAG_class_type
        || tag == DW_TAG_union_type
        || tag == DW_TAG_enumeration_type) {
        Dwarf_Bool is_decl = 0;
        Dwarf_Attribute decl_attr = nullptr;
        if (dwarf_attr(type_die, DW_AT_declaration, &decl_attr, &err) == DW_DLV_OK) {
            dwarf_formflag(decl_attr, &is_decl, &err);
            dwarf_dealloc(dbg, decl_attr, DW_DLA_ATTR);
        }
        if (is_decl) {
            char* tn = nullptr;
            if (dwarf_diename(type_die, &tn, &err) == DW_DLV_OK && tn) {
                std::string name(tn);
                dwarf_dealloc(dbg, tn, DW_DLA_STRING);
                auto range = full_type_defs.equal_range(name);
                for (auto it = range.first; it != range.second; ++it) {
                    if (it->second != type_offset) {
                        dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
                        bool const resolved = resolveType(dbg, it->second, symbol, full_type_defs, type_cache);
                        if (resolved) {
                            cacheResolvedType();
                        }
                        return resolved;
                    }
                }
            }
        }
    }

    switch (tag) {
        case DW_TAG_base_type: {
            symbol.kind = SymbolKind::Scalar;
            symbol.size = (uint32_t)type_size;

            Dwarf_Attribute enc_attr = nullptr;
            if (dwarf_attr(type_die, DW_AT_encoding, &enc_attr, &err) == DW_DLV_OK) {
                Dwarf_Unsigned encoding = 0;
                dwarf_formudata(enc_attr, &encoding, &err);
                symbol.scalar_type = encodingToScalarType(encoding);
                dwarf_dealloc(dbg, enc_attr, DW_DLA_ATTR);
            }
            break;
        }

        case DW_TAG_pointer_type: {
            symbol.kind = SymbolKind::Pointer;
            symbol.size = (type_size > 0) ? (uint32_t)type_size : 8;
            break;
        }

        case DW_TAG_array_type: {
            symbol.kind = SymbolKind::Array;

            // Get the element type
            Dwarf_Attribute elem_type_attr = nullptr;
            Dwarf_Off elem_type_offset = 0;
            bool has_elem_type = false;
            if (dwarf_attr(type_die, DW_AT_type, &elem_type_attr, &err) == DW_DLV_OK) {
                dwarf_global_formref(elem_type_attr, &elem_type_offset, &err);
                dwarf_dealloc(dbg, elem_type_attr, DW_DLA_ATTR);
                has_elem_type = true;
            }

            // Collect array dimensions from DW_TAG_subrange_type children
            std::vector<uint32_t> dimensions;
            Dwarf_Die child_die = nullptr;
            if (dwarf_child(type_die, &child_die, &err) == DW_DLV_OK) {
                do {
                    Dwarf_Half child_tag;
                    dwarf_tag(child_die, &child_tag, &err);
                    if (child_tag == DW_TAG_subrange_type) {
                        Dwarf_Attribute count_attr = nullptr;
                        if (dwarf_attr(child_die, DW_AT_count, &count_attr, &err) == DW_DLV_OK) {
                            Dwarf_Unsigned count;
                            dwarf_formudata(count_attr, &count, &err);
                            dimensions.push_back((uint32_t)count);
                            dwarf_dealloc(dbg, count_attr, DW_DLA_ATTR);
                        } else {
                            Dwarf_Attribute ub_attr = nullptr;
                            if (dwarf_attr(child_die, DW_AT_upper_bound, &ub_attr, &err) == DW_DLV_OK) {
                                Dwarf_Unsigned upper_bound;
                                dwarf_formudata(ub_attr, &upper_bound, &err);
                                dimensions.push_back((uint32_t)(upper_bound + 1));
                                dwarf_dealloc(dbg, ub_attr, DW_DLA_ATTR);
                            }
                        }
                    }

                    Dwarf_Die sibling = nullptr;
                    if (dwarf_siblingof_b(dbg, child_die, 1, &sibling, &err) != DW_DLV_OK) {
                        dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
                        child_die = nullptr;
                        break;
                    }
                    dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
                    child_die = sibling;
                } while (child_die);
            }

            if (has_elem_type && !dimensions.empty()) {
                // Resolve the innermost element type
                auto innermost = std::make_shared<SymbolDescriptor>();
                if (resolveType(dbg, elem_type_offset, *innermost, full_type_defs, type_cache)) {
                    // Build nested array structure from innermost dimension outward
                    // For dimensions [3, 3] with element type int32_t:
                    // Build: array(3, array(3, int32_t))
                    for (int i = (int)dimensions.size() - 1; i >= 1; --i) {
                        auto array_elem = std::make_shared<SymbolDescriptor>(SymbolDescriptor{
                          .kind = SymbolKind::Array,
                        });
                        array_elem->array_element_count = dimensions[i];
                        array_elem->size = innermost->size * dimensions[i];
                        array_elem->children.push_back(std::move(innermost));
                        innermost = std::move(array_elem);
                    }

                    symbol.array_element_count = dimensions[0];
                    symbol.size = innermost->size * dimensions[0];
                    symbol.children.push_back(std::move(innermost));
                }
            }
            break;
        }

        case DW_TAG_structure_type:
        case DW_TAG_class_type:
        case DW_TAG_union_type: {
            symbol.kind = SymbolKind::Object;
            symbol.size = (uint32_t)type_size;

            // Enumerate member children
            Dwarf_Die child_die = nullptr;
            if (dwarf_child(type_die, &child_die, &err) == DW_DLV_OK) {
                do {
                    Dwarf_Half child_tag;
                    dwarf_tag(child_die, &child_tag, &err);

                    if (child_tag == DW_TAG_member) {
                        char* member_name = nullptr;
                        dwarf_diename(child_die, &member_name, &err);

                        // Get member offset from containing structure
                        uint32_t offset = getDataMemberLocationOffset(dbg, child_die);

                        // Get member type
                        Dwarf_Attribute mem_type_attr = nullptr;
                        if (dwarf_attr(child_die, DW_AT_type, &mem_type_attr, &err) == DW_DLV_OK) {
                            Dwarf_Off member_type_offset;
                            dwarf_global_formref(mem_type_attr, &member_type_offset, &err);
                            dwarf_dealloc(dbg, mem_type_attr, DW_DLA_ATTR);

                            auto child_sym = std::make_shared<SymbolDescriptor>(SymbolDescriptor{
                              .name = member_name ? member_name : "",
                            });
                            child_sym->offset_to_parent = offset;

                            if (!shouldSkipSymbolChild(child_sym->name)
                                && resolveType(dbg, member_type_offset, *child_sym, full_type_defs, type_cache)) {
                                // Check for bitfield
                                Dwarf_Attribute bit_size_attr = nullptr;
                                if (dwarf_attr(child_die, DW_AT_bit_size, &bit_size_attr, &err) == DW_DLV_OK) {
                                    Dwarf_Unsigned bit_size;
                                    dwarf_formudata(bit_size_attr, &bit_size, &err);
                                    dwarf_dealloc(dbg, bit_size_attr, DW_DLA_ATTR);

                                    // Get bit offset (DWARF5: DW_AT_data_bit_offset, DWARF4: DW_AT_bit_offset)
                                    Dwarf_Attribute bit_offset_attr = nullptr;
                                    int bit_pos = -1;
                                    if (dwarf_attr(child_die, DW_AT_data_bit_offset, &bit_offset_attr, &err) == DW_DLV_OK) {
                                        Dwarf_Unsigned bit_offset;
                                        dwarf_formudata(bit_offset_attr, &bit_offset, &err);
                                        bit_pos = (int)(bit_offset - offset * 8);
                                        dwarf_dealloc(dbg, bit_offset_attr, DW_DLA_ATTR);
                                    } else if (dwarf_attr(child_die, DW_AT_bit_offset, &bit_offset_attr, &err) == DW_DLV_OK) {
                                        // DWARF4 big-endian bit offset: convert to little-endian
                                        Dwarf_Unsigned bit_offset;
                                        dwarf_formudata(bit_offset_attr, &bit_offset, &err);
                                        // For little-endian: bit_pos = container_bits - bit_offset - bit_size
                                        Dwarf_Unsigned container_bits = type_size * 8;
                                        if (container_bits == 0) {
                                            container_bits = 32;
                                        }
                                        bit_pos = (int)(container_bits - bit_offset - bit_size);
                                        dwarf_dealloc(dbg, bit_offset_attr, DW_DLA_ATTR);
                                    }

                                    child_sym->bitfield_position = bit_pos;
                                    child_sym->size = (uint32_t)bit_size;
                                }

                                if (member_name == nullptr && child_sym->kind == SymbolKind::Object) {
                                    appendMembers(symbol, *child_sym, offset);
                                } else {
                                    symbol.children.push_back(std::move(child_sym));
                                }
                            }
                        }

                        if (member_name) {
                            dwarf_dealloc(dbg, member_name, DW_DLA_STRING);
                        }
                    } else if (child_tag == DW_TAG_inheritance) {
                        Dwarf_Attribute base_type_attr = nullptr;
                        if (dwarf_attr(child_die, DW_AT_type, &base_type_attr, &err) == DW_DLV_OK) {
                            Dwarf_Off base_type_offset;
                            dwarf_global_formref(base_type_attr, &base_type_offset, &err);
                            dwarf_dealloc(dbg, base_type_attr, DW_DLA_ATTR);

                            SymbolDescriptor base_symbol{
                              .name = getTypeName(dbg, base_type_offset),
                            };
                            uint32_t const base_offset = getDataMemberLocationOffset(dbg, child_die);

                            if (!shouldSkipSymbolChild(base_symbol.name)
                                && resolveType(dbg, base_type_offset, base_symbol, full_type_defs, type_cache)) {
                                appendMembers(symbol, base_symbol, base_offset);
                            }
                        }
                    }

                    Dwarf_Die sibling = nullptr;
                    if (dwarf_siblingof_b(dbg, child_die, 1, &sibling, &err) != DW_DLV_OK) {
                        dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
                        child_die = nullptr;
                        break;
                    }
                    dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
                    child_die = sibling;
                } while (child_die);
            }
            break;
        }

        case DW_TAG_enumeration_type: {
            symbol.kind = SymbolKind::Enum;
            symbol.size = (uint32_t)type_size;
            symbol.scalar_type = ScalarType::SignedInteger;

            Dwarf_Attribute underlying_type_attr = nullptr;
            if (dwarf_attr(type_die, DW_AT_type, &underlying_type_attr, &err) == DW_DLV_OK) {
                Dwarf_Off underlying_offset;
                dwarf_global_formref(underlying_type_attr, &underlying_offset, &err);
                dwarf_dealloc(dbg, underlying_type_attr, DW_DLA_ATTR);

                SymbolDescriptor temp{};
                if (resolveType(dbg, underlying_offset, temp, full_type_defs, type_cache)) {
                    symbol.scalar_type = temp.scalar_type;
                    if (symbol.size == 0) {
                        symbol.size = temp.size;
                    }
                }
            }

            Dwarf_Die child_die = nullptr;
            if (dwarf_child(type_die, &child_die, &err) == DW_DLV_OK) {
                do {
                    Dwarf_Half child_tag;
                    dwarf_tag(child_die, &child_tag, &err);
                    if (child_tag == DW_TAG_enumerator) {
                        char* enum_name = nullptr;
                        dwarf_diename(child_die, &enum_name, &err);
                        int64_t enum_const_value = 0;
                        Dwarf_Attribute const_val_attr = nullptr;
                        if (dwarf_attr(child_die, DW_AT_const_value, &const_val_attr, &err) == DW_DLV_OK) {
                            Dwarf_Signed sval;
                            if (dwarf_formsdata(const_val_attr, &sval, &err) == DW_DLV_OK) {
                                enum_const_value = static_cast<int64_t>(sval);
                            }
                            dwarf_dealloc(dbg, const_val_attr, DW_DLA_ATTR);
                        }
                        auto enum_child = std::make_shared<SymbolDescriptor>(SymbolDescriptor{
                          .name = enum_name ? enum_name : "",
                          .kind = SymbolKind::EnumValue,
                        });
                        enum_child->enum_value = enum_const_value;
                        symbol.children.push_back(std::move(enum_child));
                        if (enum_name) {
                            dwarf_dealloc(dbg, enum_name, DW_DLA_STRING);
                        }
                    }

                    Dwarf_Die sibling = nullptr;
                    if (dwarf_siblingof_b(dbg, child_die, 1, &sibling, &err) != DW_DLV_OK) {
                        dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
                        break;
                    }
                    dwarf_dealloc(dbg, child_die, DW_DLA_DIE);
                    child_die = sibling;
                } while (child_die);
            }
            break;
        }

        default: {
            // Unknown/unhandled type
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::UnsignedInteger;
            symbol.size = (uint32_t)type_size;
            break;
        }
    }

    dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
    // Skip symbols whose size we could not determine (e.g. a forward declaration
    // for which no full definition was found in any CU's DWARF).
    if (symbol.size == 0) {
        return false;
    }
    cacheResolvedType();
    return true;
}

// ============================================================================
// Symbol collection from DWARF
// ============================================================================

// Read DW_AT_linkage_name without demangling it. Most function names are never
// displayed, so resolveFunctionAddress performs and caches demangling on demand.
static std::optional<std::string> linkageName(Dwarf_Debug dbg, Dwarf_Die die) {
    Dwarf_Error err = nullptr;
    Dwarf_Attribute link_attr = nullptr;
    if (dwarf_attr(die, DW_AT_linkage_name, &link_attr, &err) != DW_DLV_OK) {
        return std::nullopt;
    }
    char* link_name = nullptr;
    dwarf_formstring(link_attr, &link_name, &err);
    dwarf_dealloc(dbg, link_attr, DW_DLA_ATTR);

    if (link_name == nullptr) {
        return std::nullopt;
    }

    std::optional<std::string> result(link_name);
    dwarf_dealloc(dbg, link_name, DW_DLA_STRING);
    return result;
}

// Variable definitions that use DW_AT_specification need their qualified name
// while the symbol tree is being built, so they cannot use deferred demangling.
static std::optional<std::string> demangleLinkageName(Dwarf_Debug dbg, Dwarf_Die die) {
    std::optional<std::string> linkage_name = linkageName(dbg, die);
    if (!linkage_name) {
        return std::nullopt;
    }

    int demangle_status = 0;
    char* demangled = abi::__cxa_demangle(
      linkage_name->c_str(), nullptr, nullptr, &demangle_status);
    if (demangle_status != 0 || demangled == nullptr) {
        free(demangled);
        return std::nullopt;
    }

    std::string result(demangled);
    free(demangled);
    return result;
}

static void collectFullTypeDef(Dwarf_Debug dbg,
                               Dwarf_Die die,
                               Dwarf_Half tag,
                               char const* die_name,
                               DbgSymbols::FullTypeDefs& full_type_defs);

static bool readAddress(Dwarf_Debug dbg, Dwarf_Die die, MemoryAddress load_base, MemoryAddress& address) {
    Dwarf_Error err = nullptr;
    Dwarf_Attribute loc_attr = nullptr;
    if (dwarf_attr(die, DW_AT_location, &loc_attr, &err) != DW_DLV_OK) {
        return false;
    }

    Dwarf_Block* block = nullptr;
    bool found = false;
    if (dwarf_formblock(loc_attr, &block, &err) == DW_DLV_OK) {
        if (block->bl_len >= 1 + sizeof(Dwarf_Addr)) {
            auto* buf = static_cast<uint8_t*>(block->bl_data);
            if (buf[0] == DW_OP_addr) {
                Dwarf_Addr value = 0;
                memcpy(&value, buf + 1, sizeof(value));
                address = load_base + value;
                found = address != 0;
            }
        }
        dwarf_dealloc(dbg, block, DW_DLA_BLOCK);
    }
    dwarf_dealloc(dbg, loc_attr, DW_DLA_ATTR);
    return found;
}

static bool readTypeOffset(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Off& type_offset) {
    Dwarf_Error err = nullptr;
    Dwarf_Attribute type_attr = nullptr;
    if (dwarf_attr(die, DW_AT_type, &type_attr, &err) != DW_DLV_OK) {
        return false;
    }
    bool const found = dwarf_global_formref(type_attr, &type_offset, &err) == DW_DLV_OK;
    dwarf_dealloc(dbg, type_attr, DW_DLA_ATTR);
    return found;
}

static bool shouldSkipIndexedSymbolName(std::string const& qualified_name) {
    if (shouldSkipSymbolName(qualified_name)) {
        return true;
    }
    size_t const qualifier = qualified_name.rfind("::");
    return qualifier != std::string::npos
        && shouldSkipSymbolName(qualified_name.substr(qualifier + 2));
}

// Walk the DWARF DIE tree and collect global variable symbols
void DbgSymbols::walkDieTree(Dwarf_Debug dbg, Dwarf_Die die, MemoryAddress load_base, std::string const& namespace_prefix, std::string const& module_prefix, std::unordered_map<Dwarf_Off, std::string>& decl_qualified_names, FullTypeDefs& full_type_defs, std::vector<PendingGlobal>& pending_globals) {
    Dwarf_Error err = nullptr;
    char* die_name = nullptr;
    Dwarf_Half tag = 0;

    dwarf_tag(die, &tag, &err);

    bool const needs_name = tag == DW_TAG_variable
        || tag == DW_TAG_namespace
        || tag == DW_TAG_subprogram
        || tag == DW_TAG_structure_type
        || tag == DW_TAG_class_type
        || tag == DW_TAG_union_type
        || tag == DW_TAG_enumeration_type;
    if (needs_name) {
        dwarf_diename(die, &die_name, &err);
    }

    collectFullTypeDef(dbg, die, tag, die_name, full_type_defs);

    // Process global and namespace-scope variables. Subprogram children are not
    // traversed below, so function-local variables never reach this block.
    if (tag == DW_TAG_variable) {
        std::string effective_name = die_name ? die_name : "";
        Dwarf_Off spec_die_offset = 0;
        // True if effective_name is already fully qualified (came from demangling
        // a linkage name or from the declaration map). In that case namespace_prefix
        // must NOT be added again at the use site.
        bool effective_name_is_fully_qualified = false;

        // For named DIEs (declarations or inline definitions), record the fully
        // qualified name keyed by this DIE's global offset. A later definition DIE
        // that lacks a name but carries DW_AT_specification → this DIE can recover
        // the qualified name even when there is no DW_AT_linkage_name (e.g. statics).
        if (die_name != nullptr) {
            Dwarf_Off this_offset = 0;
            if (dwarf_dieoffset(die, &this_offset, &err) == DW_DLV_OK) {
                decl_qualified_names[this_offset] = namespace_prefix + die_name;
            }
        }

        if (die_name == nullptr) {
            Dwarf_Attribute spec_attr = nullptr;
            if (dwarf_attr(die, DW_AT_specification, &spec_attr, &err) == DW_DLV_OK) {
                dwarf_global_formref(spec_attr, &spec_die_offset, &err);
                dwarf_dealloc(dbg, spec_attr, DW_DLA_ATTR);
                if (spec_die_offset != 0) {
                    Dwarf_Die spec_die = nullptr;
                    if (dwarf_offdie_b(dbg, spec_die_offset, 1, &spec_die, &err) == DW_DLV_OK) {
                        if (auto demangled = demangleLinkageName(dbg, spec_die)) {
                            effective_name = std::move(*demangled);
                            effective_name_is_fully_qualified = true;
                        }
                        dwarf_dealloc(dbg, spec_die, DW_DLA_DIE);
                    }
                    // Fallback for statics (no DW_AT_linkage_name): recover the
                    // qualified name from the declaration recorded earlier while
                    // descending into the enclosing namespace(s).
                    if (effective_name.empty()) {
                        auto it = decl_qualified_names.find(spec_die_offset);
                        if (it != decl_qualified_names.end()) {
                            effective_name = it->second;
                            effective_name_is_fully_qualified = true;
                        }
                    }
                }
            }
        }

        if (!effective_name.empty() && !shouldSkipSymbolName(effective_name)) {
            MemoryAddress addr = 0;

            Dwarf_Attribute loc_attr = nullptr;
            if (dwarf_attr(die, DW_AT_location, &loc_attr, &err) == DW_DLV_OK) {
                Dwarf_Block* block = nullptr;
                if (dwarf_formblock(loc_attr, &block, &err) == DW_DLV_OK) {
                    if (block->bl_len >= 1) {
                        uint8_t* buf = (uint8_t*)block->bl_data;
                        if (buf[0] == DW_OP_addr) {
                            Dwarf_Addr addr_val = 0;
                            memcpy(&addr_val, buf + 1, sizeof(addr_val));
                            addr = addr_val + load_base;
                        }
                    }
                    dwarf_dealloc(dbg, block, DW_DLA_BLOCK);
                }
                dwarf_dealloc(dbg, loc_attr, DW_DLA_ATTR);
            }

            if (addr != 0) {
                // Get type - from current die or from specification die
                Dwarf_Attribute type_attr = nullptr;
                Dwarf_Off type_offset = 0;
                bool has_type = false;

                if (dwarf_attr(die, DW_AT_type, &type_attr, &err) == DW_DLV_OK) {
                    dwarf_global_formref(type_attr, &type_offset, &err);
                    dwarf_dealloc(dbg, type_attr, DW_DLA_ATTR);
                    has_type = true;
                } else if (spec_die_offset != 0) {
                    // Try to get type from specification die
                    Dwarf_Die type_spec_die = nullptr;
                    if (dwarf_offdie_b(dbg, spec_die_offset, 1, &type_spec_die, &err) == DW_DLV_OK) {
                        if (dwarf_attr(type_spec_die, DW_AT_type, &type_attr, &err) == DW_DLV_OK) {
                            dwarf_global_formref(type_attr, &type_offset, &err);
                            dwarf_dealloc(dbg, type_attr, DW_DLA_ATTR);
                            has_type = true;
                        }
                        dwarf_dealloc(dbg, type_spec_die, DW_DLA_DIE);
                    }
                }

                if (has_type) {
                    std::string sym_name = effective_name_is_fully_qualified ?
                                             (module_prefix + effective_name) :
                                             (module_prefix + namespace_prefix + effective_name);
                    auto symbol = std::make_unique<SymbolDescriptor>(SymbolDescriptor{
                      .name = sym_name,
                      .address = addr,
                    });
                    pending_globals.push_back({std::move(symbol), type_offset});
                }
            }
        }
    }

    // Process function declarations (for function pointer resolution)
    if (tag == DW_TAG_subprogram) {
        std::string func_name;
        bool name_from_linkage = false;

        // If this is a concrete definition with DW_AT_specification, follow it to get the name
        if (die_name == nullptr) {
            Dwarf_Off spec_die_offset = 0;
            Dwarf_Attribute spec_attr = nullptr;
            if (dwarf_attr(die, DW_AT_specification, &spec_attr, &err) == DW_DLV_OK) {
                dwarf_global_formref(spec_attr, &spec_die_offset, &err);
                dwarf_dealloc(dbg, spec_attr, DW_DLA_ATTR);
                if (spec_die_offset != 0) {
                    Dwarf_Die spec_die = nullptr;
                    if (dwarf_offdie_b(dbg, spec_die_offset, 1, &spec_die, &err) == DW_DLV_OK) {
                        if (auto linkage_name = linkageName(dbg, spec_die)) {
                            func_name = std::move(*linkage_name);
                            name_from_linkage = true;
                        }
                        // Fall back to namespace_prefix + spec_die name
                        if (func_name.empty()) {
                            char* spec_name = nullptr;
                            dwarf_diename(spec_die, &spec_name, &err);
                            if (spec_name != nullptr) {
                                func_name = namespace_prefix + spec_name;
                                dwarf_dealloc(dbg, spec_name, DW_DLA_STRING);
                            }
                        }
                        dwarf_dealloc(dbg, spec_die, DW_DLA_DIE);
                    }
                }
            }
        } else {
            // Function has a direct name
            func_name = die_name;
        }

        // A raw Itanium linkage name starts with `_Z` and would be rejected by
        // shouldSkipSymbolName as an underscore-prefixed implementation symbol.
        // Apply that policy after lazy demangling instead.
        if (!func_name.empty()
            && (name_from_linkage || !shouldSkipSymbolName(func_name))) {
            Dwarf_Attribute low_pc_attr = nullptr;
            if (dwarf_attr(die, DW_AT_low_pc, &low_pc_attr, &err) == DW_DLV_OK) {
                Dwarf_Addr low_pc = 0;
                dwarf_formaddr(low_pc_attr, &low_pc, &err);
                dwarf_dealloc(dbg, low_pc_attr, DW_DLA_ATTR);

                if (low_pc != 0) {
                    std::string full_name;
                    if (name_from_linkage) {
                        // The linkage name becomes fully qualified when it is
                        // demangled by resolveFunctionAddress.
                        full_name = func_name;
                    } else {
                        full_name = namespace_prefix + func_name;
                    }

                    m_function_addresses[load_base + low_pc] = full_name;
                }
            }
        }
    }

    // A subprogram's own DIE contains everything needed for function-address
    // resolution. Its children describe parameters, lexical blocks, and local
    // variables, none of which can be global symbols. Avoid walking the often
    // very large function-body debug trees.
    if (tag == DW_TAG_subprogram) {
        if (die_name != nullptr) {
            dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
        }
        return;
    }

    // Recursively process children
    std::string child_prefix = namespace_prefix;
    if (tag == DW_TAG_namespace && die_name != nullptr) {
        child_prefix = namespace_prefix.empty() ? std::string(die_name) + "::" : namespace_prefix + die_name + "::";
    }
    if (die_name != nullptr) {
        dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
    }

    Dwarf_Die child = nullptr;
    if (dwarf_child(die, &child, &err) == DW_DLV_OK) {
        walkDieTree(dbg, child, load_base, child_prefix, module_prefix, decl_qualified_names, full_type_defs, pending_globals);
        while (true) {
            Dwarf_Die sibling = nullptr;
            if (dwarf_siblingof_b(dbg, child, 1, &sibling, &err) != DW_DLV_OK) {
                break;
            }
            dwarf_dealloc(dbg, child, DW_DLA_DIE);
            walkDieTree(dbg, sibling, load_base, child_prefix, module_prefix, decl_qualified_names, full_type_defs, pending_globals);
            child = sibling;
        }
        dwarf_dealloc(dbg, child, DW_DLA_DIE);
    }
}

// Index a full class/struct/union/enum definition by its unqualified name. This
// runs as part of the main DIE walk; globals are resolved after that walk, when
// definitions from all compilation units have been indexed.
static void collectFullTypeDef(Dwarf_Debug dbg,
                               Dwarf_Die die,
                               Dwarf_Half tag,
                               char const* die_name,
                               DbgSymbols::FullTypeDefs& full_type_defs) {
    Dwarf_Error err = nullptr;

    if (tag == DW_TAG_structure_type
        || tag == DW_TAG_class_type
        || tag == DW_TAG_union_type
        || tag == DW_TAG_enumeration_type) {
        Dwarf_Bool is_decl = 0;
        Dwarf_Attribute decl_attr = nullptr;
        if (dwarf_attr(die, DW_AT_declaration, &decl_attr, &err) == DW_DLV_OK) {
            dwarf_formflag(decl_attr, &is_decl, &err);
            dwarf_dealloc(dbg, decl_attr, DW_DLA_ATTR);
        }
        if (!is_decl) {
            Dwarf_Unsigned byte_size = 0;
            if (dwarf_bytesize(die, &byte_size, &err) == DW_DLV_OK && byte_size > 0 && die_name != nullptr) {
                Dwarf_Off offset = 0;
                if (dwarf_dieoffset(die, &offset, &err) == DW_DLV_OK) {
                    full_type_defs.emplace(die_name, offset);
                }
            }
        }
    }

}

bool DbgSymbols::processIndexedSymbols(Dwarf_Debug dbg,
                                       MemoryAddress load_base,
                                       std::string const& module_prefix,
                                       FullTypeDefs& full_type_defs,
                                       std::vector<PendingGlobal>& pending_globals) {
    Dwarf_Error err = nullptr;
    Dwarf_Global* globals = nullptr;
    Dwarf_Signed global_count = 0;
    if (dwarf_get_globals(dbg, &globals, &global_count, &err) != DW_DLV_OK) {
        return false;
    }

    Dwarf_Global* pubtypes = nullptr;
    Dwarf_Signed pubtype_count = 0;
    bool const has_pubtypes = dwarf_get_pubtypes(
                                dbg, &pubtypes, &pubtype_count, &err)
        == DW_DLV_OK;
    bool has_debug_names = false;
    for (Dwarf_Signed i = 0; i < global_count && !has_debug_names; ++i) {
        // libdwarf reports a tag here only for entries sourced from the DWARF 5
        // .debug_names section, which includes both symbols and types.
        has_debug_names = dwarf_global_tag_number(globals[i]) != 0;
    }
    if (global_count == 0 || (!has_pubtypes && !has_debug_names)) {
        dwarf_globals_dealloc(dbg, globals, global_count);
        if (has_pubtypes) {
            dwarf_globals_dealloc(dbg, pubtypes, pubtype_count);
        }
        return false;
    }

    // The same libdwarf interface reads legacy .debug_pubnames and DWARF 5
    // .debug_names, keeping the symbol collector independent of the producer's
    // accelerator-table format.
    std::unordered_map<Dwarf_Off, std::string> indexed_names;
    std::unordered_set<Dwarf_Off> processed_variables;
    std::unordered_set<Dwarf_Off> processed_functions;

    // Pubnames entries point directly at candidate DIEs and already contain
    // their qualified names. Processing those DIEs avoids visiting unrelated
    // namespaces, class members, parameters, and lexical blocks.
    auto process_variable = [&](Dwarf_Die die, Dwarf_Off die_offset, std::string const& qualified_name) {
        if (!processed_variables.emplace(die_offset).second
            || qualified_name.empty()
            || shouldSkipIndexedSymbolName(qualified_name)) {
            return;
        }

        MemoryAddress address = 0;
        if (!readAddress(dbg, die, load_base, address)) {
            return;
        }

        Dwarf_Off type_offset = 0;
        if (!readTypeOffset(dbg, die, type_offset)) {
            // A storage-bearing definition may keep its type on a separate
            // declaration DIE rather than repeating it on the definition.
            Dwarf_Attribute spec_attr = nullptr;
            Dwarf_Off spec_offset = 0;
            if (dwarf_attr(die, DW_AT_specification, &spec_attr, &err) == DW_DLV_OK) {
                dwarf_global_formref(spec_attr, &spec_offset, &err);
                dwarf_dealloc(dbg, spec_attr, DW_DLA_ATTR);
            }
            Dwarf_Die spec_die = nullptr;
            if (spec_offset == 0
                || dwarf_offdie_b(dbg, spec_offset, 1, &spec_die, &err) != DW_DLV_OK) {
                return;
            }
            bool const found_type = readTypeOffset(dbg, spec_die, type_offset);
            dwarf_dealloc(dbg, spec_die, DW_DLA_DIE);
            if (!found_type) {
                return;
            }
        }

        auto symbol = std::make_unique<SymbolDescriptor>(SymbolDescriptor{
          .name = module_prefix + qualified_name,
          .address = address,
        });
        pending_globals.push_back({std::move(symbol), type_offset});
    };

    auto process_function = [&](Dwarf_Die die, Dwarf_Off die_offset, std::string const& qualified_name) {
        if (!processed_functions.emplace(die_offset).second) {
            return;
        }

        Dwarf_Attribute low_pc_attr = nullptr;
        if (dwarf_attr(die, DW_AT_low_pc, &low_pc_attr, &err) != DW_DLV_OK) {
            return;
        }
        Dwarf_Addr low_pc = 0;
        dwarf_formaddr(low_pc_attr, &low_pc, &err);
        dwarf_dealloc(dbg, low_pc_attr, DW_DLA_ATTR);
        if (low_pc == 0) {
            return;
        }

        // Prefer the compact linkage name so demangling remains deferred until
        // a function-pointer value actually needs to be displayed.
        std::string function_name = qualified_name;
        if (auto linkage_name = linkageName(dbg, die)) {
            function_name = std::move(*linkage_name);
        }
        if (!function_name.empty()
            && (function_name.starts_with("_Z")
                || !shouldSkipIndexedSymbolName(function_name))) {
            m_function_addresses[load_base + low_pc] = std::move(function_name);
        }
    };

    for (Dwarf_Signed i = 0; i < global_count; ++i) {
        char* indexed_name = nullptr;
        Dwarf_Off die_offset = 0;
        Dwarf_Off cu_offset = 0;
        if (dwarf_global_name_offsets(
              globals[i], &indexed_name, &die_offset, &cu_offset, &err)
            != DW_DLV_OK
            || indexed_name == nullptr) {
            continue;
        }
        std::string qualified_name(indexed_name);
        indexed_names.emplace(die_offset, qualified_name);

        Dwarf_Die die = nullptr;
        if (dwarf_offdie_b(dbg, die_offset, 1, &die, &err) != DW_DLV_OK) {
            continue;
        }
        Dwarf_Half tag = 0;
        dwarf_tag(die, &tag, &err);
        if (tag == DW_TAG_variable) {
            process_variable(die, die_offset, qualified_name);
        } else if (tag == DW_TAG_subprogram) {
            process_function(die, die_offset, qualified_name);
        } else {
            char* die_name = nullptr;
            dwarf_diename(die, &die_name, &err);
            collectFullTypeDef(dbg, die, tag, die_name, full_type_defs);
            if (die_name != nullptr) {
                dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
            }
        }
        dwarf_dealloc(dbg, die, DW_DLA_DIE);
    }
    dwarf_globals_dealloc(dbg, globals, global_count);

    if (has_pubtypes) {
        // Forward-declaration resolution needs a module-wide map of complete
        // type definitions. Pubtypes supplies those DIE offsets without a tree
        // walk; DWARF 5 producers may instead include them in .debug_names,
        // where the globals loop above already collects them.
        std::unordered_set<Dwarf_Off> processed_types;
        for (Dwarf_Signed i = 0; i < pubtype_count; ++i) {
            Dwarf_Off die_offset = 0;
            if (dwarf_global_die_offset(pubtypes[i], &die_offset, &err) != DW_DLV_OK
                || !processed_types.emplace(die_offset).second) {
                continue;
            }
            Dwarf_Die die = nullptr;
            if (dwarf_offdie_b(dbg, die_offset, 1, &die, &err) != DW_DLV_OK) {
                continue;
            }
            Dwarf_Half tag = 0;
            char* die_name = nullptr;
            dwarf_tag(die, &tag, &err);
            dwarf_diename(die, &die_name, &err);
            collectFullTypeDef(dbg, die, tag, die_name, full_type_defs);
            if (die_name != nullptr) {
                dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
            }
            dwarf_dealloc(dbg, die, DW_DLA_DIE);
        }
        dwarf_globals_dealloc(dbg, pubtypes, pubtype_count);
    }

    // GCC indexes namespace-scope declarations, but a non-trivial static's
    // storage-bearing definition can be an unnamed top-level DIE referring to
    // that declaration through DW_AT_specification. Scan only CU children for
    // those definitions; their qualified names come from the accelerator table.
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half cu_header_version = 0;
    Dwarf_Off abbrev_offset = 0;
    Dwarf_Half address_size = 0;
    Dwarf_Half offset_size = 0;
    Dwarf_Half extension_size = 0;
    Dwarf_Sig8 type_sig;
    Dwarf_Unsigned typeoffset = 0;
    Dwarf_Unsigned next_cu_header = 0;
    Dwarf_Half header_cu_type = 0;
    while (dwarf_next_cu_header_d(dbg, 1, &cu_header_length, &cu_header_version,
                                  &abbrev_offset, &address_size, &offset_size,
                                  &extension_size, &type_sig, &typeoffset,
                                  &next_cu_header, &header_cu_type, &err)
           == DW_DLV_OK) {
        Dwarf_Die cu_die = nullptr;
        if (dwarf_siblingof_b(dbg, nullptr, 1, &cu_die, &err) != DW_DLV_OK) {
            continue;
        }
        Dwarf_Die die = nullptr;
        if (dwarf_child(cu_die, &die, &err) == DW_DLV_OK) {
            while (true) {
                Dwarf_Half tag = 0;
                dwarf_tag(die, &tag, &err);
                if (tag == DW_TAG_variable || tag == DW_TAG_subprogram) {
                    Dwarf_Attribute spec_attr = nullptr;
                    Dwarf_Off spec_offset = 0;
                    if (dwarf_attr(die, DW_AT_specification, &spec_attr, &err) == DW_DLV_OK) {
                        dwarf_global_formref(spec_attr, &spec_offset, &err);
                        dwarf_dealloc(dbg, spec_attr, DW_DLA_ATTR);
                    }
                    auto const name = indexed_names.find(spec_offset);
                    if (name != indexed_names.end()) {
                        Dwarf_Off die_offset = 0;
                        dwarf_dieoffset(die, &die_offset, &err);
                        if (tag == DW_TAG_variable) {
                            process_variable(die, die_offset, name->second);
                        } else {
                            process_function(die, die_offset, name->second);
                        }
                    }
                }

                Dwarf_Die sibling = nullptr;
                if (dwarf_siblingof_b(dbg, die, 1, &sibling, &err) != DW_DLV_OK) {
                    break;
                }
                dwarf_dealloc(dbg, die, DW_DLA_DIE);
                die = sibling;
            }
            dwarf_dealloc(dbg, die, DW_DLA_DIE);
        }
        dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
    }
    return true;
}

// Process all Compilation Units in a DWARF debug info
void DbgSymbols::processAllCUs(Dwarf_Debug dbg, MemoryAddress load_base, std::string const& module_prefix) {
    Dwarf_Error err = nullptr;
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half cu_header_version = 0;
    Dwarf_Off abbrev_offset = 0;
    Dwarf_Half address_size = 0;
    Dwarf_Half offset_size = 0;
    Dwarf_Half extension_size = 0;
    Dwarf_Sig8 type_sig;
    Dwarf_Unsigned typeoffset = 0;
    Dwarf_Unsigned next_cu_header = 0;
    Dwarf_Half header_cu_type = 0;

    // Per-module map of DIE offset → fully qualified name. Built while descending
    // through DW_TAG_namespace DIEs so that definition DIEs lacking a name (e.g.
    // static namespace-scope variables, whose definition carries only
    // DW_AT_specification + DW_AT_location) can recover their qualified name.
    std::unordered_map<Dwarf_Off, std::string> decl_qualified_names;

    FullTypeDefs full_type_defs;
    TypeCache type_cache;
    std::vector<PendingGlobal> pending_globals;

    // Prefer accelerator tables when the producer emitted them. Third-party
    // shared libraries commonly omit them, so retain the recursive collector
    // below as a compatibility fallback rather than requiring special flags
    // for every loaded module.
    if (processIndexedSymbols(
          dbg, load_base, module_prefix, full_type_defs, pending_globals)) {
        for (PendingGlobal& pending : pending_globals) {
            if (resolveType(dbg, pending.type_offset, *pending.symbol, full_type_defs, type_cache)) {
                m_symbol_descriptors.push_back(std::move(pending.symbol));
                m_root_symbols.push_back(std::make_unique<VariantSymbol>(
                  m_root_symbols, m_symbol_descriptors.back().get()));
            }
        }
        return;
    }

    // Walk every CU once, indexing full definitions and collecting lightweight
    // global records. Resolve globals afterwards so definitions in later CUs
    // are available for forward-declared types.
    while (dwarf_next_cu_header_d(dbg,
                                  1,
                                  &cu_header_length,
                                  &cu_header_version,
                                  &abbrev_offset,
                                  &address_size,
                                  &offset_size,
                                  &extension_size,
                                  &type_sig,
                                  &typeoffset,
                                  &next_cu_header,
                                  &header_cu_type,
                                  &err)
           == DW_DLV_OK) {
        Dwarf_Die cu_die = nullptr;
        if (dwarf_siblingof_b(dbg, nullptr, 1, &cu_die, &err) == DW_DLV_OK) {
            walkDieTree(dbg, cu_die, load_base, "", module_prefix, decl_qualified_names, full_type_defs, pending_globals);
            dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
        }
    }

    for (PendingGlobal& pending : pending_globals) {
        if (resolveType(dbg, pending.type_offset, *pending.symbol, full_type_defs, type_cache)) {
            m_symbol_descriptors.push_back(std::move(pending.symbol));
            m_root_symbols.push_back(std::make_unique<VariantSymbol>(
              m_root_symbols, m_symbol_descriptors.back().get()));
        }
    }
}

struct SharedLibInfo {
    std::string path;
    MemoryAddress load_addr;
};

static int dl_iterate_callback(struct dl_phdr_info* info, size_t size, void* data) {
    auto* libs = static_cast<std::vector<SharedLibInfo>*>(data);
    if (info->dlpi_name && info->dlpi_name[0] != '\0') {
        libs->push_back({info->dlpi_name, (MemoryAddress)info->dlpi_addr});
    }
    return 0;
}

// Initialize symbol loading from the main executable
void DbgSymbols::initSymbolsFromPdb() {
    Dwarf_Debug dbg = nullptr;
    Dwarf_Error err = nullptr;
    const char* exe_path = "/proc/self/exe";

    int fd = open(exe_path, O_RDONLY);
    if (fd < 0) {
        return;
    }

    if (dwarf_init_b(fd, DW_GROUPNUMBER_ANY, nullptr, nullptr, &dbg, &err) != DW_DLV_OK) {
        close(fd);
        return;
    }

    processAllCUs(dbg, getLoadBase());

    dwarf_finish(dbg);
    close(fd);

    // ============================================================================
    // Shared library symbol loading
    // ============================================================================
    std::vector<SharedLibInfo> libs;
    dl_iterate_phdr(dl_iterate_callback, &libs);

    for (auto& lib : libs) {
        int fd = open(lib.path.c_str(), O_RDONLY);
        if (fd < 0) {
            continue;
        }

        Dwarf_Debug dbg = nullptr;
        Dwarf_Error err = nullptr;
        if (dwarf_init_b(fd, DW_GROUPNUMBER_ANY, nullptr, nullptr, &dbg, &err) != DW_DLV_OK) {
            close(fd);
            continue;
        }

        std::filesystem::path p(lib.path);
        std::string stem = p.stem().string();
        if (stem.starts_with("lib") && stem.size() > 3) {
            stem = stem.substr(3);
        }
        std::string module_name = stem + "|";
        processAllCUs(dbg, lib.load_addr, module_name);

        dwarf_finish(dbg);
        close(fd);
    }
}

std::string DbgSymbols::resolveFunctionAddress(MemoryAddress address) const {
    std::scoped_lock lock(m_function_addresses_mutex);
    auto it = m_function_addresses.find(address);
    if (it != m_function_addresses.end()) {
        std::string& function_name = it->second;
        // Itanium C++ ABI linkage names start with `_Z`. A successful lookup
        // replaces the cached linkage name with its demangled form, so later
        // calls skip demangling.
        if (function_name.starts_with("_Z")) {
            int demangle_status = 0;
            char* demangled = abi::__cxa_demangle(
              function_name.c_str(), nullptr, nullptr, &demangle_status);
            if (demangle_status == 0 && demangled != nullptr) {
                function_name = demangled;
                free(demangled);

                // Function-pointer values have historically displayed the
                // qualified function name without its parameter list.
                size_t const paren = function_name.find('(');
                if (paren != std::string::npos) {
                    function_name.resize(paren);
                }
                if (shouldSkipSymbolName(function_name)) {
                    function_name.clear();
                }
            } else {
                free(demangled);
            }
        }
        return function_name;
    }
    return "";
}

// ============================================================================
// Snapshot support
// ============================================================================

std::vector<SymbolValue> DbgSymbols::saveSnapshotToMemory() const {
    std::vector<SymbolValue> snapshot;
    std::function<void(VariantSymbol*)> save_symbol_to_snapshot = [&](VariantSymbol* sym) {
        if (sym->isConst()) {
            return;
        }

        VariantSymbol::Type type = sym->getType();
        if (type == VariantSymbol::Type::Arithmetic || type == VariantSymbol::Type::Enum) {
            snapshot.push_back({sym, sym->read()});
        } else if (type == VariantSymbol::Type::Pointer) {
            MemoryAddress pointed_address = sym->getPointedAddress();
            snapshot.push_back({sym, pointed_address});
        }
        for (auto const& child : sym->getChildren()) {
            save_symbol_to_snapshot(child.get());
        }
    };
    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        save_symbol_to_snapshot(sym.get());
    }
    return snapshot;
}
