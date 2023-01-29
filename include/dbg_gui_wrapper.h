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
#include <string>
#include <variant>
#include <functional>
#include <optional>
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

class DbgGuiWrapper {
  public:
    DbgGuiWrapper(double sampling_time);
    ~DbgGuiWrapper();
    void startUpdateLoop();

    void sample();
    void sampleWithTimestamp(double timestamp);

    bool isClosed();
    void close();

    void addScalar(ValueSource const& src, std::string const& group, std::string const& name);
    void addVector(ValueSource const& x, ValueSource const& y, std::string const& group, std::string const& name);
};
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
#ifdef __cplusplus
}
#endif
