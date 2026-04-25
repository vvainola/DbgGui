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
// Unlike Windows which uses DbgHelp API with PDB files, Linux uses libdwarf
// to read DWARF debug info directly from the executable's .debug_* sections.
// Also supports reading symbols from loaded shared libraries via dl_iterate_phdr.

#include "dbg_symbols.hpp"
#include "str_helpers.h"
#include "variant_symbol.h"
#include "dbghelp_helpers.h"

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
#include <set>

// ============================================================================
// Address computation helpers
// ============================================================================

// Finds the runtime base address by reading /proc/self/maps and matching it
// with the ELF section headers. DWARF addresses are relative to the ELF
// virtual address space, so we need to convert them to runtime addresses.
static MemoryAddress getDataSectionBase() {
    static MemoryAddress cached_base = 0;
    if (cached_base != 0) {
        return cached_base;
    }

    // Get the path of the current executable
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return 0;
    }
    exe_path[len] = '\0';

    // Open the ELF file to find the .data section's virtual address
    int fd = open(exe_path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    // Read ELF header
    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        return 0;
    }

    // Get section header table
    Elf64_Off shoff = ehdr.e_shoff;
    Elf64_Half shentsize = ehdr.e_shentsize;
    Elf64_Half shnum = ehdr.e_shnum;
    Elf64_Half shstrndx = ehdr.e_shstrndx;

    // Read section header string table
    lseek(fd, shoff + shstrndx * shentsize, SEEK_SET);
    Elf64_Shdr shstrhdr;
    read(fd, &shstrhdr, sizeof(shstrhdr));
    lseek(fd, shstrhdr.sh_offset, SEEK_SET);
    char* strtab = new char[shstrhdr.sh_size];
    read(fd, strtab, shstrhdr.sh_size);

    // Find .data section's virtual address in the ELF file
    Elf64_Addr data_elf_addr = 0;
    for (Elf64_Half i = 0; i < shnum; i++) {
        lseek(fd, shoff + i * shentsize, SEEK_SET);
        Elf64_Shdr shdr;
        read(fd, &shdr, sizeof(shdr));
        const char* name = strtab + shdr.sh_name;
        if (strcmp(name, ".data") == 0) {
            data_elf_addr = shdr.sh_addr;
            break;
        }
    }
    delete[] strtab;
    close(fd);

    if (data_elf_addr == 0) {
        return 0;
    }

    // Now find where the data segment is loaded in memory
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return 0;
    }
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        // Look for the data segment (rw-p) of our executable
        if (strstr(line, " rw-p ") && strstr(line, exe_path)) {
            unsigned long start, end;
            char perms[5];
            if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
                // The data segment start in memory maps to data_elf_addr in the ELF file
                // So the runtime base for any ELF virtual address is:
                // runtime_addr = runtime_data_segment_start + (elf_addr - data_elf_addr)
                cached_base = start - data_elf_addr;
                fclose(maps);
                return cached_base;
            }
        }
    }
    fclose(maps);
    return 0;
}

// Determines where the current executable was loaded in memory.
static MemoryAddress getLoadBase() {
    return getDataSectionBase();
}

// ============================================================================
// DWARF type resolution
// ============================================================================

// Map DWARF base type encoding to our BasicType enum
static BasicType encodingToBasicType(Dwarf_Unsigned encoding) {
    switch (encoding) {
        case DW_ATE_signed:
        case DW_ATE_signed_char:
            return BasicType::btInt;
        case DW_ATE_unsigned:
        case DW_ATE_unsigned_char:
            return BasicType::btUInt;
        case DW_ATE_float:
            return BasicType::btFloat;
        case DW_ATE_boolean:
            return BasicType::btBool;
        default:
            return BasicType::btNoType;
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

// Resolve a DWARF type (given by offset) and populate the RawSymbol's
// tag, size, basic_type, children, and array_element_count.
// The name, address, and offset_to_parent should already be set by the caller.
static void resolveType(Dwarf_Debug dbg, Dwarf_Off type_offset, RawSymbol& raw_sym) {
    Dwarf_Error err = nullptr;

    // Follow typedef/const/volatile chains
    type_offset = followTypeChain(dbg, type_offset);

    Dwarf_Die type_die = nullptr;
    if (dwarf_offdie_b(dbg, type_offset, 1, &type_die, &err) != DW_DLV_OK) {
        raw_sym.tag = SymTagBaseType;
        raw_sym.basic_type = BasicType::btUInt;
        raw_sym.size = 4;
        return;
    }

    Dwarf_Half tag;
    dwarf_tag(type_die, &tag, &err);

    Dwarf_Unsigned type_size = 0;
    dwarf_bytesize(type_die, &type_size, &err);

    switch (tag) {
        case DW_TAG_base_type: {
            raw_sym.tag = SymTagBaseType;
            raw_sym.size = (uint32_t)type_size;

            Dwarf_Attribute enc_attr = nullptr;
            if (dwarf_attr(type_die, DW_AT_encoding, &enc_attr, &err) == DW_DLV_OK) {
                Dwarf_Unsigned encoding = 0;
                dwarf_formudata(enc_attr, &encoding, &err);
                raw_sym.basic_type = encodingToBasicType(encoding);
                dwarf_dealloc(dbg, enc_attr, DW_DLA_ATTR);
            }
            break;
        }

        case DW_TAG_pointer_type: {
            raw_sym.tag = SymTagPointerType;
            raw_sym.size = (type_size > 0) ? (uint32_t)type_size : 8;
            break;
        }

        case DW_TAG_array_type: {
            raw_sym.tag = SymTagArrayType;

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
                auto innermost = std::make_unique<RawSymbol>("", 0, 0, SymTagNull);
                resolveType(dbg, elem_type_offset, *innermost);

                // Build nested array structure from innermost dimension outward
                // For dimensions [3, 3] with element type int32_t:
                // Build: array(3, array(3, int32_t))
                for (int i = (int)dimensions.size() - 1; i >= 1; --i) {
                    auto array_elem = std::make_unique<RawSymbol>("", 0, 0, SymTagArrayType);
                    array_elem->array_element_count = dimensions[i];
                    array_elem->size = innermost->size * dimensions[i];
                    array_elem->children.push_back(std::move(innermost));
                    innermost = std::move(array_elem);
                }

                raw_sym.array_element_count = dimensions[0];
                raw_sym.size = innermost->size * dimensions[0];
                raw_sym.children.push_back(std::move(innermost));
            }
            break;
        }

        case DW_TAG_structure_type:
        case DW_TAG_class_type:
        case DW_TAG_union_type: {
            raw_sym.tag = SymTagUDT;
            raw_sym.size = (uint32_t)type_size;

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
                        uint32_t offset = 0;
                        Dwarf_Attribute loc_attr = nullptr;
                        if (dwarf_attr(child_die, DW_AT_data_member_location, &loc_attr, &err) == DW_DLV_OK) {
                            Dwarf_Unsigned uval;
                            if (dwarf_formudata(loc_attr, &uval, &err) == DW_DLV_OK) {
                                offset = (uint32_t)uval;
                            }
                            dwarf_dealloc(dbg, loc_attr, DW_DLA_ATTR);
                        }

                        // Get member type
                        Dwarf_Attribute mem_type_attr = nullptr;
                        if (dwarf_attr(child_die, DW_AT_type, &mem_type_attr, &err) == DW_DLV_OK) {
                            Dwarf_Off member_type_offset;
                            dwarf_global_formref(mem_type_attr, &member_type_offset, &err);
                            dwarf_dealloc(dbg, mem_type_attr, DW_DLA_ATTR);

                            auto child_sym = std::make_unique<RawSymbol>(
                              member_name ? member_name : "", 0, 0, SymTagNull);
                            child_sym->offset_to_parent = offset;

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

                                resolveType(dbg, member_type_offset, *child_sym);
                                child_sym->bitfield_position = bit_pos;
                                child_sym->size = (uint32_t)bit_size;
                            } else {
                                resolveType(dbg, member_type_offset, *child_sym);
                            }

                            raw_sym.children.push_back(std::move(child_sym));
                        }

                        if (member_name) {
                            dwarf_dealloc(dbg, member_name, DW_DLA_STRING);
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
            raw_sym.tag = SymTagEnumerator;
            raw_sym.size = (uint32_t)type_size;
            raw_sym.basic_type = BasicType::btInt;

            Dwarf_Attribute underlying_type_attr = nullptr;
            if (dwarf_attr(type_die, DW_AT_type, &underlying_type_attr, &err) == DW_DLV_OK) {
                Dwarf_Off underlying_offset;
                dwarf_global_formref(underlying_type_attr, &underlying_offset, &err);
                dwarf_dealloc(dbg, underlying_type_attr, DW_DLA_ATTR);

                RawSymbol temp("", 0, 0, SymTagNull);
                resolveType(dbg, underlying_offset, temp);
                raw_sym.basic_type = temp.basic_type;
                if (raw_sym.size == 0) {
                    raw_sym.size = temp.size;
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
                        auto enum_child = std::make_unique<RawSymbol>(
                          enum_name ? enum_name : "", 0, 0, SymTagNull);
                        enum_child->enum_value = enum_const_value;
                        raw_sym.children.push_back(std::move(enum_child));
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
            raw_sym.tag = SymTagBaseType;
            raw_sym.basic_type = BasicType::btUInt;
            raw_sym.size = (type_size > 0) ? (uint32_t)type_size : 4;
            break;
        }
    }

    dwarf_dealloc(dbg, type_die, DW_DLA_DIE);
}

// ============================================================================
// Symbol collection from DWARF
// ============================================================================

// Walk the DWARF DIE tree and collect global variable symbols
void DbgSymbols::walkDieTree(Dwarf_Debug dbg, Dwarf_Die die, MemoryAddress load_base, std::string const& namespace_prefix, std::string const& module_prefix) {
    Dwarf_Error err = nullptr;
    char* die_name = nullptr;
    Dwarf_Half tag = 0;

    dwarf_diename(die, &die_name, &err);
    dwarf_tag(die, &tag, &err);

    // Process variable declarations
    if (tag == DW_TAG_variable) {
        // Get the effective name - may come from DW_AT_specification
        char* effective_name = die_name;
        bool name_from_spec = false;
        Dwarf_Off spec_die_offset = 0;

        if (die_name == nullptr) {
            Dwarf_Attribute spec_attr = nullptr;
            if (dwarf_attr(die, DW_AT_specification, &spec_attr, &err) == DW_DLV_OK) {
                dwarf_global_formref(spec_attr, &spec_die_offset, &err);
                dwarf_dealloc(dbg, spec_attr, DW_DLA_ATTR);
                if (spec_die_offset != 0) {
                    Dwarf_Die spec_die = nullptr;
                    if (dwarf_offdie_b(dbg, spec_die_offset, 1, &spec_die, &err) == DW_DLV_OK) {
                        // Get linkage name for demangling to get fully qualified name
                        Dwarf_Attribute link_attr = nullptr;
                        if (dwarf_attr(spec_die, DW_AT_linkage_name, &link_attr, &err) == DW_DLV_OK) {
                            char* link_name = nullptr;
                            dwarf_formstring(link_attr, &link_name, &err);
                            if (link_name != nullptr) {
                                int demangle_status;
                                char* demangled = abi::__cxa_demangle(link_name, nullptr, nullptr, &demangle_status);
                                if (demangle_status == 0 && demangled != nullptr) {
                                    effective_name = demangled;
                                    name_from_spec = true;
                                }
                                dwarf_dealloc(dbg, link_name, DW_DLA_STRING);
                            }
                            dwarf_dealloc(dbg, link_attr, DW_DLA_ATTR);
                        }
                        dwarf_dealloc(dbg, spec_die, DW_DLA_DIE);
                    }
                }
            }
        }

        if (effective_name != nullptr) {
            if (!startsWith(effective_name, "_") && !startsWith(effective_name, "std::")) {
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
                        std::string sym_name = module_prefix + namespace_prefix + effective_name;
                        auto raw_sym = std::make_unique<RawSymbol>(sym_name, addr, 4, SymTagNull);
                        resolveType(dbg, type_offset, *raw_sym);

                        m_raw_symbols.push_back(std::move(raw_sym));
                        m_root_symbols.push_back(std::make_unique<VariantSymbol>(
                          m_root_symbols, m_raw_symbols.back().get()));
                    }
                }
            }

            if (name_from_spec) {
                free(effective_name);
            }
        }
    }

    if (die_name != nullptr) {
        dwarf_dealloc(dbg, die_name, DW_DLA_STRING);
    }

    // Recursively process children
    std::string child_prefix = namespace_prefix;
    if (tag == DW_TAG_namespace && die_name != nullptr) {
        child_prefix = namespace_prefix.empty() ? std::string(die_name) + "::" : namespace_prefix + die_name + "::";
    }
    Dwarf_Die child = nullptr;
    if (dwarf_child(die, &child, &err) == DW_DLV_OK) {
        walkDieTree(dbg, child, load_base, child_prefix, module_prefix);
        while (true) {
            Dwarf_Die sibling = nullptr;
            if (dwarf_siblingof_b(dbg, child, 1, &sibling, &err) != DW_DLV_OK) {
                break;
            }
            dwarf_dealloc(dbg, child, DW_DLA_DIE);
            walkDieTree(dbg, sibling, load_base, child_prefix, module_prefix);
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
            walkDieTree(dbg, cu_die, load_base, "", module_prefix);
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

// ============================================================================
// Snapshot support
// ============================================================================

std::vector<SymbolValue> DbgSymbols::saveSnapshotToMemory() const {
    std::vector<SymbolValue> snapshot;
    std::function<void(VariantSymbol*)> save_symbol_to_snapshot = [&](VariantSymbol* sym) {
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
