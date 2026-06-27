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
#include "symbol_descriptor.h"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>

#if LINUX
#include <dwarf.h>
#include <libdwarf.h>
#endif

class VariantSymbol;

class DbgSymbols {
  public:
    static DbgSymbols const& getSymbols();

    /// @brief Load global symbols from JSON file
    /// @param symbol_json JSON file from which the global symbols are loaded if it matches the current binary.
    DbgSymbols(std::string const& symbol_json);

    /// @return Symbols succesfully loaded from json file
    bool symbolsLoadedFromJson() const { return m_symbols_loaded_from_json; }

    /// @brief Save symbol info collected from PDB file into a json file that can be used for
    /// loading symbol information without PDB file later on.
    /// @param filename Filename to save
    /// @param omit_names Omit names of symbols from json file.
    /// The JSON file can then be used for saving/loading snapshot of globals without the
    /// PDB file but otherwise symbol searching does not work.
    void saveSymbolInfoToJson(std::string const& filename, bool omit_names) const;

    /// @brief Fuzzy search for matching symbol names up to the requested depth. Exact match is
    /// always the first element.
    /// @param search_string Full or partial part of symbol name
    /// @param recursion_depth Maximum nested symbol depth to search. Module-prefixed globals count as depth 1.
    /// @param max_count Maximum number of results
    /// @return Matching symbols
    std::vector<VariantSymbol*> findMatchingSymbols(std::string const& search_string,
                                                    int recursion_depth = 1,
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

    /// @brief Resolve a function address to its demangled name.
    /// @return Function name or empty string if not found.
    std::string resolveFunctionAddress(MemoryAddress address) const;

#if LINUX
    using FullTypeDefs = std::unordered_multimap<std::string, Dwarf_Off>;
#endif

  private:
    DbgSymbols();
    bool loadSymbolsFromJson(std::string const& json);
    void sortSymbols();
    void initSymbolsFromPdb();

#if LINUX
    // unordered_multimap<unqualified type name, DIE offset of full definition>:
    // populated by a pre-pass over all CUs to enable resolveType to follow a
    // forward-declared class/struct/union to its full definition in another CU.
    //
    // inside_function: true when the current DIE descends from a DW_TAG_subprogram.
    // Function-local statics live in static storage but are gated by lazy-init
    // guards (mangled `_ZGV*` symbols) that the snapshot mechanism filters out by
    // name. If we exposed the static itself, save/restore would zero the storage
    // without resetting the guard — the next use would skip re-init and read
    // garbage. So we walk into subprograms (to keep building qualified-name maps,
    // resolve function pointers, etc.) but don't add their DW_TAG_variable
    // children to the symbol list.
    void walkDieTree(Dwarf_Debug dbg,
                     Dwarf_Die die,
                     MemoryAddress load_base,
                     std::string const& namespace_prefix,
                     std::string const& module_prefix,
                     std::unordered_map<Dwarf_Off, std::string>& decl_qualified_names,
                     FullTypeDefs const& full_type_defs,
                     bool inside_function);
    void processAllCUs(Dwarf_Debug dbg,
                       MemoryAddress load_base,
                       std::string const& module_prefix = "");
#endif
    mutable std::unordered_map<MemoryAddress, std::string> m_function_addresses;
#if WINDOWS
    mutable bool m_function_addresses_loaded = false;
#endif

    std::vector<std::unique_ptr<SymbolDescriptor>> m_symbol_descriptors;
    std::vector<std::unique_ptr<VariantSymbol>> m_root_symbols;
    bool m_symbols_loaded_from_json = false;
};
