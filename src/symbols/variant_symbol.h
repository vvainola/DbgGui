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
#include "arithmetic_symbol.h"
#include <optional>
#include <unordered_map>

class VariantSymbol {
  public:
    VariantSymbol(std::vector<std::unique_ptr<VariantSymbol>>& root_symbols,
                  RawSymbol* symbol,
                  VariantSymbol* parent = nullptr);

    enum class Type {
        Arithmetic,
        Pointer,
        Enum,
        Array,
        Object
    };

    std::string const& getName() const { return m_name; }
    std::string getFullName() {
        if (!m_full_name.empty()) {
            return m_full_name;
        }
        if (m_parent && m_parent->getType() == Type::Array) {
            m_full_name = m_parent->getFullName() + m_name.substr(m_name.rfind('['));
        } else if (m_parent) {
            m_full_name = m_parent->getFullName() + "." + m_name;
        } else {
            m_full_name = m_name;
        }
        return m_full_name;
    }
    VariantSymbol::Type getType() const { return m_type; }
    std::vector<std::unique_ptr<VariantSymbol>>& getChildren() { return m_children; }
    MemoryAddress getAddress() const { return m_address; }

    /// <summary>Return the pointed symbol or nullptr if the symbol is not found. Only valid for "Pointer" type symbols.</summary>
    VariantSymbol* getPointedSymbol() const;
    MemoryAddress getPointedAddress() const;
    void setPointedSymbol(VariantSymbol* symbol);
    void setPointedAddress(MemoryAddress address);

    void write(double value);
    double read() const;
    ValueSource getValueSource();
    std::string valueAsStr() const;

  private:
    std::vector<std::unique_ptr<VariantSymbol>>& m_root_symbols;
    VariantSymbol* m_parent;
    std::string m_name;
    std::string m_full_name;
    MemoryAddress m_address;
    std::optional<ArithmeticSymbol> m_arithmetic_symbol = std::nullopt;
    std::vector<std::pair<int32_t, std::string>> m_enum_mappings;
    std::vector<std::unique_ptr<VariantSymbol>> m_children;
    Type m_type;
};
