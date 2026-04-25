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

#include "dbghelp_helpers.h"
#include "dbg_symbols.hpp"

#include <fstream>
#include <sstream>
#include <climits>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <elf.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <cxxabi.h>

ModuleInfo getCurrentModuleInfo() {
    static ModuleInfo cached;
    static bool initialized = false;
    if (initialized)
        return cached;

    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        initialized = true;
        return cached;
    }
    exe_path[len] = '\0';
    cached.path = exe_path;

    // Get write time from file modification time
    struct stat st;
    if (stat(exe_path, &st) == 0) {
        cached.write_time = std::to_string(st.st_mtime);
    }

    // Find base address (lowest start) and size from /proc/self/maps.
    // The base address is used to convert between runtime addresses and
    // file-relative offsets in JSON serialization.
    // The .bss section (uninitialized globals) is mapped as anonymous pages
    // contiguous with the file-backed data segment, so we must also include
    // anonymous rw-p mappings that immediately follow the executable's mappings.
    MemoryAddress lowest_start = ULONG_MAX;
    MemoryAddress highest_end = 0;
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[512];
        while (fgets(line, sizeof(line), maps)) {
            unsigned long start, end;
            if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
                if (strstr(line, exe_path)) {
                    if (start < lowest_start) {
                        lowest_start = start;
                    }
                    if (end > highest_end) {
                        highest_end = end;
                    }
                } else if (start == highest_end && strstr(line, " rw-p ")) {
                    // Anonymous rw-p mapping contiguous with exe (.bss segment)
                    highest_end = end;
                }
            }
        }
        fclose(maps);
    }
    if (lowest_start != ULONG_MAX) {
        cached.base_address = lowest_start;
        cached.size = highest_end - lowest_start;
    } else {
        cached.base_address = 0;
        cached.size = 0;
    }

    initialized = true;
    return cached;
}

std::string getModuleName(ModuleBase module_base) {
    return "";
}

std::string getUndecoratedSymbolName(std::string const& name) {
    return name;
}

std::unique_ptr<RawSymbol> getSymbolFromAddress(MemoryAddress address) {
    std::string const& resolved = DbgSymbols::getSymbols().resolveFunctionAddress(address);
    if (!resolved.empty()) {
        return std::make_unique<RawSymbol>(resolved, address, 0, SymTagPublicSymbol);
    }

    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(address), &info) == 0 || info.dli_sname == nullptr) {
        return nullptr;
    }

    std::string name;
    int demangle_status;
    char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &demangle_status);
    if (demangle_status == 0 && demangled != nullptr) {
        name = demangled;
        free(demangled);
    } else {
        name = info.dli_sname;
    }

    return std::make_unique<RawSymbol>(name, reinterpret_cast<MemoryAddress>(info.dli_saddr), 0, SymTagPublicSymbol);
}

std::string readFile(std::string const& filename) {
    return (std::stringstream() << std::ifstream(filename).rdbuf()).str();
}

void printLastError() {
}
