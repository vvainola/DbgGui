// MIT License
//
// Copyright (c) 2025 vvainola
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

#include "test_library_loader.h"
#include <iostream>

#if WINDOWS
#include <windows.h>
#elif LINUX
#include <dlfcn.h>
#endif

TestLibraryLoader::TestLibraryLoader() {
#if WINDOWS
    handle = (void*)LoadLibraryA("test_library.dll");
    if (!handle) {
        std::cerr << "Failed to load test_library.dll, error: " << GetLastError() << std::endl;
    }
#elif LINUX
    handle = dlopen("./libtest_library.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "Failed to load libtest_library.so: " << dlerror() << std::endl;
    }
#endif
}

TestLibraryLoader::~TestLibraryLoader() {
#if WINDOWS
    if (handle) FreeLibrary((HMODULE)handle);
#elif LINUX
    if (handle) dlclose(handle);
#endif
}

// Global instance ensures the library is loaded during static initialization,
// before main() and before the DbgSymbols singleton is created.
TestLibraryLoader test_library_loader;
