#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class VariantSymbol;

class DbgHelpSymbols {
  public:
    /// @brief Load global symbols from JSON or PDB file
    /// @param symbol_json 
    /// JSON file from which the global symbols are loaded if it matches the
    /// current binary. If it doesn't, symbols are loaded from the pdb file and saved into this file.
    /// @param omit_names_from_json 
    /// Leave out names of symbols from the symbols JSON.
    /// The JSON file can then be used for saving/loading snapshot of globals without the 
    /// PDB file but otherwise symbol searching does not work.
    DbgHelpSymbols(std::string symbol_json = "", bool omit_names_from_json = false);

    /// @brief Fuzzy search for all matching symbol names in the global namespace. Exact match is 
    /// always the first element. Members of symbols are not searched.
    /// @param search_string Full or partial part of symbol name
    /// @return Matching symbols
    std::vector<VariantSymbol*> findMatchingRootSymbols(std::string const& search_string) const;

    /// @brief Search for symbol that has exactly the given name.
    /// @return Symbol if found, nullptr if not found
    VariantSymbol* getSymbol(std::string const& name) const;

    /// @brief Save snapshot containing the value of all arithmetic symbols and pointers
    /// @param Filename of the json file created 
    void saveSnapshot(std::string const& json);

    /// @brief Load value of all arithmetic symbols and restore pointers if they
    /// point to something else within the module or they are null pointer 
    /// @param Filename of the json file to load 
    void loadSnapshot(std::string const& json);

  private:
    bool loadSymbolsFromJson(std::string const& json);
    void loadSymbolsFromPdb(std::string const& json_to_save, bool omit_names_from_json);

    std::vector<std::unique_ptr<VariantSymbol>> m_root_symbols;
};
