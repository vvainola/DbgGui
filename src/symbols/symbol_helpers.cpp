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

#include "symbol_helpers.h"

#include <iostream>
#include <cassert>
#include <map>
#include <Psapi.h>
#include <fstream>
#include <format>
#include <filesystem>

static HANDLE current_process = GetCurrentProcess();

void printLastError() {
    // Printing all errors causes mostly noise with "element not found" (symbols are not found for that module?)
    DWORD error = GetLastError();
    if (error) {
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     NULL,
                                     error,
                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     (LPSTR)&messageBuffer,
                                     0,
                                     NULL);

        std::string message(messageBuffer, size);
        LocalFree(messageBuffer);
        std::cerr << "Symbol search error: " << message << std::endl;
    }
}

double getVariantEnumValue(VARIANT const& variant) {
    switch (variant.vt) {
    case VT_BOOL:
        return variant.boolVal;
    case VT_INT:
        return variant.intVal;
    case VT_I1:
        return variant.cVal;
    case VT_I2:
        return variant.iVal;
    case VT_I4:
        return variant.lVal;
    case VT_I8:
        return static_cast<double>(variant.llVal); // hide warning C4244: 'return': conversion from 'LONGLONG' to 'double', possible loss of data
    case VT_UINT:
        return variant.uintVal;
    case VT_UI1:
        return variant.bVal;
    case VT_UI2:
        return variant.uiVal;
    case VT_UI4:
        return variant.ulVal;
    case VT_UI8:
        return static_cast<double>(variant.ullVal); // hide warning C4244: 'return': conversion from 'ULONGLONG' to 'double', possible loss of data
    case VT_R4:
        return variant.fltVal;
    case VT_R8:
        return variant.dblVal;
    default:
        assert(0);
        return 0;
    }
}

int getBitPosition(RawSymbol const& sym) {
    int position = NO_VALUE;
    if (!SymGetTypeInfo(current_process, sym.info.ModBase, sym.info.Index, TI_GET_BITPOSITION, &position)) {
        return NO_VALUE;
    }
    return position;
}

DataKind getDataKind(RawSymbol const& sym) {
    DataKind data_kind = DataKind::DataIsUnknown;
    if (!SymGetTypeInfo(current_process, sym.info.ModBase, sym.info.Index, TI_GET_DATAKIND, &data_kind)) {
        // printLastError();
    }
    return data_kind;
}

BasicType getBasicType(RawSymbol const& sym) {
    assert(sym.tag == SymTagBaseType || sym.tag == SymTagEnumerator);
    BasicType base_type = BasicType::btNoType;
    if (!SymGetTypeInfo(current_process, sym.info.ModBase, sym.info.TypeIndex, TI_GET_BASETYPE, &base_type)) {
        printLastError();
        assert(("Unable to get base type of symbol.", 0));
    }
    return base_type;
}

SymTagEnum getSymbolTag(SymbolInfo const& sym) {
    SymTagEnum tag = SymTagEnum::SymTagNull;
    if (!SymGetTypeInfo(current_process, sym.ModBase, sym.TypeIndex, TI_GET_SYMTAG, &tag)) {
        // printLastError();
    }
    return tag;
}

std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address) {
    if (address == NULL) {
        return nullptr;
    }

    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 dwDisplacement = 0;
    if (SymFromAddr(current_process, address, &dwDisplacement, pSymbol)) {
        return std::make_unique<RawSymbol>(pSymbol);
    } else {
        // printLastError();
        return nullptr;
    }
}

std::unique_ptr<RawSymbol> getSymbolFromIndex(DWORD index, RawSymbol const& parent) {
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;

    if (SymFromIndex(current_process, parent.info.ModBase, index, pSymbol) && pSymbol->TypeIndex != 0) {
        return std::make_unique<RawSymbol>(pSymbol);
    } else {
        // printLastError();
        return nullptr;
    }
}

void copyChildrenFromSymbol(RawSymbol const& from, RawSymbol& parent) {
    parent.children.reserve(from.children.size());
    parent.array_element_count = from.array_element_count;
    for (size_t i = 0; i < from.children.size(); ++i) {
        std::unique_ptr<RawSymbol>& new_child = parent.children.emplace_back(
            std::make_unique<RawSymbol>(*from.children[i]));

        // Recursively copy children of children
        copyChildrenFromSymbol(*from.children[i], *new_child);
    }
}

void addFirstChildToArray(RawSymbol& parent, std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*>& reference_symbols) {
    assert((parent.tag == SymTagArrayType, "Symbol is not an array."));
    ULONG64 array_size_in_bytes = 0;
    DWORD element_count = 0;
    DWORD array_typeid = 0;
    assert(SymGetTypeInfo(GetCurrentProcess(), parent.info.ModBase, parent.info.TypeIndex, TI_GET_LENGTH, &array_size_in_bytes));
    assert(SymGetTypeInfo(GetCurrentProcess(), parent.info.ModBase, parent.info.TypeIndex, TI_GET_COUNT, &element_count));
    assert(SymGetTypeInfo(GetCurrentProcess(), parent.info.ModBase, parent.info.TypeIndex, TI_GET_TYPEID, &array_typeid));
    if (array_size_in_bytes == 0 || element_count == 0) {
        return;
    }
    parent.array_element_count = element_count;
    ULONG element_size = ULONG(array_size_in_bytes / element_count);
    // Use the parent symbol as a base info and change only relevant fields to fit the array member
    SymbolInfo base = parent.info;
    base.TypeIndex = array_typeid;
    base.Size = element_size;

    // Add only first child because the rest can be added later by just adjusting memory address
    std::unique_ptr<RawSymbol>& first_child = parent.children.emplace_back(std::make_unique<RawSymbol>(base));
    addChildrenToSymbol(*first_child, reference_symbols);
}

// https://yanshurong.wordpress.com/2009/01/02/how-to-use-dbghelp-to-access-type-information-from-www-debuginfo-com/
void addChildrenToSymbol(RawSymbol& parent, std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*>& reference_symbols) {
    // Copy structure from reference symbol if children have already been looked up for same type before
    std::pair<ModuleBase, TypeIndex> modbase_and_type_idx{parent.info.ModBase, parent.info.TypeIndex};
    if (reference_symbols.find(modbase_and_type_idx) != reference_symbols.end()) {
        copyChildrenFromSymbol(*reference_symbols[modbase_and_type_idx], parent);
        return;
    } else {
        reference_symbols[modbase_and_type_idx] = &parent;
    }

    DWORD num_children = 0;
    assert(SymGetTypeInfo(current_process, parent.info.ModBase, parent.info.TypeIndex, TI_GET_CHILDRENCOUNT, &num_children));
    if (num_children == 0) {
        if (parent.tag == SymTagArrayType) {
            addFirstChildToArray(parent, reference_symbols);
        }
        return;
    }

    // Get child indices
    int find_children_size = sizeof(TI_FINDCHILDREN_PARAMS) + num_children * sizeof(ULONG);
    TI_FINDCHILDREN_PARAMS* found_children = (TI_FINDCHILDREN_PARAMS*)_alloca(find_children_size);
    memset(found_children, 0, find_children_size);
    found_children->Count = num_children;
    assert(SymGetTypeInfo(current_process, parent.info.ModBase, parent.info.TypeIndex, TI_FINDCHILDREN, found_children));

    for (DWORD i = 0; i < num_children; i++) {
        std::unique_ptr<RawSymbol> child = getSymbolFromIndex(found_children->ChildId[i], parent);
        if (child && (child->info.PdbTag == SymTagData || child->info.PdbTag == SymTagBaseClass)) {
            // Memory address offset relative to parent
            DWORD offset_to_parent = 0;
            // If parent is an enum, the names and values can be used for mapping "enum value <-> enum string"
            if (parent.tag == SymTagEnumerator) {
                VARIANT variant;       // Enumerators have their values stored as variant
                VariantInit(&variant); // Variant has to be initialized to be empty
                assert(SymGetTypeInfo(current_process, child->info.ModBase, child->info.Index, TI_GET_VALUE, &variant));
                child->info.Value = static_cast<ULONG64>(getVariantEnumValue(variant));
                parent.children.push_back(std::move(child));
                VariantClear(&variant);
            } else if (SymGetTypeInfo(current_process, child->info.ModBase, child->info.Index, TI_GET_OFFSET, &offset_to_parent)) {
                // Members by default have no address, the address is offset relative to parent
                child->offset_to_parent = offset_to_parent;

                // Skip stdlib objects e.g. std::default_delete and symbols with reserved identifier "underscore + uppercase letter"
                std::string child_name = child->info.Name;
                if (!startsWith(child_name, "std::")
                    && !(child_name.size() > 2 && child_name[0] == '_' && isupper(child_name[1]))) {
                    addChildrenToSymbol(*child, reference_symbols);
                    parent.children.push_back(std::move(child));
                }
            }
        } else if (child) {
            // Member functions could be added here but left out for now since pointers to those are probably rarely used
        }
    }
}

std::string getUndecoratedSymbolName(std::string const& name) {
    char buffer[MAX_SYM_NAME * sizeof(TCHAR)];
    UnDecorateSymbolName(name.data(), buffer, MAX_SYM_NAME, UNDNAME_NAME_ONLY);
    return std::string(buffer);
}

ModuleInfo getCurrentModuleInfo() {
    HMODULE handle = NULL;
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&getCurrentModuleInfo,
                           &handle)) {
        printLastError();
        std::abort();
    }
    MODULEINFO module_info;
    if (!GetModuleInformation(GetCurrentProcess(), handle, &module_info, sizeof(module_info))) {
        printLastError();
        std::abort();
    }
    char path[MAX_PATH];
    if (!GetModuleFileName(handle, path, sizeof(path))) {
        printLastError();
        std::abort();
    }
    return ModuleInfo {
        .base_address = (MemoryAddress)handle,
        .size = module_info.SizeOfImage,
        .write_time = std::format("{:%Y-%m-%d %H-%M-%S}", std::filesystem::last_write_time(std::string(path)))
    };
}

std::string readFile(std::string const& filename) {
    return (std::stringstream() << std::ifstream(filename).rdbuf()).str();
}
