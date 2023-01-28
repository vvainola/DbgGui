#pragma once
#include "raw_symbol.h"
#include <optional>
#include <memory>

void printLastError();

int getBitPosition(RawSymbol const& sym);
SymTagEnum getSymbolTag(SymbolInfo const& sym);
BasicType getBaseType(RawSymbol const& sym);

void addChildrenToSymbol(RawSymbol& parent_symbol);

std::string getUndecoratedSymbolName(std::string const& name);

// Currently unused helpers
DataKind getDataKind(RawSymbol const& sym);
std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address);

