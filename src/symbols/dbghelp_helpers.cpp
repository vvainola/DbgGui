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

#include "dbghelp_helpers.h"
#include "cvconst.h"

#include <DbgHelp.h>
#include <Windows.h>

#include <array>
#include <cctype>
#include <iostream>
#include <mutex>
#include <unordered_set>
#include <variant>
#include <vector>

namespace {

static void appendInheritedMembers(SymbolDescriptor& symbol,
                                   SymbolDescriptor const& base_symbol,
                                   uint32_t base_offset) {
    for (auto const& base_child : base_symbol.children) {
        auto inherited_child = std::make_shared<SymbolDescriptor>(*base_child);
        inherited_child->offset_to_parent += base_offset;
        symbol.children.push_back(std::move(inherited_child));
    }
}

std::string narrow(std::wstring const& value) {
    if (value.empty()) {
        return "";
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return "";
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::optional<std::string> dbgHelpTypeName(HANDLE process, DWORD64 module_base, ULONG type_id) {
    WCHAR* name = nullptr;
    if (!SymGetTypeInfo(process, module_base, type_id, TI_GET_SYMNAME, &name) || name == nullptr) {
        return std::nullopt;
    }

    std::string result = narrow(name);
    LocalFree(name);
    return result;
}

template <typename T>
bool dbgHelpTypeInfo(HANDLE process, DWORD64 module_base, ULONG type_id, IMAGEHLP_SYMBOL_TYPE_INFO info, T& value) {
    return SymGetTypeInfo(process, module_base, type_id, info, &value) != FALSE;
}

ScalarType dbgHelpScalarType(BasicType basic_type) {
    switch (basic_type) {
        case btChar:
        case btInt:
        case btLong:
            return ScalarType::SignedInteger;
        case btUInt:
        case btULong:
            return ScalarType::UnsignedInteger;
        case btFloat:
            return ScalarType::FloatingPoint;
        case btBool:
            return ScalarType::Boolean;
        case btWChar:
            return ScalarType::WChar;
        default:
            return ScalarType::None;
    }
}

int64_t variantToInt64(VARIANT const& value) {
    switch (value.vt) {
        case VT_I1:
            return value.cVal;
        case VT_UI1:
            return value.bVal;
        case VT_I2:
            return value.iVal;
        case VT_UI2:
            return value.uiVal;
        case VT_I4:
        case VT_INT:
            return value.lVal;
        case VT_UI4:
        case VT_UINT:
            return value.ulVal;
        case VT_I8:
            return value.llVal;
        case VT_UI8:
            return static_cast<int64_t>(value.ullVal);
        default:
            return 0;
    }
}

std::vector<ULONG> dbgHelpTypeChildren(HANDLE process, DWORD64 module_base, ULONG type_id) {
    DWORD child_count = 0;
    if (!dbgHelpTypeInfo(process, module_base, type_id, TI_GET_CHILDRENCOUNT, child_count) || child_count == 0) {
        return {};
    }

    size_t const bytes = sizeof(TI_FINDCHILDREN_PARAMS) + (child_count - 1) * sizeof(ULONG);
    std::vector<uint8_t> buffer(bytes);
    auto* children = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(buffer.data());
    children->Count = child_count;
    children->Start = 0;
    if (!SymGetTypeInfo(process, module_base, type_id, TI_FINDCHILDREN, children)) {
        return {};
    }

    return std::vector<ULONG>(children->ChildId, children->ChildId + child_count);
}

bool shouldSkipDbgHelpChild(std::string const& name) {
    return name.starts_with("std::")
        || (name.size() > 2 && name[0] == '_' && std::isupper(static_cast<unsigned char>(name[1])));
}

bool resolveDbgHelpType(HANDLE process,
                        DWORD64 module_base,
                        ULONG type_id,
                        SymbolDescriptor& symbol,
                        std::unordered_set<ULONG>& resolving);

void addDbgHelpFields(HANDLE process,
                      DWORD64 module_base,
                      ULONG type_id,
                      SymbolDescriptor& symbol,
                      std::unordered_set<ULONG>& resolving) {
    for (ULONG child_id : dbgHelpTypeChildren(process, module_base, type_id)) {
        SymTagEnum child_tag = SymTagNull;
        if (!dbgHelpTypeInfo(process, module_base, child_id, TI_GET_SYMTAG, child_tag)) {
            continue;
        }

        if (child_tag == SymTagData) {
            DataKind data_kind = DataIsUnknown;
            if (!dbgHelpTypeInfo(process, module_base, child_id, TI_GET_DATAKIND, data_kind)
                || data_kind != DataIsMember) {
                continue;
            }

            DWORD child_type_id = 0;
            DWORD child_offset = 0;
            if (!dbgHelpTypeInfo(process, module_base, child_id, TI_GET_TYPEID, child_type_id)
                || !dbgHelpTypeInfo(process, module_base, child_id, TI_GET_OFFSET, child_offset)) {
                continue;
            }

            auto child = std::make_shared<SymbolDescriptor>();
            child->name = dbgHelpTypeName(process, module_base, child_id).value_or("");
            if (child->name.empty() || shouldSkipDbgHelpChild(child->name)) {
                continue;
            }
            child->offset_to_parent = child_offset;
            if (!resolveDbgHelpType(process, module_base, child_type_id, *child, resolving)) {
                continue;
            }

            DWORD bit_position = 0;
            if (dbgHelpTypeInfo(process, module_base, child_id, TI_GET_BITPOSITION, bit_position)) {
                child->bitfield_position = static_cast<int>(bit_position);
                ULONG64 bit_length = 0;
                if (dbgHelpTypeInfo(process, module_base, child_id, TI_GET_LENGTH, bit_length)) {
                    child->size = static_cast<uint32_t>(bit_length);
                }
            }
            symbol.children.push_back(std::move(child));
        } else if (child_tag == SymTagBaseClass) {
            DWORD base_type_id = 0;
            DWORD base_offset = 0;
            if (!dbgHelpTypeInfo(process, module_base, child_id, TI_GET_TYPEID, base_type_id)
                || !dbgHelpTypeInfo(process, module_base, child_id, TI_GET_OFFSET, base_offset)) {
                continue;
            }

            SymbolDescriptor base_symbol{
              .name = dbgHelpTypeName(process, module_base, child_id).value_or("")
            };
            if (base_symbol.name.empty() || shouldSkipDbgHelpChild(base_symbol.name)) {
                continue;
            }
            if (resolveDbgHelpType(process, module_base, base_type_id, base_symbol, resolving)) {
                appendInheritedMembers(symbol, base_symbol, base_offset);
            }
        }
    }
}

bool resolveDbgHelpType(HANDLE process,
                        DWORD64 module_base,
                        ULONG type_id,
                        SymbolDescriptor& symbol,
                        std::unordered_set<ULONG>& resolving) {
    if (type_id == 0) {
        return false;
    }
    if (resolving.contains(type_id)) {
        symbol.kind = SymbolKind::Object;
        return true;
    }

    SymTagEnum tag = SymTagNull;
    if (!dbgHelpTypeInfo(process, module_base, type_id, TI_GET_SYMTAG, tag)) {
        return false;
    }

    resolving.insert(type_id);
    bool resolved = true;
    switch (tag) {
        case SymTagBaseType: {
            BasicType basic_type = btNoType;
            ULONG64 length = 0;
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_BASETYPE, basic_type);
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_LENGTH, length);
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = dbgHelpScalarType(basic_type);
            symbol.size = static_cast<uint32_t>(length);
            break;
        }
        case SymTagPointerType: {
            ULONG64 length = 0;
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_LENGTH, length);
            symbol.kind = SymbolKind::Pointer;
            symbol.size = length != 0 ? static_cast<uint32_t>(length) : sizeof(void*);
            break;
        }
        case SymTagArrayType: {
            ULONG64 length = 0;
            DWORD element_type_id = 0;
            DWORD count = 0;
            if (!dbgHelpTypeInfo(process, module_base, type_id, TI_GET_TYPEID, element_type_id)
                || !dbgHelpTypeInfo(process, module_base, type_id, TI_GET_LENGTH, length)) {
                resolved = false;
                break;
            }
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_COUNT, count);

            auto element = std::make_shared<SymbolDescriptor>();
            resolved = resolveDbgHelpType(process, module_base, element_type_id, *element, resolving);
            if (resolved && element->size != 0) {
                symbol.kind = SymbolKind::Array;
                symbol.size = static_cast<uint32_t>(length);
                symbol.array_element_count = count != 0 ? count : static_cast<uint32_t>(length / element->size);
                symbol.children.push_back(std::move(element));
            }
            break;
        }
        case SymTagUDT: {
            ULONG64 length = 0;
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_LENGTH, length);
            symbol.kind = SymbolKind::Object;
            symbol.size = static_cast<uint32_t>(length);
            addDbgHelpFields(process, module_base, type_id, symbol, resolving);
            break;
        }
        case SymTagEnumerator: {
            BasicType basic_type = btInt;
            ULONG64 length = 0;
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_BASETYPE, basic_type);
            dbgHelpTypeInfo(process, module_base, type_id, TI_GET_LENGTH, length);
            symbol.kind = SymbolKind::Enum;
            symbol.scalar_type = dbgHelpScalarType(basic_type);
            symbol.size = length != 0 ? static_cast<uint32_t>(length) : 4;
            for (ULONG child_id : dbgHelpTypeChildren(process, module_base, type_id)) {
                SymTagEnum child_tag = SymTagNull;
                DataKind data_kind = DataIsUnknown;
                if (!dbgHelpTypeInfo(process, module_base, child_id, TI_GET_SYMTAG, child_tag)
                    || child_tag != SymTagData
                    || !dbgHelpTypeInfo(process, module_base, child_id, TI_GET_DATAKIND, data_kind)
                    || data_kind != DataIsConstant) {
                    continue;
                }
                VARIANT value{};
                if (!dbgHelpTypeInfo(process, module_base, child_id, TI_GET_VALUE, value)) {
                    continue;
                }
                auto enum_child = std::make_shared<SymbolDescriptor>(SymbolDescriptor{
                    .name = dbgHelpTypeName(process, module_base, child_id).value_or(""),
                    .kind = SymbolKind::EnumValue
                });
                enum_child->enum_value = variantToInt64(value);
                VariantClear(&value);
                symbol.children.push_back(std::move(enum_child));
            }
            break;
        }
        case SymTagTypedef: {
            DWORD underlying_type_id = 0;
            resolved = dbgHelpTypeInfo(process, module_base, type_id, TI_GET_TYPEID, underlying_type_id)
                    && resolveDbgHelpType(process, module_base, underlying_type_id, symbol, resolving);
            break;
        }
        default:
            resolved = false;
            break;
    }

    resolving.erase(type_id);
    return resolved && symbol.size > 0;
}

std::mutex& symbolHandlerMutex() {
    static std::mutex mutex;
    return mutex;
}

class ScopedSymbolHandler {
  public:
    ScopedSymbolHandler()
        : m_lock(symbolHandlerMutex()),
          m_process(GetCurrentProcess()) {
        // Symbols are loaded lazily by DbgHelp. Keep the handler scoped so it
        // cannot block another module in the process from calling SymInitialize.
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        m_initialized = SymInitialize(m_process, nullptr, TRUE) != FALSE;
        if (!m_initialized) {
            std::cerr << "SymInitialize failed with error " << GetLastError() << "\n";
        }
    }

    ScopedSymbolHandler(ScopedSymbolHandler const&) = delete;
    ScopedSymbolHandler& operator=(ScopedSymbolHandler const&) = delete;

    ~ScopedSymbolHandler() {
        if (m_initialized) {
            SymCleanup(m_process);
        }
    }

    bool initialized() const {
        return m_initialized;
    }

    HANDLE process() const {
        return m_process;
    }

  private:
    std::unique_lock<std::mutex> m_lock;
    HANDLE m_process = nullptr;
    bool m_initialized = false;
};

std::string moduleStem(std::filesystem::path const& path) {
    return path.stem().string();
}

std::string descriptorValue(std::string const& value) {
    return "\"" + value + "\"";
}

std::string descriptorValue(SymbolKind value) {
    return std::to_string(static_cast<int>(value));
}

std::string descriptorValue(ScalarType value) {
    return std::to_string(static_cast<int>(value));
}

template <typename T>
std::string descriptorValue(T value) {
    return std::to_string(value);
}

template <typename T>
bool compareDescriptorField(std::string const& path,
                            char const* field,
                            T const& raw_value,
                            T const& dbghelp_value,
                            std::string& mismatch) {
    if (raw_value == dbghelp_value) {
        return true;
    }

    mismatch = path + ": " + field + " mismatch raw="
             + descriptorValue(raw_value) + " dbghelp=" + descriptorValue(dbghelp_value);
    return false;
}

std::string childDescriptorPath(std::string const& parent, SymbolDescriptor const& child, size_t index) {
    if (!child.name.empty()) {
        return parent + "." + child.name;
    }
    return parent + ".children[" + std::to_string(index) + "]";
}

bool symbolDescriptorsMatch(SymbolDescriptor const& raw,
                            SymbolDescriptor const& dbghelp,
                            std::string const& path,
                            std::string& mismatch) {
    if (!compareDescriptorField(path, "name", raw.name, dbghelp.name, mismatch)
        || !compareDescriptorField(path, "size", raw.size, dbghelp.size, mismatch)
        || !compareDescriptorField(path, "kind", raw.kind, dbghelp.kind, mismatch)
        || !compareDescriptorField(path, "offset_to_parent", raw.offset_to_parent, dbghelp.offset_to_parent, mismatch)
        || !compareDescriptorField(path, "array_element_count", raw.array_element_count, dbghelp.array_element_count, mismatch)
        || !compareDescriptorField(path, "scalar_type", raw.scalar_type, dbghelp.scalar_type, mismatch)
        || !compareDescriptorField(path, "bitfield_position", raw.bitfield_position, dbghelp.bitfield_position, mismatch)
        || !compareDescriptorField(path, "enum_value", raw.enum_value, dbghelp.enum_value, mismatch)
        || !compareDescriptorField(path, "child_count", raw.children.size(), dbghelp.children.size(), mismatch)) {
        return false;
    }

    for (size_t i = 0; i < raw.children.size(); ++i) {
        if (raw.children[i] == nullptr || dbghelp.children[i] == nullptr) {
            mismatch = path + ": null child at index " + std::to_string(i);
            return false;
        }
        if (!symbolDescriptorsMatch(*raw.children[i],
                                    *dbghelp.children[i],
                                    childDescriptorPath(path, *raw.children[i], i),
                                    mismatch)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<SymbolDescriptor> loadDbgHelpSymbolDescriptor(DbgHelpModuleContext const& module,
                                                            std::string const& raw_name,
                                                            std::string const& display_name,
                                                            MemoryAddress address) {
    ScopedSymbolHandler symbol_handler;
    if (!symbol_handler.initialized()) {
        return std::nullopt;
    }
    HANDLE process = symbol_handler.process();

    std::array<uint8_t, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbol_buffer{};
    auto* symbol_info = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer.data());
    symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol_info->MaxNameLen = MAX_SYM_NAME;

    std::string const module_name = moduleStem(module.path);
    std::array<std::string, 2> names = {
      module_name + "!" + raw_name,
      raw_name
    };
    bool found = false;
    for (std::string const& name : names) {
        if (SymFromName(process, name.c_str(), symbol_info) && symbol_info->ModBase == module.base_address) {
            found = true;
            break;
        }
    }
    if (!found) {
        return std::nullopt;
    }

    SymbolDescriptor symbol{
      .name = display_name,
      .address = address
    };
    std::unordered_set<ULONG> resolving;
    if (!resolveDbgHelpType(process, symbol_info->ModBase, symbol_info->TypeIndex, symbol, resolving)) {
        return std::nullopt;
    }
    return symbol;
}

void verifyRawPdbSymbolWithDbgHelp(DbgHelpModuleContext const& module,
                                   std::string const& raw_name,
                                   SymbolDescriptor const& symbol,
                                   MemoryAddress address) {
    auto dbghelp_symbol = loadDbgHelpSymbolDescriptor(module, raw_name, symbol.name, address);
    if (!dbghelp_symbol) {
        std::cerr << "RawPDB/DbgHelp verification skipped for \"" << symbol.name
                  << "\": DbgHelp could not resolve the symbol\n";
        return;
    }

    std::string mismatch;
    if (!symbolDescriptorsMatch(symbol, *dbghelp_symbol, symbol.name, mismatch)) {
        std::cerr << "RawPDB/DbgHelp mismatch for \"" << symbol.name << "\": " << mismatch << "\n";
    }
}
