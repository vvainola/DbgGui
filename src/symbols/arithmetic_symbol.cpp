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

#include "arithmetic_symbol.h"
#include <bitset>
#include <cassert>

ValueSource addressAsVariant(BasicType basic_type, MemoryAddress address, uint32_t size);

ArithmeticSymbol::ArithmeticSymbol(BasicType basic_type,
                                   MemoryAddress address,
                                   uint32_t size_in_bytes,
                                   int bitfield_idx)
    : m_bitfield_idx(bitfield_idx) {
    if (m_bitfield_idx >= 0) {
        m_bf_size = size_in_bytes;
        size_in_bytes = (size_in_bytes - 1 + m_bitfield_idx) / 8 + 1;
    }
    assert(size_in_bytes > 0);
    m_value = addressAsVariant(basic_type, address, size_in_bytes);
}

void ArithmeticSymbol::write(double value) {
    if (m_bitfield_idx >= 0) {
        std::bitset<64> bits_to_write = static_cast<unsigned>(value);
        // Read old value, change the single bits and write it back.
        std::bitset<64> value_to_write = static_cast<unsigned>(getAddressValue());
        for (unsigned i = 0; i < m_bf_size; ++i) {
            value_to_write.set(m_bitfield_idx + i, bits_to_write.test(i));
        }
        value = value_to_write.to_ulong();
    }
    std::visit(
      [=](auto&& src) {
          using T = std::decay_t<decltype(src)>;
          if constexpr (std::is_same_v<T, ReadWriteFn> || std::is_same_v<T, ReadWriteFnCustomStr>) {
              src(value);
          } else {
              *src = static_cast<std::remove_pointer<T>::type>(value);
          }
      },
      m_value);
}

double ArithmeticSymbol::read() const {
    double address_value = getAddressValue();
    if (m_bitfield_idx >= 0) {
        double bitfield_value = 0;
        std::bitset<64> value = static_cast<unsigned>(address_value);
        for (unsigned i = 0; i < m_bf_size; ++i) {
            bitfield_value += pow(2, i) * value.test(m_bitfield_idx + i);
        }
        return bitfield_value;
    } else {
        return address_value;
    }
}

ValueSource addressAsVariant(BasicType basic_type, MemoryAddress address, uint32_t size) {
    assert(size != 0);
    switch (basic_type) {
        case btInt:
        case btLong:
            if (size == 1)
                return (int8_t*)address;
            else if (size == 2)
                return (int16_t*)address;
            else if (size == 4)
                return (int32_t*)address;
            else if (size == 8)
                return (int64_t*)address;
            break;
        case btBool:
        case btUInt:
        case btULong:
        case btChar:
        case btWChar:
        case btChar16:
        case btChar32:
            if (size == 1)
                return (uint8_t*)address;
            else if (size == 2)
                return (uint16_t*)address;
            else if (size <= 4)
                return (uint32_t*)address;
            else if (size <= 8)
                return (uint64_t*)address;
            break;
        case btFloat:
            if (size == 4)
                return (float*)address;
            else if (size == 8)
                return (double*)address;
    }
    return (uint32_t*)address;
}

double ArithmeticSymbol::getAddressValue() const {
    return std::visit(
      [=](auto&& src) {
          using T = std::decay_t<decltype(src)>;
          if constexpr (std::is_same_v<T, ReadWriteFn>) {
              return src(std::nullopt);
          } else if constexpr (std::is_same_v<T, ReadWriteFnCustomStr>) {
              return src(std::nullopt).second;
          } else {
              return static_cast<double>(*src);
          }
      },
      m_value);
    ;
}
