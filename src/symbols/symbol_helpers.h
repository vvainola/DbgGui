#pragma once
#include "raw_symbol.h"
#include <optional>
#include <memory>

void printLastError();

int getBitPosition(RawSymbol const& sym);
SymTagEnum getSymbolTag(SymbolInfo const& sym);
BasicType getBasicType(RawSymbol const& sym);
void addChildrenToSymbol(RawSymbol& parent_symbol);
std::string getUndecoratedSymbolName(std::string const& name);
std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address);

inline bool startsWith(std::string const& s, std::string const& w) {
    return s.rfind(w, 0) == 0;
}

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

struct ModuleInfo {
    MemoryAddress base_address;
    MemoryAddress size;
    std::string md5_hash;
};
ModuleInfo getCurrentModuleInfo();

std::string readFile(std::string const& filename);

// Currently unused helpers
DataKind getDataKind(RawSymbol const& sym);
