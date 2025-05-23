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
#include "str_helpers.h"

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
#include <psapi.h>

BOOL CALLBACK storeSymbols(PSYMBOL_INFO pSymInfo, ULONG /*SymbolSize*/, PVOID UserContext) {
    std::string symbol_name = pSymInfo->Name;
    if (pSymInfo->TypeIndex == 0
        || (pSymInfo->Tag != SymTagData)
        || startsWith(symbol_name, "_")
        || startsWith(symbol_name, "std::")
        || endsWith(symbol_name, "$initializer$")
        || startsWith(symbol_name, "IID_")
        || startsWith(symbol_name, "GUID_")
        || startsWith(symbol_name, "CLSID_")
        || startsWith(symbol_name, "LIBID_")
        || startsWith(symbol_name, "FONT_ATLAS_")
        || startsWith(symbol_name, "nlohmann::")
        || startsWith(symbol_name, "Concurrency::")
        || startsWith(symbol_name, "ImPlot::")
        || startsWith(symbol_name, "Catch::")
        || symbol_name == "GImGui"
        || symbol_name == "GImPlot"
        || symbol_name == "imgl3wProcs"
        || symbol_name == "g_dbg_gui") {
        return TRUE;
    }

    std::vector<SymbolInfo>* symbols = (std::vector<SymbolInfo>*)UserContext;
    symbols->push_back(pSymInfo);
    return TRUE;
}

void DbgHelpSymbols::saveSymbolInfoToJson(std::string const& filename, bool omit_names) const {
    saveSymbolsToJson(filename, m_raw_symbols, omit_names);
}

DbgHelpSymbols::DbgHelpSymbols() {
    loadSymbolsFromPdb();
    // Sort addresses so that lookup for pointed symbol can use binary search on addresses to find the symbol
    std::sort(m_root_symbols.begin(), m_root_symbols.end(), [](std::unique_ptr<VariantSymbol> const& l, std::unique_ptr<VariantSymbol> const& r) {
        return l->getAddress() < r->getAddress();
    });
}

DbgHelpSymbols::DbgHelpSymbols(std::string const& symbol_json) {
    m_symbols_loaded_from_json = loadSymbolsFromJson(symbol_json);
    // Sort addresses so that lookup for pointed symbol can use binary search on addresses to find the symbol
    std::sort(m_root_symbols.begin(), m_root_symbols.end(), [](std::unique_ptr<VariantSymbol> const& l, std::unique_ptr<VariantSymbol> const& r) {
        return l->getAddress() < r->getAddress();
    });
}

DbgHelpSymbols const& DbgHelpSymbols::getSymbolsFromPdb() {
    static DbgHelpSymbols dbghelp_symbols;
    return dbghelp_symbols;
}

// For name[2][3][4] return {2, 3, 4}
std::vector<size_t> getArrayIndices(std::string const& s) {
    std::vector<size_t> indices;
    size_t start_idx = s.find('[');              // find the first '[' character
    while (start_idx != std::string::npos) {
        size_t end_idx = s.find(']', start_idx); // find the corresponding ']' character
        if (end_idx == std::string::npos) {
            return {};                           // error: no matching ']' found
        }

        std::string idx_str = s.substr(start_idx + 1, end_idx - start_idx - 1); // extract the index string
        int index;
        try {
            index = stoi(idx_str); // convert the index string to an integer
        } catch (...) {
            return {};             // error: index is not a valid integer
        }
        indices.push_back(index);
        start_idx = s.find('[', end_idx); // find the next '[' character
    }
    return indices;
}

VariantSymbol* DbgHelpSymbols::getSymbol(std::string const& name) const {
    std::vector<std::string> split_name = str::split(name, '.');
    std::vector<std::unique_ptr<VariantSymbol>> const* container = &m_root_symbols;
    for (size_t i = 0; i < split_name.size(); ++i) {
        bool last_section = (i == split_name.size() - 1);
        std::string const& member_name = split_name[i];
        bool is_array = member_name.ends_with(']');
        if (is_array) {
            auto left_bracket = member_name.find('[');
            // Pick "name" from "name[xx]"
            std::string array_name = member_name.substr(0, left_bracket);
            // Try find member
            auto it = std::find_if(container->begin(), container->end(), [&](std::unique_ptr<VariantSymbol> const& p) {
                return p->getName() == array_name;
            });
            if (it != container->end()) {
                VariantSymbol* sym = it->get();
                for (size_t idx : getArrayIndices(member_name)) {
                    if (idx < sym->getChildren().size()) {
                        sym = sym->getChildren()[idx].get();
                    } else {
                        return nullptr;
                    }
                }
                if (last_section) {
                    return sym;
                } else {
                    container = &sym->getChildren();
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
                if (last_section) {
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

std::vector<VariantSymbol*> DbgHelpSymbols::findMatchingSymbols(std::string const& name,
                                                                bool recursive,
                                                                int max_count) const {
    std::vector<VariantSymbol*> matching_symbols;
    // Find from all symbols, can be pretty slow
    if (recursive) {
        std::function<void(VariantSymbol*)> find_matching_recursively = [&](VariantSymbol* sym) {
            // Exact match is shown first
            if (name == sym->getFullName()) {
                matching_symbols.insert(matching_symbols.begin(), sym);
            } else if (matching_symbols.size() < max_count
                       && fts::fuzzy_match_simple(name.c_str(), sym->getFullName().c_str())) {
                matching_symbols.push_back(sym);
            }
            // Find children
            for (std::unique_ptr<VariantSymbol> const& child : sym->getChildren()) {
                find_matching_recursively(child.get());
            }
        };
        for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
            find_matching_recursively(sym.get());
        }
        return matching_symbols;
    }

    std::vector<std::unique_ptr<VariantSymbol>> const* symbols_to_search = &m_root_symbols;
    std::string name_to_search = name;

    // Search only members of a symbol if the name contains "."
    size_t idx = name.rfind('.');
    if (idx != std::string::npos) {
        std::string parent_name = name.substr(0, idx);
        VariantSymbol* parent = getSymbol(parent_name);
        if (parent) {
            symbols_to_search = &parent->getChildren();
            name_to_search = name.substr(idx + 1, name.size());
        }
    }

    for (std::unique_ptr<VariantSymbol> const& sym : *symbols_to_search) {
        // Exact match is shown first
        if (name_to_search == sym->getName()) {
            matching_symbols.insert(matching_symbols.begin(), sym.get());
        } else if (matching_symbols.size() < max_count
                   && fts::fuzzy_match_simple(name_to_search.c_str(), sym->getName().c_str())) {
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
        nlohmann::json symbols_json = nlohmann::json::parse(std::ifstream(json));
        if (symbols_json["write_time"] != getCurrentModuleInfo().write_time) {
            return false;
        }

        m_root_symbols.reserve(symbols_json.size());
        for (nlohmann::json const& symbol_data : symbols_json["symbols"]) {
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

void DbgHelpSymbols::loadSymbolsFromPdb() {
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

        std::unique_ptr<RawSymbol>& raw_symbol = m_raw_symbols.emplace_back(std::make_unique<RawSymbol>(symbol));
        if (!symbol_in_current_module) {
            raw_symbol->info.Name = std::format("{}|{}", module_names[symbol.ModBase], raw_symbol->info.Name);
        }
        addChildrenToSymbol(*raw_symbol, reference_symbols);
        m_root_symbols.push_back(std::make_unique<VariantSymbol>(m_root_symbols, raw_symbol.get()));
    }
}

void DbgHelpSymbols::saveSnapshotToFile(std::string const& json) const {
    nlohmann::json snapshot;
    auto module_info = getCurrentModuleInfo();
    snapshot["write_time"] = module_info.write_time;

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

std::vector<SymbolValue> DbgHelpSymbols::saveSnapshotToMemory() const {
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

void DbgHelpSymbols::loadSnapshotFromFile(std::string const& json) const {
    auto module_info = getCurrentModuleInfo();
    nlohmann::json snapshot = nlohmann::json::parse(std::ifstream(json));
    if (module_info.write_time != snapshot["write_time"]) {
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

void DbgHelpSymbols::loadSnapshotFromMemory(std::vector<SymbolValue> const snapshot) const {
    for (SymbolValue symbol_snapshot : snapshot) {
        VariantSymbol* sym = symbol_snapshot.symbol;
        std::visit(
          [=](auto&& value) {
              using T = std::decay_t<decltype(value)>;
              // Change value only if it is different because trying to write const variables causes crash
              // and there seems to be no easy way to determine if a symbol is const
              if constexpr (std::is_same_v<T, MemoryAddress>) {
                  MemoryAddress current_pointed_address = sym->getPointedAddress();
                  MemoryAddress new_pointed_address = value;
                  if (current_pointed_address != new_pointed_address) {
                      sym->setPointedAddress(new_pointed_address);
                  }
              } else {
                  if (sym->read() != value && !std::isnan(value)) {
                      sym->write(value);
                  }
              }
          },
          symbol_snapshot.value);
    }
}
