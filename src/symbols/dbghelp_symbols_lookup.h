#pragma once

#include "variant_symbol.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class DbgHelpSymbols {
  public:
    DbgHelpSymbols();

    /// <summary>Fuzzy search for all matching symbol names in the global namespace. Exact match is 
    /// always the first element. Members of symbols are not searched.</summary>
    /// <param name="search_string">Full or partial part of symbol name</param>
    std::vector<VariantSymbol*> findMatchingRootSymbols(std::string const& search_string) const;

    /// <summary>Search for symbol that has exactly the given name.</summary>
    /// <returns>Symbol if found, nullptr if not found</returns>
    VariantSymbol* getSymbol(std::string const& name) const;

    void saveState();
    void loadState();

  private:
    std::vector<std::unique_ptr<VariantSymbol>> m_root_symbols;

    struct SavedSymbol {
        VariantSymbol* symbol;
        std::variant<double, MemoryAddress> value;
    };
    std::vector<SavedSymbol> m_saved_state;
};
