#pragma once
#include "raw_symbol.h"
#include <optional>
#include <memory>

void printLastError();

std::optional<DWORD> getBitPosition(RawSymbol const& sym);
SymTagEnum getSymbolTag(SymbolInfo const& sym);
BasicType getBaseType(RawSymbol const& sym);

void addChildrenToSymbol(RawSymbol& parent_symbol);
