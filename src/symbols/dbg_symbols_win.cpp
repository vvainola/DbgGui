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

#include "dbg_symbols.hpp"
#include "str_helpers.h"
#include "variant_symbol.h"
#include "dbghelp_helpers.h"

#include <nlohmann/json.hpp>
#include <DbgHelp.h>
#include <psapi.h>

BOOL CALLBACK storeSymbols(PSYMBOL_INFO pSymInfo, ULONG /*SymbolSize*/, PVOID UserContext) {
    std::string symbol_name = pSymInfo->Name;
    if (pSymInfo->TypeIndex == 0
        || (pSymInfo->Tag != SymTagData)
        || shouldSkipSymbolName(symbol_name)) {
        return TRUE;
    }

    std::vector<SymbolInfo>* symbols = (std::vector<SymbolInfo>*)UserContext;
    symbols->push_back(pSymInfo);
    return TRUE;
}

void DbgSymbols::initSymbolsFromPdb() {
    ScopedSymbolHandler symbol_handler;
    if (!symbol_handler.initialized()) {
        return;
    }

    // Collect symbol infos into vector
    std::vector<SymbolInfo> symbols;
    if (SymEnumSymbols(symbol_handler.getCurrentProcess(), // Process handle from SymInitialize.
                       0,                                  // Base address of module.
                       "*!*",                              // Name of symbols to match.
                       storeSymbols,                       // Symbol handler procedure.
                       &symbols))                          // User context.
    {
        // SymEnumSymbols succeeded
    } else {
        printLastError();
        _ASSERTE(!"Invalid symbols?");
    }
    // Symbols from other modules are included with module name as prefix because if same DLL is loaded more
    // than once within the executable, same name symbols are found for all DLLs. The symbol search will then
    // contain duplicates for every symbol and it is not possible to know which symbol belongs in which DLL.
    ModuleInfo module_info = getCurrentModuleInfo();
    std::map<uint64_t, std::string> module_names;

    // Process symbol info. Raw symbols are stored into a vector so that when adding children to symbol, the
    // children can be copied from reference symbol if children have been added to that type of symbol already
    // before. The tree structure for each type has to be then looked up only once.
    std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*> reference_symbols;
    m_raw_symbols.reserve(symbols.size());
    m_root_symbols.reserve(symbols.size());
    for (SymbolInfo const& symbol : symbols) {
        bool symbol_in_current_module = symbol.Address >= module_info.base_address
                                     && symbol.Address < module_info.base_address + module_info.size;
        if (symbol.Address == 0) {
            continue;
        }
        if (!module_names.contains(symbol.ModBase)) {
            module_names[symbol.ModBase] = getModuleName(symbol.ModBase);
        }

        std::unique_ptr<RawSymbol>& raw_symbol = m_raw_symbols.emplace_back(std::make_unique<RawSymbol>(rawSymbolFromSymInfo(symbol)));
        if (!symbol_in_current_module) {
            raw_symbol->name = std::format("{}|{}", module_names[symbol.ModBase], raw_symbol->name);
        }
        addChildrenToSymbol(*raw_symbol, reference_symbols);
        m_root_symbols.push_back(std::make_unique<VariantSymbol>(m_root_symbols, raw_symbol.get()));
    }
}

std::vector<SymbolValue> DbgSymbols::saveSnapshotToMemory() const {
    ScopedSymbolHandler scoped_symbol_handler;

    std::vector<SymbolValue> snapshot;
    std::function<void(VariantSymbol*)> save_symbol_to_snapshot = [&](VariantSymbol* sym) {
        // Add symbol value to snapshot
        VariantSymbol::Type type = sym->getType();
        if (type == VariantSymbol::Type::Arithmetic || type == VariantSymbol::Type::Enum) {
            snapshot.push_back({sym, sym->read()});
        } else if (type == VariantSymbol::Type::Pointer) {
            MemoryAddress pointed_address = sym->getPointedAddress();
            VariantSymbol* pointed_symbol = sym->getPointedSymbol();
            // Set pointer only if it points to some other global
            if (pointed_address == NULL) {
                snapshot.push_back({sym, MemoryAddress(NULL)});
            } else if (pointed_symbol) {
                snapshot.push_back({sym, sym->getPointedAddress()});
            } else {
                // Try finding the symbol with directly from address with symbol handler
                auto raw_sym = getSymbolFromAddress(pointed_address);
                if (raw_sym) {
                    snapshot.push_back({sym, pointed_address});
                }
            }
        }
        // Add all children
        for (auto const& child : sym->getChildren()) {
            save_symbol_to_snapshot(child.get());
        }
    };
    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        save_symbol_to_snapshot(sym.get());
    }

    return snapshot;
}
