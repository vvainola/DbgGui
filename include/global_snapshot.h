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

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Collect information of all global symbols.
/// @param symbol_json	JSON file containing the symbol lookup data extracted from PDB file.
///						If the JSON file does not exist or it is out of date, the PDB file
///						will be used and saved to this file.
///						Pass NULL if PDB file should always be used
/// @param omit_names	Leave out names of symbols when looking up the information from PDB file
/// @return Symbol information. SNP_DeleteSymbolLookup must be used for deleting the object.
void* SNP_newSymbolLookup(const char* symbols_json, int omit_names_from_json);

/// @brief Delete symbol lookup created by SNP_NewSymbolLookup
/// @param symbol_lookup Symbol information from SNP_NewSymbolLookup
void SNP_deleteSymbolLookup(void* symbol_lookup);

/// @brief Save snapshot of global symbols to file using the given symbol file
/// @param symbols			Symbol lookup from SNP_NewSymbolLookup
/// @param snapshot_file	File to save current value of all globals
void SNP_saveSnapshotToFile(void* symbols, const char* snapshot_file);

/// @brief Load snapshot of all global symbols from file
/// @param symbols			Symbol lookup from SNP_NewSymbolLookup
/// @param snapshot_file	File from which to load the values of globals
void SNP_loadSnapshotFromFile(void* symbols, const char* snapshot_file);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <vector>
#include <variant>
class VariantSymbol;
using MemoryAddress = uint64_t;
struct SymbolValue {
    VariantSymbol* symbol;
    std::variant<double, MemoryAddress> value;
};
/// @brief Save values of all global symbols into a vector
/// @param symbols Symbol lookup from SNP_NewSymbolLookup
/// @return Current values that can be loaded with SNP_loadSnapshotFromMemory
std::vector<SymbolValue> SNP_saveSnapshotToMemory(void* symbols);

/// @brief Load previously saved values of global symbols
/// @param symbols Symbol lookup from SNP_NewSymbolLookup
/// @param snapshot Values to load
void SNP_loadSnapshotFromMemory(void* symbols, std::vector<SymbolValue> const& snapshot);

#endif
