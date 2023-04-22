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

#include "global_snapshot.h"
#include "dbghelp_symbols_lookup.h"
#include "variant_symbol.h"

void* SNP_NewSymbolLookup(const char* symbols_json, int omit_names_from_json) {
    std::string symbols_json_name = symbols_json == NULL ? "" : symbols_json;
    return new DbgHelpSymbols(symbols_json_name, omit_names_from_json);
}

void SNP_DeleteSymbolLookup(void* symbol_lookup) {
    delete (DbgHelpSymbols*)symbol_lookup;
}

void SNP_saveSnapshotToFile(void* symbols, const char* snapshot_file) {
    ((DbgHelpSymbols*)symbols)->saveSnapshotToFile(snapshot_file);
}

void SNP_loadSnapshotFromFile(void* symbols, const char* snapshot_file)  {
    ((DbgHelpSymbols*)symbols)->loadSnapshotFromFile(snapshot_file);
}

std::vector<SymbolValue> SNP_saveSnapshotToMemory(void* symbols) {
    return ((DbgHelpSymbols*)symbols)->saveSnapshotToMemory();
}

void SNP_loadSnapshotFromMemory(void* symbols, std::vector<SymbolValue> const& snapshot) {
    ((DbgHelpSymbols*)symbols)->loadSnapshotFromMemory(snapshot);
}
