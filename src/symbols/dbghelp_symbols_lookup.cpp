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
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

BOOL CALLBACK storeSymbols(PSYMBOL_INFO pSymInfo, ULONG /*SymbolSize*/, PVOID UserContext) {
    if (pSymInfo->TypeIndex == 0
        || (pSymInfo->Tag != SymTagData)
        || startsWith(pSymInfo->Name, "_")
        || startsWith(pSymInfo->Name, "IID_")
        || startsWith(pSymInfo->Name, "CLSID_")
        || startsWith(pSymInfo->Name, "LIBID_")
        || startsWith(pSymInfo->Name, "FONT_ATLAS_")
        || startsWith(pSymInfo->Name, "nlohmann::")
        || startsWith(pSymInfo->Name, "Concurrency::")
        || startsWith(pSymInfo->Name, "ImPlot::")
        || startsWith(pSymInfo->Name, "std::")) {
        return TRUE;
    }

    std::vector<SymbolInfo>* symbols = (std::vector<SymbolInfo>*)UserContext;
    symbols->push_back(pSymInfo);
    return TRUE;
}

DbgHelpSymbols::DbgHelpSymbols(std::string symbol_json, bool omit_names_from_json) {
    bool load_ok = loadSymbolsFromJson(symbol_json);
    if (!load_ok) {
        loadSymbolsFromPdb(symbol_json, omit_names_from_json);
    }
    // Sort addresses so that lookup for pointed symbol can use binary search on addresses to find the symbol
    std::sort(m_root_symbols.begin(), m_root_symbols.end(), [](std::unique_ptr<VariantSymbol> const& l, std::unique_ptr<VariantSymbol> const& r) {
        return l->getAddress() < r->getAddress();
    });
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
                // Avoid overindexing
                if ((*it)->getChildren().size() <= idx) {
                    return nullptr;
                }

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

bool DbgHelpSymbols::loadSymbolsFromJson(std::string const& json) {
    if (!std::filesystem::exists(json)) {
        return false;
    }

    try {
        nlohmann::ordered_json symbols_json = nlohmann::ordered_json::parse(std::ifstream(json));
        if (symbols_json["md5"] != getCurrentModuleInfo().md5_hash) {
            return false;
        }

        m_root_symbols.reserve(symbols_json.size());
        for (nlohmann::ordered_json const& symbol_data : symbols_json["symbols"]) {
            RawSymbol raw_symbol(symbol_data);
            m_root_symbols.push_back(std::make_unique<VariantSymbol>(m_root_symbols, &raw_symbol));
        }
    } catch (nlohmann::json::exception err) {
        std::cerr << err.what();
        m_root_symbols.clear();
        return false;
    }

    return true;
}

void DbgHelpSymbols::loadSymbolsFromPdb(std::string const& json_to_save, bool omit_names_from_json) {
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
    if (!json_to_save.empty()) {
        saveSymbolsToJson(json_to_save, raw_symbols, omit_names_from_json);
    }
}

void DbgHelpSymbols::saveSnapshot(std::string const& json) {
    nlohmann::json snapshot;
    auto module_info = getCurrentModuleInfo();
    snapshot["md5"] = module_info.md5_hash;

    std::function<void(VariantSymbol*)> save_symbol_state = [&](VariantSymbol* sym) {
        VariantSymbol::Type type = sym->getType();
        MemoryAddress address_offset = sym->getAddress() - module_info.base_address;
        std::string key = std::format("{} {}", sym->getFullName(), address_offset);
        if (type == VariantSymbol::Type::Arithmetic || type == VariantSymbol::Type::Enum) {
            double value = sym->read();
            bool value_ok = !isnan(value) && !isinf(value);
            if (value_ok) {
                snapshot["state"][key] = sym->read();
            }
        } else if (type == VariantSymbol::Type::Pointer) {
            MemoryAddress pointed_address = sym->getPointedAddress();
            MemoryAddress pointed_address_offset = sym->getPointedAddress() - module_info.base_address;
            // Set pointer only if it points to something else within this module
            bool pointed_address_ok = pointed_address_offset < module_info.size;
            if (pointed_address == NULL) {
                snapshot["state"][key] = 0;
            }
            if (pointed_address_ok) {
                snapshot["state"][key] = pointed_address_offset;
            }
        }

        for (auto const& child : sym->getChildren()) {
            save_symbol_state(child.get());
        }
    };

    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        save_symbol_state(sym.get());
    }

    std::ofstream(json) << std::setw(4) << snapshot;
}

void DbgHelpSymbols::loadSnapshot(std::string const& json) {
    auto module_info = getCurrentModuleInfo();
    nlohmann::json snapshot = nlohmann::json::parse(std::ifstream(json));
    if (module_info.md5_hash != snapshot["md5"]) {
        std::cerr << "Snapshot has been made with different binary" << std::endl;
        return;
    }
    auto& state = snapshot["state"];

    std::function<void(VariantSymbol*)> load_symbol_state = [&](VariantSymbol* sym) {
        VariantSymbol::Type type = sym->getType();
        MemoryAddress address_offset = sym->getAddress() - module_info.base_address;
        std::string key = std::format("{} {}", sym->getFullName(), address_offset);
        if (!state.contains(key)) {
            // Do nothing
        } else if (type == VariantSymbol::Type::Arithmetic || type == VariantSymbol::Type::Enum) {
            double new_value = state[key];
            double current_value = sym->read();
            if (new_value != current_value) {
                sym->write(new_value);
            }
        } else if (type == VariantSymbol::Type::Pointer) {
            MemoryAddress current_pointed_address = sym->getPointedAddress();
            MemoryAddress new_pointed_address_offset = state[key];
            MemoryAddress new_pointed_address = new_pointed_address_offset + module_info.base_address;
            // Change pointer only if it is different
            if (new_pointed_address_offset == NULL && current_pointed_address != NULL) {
                sym->setPointedAddress(NULL);
            } else if (current_pointed_address != new_pointed_address
                       && new_pointed_address_offset != NULL) {
                sym->setPointedAddress(new_pointed_address);
            }
        }

        for (auto const& child : sym->getChildren()) {
            load_symbol_state(child.get());
        }
    };

    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        load_symbol_state(sym.get());
    }
}
