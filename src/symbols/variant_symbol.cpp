#include "variant_symbol.h"
#include "symbol_helpers.h"
#include <cassert>
#include <sstream>
#include <numeric>

VariantSymbol::VariantSymbol(std::vector<std::unique_ptr<VariantSymbol>>& root_symbols,
                             RawSymbol* symbol,
                             VariantSymbol* parent)
    : m_root_symbols(root_symbols),
      m_parent(parent) {
    if (parent) {
        m_address = parent->getAddress() + symbol->offset_to_parent;
    } else {
        m_address = symbol->info.Address;
    }

    if (parent && parent->getType() == Type::Array) {
        std::string idx = "[" + std::to_string(parent->getChildren().size()) + "]";
        m_name = parent->getName() + idx;
    } else if (parent) {
        m_name = symbol->name;
    } else {
        m_name = symbol->name;
    }

    switch (symbol->tag) {
    case SymTagFunction:
        m_type = Type::Function;
        break;
    case SymTagPointerType:
        m_type = Type::Pointer;
        break;
    case SymTagBaseType: {
        m_type = Type::Arithmetic;
        BasicType basic_type = getBaseType(*symbol);
        std::optional<DWORD> bitfield_position = std::nullopt;
        if (basic_type == BasicType::btUInt || basic_type == BasicType::btBool) {
            bitfield_position = getBitPosition(*symbol);
        }
        m_arithmetic_symbol.emplace(basic_type, m_address, symbol->info.Size, bitfield_position);
        break;
    }
    case SymTagEnumerator: {
        m_arithmetic_symbol.emplace(getBaseType(*symbol), m_address, symbol->info.Size, std::nullopt);
        m_type = Type::Enum;
        // Children of enum contain the enum values as strings.
        for (auto& child : symbol->children) {
            m_enum_mappings.push_back(std::make_pair(static_cast<int32_t>(child->info.Value), child->name));
        }
        break;
    }
    case SymTagArrayType: {
        m_type = Type::Array;
        m_children.reserve(symbol->array_element_count);
        RawSymbol* first_element = symbol->children[0].get();
        MemoryAddress original_address = m_address;
        for (uint32_t i = 0; i < symbol->array_element_count; ++i) {
            m_children.push_back(std::make_unique<VariantSymbol>(m_root_symbols, first_element, this));
            m_address += first_element->info.Size;
        }
        m_address = original_address;
        break;
    }
    case SymTagUDT:
        m_type = Type::Object;
        m_children.reserve(symbol->children.size());
        for (auto& child : symbol->children) {
            m_children.push_back(std::make_unique<VariantSymbol>(m_root_symbols, child.get(), this));
        }
        break;
    default:
        assert((0, "Unknown type for variant symbol"));
    }
}

VariantSymbol* binarySearchSymbol(std::vector<std::unique_ptr<VariantSymbol>>& symbols, MemoryAddress address) {
    int32_t start = 0;
    int32_t end = static_cast<int32_t>(symbols.size() - 1);
    int32_t mid = std::midpoint(start, end);
    while (start <= end) {
        mid = std::midpoint(start, end);
        MemoryAddress current_address = symbols[mid]->getAddress();
        if (address == current_address) {
            return symbols[mid].get();
        } else if (current_address < address) {
            start = mid + 1;
        } else if (current_address > address) {
            end = mid - 1;
        }
    }
    // The pointed symbol may be a member of the last member
    // so the address is larger than the last element in the vector
    if (symbols.size() > 0
        && end <= symbols.size() - 1
        && address > symbols[end]->getAddress()) {
        return binarySearchSymbol(symbols[end]->getChildren(), address);
    }
    return nullptr;
}

VariantSymbol* VariantSymbol::getPointedSymbol() const {
    assert(m_type == Type::Pointer);
    if (m_type != Type::Pointer) {
        return nullptr;
    }
#if _WIN64
    MemoryAddress pointed_address = *(ULONG64*)m_address;
#else
    MemoryAddress pointed_address = *(DWORD*)m_address;
#endif
    return binarySearchSymbol(m_root_symbols, pointed_address);
}

void VariantSymbol::setPointedSymbol(VariantSymbol* symbol) {
#if _WIN64
    *(ULONG64*)m_address = symbol->getAddress();
#else
    *(DWORD*)m_address = (DWORD)symbol->getAddress();
#endif
}

void VariantSymbol::write(double value) {
    assert(m_arithmetic_symbol);
    if (m_arithmetic_symbol) {
        m_arithmetic_symbol->write(value);
    }
}

double VariantSymbol::read() const {
    assert(m_arithmetic_symbol);
    if (m_arithmetic_symbol) {
        return m_arithmetic_symbol->read();
    }
    return 0;
}

ValueSource VariantSymbol::getValueSource() {
    assert(m_arithmetic_symbol);
    if (m_arithmetic_symbol->isBitfield()) {
        return [&](std::optional<double> write) {
            if (write) {
                m_arithmetic_symbol->write(*write);
            }
            return m_arithmetic_symbol->read();
        };
    } else if (m_type == Type::Enum) {
        return [&](std::optional<double> write) {
            if (write) {
                m_arithmetic_symbol->write(*write);
            }
            return std::make_pair(valueAsStr(), m_arithmetic_symbol->read());
        };
    } else {
        return m_arithmetic_symbol->getValueSource();
    }
}

std::string VariantSymbol::valueAsStr() const {
    switch (m_type) {
    case Type::Arithmetic: {
        std::stringstream ss; // Remove trailing zeros.
        ss << m_arithmetic_symbol->read();
        return ss.str();
    }
    case Type::Pointer: {
#if _WIN64
        MemoryAddress pointed_address = *(ULONG64*)m_address;
#else
        MemoryAddress pointed_address = *(DWORD*)m_address;
#endif
        if (pointed_address == NULL) {
            return "NULL";
        }
        VariantSymbol* symbol = getPointedSymbol();
        if (symbol) {
            // Return "name (value)"
            return symbol->getFullName() + " (" + symbol->valueAsStr() + ")";
        }
        return "??";
    }
    case Type::Enum: {
        int32_t value = static_cast<int32_t>(m_arithmetic_symbol->read());
        auto it = std::find_if(m_enum_mappings.begin(), m_enum_mappings.end(), [=](auto& enum_mapping) {
            return enum_mapping.first == value;
        });
        if (it != m_enum_mappings.end()) {
            return it->second;
        }
        return "";
    }
    case Type::Function:
        return "Function";
    case Type::Array:
        return "Array[" + std::to_string(m_children.size()) + "]";
    case Type::Object:
        return "Object";
    default:
        assert((0, "Invalid type"));
        return "Unknown";
    }
}
