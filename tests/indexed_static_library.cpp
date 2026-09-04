#include <cstdint>

int indexed_static_library_value = 314;

extern "C" std::uintptr_t keep_indexed_static_library_alive() {
    return reinterpret_cast<std::uintptr_t>(&indexed_static_library_value);
}
