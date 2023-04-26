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

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include "global_snapshot.h"

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
    ~DbgHelpSymbols();

    /// @brief Fuzzy search for all matching symbol names in the global namespace. Exact match is
    /// always the first element. Members of a symbol are searched if the parent name is an exact
    /// match
    /// @param search_string Full or partial part of symbol name
    /// @param recursive Search also members of all symbols
    /// @param max_count Maximum number of results
    /// @return Matching symbols
    std::vector<VariantSymbol*> findMatchingSymbols(std::string const& search_string,
                                                    bool recursive = false,
                                                    int max_count = 1000) const;

    /// @brief Search for symbol that has exactly the given name.
    /// @return Symbol if found, nullptr if not found
    VariantSymbol* getSymbol(std::string const& name) const;

    /// @brief Save snapshot containing the value of all arithmetic symbols and pointers
    /// @param Filename of the json file created
    void saveSnapshotToFile(std::string const& json) const;
    std::vector<SymbolValue> saveSnapshotToMemory() const;

    /// @brief Load value of all arithmetic symbols and restore pointers if they
    /// point to something else within the module or they are null pointer
    /// @param Filename of the json file to load
    void loadSnapshotFromFile(std::string const& json) const;
    void loadSnapshotFromMemory(std::vector<SymbolValue> const snapshot) const;

  private:
    bool loadSymbolsFromJson(std::string const& json);
    void loadSymbolsFromPdb(std::string const& json_to_save, bool omit_names_from_json);

    std::vector<std::unique_ptr<VariantSymbol>> m_root_symbols;
};
