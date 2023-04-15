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

/// @brief Save snapshot of global symbols to file using the given symbol file
/// @param symbol_json JSON file containing the symbol lookup data extracted from PDB file.
///                    If the JSON file does not exist or it is out of date, the PDB file
///                    will be used and saved to this file.
///                    Pass NULL if PDB file should always be used
/// @param snapshot_file File to save current value of all globals
/// @param omit_names Leave out names of symbols when looking up the information from PDB file
void DbgGui_saveSnapshot(const char* symbols_json, const char* snapshot_file, int omit_names);

/// @brief Load snapshot of all global symbols from file
/// @param symbols_json Symbol JSON to use for loading the values. Pass NULL if PDB file should
///                     always be used
/// @param snapshot_file File from which to load the values of globals
void DbgGui_loadSnapshot(const char* symbols_json, const char* snapshot_file);

#ifdef __cplusplus
}
#endif
