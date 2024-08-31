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
#include "raw_symbol.h"
#include <optional>
#include <memory>
#include <map>
#include <iostream>

using ModuleBase = ULONG64;
using TypeIndex = ULONG;

void printLastError();

int getBitPosition(RawSymbol const& sym);
SymTagEnum getSymbolTag(SymbolInfo const& sym);
BasicType getBasicType(RawSymbol const& sym);
void addChildrenToSymbol(RawSymbol& parent_symbol, std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*>& reference_symbols);
std::string getUndecoratedSymbolName(std::string const& name);
std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address);

inline bool startsWith(std::string const& s, std::string const& w) {
    return s.rfind(w, 0) == 0;
}

inline bool endsWith(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

struct ModuleInfo {
    MemoryAddress base_address;
    MemoryAddress size;
    std::string write_time;
    std::string path;
};
ModuleInfo getCurrentModuleInfo();
std::string getModuleName(ULONG64 module_base);

std::string readFile(std::string const& filename);

// Currently unused helpers
DataKind getDataKind(RawSymbol const& sym);

class ScopedSymbolHandler {
  public:
    ScopedSymbolHandler() {
        // Symbols are not loaded until a reference is made requiring the symbols be loaded.
        // This is the fastest, most efficient way to use the symbol handler.
        SymSetOptions(SYMOPT_DEFERRED_LOADS);
        m_symbol_handler_initialized = SymInitialize(m_current_process, NULL, TRUE);
        if (!m_symbol_handler_initialized) {
            std::cerr << "SymInitialize failed with error:\n";
            printLastError();
            std::cerr << "Unable to load symbols from PDB file.\n";
        }
    }

    bool initialized() {
        return m_symbol_handler_initialized;
    }

    HANDLE getCurrentProcess() {
        return m_current_process;
    }

    ~ScopedSymbolHandler() {
        if (m_symbol_handler_initialized) {
            SymCleanup(m_current_process);
        }
    }

  private:
    bool m_symbol_handler_initialized = false;
    HANDLE m_current_process = GetCurrentProcess();
};
