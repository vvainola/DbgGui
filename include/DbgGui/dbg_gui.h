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

#include <stdint.h>

// C++ api
#ifdef __cplusplus
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
using ReadWriteFn = std::function<double(std::optional<double>)>;
using ReadWriteFnCustomStr = std::function<std::pair<std::string, double>(std::optional<double>)>;
using ValueSource = std::variant<
    int8_t*,
    int16_t*,
    int32_t*,
    int64_t*,
    uint8_t*,
    uint16_t*,
    uint32_t*,
    uint64_t*,
    float*,
    double*,
    ReadWriteFn,
    ReadWriteFnCustomStr>;

void DbgGui_addScalar(ValueSource const& src, std::string const& group, std::string const& name, double scale = 1.0, double offset = 0.0);
void DbgGui_addVector(ValueSource const& x, ValueSource const& y, std::string const& group, std::string const& name, double scale = 1.0, double offset = 0.0);
void DbgGui_addSymbol(std::string const& src, std::string const& group, std::string const& name, double scale = 1.0, double offset = 0.0);

/// Scalar metadata copied from the loaded debug symbols.
enum class DbgGui_ScalarKind {
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Boolean,
    Enum
};

struct DbgGui_BitfieldInfo {
    // Offset in bits from the scalar address, starting at the least significant bit.
    std::uint32_t bit_offset;
    std::uint32_t bit_width;
};

struct DbgGui_ScalarInfo {
    std::string full_name;
    DbgGui_ScalarKind kind;
    std::uintptr_t address;
    std::uint32_t size_in_bytes;
    // Value/name pairs; empty for non-enums. Keeps aliases with the same value.
    std::vector<std::pair<std::int64_t, std::string>> enum_mapping;
    // Present only for bitfields, including fields starting at bit zero.
    std::optional<DbgGui_BitfieldInfo> bitfield = std::nullopt;
};

/// Enumerate all loaded arithmetic and enum symbols, including object members
/// and expanded array elements. Does not follow pointers. Very large arrays
/// omitted by the symbol loader are unavailable. Results own their metadata.
/// For bitfields, size_in_bytes is the byte span containing the field.
std::vector<DbgGui_ScalarInfo> DbgGui_getScalars();

/// Read an arithmetic or enum symbol by name.
/// Returns std::nullopt if the symbol does not exist or cannot be read as a scalar value.
std::optional<double> DbgGui_readSymbol(std::string_view name);

/// Write an arithmetic or enum symbol by name.
/// Returns true if the value was written, or false if the symbol does not exist,
/// is not a scalar value, or is const.
bool DbgGui_writeSymbol(std::string_view name, double value);
#endif

// C-api
#ifdef __cplusplus
extern "C" {
#endif
void DbgGui_addScalar_u8(uint8_t* src, const char* group, const char* name);
void DbgGui_addScalar_u16(uint16_t* src, const char* group, const char* name);
void DbgGui_addScalar_u32(uint32_t* src, const char* group, const char* name);
void DbgGui_addScalar_u64(uint64_t* src, const char* group, const char* name);

void DbgGui_addScalar_i8(int8_t* src, const char* group, const char* name);
void DbgGui_addScalar_i16(int16_t* src, const char* group, const char* name);
void DbgGui_addScalar_i32(int32_t* src, const char* group, const char* name);
void DbgGui_addScalar_i64(int64_t* src, const char* group, const char* name);

void DbgGui_addScalar_f32(float* src, const char* group, const char* name);
void DbgGui_addScalar_f64(double* src, const char* group, const char* name);

void DbgGui_addVector_f32(float* x, float* y, const char* group, const char* name);
void DbgGui_addVector_f64(double* x, double* y, const char* group, const char* name);

void DbgGui_create(double sampling_time);
void DbgGui_startUpdateLoop(void);
void DbgGui_sample(void);
void DbgGui_sampleWithTimestamp(double timestamp);
int DbgGui_isClosed(void);
void DbgGui_close(void);
void DbgGui_pause(void);
void DbgGui_log(const char* fmt, ...);
#ifdef __cplusplus
}
#endif
