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

#pragma once

#include "DbgGui/global_snapshot.h"
#include "dbghelp_symbols_lookup.h"
#include "variant_symbol.h"
#include <iostream>

static double getSourceValue(ValueSource src) {
    return std::visit(
      [=](auto&& src) {
          using T = std::decay_t<decltype(src)>;
          if constexpr (std::is_same_v<T, ReadWriteFn>) {
              return src(std::nullopt);
          } else if constexpr (std::is_same_v<T, ReadWriteFnCustomStr>) {
              return src(std::nullopt).second;
          } else {
              return static_cast<double>(*src);
          }
      },
      src);
}

void* SNP_getSymbolsFromPdb() {
    return (void*)&DbgHelpSymbols::getSymbolsFromPdb();
}
void* SNP_getSymbolsFromJson(const char* symbols_json) {
    DbgHelpSymbols* symbols = new DbgHelpSymbols(symbols_json);
    if (!symbols->symbolsLoadedFromJson()) {
        delete (DbgHelpSymbols*)symbols;
        symbols = nullptr;
    }
    return symbols;
}

void SNP_deleteSymbolLookup(void* symbols) {
    if (((DbgHelpSymbols*)symbols)->symbolsLoadedFromJson()) {
        delete (DbgHelpSymbols*)symbols;
    }
}

void SNP_saveSymbolInfoToJson(void* symbols, const char* symbols_file, int omit_names) {
    ((DbgHelpSymbols*)symbols)->saveSymbolInfoToJson(symbols_file, omit_names);
}

void SNP_saveSnapshotToFile(void* symbols, const char* snapshot_file) {
    ((DbgHelpSymbols*)symbols)->saveSnapshotToFile(snapshot_file);
}

void SNP_loadSnapshotFromFile(void* symbols, const char* snapshot_file) {
    ((DbgHelpSymbols*)symbols)->loadSnapshotFromFile(snapshot_file);
}

std::vector<SymbolValue> SNP_saveSnapshotToMemory(void* symbols) {
    return ((DbgHelpSymbols*)symbols)->saveSnapshotToMemory();
}

void SNP_loadSnapshotFromMemory(void* symbols, std::vector<SymbolValue> const& snapshot) {
    ((DbgHelpSymbols*)symbols)->loadSnapshotFromMemory(snapshot);
}

std::function<double(void)> SNP_getSymbolReadFn(std::string const& symbol_name, void* symbols) {
    if (symbols == nullptr) {
        symbols = SNP_getSymbolsFromPdb();
    }
    VariantSymbol* sym = ((DbgHelpSymbols*)symbols)->getSymbol(symbol_name);
    if (sym) {
        return [=]() {
            return getSourceValue(sym->getValueSource());
        };
    } else {
        std::cerr << "Symbol \"" << symbol_name << "\" not found\n";
        assert(sym != nullptr);
        return []() {
            return 0;
        };
    }
}
