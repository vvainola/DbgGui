#include "dbghelp_symbols_lookup.h"

#pragma warning(push, 0)
#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"
#pragma warning(pop)

#include "variant_symbol.h"
#include "symbol_helpers.h"
#include <DbgHelp.h>
#include <cassert>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <format>

bool startsWith(std::string const& s, std::string const& w) {
    return s.rfind(w, 0) == 0;
}

BOOL CALLBACK storeSymbols(PSYMBOL_INFO pSymInfo, ULONG /*SymbolSize*/, PVOID UserContext) { 
    if (pSymInfo->TypeIndex == 0
        || (pSymInfo->Tag != SymTagData)
        || startsWith(pSymInfo->Name, "_")
        || startsWith(pSymInfo->Name, "IID_")
        || startsWith(pSymInfo->Name, "CLSID_")
        || startsWith(pSymInfo->Name, "std::")) {
        return TRUE;
    }

    std::vector<SymbolInfo>* symbols = (std::vector<SymbolInfo>*)UserContext;
    symbols->push_back(pSymInfo);
    return TRUE;
}

DbgHelpSymbols::DbgHelpSymbols() {
    // Symbols are not loaded until a reference is made requiring the symbols be loaded.
    // This is the fastest, most efficient way to use the symbol handler.
    SymSetOptions(SYMOPT_DEFERRED_LOADS);

    HANDLE current_process = GetCurrentProcess();
    SymInitialize(current_process, NULL, TRUE);

    // Collect symbol infos into vector
    std::vector<SymbolInfo> symbols;
    if (SymEnumSymbols(current_process, // Process handle from SymInitialize.
                       0,               // Base address of module.
                       "*!*",           // Name of symbols to match.
                       storeSymbols,    // Symbol handler procedure.
                       &symbols))       // User context.
    {
        // SymEnumSymbols succeeded
    } else {
        printLastError();
        _ASSERTE(!"Invalid symbols?");
    }

    // Process symbol info. Raw symbols are stored into a vector so that when adding children to symbol, the
    // children can be copied from reference symbol if children have been added to that type of symbol already
    // before. The tree structure for each type has to be then looked up only once.
    std::vector<std::unique_ptr<RawSymbol>> raw_symbols;
    raw_symbols.reserve(symbols.size());
    m_root_symbols.reserve(symbols.size());
    for (SymbolInfo const& symbol : symbols) {
        if (symbol.Address == 0) {
            continue;
        }

        std::unique_ptr<RawSymbol>& raw_symbol = raw_symbols.emplace_back(std::make_unique<RawSymbol>(symbol));
        addChildrenToSymbol(*raw_symbol);
        m_root_symbols.push_back(std::make_unique<VariantSymbol>(m_root_symbols, raw_symbol.get()));
    }

    // Sort addresses so that lookup for pointed symbol can use binary search on addresses to find the symbol
    std::sort(m_root_symbols.begin(), m_root_symbols.end(), [](std::unique_ptr<VariantSymbol> const& l, std::unique_ptr<VariantSymbol> const& r) {
        return l->getAddress() < r->getAddress();
    });
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

VariantSymbol* DbgHelpSymbols::getSymbol(std::string const& name) const {
    std::vector<std::string> split_name = split(name, '.');
    std::vector<std::unique_ptr<VariantSymbol>> const* container = &m_root_symbols;
    for (size_t i = 0; i < split_name.size(); ++i) {
        std::string const& member_name = split_name[i];
        bool is_array = member_name.ends_with(']');
        if (is_array) {
            auto left_bracket = member_name.rfind('[');
            // Leave out brackets from [xx]
            size_t idx_char_cnt = (member_name.size() - 1) - (left_bracket + 1);
            size_t idx = std::stoull(member_name.substr(left_bracket + 1, idx_char_cnt));
            // Pick name from name[xx] so leave out 2 characters to omit the brackets
            std::string array_name = member_name.substr(0, (member_name.size()) - idx_char_cnt - 2);
            // Try find member
            auto it = std::find_if(container->begin(), container->end(), [&](std::unique_ptr<VariantSymbol> const& p) {
                return p->getName() == array_name;
            });
            if (it != container->end()) {
                if (i == split_name.size() - 1) {
                    // Last section -> return pointer to member
                    return (*it)->getChildren()[idx].get();
                } else {
                    // Try find next part of name from children
                    container = &(*it)->getChildren()[idx]->getChildren();
                }
            } else {
                return nullptr;
            }
        } else {
            // Try find member
            auto it = std::find_if(container->begin(), container->end(), [&](std::unique_ptr<VariantSymbol> const& p) {
                return p->getName() == member_name;
            });
            if (it != container->end()) {
                if (i == split_name.size() - 1) {
                    // Last section -> return pointer to member
                    return it->get();
                } else {
                    // Try find next part of name from children
                    container = &(*it)->getChildren();
                }
            } else {
                return nullptr;
            }
        }
    }
    return nullptr;
}

std::vector<VariantSymbol*> DbgHelpSymbols::findMatchingRootSymbols(std::string const& name) const {
    std::vector<VariantSymbol*> matching_symbols;
    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        // Exact match is shown first
        if (name == sym->getName()) {
            matching_symbols.insert(matching_symbols.begin(), sym.get());
        } else if (fts::fuzzy_match_simple(name.c_str(), sym->getName().c_str())) {
            matching_symbols.push_back(sym.get());
        }
    }
    return matching_symbols;
}

void DbgHelpSymbols::saveState() {
    //std::ofstream f("temp.txt");
    m_saved_state.clear();
    std::function<void(VariantSymbol*)> save_symbol_state = [&](VariantSymbol* sym) {
        VariantSymbol::Type type = sym->getType();
        if (type == VariantSymbol::Type::Arithmetic || type == VariantSymbol::Type::Enum) {
            //f << std::format("{} = {}\n", sym->getFullName(), sym->read());
            m_saved_state.push_back({sym, sym->read()});
        } else if (type == VariantSymbol::Type::Pointer) {
            //f << std::format("{} = {}\n", sym->getFullName(), sym->getPointedAddress());
            m_saved_state.push_back({sym, sym->getPointedAddress()});
        }

        for (auto const& child : sym->getChildren()) {
            save_symbol_state(child.get());
        }
    };

    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        save_symbol_state(sym.get());
    }
}

void DbgHelpSymbols::loadState() {
    for (SavedSymbol& saved_symbol : m_saved_state) {
        std::visit(
            [=](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, MemoryAddress>) {
                    if (saved_symbol.symbol->getPointedAddress() != value) {
                        saved_symbol.symbol->setPointedAddress(value);
                    }
                } else {
                    if (saved_symbol.symbol->read() != value) {
                        saved_symbol.symbol->write(value);
                    }
                }
            },
            saved_symbol.value);
    }
}
