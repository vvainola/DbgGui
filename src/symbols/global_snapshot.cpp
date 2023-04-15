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

void DbgGui_saveSnapshot(const char* symbols_json, const char* snapshot_file, int omit_names) {
    std::string symbols_json_name = symbols_json == NULL ? "" : symbols_json;
    DbgHelpSymbols dbghelp_symbols(symbols_json_name, omit_names);
    if (snapshot_file != NULL) {
        dbghelp_symbols.saveSnapshot(snapshot_file);
    }
}

void DbgGui_loadSnapshot(const char* symbols_json, const char* snapshot_file) {
    std::string symbols_json_name = symbols_json == NULL ? "" : symbols_json;
    DbgHelpSymbols dbghelp_symbols(symbols_json_name, true);
    dbghelp_symbols.loadSnapshot(snapshot_file);
}
