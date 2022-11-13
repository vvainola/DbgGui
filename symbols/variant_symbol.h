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
        Function,
        Array,
        Object
    };

    std::string const& getName() const { return m_name; }
    std::string const& getFullName() const { return m_full_name; }
    VariantSymbol::Type getType() const { return m_type; }
    std::vector<std::unique_ptr<VariantSymbol>>& getChildren() { return m_children; }
    MemoryAddress getAddress() const { return m_address; }

    /// <summary>Return the pointed symbol or nullptr if the symbol is not found. Only valid for "Pointer" type symbols.</summary>
    VariantSymbol* getPointedSymbol() const;
    void setPointedSymbol(VariantSymbol* symbol);

    void write(double value);
    double read() const;
    ValueSource getValueSource();
    std::string valueAsStr() const;

  private:
    std::vector<std::unique_ptr<VariantSymbol>>& m_root_symbols;
    std::string m_full_name;
    std::string m_name;
    MemoryAddress m_address;
    std::optional<ArithmeticSymbol> m_arithmetic_symbol = std::nullopt;
    std::vector<std::pair<int32_t, std::string>> m_enum_mappings;
    std::vector<std::unique_ptr<VariantSymbol>> m_children;
    Type m_type;
};
