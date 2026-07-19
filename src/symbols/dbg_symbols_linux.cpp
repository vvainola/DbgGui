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
#include <vector>

static void appendInheritedMembers(SymbolDescriptor& symbol,
                                   SymbolDescriptor const& base_symbol,
                                   uint32_t base_offset) {
    for (auto const& base_child : base_symbol.children) {
        auto inherited_child = std::make_shared<SymbolDescriptor>(*base_child);
        inherited_child->offset_to_parent += base_offset;
        symbol.children.push_back(std::move(inherited_child));
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
                        DbgSymbols::FullTypeDefs const& full_type_defs) {
    Dwarf_Error err = nullptr;

    // Follow typedef/const/volatile chains
    symbol.is_const = symbol.is_const || isConstQualifiedType(dbg, type_offset);
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
                        return resolveType(dbg, it->second, symbol, full_type_defs);
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
                if (resolveType(dbg, elem_type_offset, *innermost, full_type_defs)) {
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

                            if (resolveType(dbg, member_type_offset, *child_sym, full_type_defs)) {
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

                                symbol.children.push_back(std::move(child_sym));
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

                            if (resolveType(dbg, base_type_offset, base_symbol, full_type_defs)) {
                                appendInheritedMembers(symbol, base_symbol, base_offset);
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
                if (resolveType(dbg, underlying_offset, temp, full_type_defs)) {
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
    return symbol.size > 0;
}

// ============================================================================
// Symbol collection from DWARF
// ============================================================================

// Demangle a DW_AT_linkage_name from a DIE. Returns the demangled string
// or std::nullopt if no linkage name is present or demangling fails.
static std::optional<std::string> demangleLinkageName(Dwarf_Debug dbg, Dwarf_Die die) {
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

    std::optional<std::string> result;
    int demangle_status;
    char* demangled = abi::__cxa_demangle(link_name, nullptr, nullptr, &demangle_status);
    if (demangle_status == 0 && demangled != nullptr) {
        result = demangled;
        free(demangled);
    }
    dwarf_dealloc(dbg, link_name, DW_DLA_STRING);
    return result;
}

// Walk the DWARF DIE tree and collect global variable symbols
void DbgSymbols::walkDieTree(Dwarf_Debug dbg, Dwarf_Die die, MemoryAddress load_base, std::string const& namespace_prefix, std::string const& module_prefix, std::unordered_map<Dwarf_Off, std::string>& decl_qualified_names, FullTypeDefs const& full_type_defs, bool inside_function) {
    Dwarf_Error err = nullptr;
    char* die_name = nullptr;
    Dwarf_Half tag = 0;

    dwarf_diename(die, &die_name, &err);
    dwarf_tag(die, &tag, &err);

    // Process variable declarations. Skip when inside a function: a function-local
    // static is gated by a `_ZGV*` init guard that shouldSkipSymbolName filters out,
    // so snapshot save/restore would zero the static's storage without resetting
    // the guard — the next use would skip re-init and read garbage (e.g. the
    // Catch2 CATCH_REGISTER_ENUM crash in EnumInfo::lookup).
    if (tag == DW_TAG_variable && !inside_function) {
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
                      .is_const = isConstQualifiedType(dbg, type_offset),
                    });
                    if (resolveType(dbg, type_offset, *symbol, full_type_defs)) {
                        m_symbol_descriptors.push_back(std::move(symbol));
                        m_root_symbols.push_back(std::make_unique<VariantSymbol>(
                          m_root_symbols, m_symbol_descriptors.back().get()));
                    }
                }
            }
        }
    }

    // Process function declarations (for function pointer resolution)
    if (tag == DW_TAG_subprogram) {
        std::string func_name;
        bool name_from_spec = false;

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
                        if (auto demangled = demangleLinkageName(dbg, spec_die)) {
                            func_name = std::move(*demangled);
                            name_from_spec = true;
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

        if (!func_name.empty() && !shouldSkipSymbolName(func_name)) {
            // Strip trailing args from demangled function names
            size_t paren = func_name.find('(');
            if (paren != std::string::npos) {
                func_name.resize(paren);
            }
            Dwarf_Attribute low_pc_attr = nullptr;
            if (dwarf_attr(die, DW_AT_low_pc, &low_pc_attr, &err) == DW_DLV_OK) {
                Dwarf_Addr low_pc = 0;
                dwarf_formaddr(low_pc_attr, &low_pc, &err);
                dwarf_dealloc(dbg, low_pc_attr, DW_DLA_ATTR);

                if (low_pc != 0) {
                    std::string full_name;
                    if (name_from_spec) {
                        // func_name already has the full demangled name
                        full_name = func_name;
                    } else {
                        full_name = namespace_prefix + func_name;
                    }

                    m_function_addresses[load_base + low_pc] = full_name;
                }
            }
        }
    }

    // Recursively process children
    std::string child_prefix = namespace_prefix;
    if (tag == DW_TAG_namespace && die_name != nullptr) {
        child_prefix = namespace_prefix.empty() ? std::string(die_name) + "::" : namespace_prefix + die_name + "::";
    }
    // Once we descend into a subprogram, every nested DW_TAG_variable is a
    // function-local (auto or static) and must be excluded from m_root_symbols.
    bool child_inside_function = inside_function || tag == DW_TAG_subprogram;
    if (die_name != nullptr) {
        dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
    }

    Dwarf_Die child = nullptr;
    if (dwarf_child(die, &child, &err) == DW_DLV_OK) {
        walkDieTree(dbg, child, load_base, child_prefix, module_prefix, decl_qualified_names, full_type_defs, child_inside_function);
        while (true) {
            Dwarf_Die sibling = nullptr;
            if (dwarf_siblingof_b(dbg, child, 1, &sibling, &err) != DW_DLV_OK) {
                break;
            }
            dwarf_dealloc(dbg, child, DW_DLA_DIE);
            walkDieTree(dbg, sibling, load_base, child_prefix, module_prefix, decl_qualified_names, full_type_defs, child_inside_function);
            child = sibling;
        }
        dwarf_dealloc(dbg, child, DW_DLA_DIE);
    }
}

// Pre-pass: walk a DIE tree and index every full class/struct/union/enum
// definition (has DW_AT_byte_size, not DW_AT_declaration=1) by its unqualified
// name so resolveType can follow a forward declaration in one CU to the full
// definition in another.
static void collectFullTypeDefs(Dwarf_Debug dbg,
                                Dwarf_Die die,
                                DbgSymbols::FullTypeDefs& full_type_defs) {
    Dwarf_Error err = nullptr;
    Dwarf_Half tag = 0;
    dwarf_tag(die, &tag, &err);

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
            if (dwarf_bytesize(die, &byte_size, &err) == DW_DLV_OK && byte_size > 0) {
                char* die_name = nullptr;
                if (dwarf_diename(die, &die_name, &err) == DW_DLV_OK && die_name != nullptr) {
                    Dwarf_Off offset = 0;
                    if (dwarf_dieoffset(die, &offset, &err) == DW_DLV_OK) {
                        full_type_defs.emplace(die_name, offset);
                    }
                    dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
                }
            }
        }
    }

    Dwarf_Die child = nullptr;
    if (dwarf_child(die, &child, &err) == DW_DLV_OK) {
        collectFullTypeDefs(dbg, child, full_type_defs);
        while (true) {
            Dwarf_Die sibling = nullptr;
            if (dwarf_siblingof_b(dbg, child, 1, &sibling, &err) != DW_DLV_OK) {
                break;
            }
            dwarf_dealloc(dbg, child, DW_DLA_DIE);
            collectFullTypeDefs(dbg, sibling, full_type_defs);
            child = sibling;
        }
        dwarf_dealloc(dbg, child, DW_DLA_DIE);
    }
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

    // First pass: collect full class/struct/union definitions across all CUs.
    // The second pass uses this to resolve forward declarations in one CU
    // against the full definition in another.
    FullTypeDefs full_type_defs;
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
            collectFullTypeDefs(dbg, cu_die, full_type_defs);
            dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
        }
    }

    // Second pass: walk every CU and collect global variable symbols.
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
            walkDieTree(dbg, cu_die, load_base, "", module_prefix, decl_qualified_names, full_type_defs, false);
            dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
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
    auto it = m_function_addresses.find(address);
    if (it != m_function_addresses.end()) {
        return it->second;
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
