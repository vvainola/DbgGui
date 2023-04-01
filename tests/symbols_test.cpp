#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include "global_snapshot.h"

#include <random>

std::default_random_engine eng;


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

// Define global variables of different types
int global_int;
float global_float;
double global_double;

TEST_CASE("Snapshot") {
    int omit_names = 0;

    // Create temp variables
    int temp_int = random<int>();
    float temp_float = random<float>();
    double temp_double = random<double>();

    // Assign random values to global variables
    global_int = temp_int;
    global_float = temp_float;
    global_double = temp_double;

    // Save snapshot twice to use the json file on second save instead of the PDB
    saveSnapshot("test_symbols.json", "test_snapshot.json", omit_names);
    saveSnapshot("test_symbols.json", "test_snapshot.json", omit_names);

    // Assign new random values to global variables
    global_int = random<int>();
    global_float = random<float>();
    global_double = random<double>();

    // Load snapshot
    loadSnapshot("test_symbols.json", "test_snapshot.json");

    // Check that the values of all global variables are the same as assigned before saving the snapshot
    REQUIRE(global_int == temp_int);
    REQUIRE(global_float == temp_float);
    REQUIRE(global_double == temp_double);
}
