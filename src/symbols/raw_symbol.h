#pragma once

#include "cvconst.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <memory>
#include <string>
#include <vector>

using MemoryAddress = uint64_t;
inline constexpr int NO_VALUE = -1;

struct SymbolInfo {
    SymbolInfo(SYMBOL_INFO* symbol)
        : SizeOfStruct(symbol->SizeOfStruct),
          TypeIndex(symbol->TypeIndex),
          Index(symbol->Index),
          Size(symbol->Size),
          ModBase(symbol->ModBase),
          Flags(symbol->Flags),
          Value(symbol->Value),
          Address(symbol->Address),
          Register(symbol->Register),
          Scope(symbol->Scope),
          PdbTag((SymTagEnum)symbol->Tag),
          Name(symbol->Name) {
    }
    ULONG SizeOfStruct;
    ULONG TypeIndex; // Type Index of symbol
    ULONG Index;
    ULONG Size;
    ULONG64 ModBase; // Base Address of module containing this symbol
    ULONG Flags;
    ULONG64 Value;     // Value of symbol, ValuePresent should be 1
    ULONG64 Address;   // Address of symbol including base address of module
    ULONG Register;    // register holding value or pointer to value
    ULONG Scope;       // scope of the symbol
    SymTagEnum PdbTag; // pdb classification
    std::string Name;
};

SymTagEnum getSymbolTag(SymbolInfo const& sym);
struct RawSymbol {
    RawSymbol(SymbolInfo const& symbol) :
          info(symbol),
          tag(getSymbolTag(info)) {
    }

    // Copy from another symbol
    RawSymbol(RawSymbol const& other) :
          info(other.info),
          tag(other.tag),
          offset_to_parent(other.offset_to_parent),
          array_element_count(other.array_element_count) {
    }

    SymbolInfo info;
    SymTagEnum tag;
    DWORD offset_to_parent = 0;
    // Children/members of the symbol
    std::vector<std::unique_ptr<RawSymbol>> children;
    uint32_t array_element_count = 0;
};
