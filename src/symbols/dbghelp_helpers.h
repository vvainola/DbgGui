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
#include <string>

using ModuleBase = uint64_t;
using TypeIndex = uint32_t;

#if WINDOWS
void printLastError();
int getBitPosition(SymbolInfo const& info);
BasicType getBasicType(SymbolInfo const& info);
void addChildrenToSymbol(RawSymbol& parent_symbol, std::map<std::pair<ModuleBase, TypeIndex>, RawSymbol*>& reference_symbols);
// Currently unused helpers
DataKind getDataKind(SymbolInfo const& info);
std::string getUndecoratedSymbolName(std::string const& name);
std::string getModuleName(ModuleBase module_base);
#endif
std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address);

inline bool startsWith(std::string const& s, std::string const& w) {
    return s.rfind(w, 0) == 0;
}

inline bool endsWith(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

inline bool shouldSkipSymbolName(std::string const& name) {
    return startsWith(name, "_")
        || startsWith(name, "std::")
        || endsWith(name, "$initializer$")
        || startsWith(name, "IID_")
        || startsWith(name, "GUID_")
        || startsWith(name, "CLSID_")
        || startsWith(name, "LIBID_")
        || startsWith(name, "FONT_ATLAS_")
        || startsWith(name, "nlohmann::")
        || startsWith(name, "Concurrency::")
        || startsWith(name, "ImPlot::")
        || startsWith(name, "Catch::")
        || name == "GImGui"
        || name == "GImPlot"
        || name == "g_ContextMap"
        || name == "imgl3wProcs"
        || name == "g_dbg_gui";
}

struct ModuleInfo {
    MemoryAddress base_address;
    size_t size;
    std::string write_time;
    std::string path;
};
ModuleInfo getCurrentModuleInfo();

std::string readFile(std::string const& filename);

#if WINDOWS
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
#else
class ScopedSymbolHandler {
  public:
    ScopedSymbolHandler() : m_initialized(false) {}
    bool initialized() const { return m_initialized; }
    ~ScopedSymbolHandler() {}

  private:
    bool m_initialized;
};
#endif
