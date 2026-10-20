#include "symbols/dbg_symbols.hpp"

int current_module_library_value = 42;

// Keep the symbol reader in this library rather than the test host.
DbgSymbols const& currentModuleSymbols() {
    return DbgSymbols::getSymbols();
}
