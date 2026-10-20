#include <catch2/catch_test_macros.hpp>

#include "symbols/dbg_symbols.hpp"
#include "symbols/variant_symbol.h"

int current_module_host_value = 17;

DbgSymbols const& currentModuleSymbols();

TEST_CASE("Symbols from the current shared library remain unprefixed") {
    auto const& symbols = currentModuleSymbols();

    SECTION("Own symbols are accessible and searchable without a prefix") {
        auto* own = symbols.getSymbol("current_module_library_value");
        REQUIRE(own != nullptr);
        CHECK(own->read() == 42);
        CHECK(symbols.getSymbol("current_module_library|current_module_library_value") == nullptr);

        auto const matches = symbols.findMatchingSymbols("current_module_library_value", 0);
        REQUIRE_FALSE(matches.empty());
        CHECK(matches.front() == own);
    }

    SECTION("Symbols from the host executable retain their module prefix") {
        auto* host = symbols.getSymbol("current_module_test|current_module_host_value");
        REQUIRE(host != nullptr);
        CHECK(host->read() == 17);
        CHECK(symbols.getSymbol("current_module_host_value") == nullptr);
    }
}
