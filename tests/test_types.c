// MIT License
//
// Copyright (c) 2026 vvainola
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

#include <stdint.h>

typedef struct {
    int64_t i64;
    float f32;
    uint32_t u32;
    int32_t i32;
    uint16_t u16;
} test_type_values_t;

typedef struct {
    test_type_values_t values;
    int32_t i32;
} test_type_container_t;

test_type_container_t g_test_types[3] = {
  {
    .values = {
      .i64 = -10,
      .f32 = 1.25f,
      .u32 = 20u,
      .i32 = -30,
      .u16 = 40u,
    },
    .i32 = 50,
  },
  {
    .values = {
      .i64 = -11,
      .f32 = 2.50f,
      .u32 = 21u,
      .i32 = -31,
      .u16 = 41u,
    },
    .i32 = 51,
  },
  {
    .values = {
      .i64 = -12,
      .f32 = 3.75f,
      .u32 = 22u,
      .i32 = -32,
      .u16 = 42u,
    },
    .i32 = 52,
  },
};
