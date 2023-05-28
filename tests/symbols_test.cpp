#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include "global_snapshot.h"


#include <filesystem>
#include <random>

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

void test_fn1() {}
void test_fn2() {}

// Define global variables of different types
int g_int;
float g_float;
double g_double1;
double g_double2;
double g_multidim_double[5][5];
double* g_double_ptr;
void (*g_fn_ptr)();
void (*g_fn_ptr2)();
BitField g_bitfield;

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

    // Assign random values to global variables
    g_int = temp_int;
    g_float = temp_float;
    g_double1 = temp_double1;
    g_fn_ptr = &test_fn1;
    g_fn_ptr2 = NULL;
    g_double_ptr = &g_double1;
    g_multidim_double[idx1][idx2] = temp_double2;
    g_bitfield.b0 = temp_bf0;
    g_bitfield.b9 = temp_bf9;

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
    g_float = random<float>();
    g_double1 = random<double>();
    g_fn_ptr = &test_fn2;
    g_fn_ptr2 = &test_fn2;
    g_double_ptr = &g_double2;
    g_multidim_double[idx1][idx2] = random<double>();
    g_bitfield.b0 = random<uint32_t>() % 9;
    g_bitfield.b9 = random<uint32_t>() % 17;
    // Load snapshot
    SNP_loadSnapshotFromFile(symbols, "test_snapshot.json");

    // Check that the values of all global variables are the same as assigned before saving the snapshot
    REQUIRE(g_int == temp_int);
    REQUIRE(g_float == temp_float);
    REQUIRE(g_double1 == temp_double1);
    REQUIRE(g_double_ptr == &g_double1);
    REQUIRE(g_fn_ptr == test_fn1);
    REQUIRE(g_fn_ptr2 == NULL);
    REQUIRE(g_multidim_double[idx1][idx2] == temp_double2);
    REQUIRE(g_bitfield.b0 == temp_bf0);
    REQUIRE(g_bitfield.b9 == temp_bf9);

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

    // Assign random values to global variables
    g_int = temp_int;
    g_float = temp_float;
    g_double1 = temp_double1;
    g_fn_ptr = &test_fn1;
    g_fn_ptr2 = NULL;
    g_double_ptr = &g_double1;
    g_multidim_double[idx1][idx2] = temp_double2;
    g_bitfield.b0 = temp_bf0;
    g_bitfield.b9 = temp_bf9;

    void* symbols = SNP_getSymbolsFromPdb();
    auto snapshot = SNP_saveSnapshotToMemory(symbols);

    // Assign new random values to global variables
    g_int = random<int>();
    g_float = random<float>();
    g_double1 = random<double>();
    g_fn_ptr = &test_fn2;
    g_fn_ptr2 = &test_fn2;
    g_double_ptr = &g_double2;
    g_multidim_double[idx1][idx2] = random<double>();
    g_bitfield.b0 = random<uint32_t>() % 9;
    g_bitfield.b9 = random<uint32_t>() % 17;

    // Load snapshot
    SNP_loadSnapshotFromMemory(symbols, snapshot);

    // Check that the values of all global variables are the same as assigned before saving the snapshot
    REQUIRE(g_int == temp_int);
    REQUIRE(g_float == temp_float);
    REQUIRE(g_double1 == temp_double1);
    REQUIRE(g_double_ptr == &g_double1);
    REQUIRE(g_fn_ptr == test_fn1);
    REQUIRE(g_fn_ptr2 == NULL);
    REQUIRE(g_multidim_double[idx1][idx2] == temp_double2);
    REQUIRE(g_bitfield.b0 == temp_bf0);
    REQUIRE(g_bitfield.b9 == temp_bf9);

    SNP_deleteSymbolLookup(symbols);
}
