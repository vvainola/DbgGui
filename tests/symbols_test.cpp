#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include "DbgGui/global_snapshot.h"
#include "symbols/variant_symbol.h"
#include "symbols/dbg_symbols.hpp"

#include "test_library_loader.h"
#include "fwd_decl_types.h"

#include <algorithm>
#include <filesystem>
#include <random>

using Approx = Catch::Approx;

std::random_device rd;
std::mt19937 gen(rd());

template <typename T>
T random() {
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
        std::uniform_real_distribution<T> dis(-1, 1);
        return T(1e10) * dis(gen);
    } else {
        std::uniform_int_distribution<T> dis(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
        return dis(gen);
    }
}

union BitField {
    uint32_t u32;
    struct {
        uint32_t b0 : 7;
        uint32_t b9 : 17;
    };
};

struct A {
    int m_a = 2;
};

struct B {
    double m_b = 1;
    A a;
};

void test_fn1();

struct ResetBase {
    int base_value = 0;
    double base_double = 0;
};

struct ResetDerived : ResetBase {
    int derived_value = 0;
};

struct ResetDoubleDerived : ResetDerived {
    int double_derived_value = 0;
};

enum Enumeration {
    EnumValue_1n = -1,
    EnumValue0 = 0,
    EnumValue1 = 1,
    EnumValue2 = 2
};

void test_fn1() {}

namespace pdb_collision {
struct A {
    void hello() {};
    int m_a = 2;
    void (*m_ap)() = &A::f;
    static void f() {};
};

struct B {
    double m_b = 1;
    A a;
    void (*m_ap)() = &A::f;
};

struct C {
    A a;
    B b;
    float m_c = 0;
    B m_d[3];
    B m_e[3][3];
};

C a_struct;
} // namespace pdb_collision

// Define global variables of different types
int g_int;
namespace g {
A g_a;
B g_b1;
B g_b2;
B g_b_array[2];
float g_float;
void test_fn2() {}
void test_fn3(int) {}
} // namespace g
double g_double1;
double g_double2;
double g_multidim_double[5][5];
double* g_double_ptr;
void (*g_fn_ptr)();
void (*g_fn_ptr2)();
void (*g_fn_ptr3)(int);
BitField g_bitfield;
Enumeration g_enum;
ResetDerived g_reset_derived;
ResetDoubleDerived g_reset_double_derived;

// A type with a non-trivial constructor forces GCC to emit the variable's
// definition as a top-level DW_TAG_variable carrying DW_AT_specification +
// DW_AT_location, with the declaration nested inside the namespace DIE. When
// such a variable is also `static`, the definition DIE has no DW_AT_name and
// no DW_AT_linkage_name (internal linkage → no mangled symbol), so the symbol
// walker must recover the qualified name by following the specification back
// to the namespace-nested declaration.
struct StaticCtorType {
    StaticCtorType() : value(123) {}
    int value;
};

namespace static_ns {
static int s_int = 7;
static double s_double = 1.5;
static A s_a;
static B s_b;
static StaticCtorType s_ctor;
static int s_array[3] = {10, 20, 30};
} // namespace static_ns

// FwdDeclOuter contains a FwdDeclInner member. This TU (symbols_test.cpp) does
// not define any FwdDeclInner methods, so GCC's limited-debug-info default
// emits FwdDeclInner as DW_AT_declaration=1 in this TU's DWARF. The full
// definition is carried by fwd_decl_types.cpp. The symbol walker must follow
// the forward declaration to the full definition in order to size up the
// member.
FwdDeclOuter g_fwd_outer;

// A function with a function-local static — same pattern Catch2 uses inside
// CATCH_REGISTER_ENUM. The static must NOT be exposed by DbgSymbols, because
// its lazy-init guard (a `_ZGV*` symbol) is filtered out by name and so a
// snapshot save/restore would zero the static's storage without resetting
// the guard — the next use would skip re-init and dereference garbage.
int* getLocalStaticPtr() {
    static int  s_local_static_int = 12345;
    static int* s_local_static_ptr = &s_local_static_int;
    return s_local_static_ptr;
}

TEST_CASE("Basic symbol access") {
    DbgSymbols const& symbols = DbgSymbols::getSymbols();
    g_int = random<int>();
    VariantSymbol* g_int_sym = symbols.getSymbol("g_int");
    CHECK(g_int_sym->read() == g_int);

    g::g_float = random<float>();
    VariantSymbol* g_float_sym = symbols.getSymbol("g::g_float");
    REQUIRE(g_float_sym != nullptr);
    CHECK(g_float_sym->read() == Approx(g::g_float));

    g_enum = EnumValue2;
    VariantSymbol* g_enum_sym = symbols.getSymbol("g_enum");
    CHECK(g_enum_sym->read() == static_cast<int>(g_enum));
    CHECK(g_enum_sym->valueAsStr() == "EnumValue2");

    g_enum = EnumValue0;
    CHECK(g_enum_sym->valueAsStr() == "EnumValue0");

    g_enum = EnumValue1;
    CHECK(g_enum_sym->valueAsStr() == "EnumValue1");

    g_enum = EnumValue_1n;
    CHECK(g_enum_sym->valueAsStr() == "EnumValue_1n");

    VariantSymbol* g_fn_ptr_sym = symbols.getSymbol("g_fn_ptr");
    g_fn_ptr = &test_fn1;
    CHECK(g_fn_ptr_sym->valueAsStr() == "test_fn1");
    g_fn_ptr = &g::test_fn2;
    CHECK(g_fn_ptr_sym->valueAsStr() == "g::test_fn2");

    VariantSymbol* g_fn_ptr3_sym = symbols.getSymbol("g_fn_ptr3");
    g_fn_ptr3 = &g::test_fn3;
    CHECK(g_fn_ptr3_sym->valueAsStr() == "g::test_fn3");

    VariantSymbol* g_a_sym = symbols.getSymbol("g::g_a");
    CHECK(g_a_sym->getChildren().size() == 1);

    VariantSymbol* g_b1_sym = symbols.getSymbol("g::g_b1");
    CHECK(g_b1_sym->getChildren().size() == 2);
    VariantSymbol* g_b2_sym = symbols.getSymbol("g::g_b2");
    CHECK(g_b2_sym->getChildren().size() == 2);

    VariantSymbol* g_b1_a_sym = symbols.getSymbol("g::g_b1.a");
    CHECK(g_b1_a_sym->getChildren().size() == 1);
    VariantSymbol* g_b2_a_sym = symbols.getSymbol("g::g_b2.a");
    CHECK(g_b2_a_sym->getChildren().size() == 1);

    g::g_b_array[0].a.m_a = 1;
    g::g_b_array[1].a.m_a = 2;
    VariantSymbol* g_b_array_member_sym = symbols.getSymbol("g::g_b_array[1].a.m_a");
    REQUIRE(g_b_array_member_sym != nullptr);
    CHECK(g_b_array_member_sym->read() == g::g_b_array[1].a.m_a);

    pdb_collision::a_struct.m_d[0].a.m_a = 1;
    pdb_collision::a_struct.m_d[1].a.m_a = 2;
    VariantSymbol* a_struct_member_sym = symbols.getSymbol("pdb_collision::a_struct.m_d[1].a.m_a");
    REQUIRE(a_struct_member_sym != nullptr);
    CHECK(a_struct_member_sym->read() == pdb_collision::a_struct.m_d[1].a.m_a);

    g_reset_derived.base_value = random<int>();
    g_reset_derived.base_double = random<double>();
    VariantSymbol* derived_sym = symbols.getSymbol("g_reset_derived");
    REQUIRE(derived_sym != nullptr);

    VariantSymbol* base_value_sym = symbols.getSymbol("g_reset_derived.base_value");
    VariantSymbol* base_double_sym = symbols.getSymbol("g_reset_derived.base_double");
    REQUIRE(base_value_sym != nullptr);
    REQUIRE(base_double_sym != nullptr);
    CHECK(base_value_sym->getFullName() == "g_reset_derived.base_value");
    CHECK(base_double_sym->getFullName() == "g_reset_derived.base_double");
    CHECK(base_value_sym->read() == g_reset_derived.base_value);
    CHECK(base_double_sym->read() == Approx(g_reset_derived.base_double));

    g_reset_double_derived.base_value = random<int>();
    g_reset_double_derived.base_double = random<double>();
    g_reset_double_derived.derived_value = random<int>();
    VariantSymbol* double_derived_sym = symbols.getSymbol("g_reset_double_derived");
    REQUIRE(double_derived_sym != nullptr);

    VariantSymbol* double_derived_base_value_sym = symbols.getSymbol("g_reset_double_derived.base_value");
    VariantSymbol* double_derived_base_double_sym = symbols.getSymbol("g_reset_double_derived.base_double");
    VariantSymbol* inherited_derived_value_sym = symbols.getSymbol("g_reset_double_derived.derived_value");
    REQUIRE(double_derived_base_value_sym != nullptr);
    REQUIRE(double_derived_base_double_sym != nullptr);
    REQUIRE(inherited_derived_value_sym != nullptr);
    CHECK(double_derived_base_value_sym->getFullName() == "g_reset_double_derived.base_value");
    CHECK(double_derived_base_double_sym->getFullName() == "g_reset_double_derived.base_double");
    CHECK(inherited_derived_value_sym->getFullName() == "g_reset_double_derived.derived_value");
    CHECK(double_derived_base_value_sym->read() == g_reset_double_derived.base_value);
    CHECK(double_derived_base_double_sym->read() == Approx(g_reset_double_derived.base_double));
    CHECK(inherited_derived_value_sym->read() == g_reset_double_derived.derived_value);
}

TEST_CASE("Static namespace-scope symbol access") {
    DbgSymbols const& symbols = DbgSymbols::getSymbols();

    // Statics have internal linkage, so their definition DIE in DWARF lacks
    // both DW_AT_name and DW_AT_linkage_name. The walker must recover the
    // qualified name by following DW_AT_specification back to the declaration
    // nested in the namespace.
    static_ns::s_int = random<int>();
    VariantSymbol* s_int_sym = symbols.getSymbol("static_ns::s_int");
    REQUIRE(s_int_sym != nullptr);
    CHECK(s_int_sym->read() == static_ns::s_int);

    static_ns::s_double = 42.5;
    VariantSymbol* s_double_sym = symbols.getSymbol("static_ns::s_double");
    REQUIRE(s_double_sym != nullptr);
    CHECK(s_double_sym->read() == Approx(static_ns::s_double));

    static_ns::s_a.m_a = 999;
    VariantSymbol* s_a_sym = symbols.getSymbol("static_ns::s_a");
    REQUIRE(s_a_sym != nullptr);
    CHECK(s_a_sym->getChildren().size() == 1);
    VariantSymbol* s_a_m_sym = symbols.getSymbol("static_ns::s_a.m_a");
    REQUIRE(s_a_m_sym != nullptr);
    CHECK(s_a_m_sym->read() == static_ns::s_a.m_a);

    static_ns::s_b.m_b = 7.25;
    static_ns::s_b.a.m_a = 11;
    VariantSymbol* s_b_sym = symbols.getSymbol("static_ns::s_b");
    REQUIRE(s_b_sym != nullptr);
    CHECK(s_b_sym->getChildren().size() == 2);
    VariantSymbol* s_b_a_m_sym = symbols.getSymbol("static_ns::s_b.a.m_a");
    REQUIRE(s_b_a_m_sym != nullptr);
    CHECK(s_b_a_m_sym->read() == static_ns::s_b.a.m_a);

    // Variable with non-trivial constructor — forces GCC into the
    // specification-pattern DWARF encoding that exposed the original bug.
    VariantSymbol* s_ctor_sym = symbols.getSymbol("static_ns::s_ctor");
    REQUIRE(s_ctor_sym != nullptr);
    VariantSymbol* s_ctor_v_sym = symbols.getSymbol("static_ns::s_ctor.value");
    REQUIRE(s_ctor_v_sym != nullptr);
    CHECK(s_ctor_v_sym->read() == static_ns::s_ctor.value);

    VariantSymbol* s_arr_sym = symbols.getSymbol("static_ns::s_array");
    REQUIRE(s_arr_sym != nullptr);
    VariantSymbol* s_arr1_sym = symbols.getSymbol("static_ns::s_array[1]");
    REQUIRE(s_arr1_sym != nullptr);
    CHECK(s_arr1_sym->read() == static_ns::s_array[1]);
}

TEST_CASE("Function-local statics are not exposed") {
    // Make sure the function actually runs so the linker keeps the statics —
    // otherwise the compiler/linker may strip them and the test trivially
    // passes for the wrong reason.
    REQUIRE(getLocalStaticPtr() != nullptr);

    DbgSymbols const& symbols = DbgSymbols::getSymbols();

    // The statics are inside a DW_TAG_subprogram, so the walker must skip them.
    // If exposed, snapshot save/restore could zero the storage while leaving the
    // C++ init guard set, producing a SIGSEGV on the next use (the Catch2
    // CATCH_REGISTER_ENUM crash mechanism).
    CHECK(symbols.getSymbol("s_local_static_int") == nullptr);
    CHECK(symbols.getSymbol("s_local_static_ptr") == nullptr);
    CHECK(symbols.getSymbol("getLocalStaticPtr::s_local_static_int") == nullptr);
    CHECK(symbols.getSymbol("getLocalStaticPtr::s_local_static_ptr") == nullptr);
}

TEST_CASE("Forward-declared type definition lookup") {
    // FwdDeclInner is emitted as a forward declaration in this TU's DWARF
    // because no methods of it are defined here. The full definition lives
    // in fwd_decl_types.cpp. The walker must resolve the forward declaration
    // to the full definition to be able to size up FwdDeclOuter and expose
    // FwdDeclInner's members.
    DbgSymbols const& symbols = DbgSymbols::getSymbols();

    g_fwd_outer.inner.a = 17;
    g_fwd_outer.inner.b = 2.75;
    g_fwd_outer.outer_value = 99;

    VariantSymbol* outer_sym = symbols.getSymbol("g_fwd_outer");
    REQUIRE(outer_sym != nullptr);

    VariantSymbol* inner_sym = symbols.getSymbol("g_fwd_outer.inner");
    REQUIRE(inner_sym != nullptr);

    VariantSymbol* inner_a_sym = symbols.getSymbol("g_fwd_outer.inner.a");
    REQUIRE(inner_a_sym != nullptr);
    CHECK(inner_a_sym->read() == g_fwd_outer.inner.a);

    VariantSymbol* inner_b_sym = symbols.getSymbol("g_fwd_outer.inner.b");
    REQUIRE(inner_b_sym != nullptr);
    CHECK(inner_b_sym->read() == Approx(g_fwd_outer.inner.b));

    VariantSymbol* outer_value_sym = symbols.getSymbol("g_fwd_outer.outer_value");
    REQUIRE(outer_value_sym != nullptr);
    CHECK(outer_value_sym->read() == g_fwd_outer.outer_value);
}

TEST_CASE("Snapshot from file") {
    // Create temp variables
    int idx1 = random<uint16_t>() % 4;
    int idx2 = random<uint16_t>() % 4;
    int temp_int = random<int>();
    float temp_float = random<float>();
    double temp_double1 = random<double>();
    double temp_double2 = random<double>();
    uint32_t temp_bf0 = random<uint32_t>() % 7;
    uint32_t temp_bf9 = random<uint32_t>() % 17;
    int temp_reset_base_value = random<int>();
    double temp_reset_base_double = random<double>();

    // Assign random values to global variables
    g_int = temp_int;
    g::g_float = temp_float;
    g_double1 = temp_double1;
    g_fn_ptr = &test_fn1;
    g_fn_ptr2 = NULL;
    g_double_ptr = &g_double1;
    g_multidim_double[idx1][idx2] = temp_double2;
    g_bitfield.b0 = temp_bf0;
    g_bitfield.b9 = temp_bf9;
    g_reset_derived.base_value = temp_reset_base_value;
    g_reset_derived.base_double = temp_reset_base_double;

    // Try loading non-existent json file
    std::string const& symbols_json = "test_symbols.json";
    if (std::filesystem::exists(symbols_json)) {
        std::filesystem::remove(symbols_json);
    }
    void* symbols = SNP_getSymbolsFromJson(symbols_json.c_str());
    REQUIRE(symbols == nullptr);

    // Load symbols from PDB and save info to json
    symbols = SNP_getSymbolsFromPdb();
    int omit_names_from_json = 0;
    SNP_saveSymbolInfoToJson(symbols, symbols_json.c_str(), omit_names_from_json);

    // Try again loading info from json and use the json info for snapshot
    symbols = SNP_getSymbolsFromJson(symbols_json.c_str());
    REQUIRE(symbols != nullptr);
    SNP_saveSnapshotToFile(symbols, "test_snapshot.json");

    // Assign new random values to global variables
    g_int = random<int>();
    g::g_float = random<float>();
    g_double1 = random<double>();
    g_fn_ptr = &g::test_fn2;
    g_fn_ptr2 = &g::test_fn2;
    g_double_ptr = &g_double2;
    g_multidim_double[idx1][idx2] = random<double>();
    g_bitfield.b0 = random<uint32_t>() % 9;
    g_bitfield.b9 = random<uint32_t>() % 17;
    g_reset_derived.base_value = random<int>();
    g_reset_derived.base_double = random<double>();
    // Load snapshot
    SNP_loadSnapshotFromFile(symbols, "test_snapshot.json");

    // Check that the values of all global variables are the same as assigned before saving the snapshot
    REQUIRE(g_int == temp_int);
    REQUIRE(g::g_float == temp_float);
    REQUIRE(g_double1 == temp_double1);
    REQUIRE(g_double_ptr == &g_double1);
    REQUIRE(g_fn_ptr == test_fn1);
    REQUIRE(g_fn_ptr2 == NULL);
    REQUIRE(g_multidim_double[idx1][idx2] == temp_double2);
    REQUIRE(g_bitfield.b0 == temp_bf0);
    REQUIRE(g_bitfield.b9 == temp_bf9);
    REQUIRE(g_reset_derived.base_value == temp_reset_base_value);
    REQUIRE(g_reset_derived.base_double == temp_reset_base_double);

    SNP_deleteSymbolLookup(symbols);
}

TEST_CASE("Snapshot from memory") {
    // Create temp variables
    int idx1 = random<uint16_t>() % 4;
    int idx2 = random<uint16_t>() % 4;
    int temp_int = random<int>();
    float temp_float = random<float>();
    double temp_double1 = random<double>();
    double temp_double2 = random<double>();
    uint32_t temp_bf0 = random<uint32_t>() % 7;
    uint32_t temp_bf9 = random<uint32_t>() % 17;
    int temp_reset_base_value = random<int>();
    double temp_reset_base_double = random<double>();

    // Assign random values to global variables
    g_int = temp_int;
    g::g_float = temp_float;
    g_double1 = temp_double1;
    g_fn_ptr = &test_fn1;
    g_fn_ptr2 = NULL;
    g_double_ptr = &g_double1;
    g_multidim_double[idx1][idx2] = temp_double2;
    g_bitfield.b0 = temp_bf0;
    g_bitfield.b9 = temp_bf9;
    g_reset_derived.base_value = temp_reset_base_value;
    g_reset_derived.base_double = temp_reset_base_double;

    void* symbols = SNP_getSymbolsFromPdb();
    auto snapshot = SNP_saveSnapshotToMemory(symbols);

    // Assign new random values to global variables
    g_int = random<int>();
    g::g_float = random<float>();
    g_double1 = random<double>();
    g_fn_ptr = &g::test_fn2;
    g_fn_ptr2 = &g::test_fn2;
    g_double_ptr = &g_double2;
    g_multidim_double[idx1][idx2] = random<double>();
    g_bitfield.b0 = random<uint32_t>() % 9;
    g_bitfield.b9 = random<uint32_t>() % 17;
    g_reset_derived.base_value = random<int>();
    g_reset_derived.base_double = random<double>();

    // Load snapshot
    SNP_loadSnapshotFromMemory(symbols, snapshot);

    // Check that the values of all global variables are the same as assigned before saving the snapshot
    REQUIRE(g_int == temp_int);
    REQUIRE(g::g_float == temp_float);
    REQUIRE(g_double1 == temp_double1);
    REQUIRE(g_double_ptr == &g_double1);
    REQUIRE(g_fn_ptr == test_fn1);
    REQUIRE(g_fn_ptr2 == NULL);
    REQUIRE(g_multidim_double[idx1][idx2] == temp_double2);
    REQUIRE(g_bitfield.b0 == temp_bf0);
    REQUIRE(g_bitfield.b9 == temp_bf9);
    REQUIRE(g_reset_derived.base_value == temp_reset_base_value);
    REQUIRE(g_reset_derived.base_double == temp_reset_base_double);

    SNP_deleteSymbolLookup(symbols);
}

// ============================================================================
// Shared library symbol reading tests
// On Windows, RawPDB reads symbols from loaded modules and prefixes symbols
// from other modules with "modulename|symbolname".
// On Linux, libdwarf reads DWARF debug info from loaded shared libraries
// via dl_iterate_phdr.
// ============================================================================
TEST_CASE("Read symbols from shared library") {
    REQUIRE(test_library_loader.handle != nullptr);

    DbgSymbols const& symbols = DbgSymbols::getSymbols();

    std::string prefix = "test_library|";
    SECTION("Search recursion depth controls module-prefixed globals") {
        auto contains_symbol = [](std::vector<VariantSymbol*> const& matches, std::string const& full_name) {
            return std::ranges::any_of(matches, [&](VariantSymbol* symbol) {
                return symbol->getFullName() == full_name;
            });
        };

        CHECK(contains_symbol(symbols.findMatchingSymbols("g_int", 0), "g_int"));
        CHECK_FALSE(contains_symbol(symbols.findMatchingSymbols("base_value", 0), "g_reset_derived.base_value"));
        CHECK(contains_symbol(symbols.findMatchingSymbols("g_reset_derived.base_value", 0), "g_reset_derived.base_value"));
        CHECK(contains_symbol(symbols.findMatchingSymbols("base_value", 1), "g_reset_derived.base_value"));
        CHECK(contains_symbol(symbols.findMatchingSymbols("base_value", 1), "g_reset_double_derived.base_value"));
        CHECK_FALSE(contains_symbol(symbols.findMatchingSymbols("lib_int32", 0), prefix + "lib_int32"));
        CHECK(contains_symbol(symbols.findMatchingSymbols(prefix + "lib_motor.status.flags.enabled", 0),
                              prefix + "lib_motor.status.flags.enabled"));
        CHECK(contains_symbol(symbols.findMatchingSymbols("lib_int32", 1), prefix + "lib_int32"));
    }

    // ---- Primitive types ----
    SECTION("Primitive integer types") {
        VariantSymbol* sym_int32 = symbols.getSymbol(prefix + "lib_int32");
        REQUIRE(sym_int32 != nullptr);
        CHECK(sym_int32->read() == 42.0);

        VariantSymbol* sym_uint32 = symbols.getSymbol(prefix + "lib_uint32");
        REQUIRE(sym_uint32 != nullptr);
        CHECK(sym_uint32->read() == 100.0);

        VariantSymbol* sym_int16 = symbols.getSymbol(prefix + "lib_int16");
        REQUIRE(sym_int16 != nullptr);
        CHECK(sym_int16->read() == -1234.0);

        VariantSymbol* sym_uint16 = symbols.getSymbol(prefix + "lib_uint16");
        REQUIRE(sym_uint16 != nullptr);
        CHECK(sym_uint16->read() == 5678.0);

        VariantSymbol* sym_int8 = symbols.getSymbol(prefix + "lib_int8");
        REQUIRE(sym_int8 != nullptr);
        CHECK(sym_int8->read() == -42.0);

        VariantSymbol* sym_uint8 = symbols.getSymbol(prefix + "lib_uint8");
        REQUIRE(sym_uint8 != nullptr);
        CHECK(sym_uint8->read() == 200.0);

        VariantSymbol* sym_int64 = symbols.getSymbol(prefix + "lib_int64");
        REQUIRE(sym_int64 != nullptr);
        CHECK(sym_int64->read() == -123456789.0);

        VariantSymbol* sym_uint64 = symbols.getSymbol(prefix + "lib_uint64");
        REQUIRE(sym_uint64 != nullptr);
        CHECK(sym_uint64->read() == 987654321.0);
    }

    SECTION("Floating point types") {
        VariantSymbol* sym_float = symbols.getSymbol(prefix + "lib_float");
        REQUIRE(sym_float != nullptr);
        CHECK(sym_float->read() == Approx(3.14f));

        VariantSymbol* sym_double = symbols.getSymbol(prefix + "lib_double");
        REQUIRE(sym_double != nullptr);
        CHECK(sym_double->read() == Approx(2.71828));
    }

    // ---- Structs ----
    SECTION("Struct members") {
        VariantSymbol* sym_point2d = symbols.getSymbol(prefix + "lib_point2d");
        REQUIRE(sym_point2d != nullptr);
        CHECK(sym_point2d->getType() == VariantSymbol::Type::Object);

        VariantSymbol* sym_point2d_x = symbols.getSymbol(prefix + "lib_point2d.x");
        REQUIRE(sym_point2d_x != nullptr);
        CHECK(sym_point2d_x->read() == Approx(1.5f));

        VariantSymbol* sym_point2d_y = symbols.getSymbol(prefix + "lib_point2d.y");
        REQUIRE(sym_point2d_y != nullptr);
        CHECK(sym_point2d_y->read() == Approx(2.5f));

        VariantSymbol* sym_point3d_x = symbols.getSymbol(prefix + "lib_point3d.x");
        REQUIRE(sym_point3d_x != nullptr);
        CHECK(sym_point3d_x->read() == Approx(10.0));

        VariantSymbol* sym_point3d_y = symbols.getSymbol(prefix + "lib_point3d.y");
        REQUIRE(sym_point3d_y != nullptr);
        CHECK(sym_point3d_y->read() == Approx(20.0));

        VariantSymbol* sym_point3d_z = symbols.getSymbol(prefix + "lib_point3d.z");
        REQUIRE(sym_point3d_z != nullptr);
        CHECK(sym_point3d_z->read() == Approx(30.0));
    }

    // ---- Nested struct ----
    SECTION("Nested struct members") {
        VariantSymbol* sym_motor = symbols.getSymbol(prefix + "lib_motor");
        REQUIRE(sym_motor != nullptr);
        CHECK(sym_motor->getType() == VariantSymbol::Type::Object);

        VariantSymbol* sym_speed = symbols.getSymbol(prefix + "lib_motor.speed");
        REQUIRE(sym_speed != nullptr);
        CHECK(sym_speed->read() == Approx(1500.0));

        VariantSymbol* sym_torque = symbols.getSymbol(prefix + "lib_motor.torque");
        REQUIRE(sym_torque != nullptr);
        CHECK(sym_torque->read() == Approx(12.5));

        VariantSymbol* sym_direction = symbols.getSymbol(prefix + "lib_motor.direction");
        REQUIRE(sym_direction != nullptr);
        CHECK(sym_direction->read() == 1.0);

        // Nested struct member within nested struct
        VariantSymbol* sym_pos_x = symbols.getSymbol(prefix + "lib_motor.position.x");
        REQUIRE(sym_pos_x != nullptr);
        CHECK(sym_pos_x->read() == Approx(100.0f));

        VariantSymbol* sym_pos_y = symbols.getSymbol(prefix + "lib_motor.position.y");
        REQUIRE(sym_pos_y != nullptr);
        CHECK(sym_pos_y->read() == Approx(200.0f));
    }

    // ---- Class members ----
    SECTION("Class members") {
        VariantSymbol* sym_pid = symbols.getSymbol(prefix + "lib_pid");
        REQUIRE(sym_pid != nullptr);
        CHECK(sym_pid->getType() == VariantSymbol::Type::Object);

        VariantSymbol* sym_kp = symbols.getSymbol(prefix + "lib_pid.kp");
        REQUIRE(sym_kp != nullptr);
        CHECK(sym_kp->read() == Approx(1.0));

        VariantSymbol* sym_ki = symbols.getSymbol(prefix + "lib_pid.ki");
        REQUIRE(sym_ki != nullptr);
        CHECK(sym_ki->read() == Approx(0.1));

        VariantSymbol* sym_kd = symbols.getSymbol(prefix + "lib_pid.kd");
        REQUIRE(sym_kd != nullptr);
        CHECK(sym_kd->read() == Approx(0.01));

        VariantSymbol* sym_setpoint = symbols.getSymbol(prefix + "lib_pid.setpoint");
        REQUIRE(sym_setpoint != nullptr);
        CHECK(sym_setpoint->read() == Approx(100.0));

        VariantSymbol* sym_enabled = symbols.getSymbol(prefix + "lib_pid.enabled");
        REQUIRE(sym_enabled != nullptr);
        CHECK(sym_enabled->read() == 1.0);
    }

    // ---- Bitfield ----
    SECTION("Bitfield members") {
        VariantSymbol* sym_status = symbols.getSymbol(prefix + "lib_status");
        REQUIRE(sym_status != nullptr);

        // Verify the raw value contains the bitfield data:
        // enabled=1 (bit 0), mode=5 (bits 1-3), priority=10 (bits 4-7), error_code=0xAB (bits 8-15)
        // raw = 1 | (5 << 1) | (10 << 4) | (0xAB << 8)
        VariantSymbol* sym_raw = symbols.getSymbol(prefix + "lib_status.raw");
        REQUIRE(sym_raw != nullptr);
        uint32_t expected_raw = 1u | (5u << 1) | (10u << 4) | (0xABu << 8);
        CHECK(static_cast<uint32_t>(sym_raw->read()) == expected_raw);

        // Verify individual bitfield members can be read correctly
        VariantSymbol* sym_enabled = symbols.getSymbol(prefix + "lib_status.flags.enabled");
        REQUIRE(sym_enabled != nullptr);
        CHECK(sym_enabled->read() == 1.0);

        VariantSymbol* sym_mode = symbols.getSymbol(prefix + "lib_status.flags.mode");
        REQUIRE(sym_mode != nullptr);
        CHECK(sym_mode->read() == 5.0);

        VariantSymbol* sym_priority = symbols.getSymbol(prefix + "lib_status.flags.priority");
        REQUIRE(sym_priority != nullptr);
        CHECK(sym_priority->read() == 10.0);

        VariantSymbol* sym_error_code = symbols.getSymbol(prefix + "lib_status.flags.error_code");
        REQUIRE(sym_error_code != nullptr);
        CHECK(sym_error_code->read() == 0xAB);
    }

    // ---- Arrays ----
    SECTION("Array elements") {
        VariantSymbol* sym_array = symbols.getSymbol(prefix + "lib_array");
        REQUIRE(sym_array != nullptr);
        CHECK(sym_array->getType() == VariantSymbol::Type::Array);

        VariantSymbol* sym_arr0 = symbols.getSymbol(prefix + "lib_array[0]");
        REQUIRE(sym_arr0 != nullptr);
        CHECK(sym_arr0->read() == Approx(1.0));

        VariantSymbol* sym_arr1 = symbols.getSymbol(prefix + "lib_array[1]");
        REQUIRE(sym_arr1 != nullptr);
        CHECK(sym_arr1->read() == Approx(2.0));

        VariantSymbol* sym_arr2 = symbols.getSymbol(prefix + "lib_array[2]");
        REQUIRE(sym_arr2 != nullptr);
        CHECK(sym_arr2->read() == Approx(3.0));

        VariantSymbol* sym_arr3 = symbols.getSymbol(prefix + "lib_array[3]");
        REQUIRE(sym_arr3 != nullptr);
        CHECK(sym_arr3->read() == Approx(4.0));
    }

    SECTION("Multidimensional array") {
        VariantSymbol* sym_matrix = symbols.getSymbol(prefix + "lib_matrix");
        REQUIRE(sym_matrix != nullptr);

        VariantSymbol* sym_m00 = symbols.getSymbol(prefix + "lib_matrix[0][0]");
        REQUIRE(sym_m00 != nullptr);
        CHECK(sym_m00->read() == 1.0);

        VariantSymbol* sym_m11 = symbols.getSymbol(prefix + "lib_matrix[1][1]");
        REQUIRE(sym_m11 != nullptr);
        CHECK(sym_m11->read() == 5.0);

        VariantSymbol* sym_m22 = symbols.getSymbol(prefix + "lib_matrix[2][2]");
        REQUIRE(sym_m22 != nullptr);
        CHECK(sym_m22->read() == 9.0);
    }

    SECTION("Array of structs") {
        VariantSymbol* sym_points = symbols.getSymbol(prefix + "lib_points");
        REQUIRE(sym_points != nullptr);

        VariantSymbol* sym_p0x = symbols.getSymbol(prefix + "lib_points[0].x");
        REQUIRE(sym_p0x != nullptr);
        CHECK(sym_p0x->read() == Approx(0.0f));

        VariantSymbol* sym_p1x = symbols.getSymbol(prefix + "lib_points[1].x");
        REQUIRE(sym_p1x != nullptr);
        CHECK(sym_p1x->read() == Approx(1.0f));

        VariantSymbol* sym_p2y = symbols.getSymbol(prefix + "lib_points[2].y");
        REQUIRE(sym_p2y != nullptr);
        CHECK(sym_p2y->read() == Approx(2.0f));
    }

    // ---- Pointers ----
    SECTION("Pointers") {
        VariantSymbol* sym_ptr = symbols.getSymbol(prefix + "lib_double_ptr");
        REQUIRE(sym_ptr != nullptr);
        CHECK(sym_ptr->getType() == VariantSymbol::Type::Pointer);

        VariantSymbol* sym_null = symbols.getSymbol(prefix + "lib_null_ptr");
        REQUIRE(sym_null != nullptr);
        CHECK(sym_null->getType() == VariantSymbol::Type::Pointer);
        CHECK(sym_null->getPointedAddress() == 0);
    }

    SECTION("Memory snapshot restores pointer within shared library") {
        VariantSymbol* sym_ptr = symbols.getSymbol(prefix + "lib_double_ptr");
        REQUIRE(sym_ptr != nullptr);
        REQUIRE(sym_ptr->getType() == VariantSymbol::Type::Pointer);

        VariantSymbol* sym_double = symbols.getSymbol(prefix + "lib_double");
        REQUIRE(sym_double != nullptr);

        VariantSymbol* sym_array_1 = symbols.getSymbol(prefix + "lib_array[1]");
        REQUIRE(sym_array_1 != nullptr);

        sym_ptr->setPointedAddress(sym_double->getAddress());
        auto snapshot = symbols.saveSnapshotToMemory();

        sym_ptr->setPointedAddress(sym_array_1->getAddress());
        REQUIRE(sym_ptr->getPointedAddress() == sym_array_1->getAddress());

        symbols.loadSnapshotFromMemory(snapshot);
        CHECK(sym_ptr->getPointedAddress() == sym_double->getAddress());
        CHECK(sym_ptr->read() == Approx(sym_double->read()));
    }

    // ---- Write and read back ----
    SECTION("Write to shared library symbol and read back") {
        VariantSymbol* sym_int32 = symbols.getSymbol(prefix + "lib_int32");
        REQUIRE(sym_int32 != nullptr);

        // Write a new value
        sym_int32->write(999.0);
        CHECK(sym_int32->read() == 999.0);

        // Restore original value
        sym_int32->write(42.0);
        CHECK(sym_int32->read() == 42.0);
    }
}
