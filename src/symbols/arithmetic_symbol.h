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
#include "raw_symbol.h"
#include <functional>
#include <optional>
#include <variant>

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

class ArithmeticSymbol {
  public:
    ArithmeticSymbol(BasicType basic_type,
                     MemoryAddress address,
                     uint32_t size,
                     int bitfield_idx = NO_VALUE);

    void write(double read);
    double read() const;

    ValueSource getValueSource() { return m_value; }
    bool isBitfield() const { return m_bitfield_idx >= 0; };

  private:
    double getAddressValue() const;

    MemoryAddress m_address;
    int m_bitfield_idx;
    uint32_t m_bf_size = 0;
    ValueSource m_value;
};
