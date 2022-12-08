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
                     std::optional<uint32_t> bitfield_idx);

    void write(double read);
    double read() const;

    ValueSource getValueSource() { return m_value; }
    bool isBitfield() const { return bool(m_bitfield_idx); };

  private:
    double getAddressValue() const;

    MemoryAddress m_address;
    uint32_t m_size;
    std::optional<uint32_t> m_bitfield_idx;
    uint32_t m_bf_size = 0;
    ValueSource m_value;
};
