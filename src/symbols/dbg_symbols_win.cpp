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

#include "dbg_symbols.hpp"
#include "dbghelp_helpers.h"
#include "symbol_helpers.h"
#include "variant_symbol.h"

#include "PDB.h"
#include "PDB_CoalescedMSFStream.h"
#include "PDB_DBIStream.h"
#include "PDB_DBITypes.h"
#include "PDB_GlobalSymbolStream.h"
#include "PDB_ImageSectionStream.h"
#include "PDB_ModuleInfoStream.h"
#include "PDB_ModuleSymbolStream.h"
#include "PDB_PublicSymbolStream.h"
#include "PDB_RawFile.h"
#include "PDB_TPIStream.h"
#include "PDB_TPITypes.h"

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Uncomment this while changing RawPDB type resolution to compare every final
// symbol descriptor tree against the tree DbgHelp produces for the same symbol.
// This is intentionally slow and logs mismatches to stderr for verification.
// #define DBGGUI_VERIFY_RAWPDB_WITH_DBGHELP 1

namespace {

// Windows symbol loading bypasses DbgHelp for bulk enumeration. RawPDB gives
// direct access to the PDB streams, so this file maps each loaded module's PDB,
// reads CodeView DBI/TPI records, translates them into backend-neutral symbol
// descriptor trees, and then exposes those roots through VariantSymbol.
using DbiRecord = PDB::CodeView::DBI::Record;
using DbiRecordKind = PDB::CodeView::DBI::SymbolRecordKind;
using TpiRecord = PDB::CodeView::TPI::Record;
using TpiRecordKind = PDB::CodeView::TPI::TypeRecordKind;
using TypeIndexKind = PDB::CodeView::TPI::TypeIndexKind;

// Forward declarations and full definitions are separate TPI records. This key
// lets TypeTable pair a forward reference with the record that has the layout.
struct TypeDefinitionKey {
    TpiRecordKind kind = {};
    std::string name;

    bool operator==(TypeDefinitionKey const&) const = default;
};

struct TypeDefinitionKeyHash {
    size_t operator()(TypeDefinitionKey const& key) const {
        size_t seed = std::hash<uint16_t>{}(static_cast<uint16_t>(key.kind));
        size_t const value = std::hash<std::string>{}(key.name);
        seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct TypeDefinitionRecord {
    uint32_t type_index = 0;
    TpiRecord const* record = nullptr;
};

// RawPDB keeps lightweight views into the bytes passed to CreateRawFile. Keeping
// the PDB memory mapped avoids copying the whole file and keeps those views
// valid for as long as the RawPDB streams are being used.
class MappedFile {
  public:
    explicit MappedFile(std::filesystem::path const& path) {
        m_file = CreateFileA(path.string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) {
            return;
        }

        LARGE_INTEGER file_size{};
        if (!GetFileSizeEx(m_file, &file_size) || file_size.QuadPart <= 0) {
            return;
        }
        m_size = static_cast<size_t>(file_size.QuadPart);

        m_mapping = CreateFileMappingA(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (m_mapping == nullptr) {
            return;
        }

        m_data = MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0);
    }

    ~MappedFile() {
        if (m_data != nullptr) {
            UnmapViewOfFile(m_data);
        }
        if (m_mapping != nullptr) {
            CloseHandle(m_mapping);
        }
        if (m_file != INVALID_HANDLE_VALUE) {
            CloseHandle(m_file);
        }
    }

    bool valid() const {
        return m_data != nullptr && m_size > 0;
    }

    void const* data() const {
        return m_data;
    }

    size_t size() const {
        return m_size;
    }

  private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = nullptr;
    void const* m_data = nullptr;
    size_t m_size = 0;
};

// CodeView type indices are dense inside the TPI stream. TypeTable turns the
// RawPDB record offsets into stable record pointers and caches resolved
// SymbolDescriptor layouts so repeated uses of the same type do not need to
// rebuild the full child tree.
class TypeTable {
  public:
    explicit TypeTable(PDB::TPIStream const& tpi_stream)
        : m_type_index_begin(tpi_stream.GetFirstTypeIndex()),
          m_type_index_end(tpi_stream.GetLastTypeIndex()) {
        // TPI records live in an MSF stream that may be split across PDB
        // blocks. Coalesce it once so the record offsets reported by RawPDB can
        // be stored as simple pointers.
        PDB::DirectMSFStream const& direct_stream = tpi_stream.GetDirectMSFStream();
        m_stream = PDB::CoalescedMSFStream(direct_stream, static_cast<uint32_t>(direct_stream.GetSize()), 0);

        m_records.resize(tpi_stream.GetTypeRecordCount());
        size_t type_index = 0;
        tpi_stream.ForEachTypeRecordHeaderAndOffset([&](PDB::CodeView::TPI::RecordHeader const&, size_t offset) {
            m_records[type_index] = m_stream.GetDataAtOffset<TpiRecord const>(offset);
            ++type_index;
        });
    }

    uint32_t firstTypeIndex() const {
        return m_type_index_begin;
    }

    uint32_t lastTypeIndex() const {
        return m_type_index_end;
    }

    TpiRecord const* getTypeRecord(uint32_t type_index) const {
        if (type_index < m_type_index_begin || type_index >= m_type_index_end) {
            return nullptr;
        }
        return m_records[type_index - m_type_index_begin];
    }

    SymbolDescriptor const* cachedType(uint32_t type_index) const {
        auto it = m_resolved_types.find(type_index);
        if (it == m_resolved_types.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    void cacheType(uint32_t type_index, SymbolDescriptor const& symbol) const {
        m_resolved_types.try_emplace(type_index, symbol.clone());
    }

    TpiRecord const* findFullDefinition(uint32_t forward_type_index, TpiRecord const* forward_record) const;

  private:
    void buildFullDefinitions() const;

    uint32_t m_type_index_begin = 0;
    uint32_t m_type_index_end = 0;
    PDB::CoalescedMSFStream m_stream;
    std::vector<TpiRecord const*> m_records;
    mutable std::unordered_map<uint32_t, std::unique_ptr<SymbolDescriptor>> m_resolved_types;
    mutable std::unordered_map<TypeDefinitionKey, std::vector<TypeDefinitionRecord>, TypeDefinitionKeyHash> m_full_definitions;
    mutable bool m_full_definitions_built = false;
};

// Per-loaded-module state needed to translate PDB section/offset pairs into
// live process addresses. Symbols from DLLs get a module stem prefix so
// same-named globals from different modules do not collide in the root list.
struct ModuleContext {
    HMODULE handle = nullptr;
    MemoryAddress base_address = 0;
    size_t size = 0;
    std::filesystem::path path;
    std::string symbol_prefix;
};

DbgHelpModuleContext dbgHelpModuleContext(ModuleContext const& module) {
    return DbgHelpModuleContext{
      .base_address = module.base_address,
      .path = module.path,
    };
}

// The same data record can be reachable through the global hash stream and a
// module symbol stream. Deduplicate after address translation, but keep the
// display name in the key so prefixed symbols from different modules remain
// distinct.
using SeenSymbols = std::set<std::pair<MemoryAddress, std::string>>;

static void appendInheritedMembers(SymbolDescriptor& symbol,
                                   SymbolDescriptor const& base_symbol,
                                   uint32_t base_offset) {
    for (auto const& base_child : base_symbol.children) {
        auto inherited_child = std::make_shared<SymbolDescriptor>(*base_child);
        inherited_child->offset_to_parent += base_offset;
        symbol.children.push_back(std::move(inherited_child));
    }
}

// CodeView stores many sizes and offsets as "numeric leaves": small values are
// encoded directly in the first 16 bits, while larger values start with an
// LF_* discriminator followed by the payload of that leaf kind.
uint8_t getLeafSize(TpiRecordKind kind) {
    if (kind < TpiRecordKind::LF_NUMERIC) {
        return sizeof(TpiRecordKind);
    }

    switch (kind) {
        case TpiRecordKind::LF_CHAR:
            return sizeof(TpiRecordKind) + sizeof(uint8_t);
        case TpiRecordKind::LF_SHORT:
        case TpiRecordKind::LF_USHORT:
            return sizeof(TpiRecordKind) + sizeof(uint16_t);
        case TpiRecordKind::LF_LONG:
        case TpiRecordKind::LF_ULONG:
        case TpiRecordKind::LF_REAL32:
            return sizeof(TpiRecordKind) + sizeof(uint32_t);
        case TpiRecordKind::LF_QUADWORD:
        case TpiRecordKind::LF_UQUADWORD:
        case TpiRecordKind::LF_REAL64:
            return sizeof(TpiRecordKind) + sizeof(uint64_t);
        default:
            return sizeof(TpiRecordKind);
    }
}

uint64_t readUnsignedLeaf(char const* data) {
    TpiRecordKind kind = *reinterpret_cast<TpiRecordKind const*>(data);
    if (kind < TpiRecordKind::LF_NUMERIC) {
        return static_cast<uint16_t>(kind);
    }

    char const* value = data + sizeof(TpiRecordKind);
    switch (kind) {
        case TpiRecordKind::LF_CHAR:
            return *reinterpret_cast<uint8_t const*>(value);
        case TpiRecordKind::LF_SHORT:
            return static_cast<uint64_t>(*reinterpret_cast<int16_t const*>(value));
        case TpiRecordKind::LF_USHORT:
            return *reinterpret_cast<uint16_t const*>(value);
        case TpiRecordKind::LF_LONG:
            return static_cast<uint64_t>(*reinterpret_cast<int32_t const*>(value));
        case TpiRecordKind::LF_ULONG:
        case TpiRecordKind::LF_REAL32:
            return *reinterpret_cast<uint32_t const*>(value);
        case TpiRecordKind::LF_QUADWORD:
            return static_cast<uint64_t>(*reinterpret_cast<int64_t const*>(value));
        case TpiRecordKind::LF_UQUADWORD:
        case TpiRecordKind::LF_REAL64:
            return *reinterpret_cast<uint64_t const*>(value);
        default:
            return 0;
    }
}

int64_t readSignedLeaf(char const* data) {
    TpiRecordKind kind = *reinterpret_cast<TpiRecordKind const*>(data);
    if (kind < TpiRecordKind::LF_NUMERIC) {
        return static_cast<int16_t>(static_cast<uint16_t>(kind));
    }

    char const* value = data + sizeof(TpiRecordKind);
    switch (kind) {
        case TpiRecordKind::LF_CHAR:
            return *reinterpret_cast<int8_t const*>(value);
        case TpiRecordKind::LF_SHORT:
            return *reinterpret_cast<int16_t const*>(value);
        case TpiRecordKind::LF_USHORT:
            return *reinterpret_cast<uint16_t const*>(value);
        case TpiRecordKind::LF_LONG:
            return *reinterpret_cast<int32_t const*>(value);
        case TpiRecordKind::LF_ULONG:
            return *reinterpret_cast<uint32_t const*>(value);
        case TpiRecordKind::LF_QUADWORD:
            return *reinterpret_cast<int64_t const*>(value);
        case TpiRecordKind::LF_UQUADWORD:
            return static_cast<int64_t>(*reinterpret_cast<uint64_t const*>(value));
        default:
            return 0;
    }
}

char const* getLeafName(char const* data) {
    TpiRecordKind kind = *reinterpret_cast<TpiRecordKind const*>(data);
    return data + getLeafSize(kind);
}

// Older *_ST CodeView records store length-prefixed strings. The non-ST records
// used by modern PDBs store null-terminated strings in place.
std::string readCodeViewString(char const* data, bool string_table_format) {
    if (string_table_format) {
        size_t const size = static_cast<unsigned char>(data[0]);
        return std::string(data + 1, size);
    }
    return data;
}

size_t codeViewStringSize(char const* data, bool string_table_format) {
    if (string_table_format) {
        return static_cast<size_t>(static_cast<unsigned char>(data[0])) + 1;
    }
    return std::strlen(data) + 1;
}

constexpr uint32_t TypePropertyForwardReference = 1u << 7;
constexpr uint32_t TypePropertyHasUniqueName = 1u << 9;

std::string recordName(TpiRecord const* record) {
    if (record == nullptr) {
        return "";
    }

    switch (record->header.kind) {
        case TpiRecordKind::LF_CLASS:
        case TpiRecordKind::LF_STRUCTURE:
            return getLeafName(record->data.LF_CLASS.data);
        case TpiRecordKind::LF_CLASS2:
        case TpiRecordKind::LF_STRUCTURE2:
            return getLeafName(record->data.LF_CLASS2.data);
        case TpiRecordKind::LF_UNION:
            return getLeafName(record->data.LF_UNION.data);
        case TpiRecordKind::LF_ENUM:
            return record->data.LF_ENUM.name;
        default:
            return "";
    }
}

// Decorated unique name that follows the regular name when property.hasuniquename
// is set. MSVC emits it to distinguish otherwise identically-named types.
std::string recordUniqueName(TpiRecord const* record) {
    if (record == nullptr) {
        return "";
    }

    char const* name = nullptr;
    bool has_unique = false;
    switch (record->header.kind) {
        case TpiRecordKind::LF_CLASS:
        case TpiRecordKind::LF_STRUCTURE:
            name = getLeafName(record->data.LF_CLASS.data);
            has_unique = record->data.LF_CLASS.property.hasuniquename != 0;
            break;
        case TpiRecordKind::LF_CLASS2:
        case TpiRecordKind::LF_STRUCTURE2:
            name = getLeafName(record->data.LF_CLASS2.data);
            has_unique = (record->data.LF_CLASS2.property & TypePropertyHasUniqueName) != 0;
            break;
        case TpiRecordKind::LF_UNION:
            name = getLeafName(record->data.LF_UNION.data);
            has_unique = record->data.LF_UNION.property.hasuniquename != 0;
            break;
        default:
            return "";
    }
    if (!has_unique) {
        return "";
    }
    return std::string(name + std::strlen(name) + 1);
}

bool isForwardDeclaration(TpiRecord const* record) {
    if (record == nullptr) {
        return false;
    }

    switch (record->header.kind) {
        case TpiRecordKind::LF_CLASS:
        case TpiRecordKind::LF_STRUCTURE:
            return record->data.LF_CLASS.property.fwdref != 0;
        case TpiRecordKind::LF_CLASS2:
        case TpiRecordKind::LF_STRUCTURE2:
            return (record->data.LF_CLASS2.property & TypePropertyForwardReference) != 0;
        case TpiRecordKind::LF_UNION:
            return record->data.LF_UNION.property.fwdref != 0;
        default:
            return false;
    }
}

// Key for the full-definition lookup. The decorated unique name (when present)
// distinguishes unrelated types that share a plain name; otherwise fall back to
// the record kind plus plain name.
TypeDefinitionKey fullDefinitionKey(TpiRecord const* record) {
    std::string const unique_name = recordUniqueName(record);
    if (!unique_name.empty()) {
        return TypeDefinitionKey{.name = unique_name};
    }
    return TypeDefinitionKey{.kind = record->header.kind, .name = recordName(record)};
}

void TypeTable::buildFullDefinitions() const {
    if (m_full_definitions_built) {
        return;
    }

    for (size_t i = 0; i < m_records.size(); ++i) {
        TpiRecord const* record = m_records[i];
        if (record == nullptr || isForwardDeclaration(record) || recordName(record).empty()) {
            continue;
        }
        TypeDefinitionKey const key = fullDefinitionKey(record);
        m_full_definitions[key].push_back(TypeDefinitionRecord{
          .type_index = m_type_index_begin + static_cast<uint32_t>(i),
          .record = record,
        });
    }
    m_full_definitions_built = true;
}

TpiRecord const* TypeTable::findFullDefinition(uint32_t forward_type_index, TpiRecord const* forward_record) const {
    std::string const name = recordName(forward_record);
    if (name.empty()) {
        return forward_record;
    }

    buildFullDefinitions();

    auto it = m_full_definitions.find(fullDefinitionKey(forward_record));
    if (it == m_full_definitions.end() || it->second.empty()) {
        return forward_record;
    }

    std::vector<TypeDefinitionRecord> const& candidates = it->second;
    // Incremental PDBs can retain stale full definitions with the same decorated
    // name. The forward reference and its matching full definition are emitted
    // into the same TPI neighborhood, while stale definitions can be larger or
    // smaller, so size is not a reliable tie-breaker.
    auto after_forward = std::find_if(candidates.begin(),
                                      candidates.end(),
                                      [forward_type_index](TypeDefinitionRecord const& candidate) {
                                          return candidate.type_index > forward_type_index;
                                      });
    if (after_forward != candidates.end()) {
        return after_forward->record;
    }

    return candidates.back().record;
}

bool isSpecialPointerType(uint32_t type_index) {
    // CodeView simple pointer type indices encode pointer mode in bits 8..10.
    return (type_index & 0x0700u) != 0;
}

void setBuiltinType(uint32_t type_index, SymbolDescriptor& symbol) {
    if (isSpecialPointerType(type_index)) {
        symbol.kind = SymbolKind::Pointer;
        symbol.size = sizeof(void*);
        return;
    }

    switch (static_cast<TypeIndexKind>(type_index)) {
        case TypeIndexKind::T_CHAR:
        case TypeIndexKind::T_RCHAR:
        case TypeIndexKind::T_INT1:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::SignedInteger;
            symbol.size = 1;
            break;
        case TypeIndexKind::T_UCHAR:
        case TypeIndexKind::T_UINT1:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::UnsignedInteger;
            symbol.size = 1;
            break;
        case TypeIndexKind::T_SHORT:
        case TypeIndexKind::T_INT2:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::SignedInteger;
            symbol.size = 2;
            break;
        case TypeIndexKind::T_USHORT:
        case TypeIndexKind::T_UINT2:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::UnsignedInteger;
            symbol.size = 2;
            break;
        case TypeIndexKind::T_LONG:
        case TypeIndexKind::T_INT4:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::SignedInteger;
            symbol.size = 4;
            break;
        case TypeIndexKind::T_ULONG:
        case TypeIndexKind::T_UINT4:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::UnsignedInteger;
            symbol.size = 4;
            break;
        case TypeIndexKind::T_QUAD:
        case TypeIndexKind::T_INT8:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::SignedInteger;
            symbol.size = 8;
            break;
        case TypeIndexKind::T_UQUAD:
        case TypeIndexKind::T_UINT8:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::UnsignedInteger;
            symbol.size = 8;
            break;
        case TypeIndexKind::T_REAL32:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::FloatingPoint;
            symbol.size = 4;
            break;
        case TypeIndexKind::T_REAL64:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::FloatingPoint;
            symbol.size = 8;
            break;
        case TypeIndexKind::T_BOOL08:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::Boolean;
            symbol.size = 1;
            break;
        case TypeIndexKind::T_BOOL16:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::Boolean;
            symbol.size = 2;
            break;
        case TypeIndexKind::T_BOOL32:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::Boolean;
            symbol.size = 4;
            break;
        case TypeIndexKind::T_BOOL64:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::Boolean;
            symbol.size = 8;
            break;
        case TypeIndexKind::T_WCHAR:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::WChar;
            symbol.size = 2;
            break;
        case TypeIndexKind::T_CHAR16:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::Char16;
            symbol.size = 2;
            break;
        case TypeIndexKind::T_CHAR32:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::Char32;
            symbol.size = 4;
            break;
        default:
            symbol.kind = SymbolKind::Scalar;
            symbol.scalar_type = ScalarType::None;
            symbol.size = 4;
            break;
    }
}

void copyTypePayload(SymbolDescriptor& dst, SymbolDescriptor const& src) {
    std::string name = dst.name;
    MemoryAddress address = dst.address;
    uint32_t offset_to_parent = dst.offset_to_parent;

    dst = SymbolDescriptor{};
    dst.name = std::move(name);
    dst.address = address;
    dst.offset_to_parent = offset_to_parent;
    dst.size = src.size;
    dst.kind = src.kind;
    dst.array_element_count = src.array_element_count;
    dst.scalar_type = src.scalar_type;
    dst.bitfield_position = src.bitfield_position;
    dst.enum_value = src.enum_value;
    dst.children = src.children;
}

bool resolveType(TypeTable const& type_table,
                 uint32_t type_index,
                 SymbolDescriptor& symbol,
                 std::unordered_set<uint32_t>& resolving);

void addFields(TypeTable const& type_table,
               TpiRecord const* field_list,
               SymbolDescriptor& symbol,
               std::unordered_set<uint32_t>& resolving) {
    if (field_list == nullptr || field_list->header.kind != TpiRecordKind::LF_FIELDLIST) {
        return;
    }

    // LF_FIELDLIST is a byte stream of variable-length records. Only records
    // that contribute to inspectable object layout become children; methods,
    // friends, nested types, and virtual base metadata are skipped by advancing
    // over their payload. Each record starts on a 4-byte boundary.
    size_t const maximum_size = field_list->header.size - sizeof(uint16_t);
    for (size_t i = 0; i < maximum_size;) {
        auto const* field_record = reinterpret_cast<PDB::CodeView::TPI::FieldList const*>(
          reinterpret_cast<uint8_t const*>(&field_list->data.LF_FIELD.list) + i);

        if (field_record->kind == TpiRecordKind::LF_MEMBER || field_record->kind == TpiRecordKind::LF_MEMBER_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_MEMBER_ST;
            char const* name = getLeafName(field_record->data.LF_MEMBER.offset);
            auto child = std::make_shared<SymbolDescriptor>();
            child->name = readCodeViewString(name, string_table_format);
            child->offset_to_parent = static_cast<uint32_t>(readUnsignedLeaf(field_record->data.LF_MEMBER.offset));

            std::string const child_name = child->name;
            if (!startsWith(child_name, "std::")
                && !(child_name.size() > 2 && child_name[0] == '_' && std::isupper(static_cast<unsigned char>(child_name[1])))
                && resolveType(type_table, field_record->data.LF_MEMBER.index, *child, resolving)) {
                symbol.children.push_back(std::move(child));
            }

            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_BCLASS) {
            char const* offset = field_record->data.LF_BCLASS.offset;
            SymbolDescriptor base_symbol;
            TpiRecord const* base_record = type_table.getTypeRecord(field_record->data.LF_BCLASS.index);
            base_symbol.name = recordName(base_record);
            uint32_t const base_offset = static_cast<uint32_t>(readUnsignedLeaf(offset));

            if (!startsWith(base_symbol.name, "std::")
                && !(base_symbol.name.size() > 2 && base_symbol.name[0] == '_' && std::isupper(static_cast<unsigned char>(base_symbol.name[1])))
                && resolveType(type_table, field_record->data.LF_BCLASS.index, base_symbol, resolving)) {
                appendInheritedMembers(symbol, base_symbol, base_offset);
            }

            i += static_cast<size_t>(getLeafName(offset) - reinterpret_cast<char const*>(field_record));
        } else if (field_record->kind == TpiRecordKind::LF_VBCLASS || field_record->kind == TpiRecordKind::LF_IVBCLASS) {
            char const* vbp_offset = field_record->data.LF_VBCLASS.vbpOffset;
            char const* vb_offset = getLeafName(vbp_offset);
            i += static_cast<size_t>(getLeafName(vb_offset) - reinterpret_cast<char const*>(field_record));
        } else if (field_record->kind == TpiRecordKind::LF_INDEX) {
            // Large field lists can be continued in another TPI record.
            TpiRecord const* continued = type_table.getTypeRecord(field_record->data.LF_INDEX.type);
            addFields(type_table, continued, symbol, resolving);
            i += sizeof(PDB::CodeView::TPI::FieldList::Data::LF_INDEX);
        } else if (field_record->kind == TpiRecordKind::LF_VFUNCTAB) {
            i += sizeof(PDB::CodeView::TPI::FieldList::Data::LF_VFUNCTAB);
        } else if (field_record->kind == TpiRecordKind::LF_STMEMBER || field_record->kind == TpiRecordKind::LF_STMEMBER_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_STMEMBER_ST;
            char const* name = field_record->data.LF_STMEMBER.name;
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_NESTTYPE || field_record->kind == TpiRecordKind::LF_NESTTYPE_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_NESTTYPE_ST;
            char const* name = field_record->data.LF_NESTTYPE.name;
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_NESTTYPEEX || field_record->kind == TpiRecordKind::LF_NESTTYPEEX_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_NESTTYPEEX_ST;
            char const* name = reinterpret_cast<char const*>(field_record)
                             + sizeof(TpiRecordKind)
                             + sizeof(PDB::CodeView::TPI::MemberAttributes)
                             + sizeof(uint32_t);
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_MEMBERMODIFY || field_record->kind == TpiRecordKind::LF_MEMBERMODIFY_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_MEMBERMODIFY_ST;
            char const* name = reinterpret_cast<char const*>(field_record)
                             + sizeof(TpiRecordKind)
                             + sizeof(PDB::CodeView::TPI::MemberAttributes)
                             + sizeof(uint32_t);
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_METHOD || field_record->kind == TpiRecordKind::LF_METHOD_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_METHOD_ST;
            char const* name = field_record->data.LF_METHOD.name;
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_ONEMETHOD || field_record->kind == TpiRecordKind::LF_ONEMETHOD_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_ONEMETHOD_ST;
            char const* name = reinterpret_cast<char const*>(field_record->data.LF_ONEMETHOD.vbaseoff);
            auto method_property = static_cast<PDB::CodeView::TPI::MethodProperty>(
              field_record->data.LF_ONEMETHOD.attributes.mprop);
            if (method_property == PDB::CodeView::TPI::MethodProperty::Intro
                || method_property == PDB::CodeView::TPI::MethodProperty::PureIntro) {
                name += sizeof(uint32_t);
            }
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_FRIENDFCN || field_record->kind == TpiRecordKind::LF_FRIENDFCN_ST) {
            bool const string_table_format = field_record->kind == TpiRecordKind::LF_FRIENDFCN_ST;
            char const* name = reinterpret_cast<char const*>(field_record)
                             + sizeof(TpiRecordKind)
                             + sizeof(uint16_t)
                             + sizeof(uint32_t);
            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += codeViewStringSize(name, string_table_format);
        } else if (field_record->kind == TpiRecordKind::LF_FRIENDCLS) {
            i += sizeof(TpiRecordKind) + sizeof(uint16_t) + sizeof(uint32_t);
        } else if (field_record->kind == TpiRecordKind::LF_VFUNCOFF) {
            i += sizeof(TpiRecordKind) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t);
        } else {
            break;
        }

        i = (i + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1);
    }
}

void addEnumerators(TypeTable const& type_table,
                    TpiRecord const* field_list,
                    SymbolDescriptor& symbol) {
    if (field_list == nullptr || field_list->header.kind != TpiRecordKind::LF_FIELDLIST) {
        return;
    }

    // Enum constants also live in an LF_FIELDLIST. They are represented as
    // children so VariantSymbol can display the names next to the numeric value.
    size_t const maximum_size = field_list->header.size - sizeof(uint16_t);
    for (size_t i = 0; i < maximum_size;) {
        auto const* field_record = reinterpret_cast<PDB::CodeView::TPI::FieldList const*>(
          reinterpret_cast<uint8_t const*>(&field_list->data.LF_FIELD.list) + i);

        if (field_record->kind == TpiRecordKind::LF_ENUMERATE) {
            char const* name = getLeafName(field_record->data.LF_ENUMERATE.value);
            auto child = std::make_shared<SymbolDescriptor>();
            child->name = name;
            child->kind = SymbolKind::EnumValue;
            child->enum_value = readSignedLeaf(field_record->data.LF_ENUMERATE.value);
            symbol.children.push_back(std::move(child));

            i += static_cast<size_t>(name - reinterpret_cast<char const*>(field_record));
            i += std::strlen(name) + 1;
        } else if (field_record->kind == TpiRecordKind::LF_INDEX) {
            TpiRecord const* continued = type_table.getTypeRecord(field_record->data.LF_INDEX.type);
            addEnumerators(type_table, continued, symbol);
            i += sizeof(PDB::CodeView::TPI::FieldList::Data::LF_INDEX);
        } else {
            break;
        }

        i = (i + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1);
    }
}

bool resolveType(TypeTable const& type_table,
                 uint32_t type_index,
                 SymbolDescriptor& symbol,
                 std::unordered_set<uint32_t>& resolving) {
    // CodeView simple type indices below the TPI range encode built-in scalar
    // and pointer types directly; everything else indexes a TPI record.
    if (type_index < type_table.firstTypeIndex()) {
        setBuiltinType(type_index, symbol);
        return true;
    }

    // Self-referential types such as linked-list nodes point back to a type that
    // is currently being expanded. Stop there and keep the child as an opaque UDT
    // instead of recursing forever.
    if (resolving.contains(type_index)) {
        symbol.kind = SymbolKind::Object;
        return true;
    }

    // Type records are shared by every symbol that uses the type. Reuse the
    // already translated descriptor payload, preserving the caller-provided name,
    // address, and member offset.
    if (SymbolDescriptor const* cached = type_table.cachedType(type_index)) {
        copyTypePayload(symbol, *cached);
        return true;
    }

    TpiRecord const* record = type_table.getTypeRecord(type_index);
    if (record == nullptr) {
        return false;
    }

    resolving.insert(type_index);
    bool resolved = true;

    switch (record->header.kind) {
        case TpiRecordKind::LF_MODIFIER:
            resolved = resolveType(type_table, record->data.LF_MODIFIER.type, symbol, resolving);
            break;
        case TpiRecordKind::LF_POINTER:
            symbol.kind = SymbolKind::Pointer;
            symbol.size = record->data.LF_POINTER.attr.size != 0 ? record->data.LF_POINTER.attr.size : sizeof(void*);
            break;
        case TpiRecordKind::LF_BITFIELD: {
            SymbolDescriptor underlying;
            resolved = resolveType(type_table, record->data.LF_BITFIELD.type, underlying, resolving);
            if (resolved) {
                copyTypePayload(symbol, underlying);
                symbol.size = record->data.LF_BITFIELD.length;
                symbol.bitfield_position = record->data.LF_BITFIELD.position;
            }
            break;
        }
        case TpiRecordKind::LF_ARRAY: {
            symbol.kind = SymbolKind::Array;
            symbol.size = static_cast<uint32_t>(readUnsignedLeaf(record->data.LF_ARRAY.data));

            auto element = std::make_shared<SymbolDescriptor>();
            resolved = resolveType(type_table, record->data.LF_ARRAY.elemtype, *element, resolving);
            if (resolved && element->size != 0) {
                symbol.array_element_count = symbol.size / element->size;
                symbol.children.push_back(std::move(element));
            }
            break;
        }
        case TpiRecordKind::LF_CLASS:
        case TpiRecordKind::LF_STRUCTURE:
        case TpiRecordKind::LF_CLASS2:
        case TpiRecordKind::LF_STRUCTURE2:
        case TpiRecordKind::LF_UNION: {
            // Data symbols often reference a forward declaration record. Swap it
            // for the full definition before reading size and member layout.
            bool const forward_declaration = isForwardDeclaration(record);
            TpiRecord const* full_record = forward_declaration ? type_table.findFullDefinition(type_index, record) : record;
            symbol.kind = SymbolKind::Object;

            uint32_t field_type = 0;
            if (full_record->header.kind == TpiRecordKind::LF_CLASS || full_record->header.kind == TpiRecordKind::LF_STRUCTURE) {
                symbol.size = static_cast<uint32_t>(readUnsignedLeaf(full_record->data.LF_CLASS.data));
                field_type = full_record->data.LF_CLASS.field;
            } else if (full_record->header.kind == TpiRecordKind::LF_CLASS2 || full_record->header.kind == TpiRecordKind::LF_STRUCTURE2) {
                symbol.size = static_cast<uint32_t>(readUnsignedLeaf(full_record->data.LF_CLASS2.data));
                field_type = full_record->data.LF_CLASS2.field;
            } else {
                symbol.size = static_cast<uint32_t>(readUnsignedLeaf(full_record->data.LF_UNION.data));
                field_type = full_record->data.LF_UNION.field;
            }

            addFields(type_table, type_table.getTypeRecord(field_type), symbol, resolving);
            break;
        }
        case TpiRecordKind::LF_ENUM: {
            symbol.kind = SymbolKind::Enum;
            SymbolDescriptor underlying;
            if (resolveType(type_table, record->data.LF_ENUM.utype, underlying, resolving)) {
                symbol.size = underlying.size;
                symbol.scalar_type = underlying.scalar_type == ScalarType::None ? ScalarType::SignedInteger : underlying.scalar_type;
            } else {
                symbol.size = 4;
                symbol.scalar_type = ScalarType::SignedInteger;
            }
            addEnumerators(type_table, type_table.getTypeRecord(record->data.LF_ENUM.field), symbol);
            break;
        }
        default:
            resolved = false;
            break;
    }

    resolving.erase(type_index);
    if (resolved) {
        type_table.cacheType(type_index, symbol);
    }
    return resolved;
}

bool resolveType(TypeTable const& type_table, uint32_t type_index, SymbolDescriptor& symbol) {
    std::unordered_set<uint32_t> resolving;
    return resolveType(type_table, type_index, symbol, resolving);
}

MemoryAddress sectionOffsetToAddress(ModuleContext const& module,
                                     PDB::ImageSectionStream const& image_sections,
                                     uint16_t section,
                                     uint32_t offset) {
    // DBI data records store a PE section number plus an offset inside that
    // section, not an absolute address. The image section stream converts that
    // pair to an RVA, which becomes a live address by adding the module base.
    uint32_t const rva = image_sections.ConvertSectionOffsetToRVA(section, offset);
    if (rva == 0) {
        return 0;
    }
    return module.base_address + rva;
}

std::string moduleStem(std::filesystem::path const& path) {
    return path.stem().string();
}

std::optional<std::filesystem::path> codeViewPdbPath(HMODULE module) {
    auto const* base = reinterpret_cast<uint8_t const*>(module);
    auto const* dos_header = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return std::nullopt;
    }

    auto const* nt_headers = reinterpret_cast<IMAGE_NT_HEADERS const*>(base + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return std::nullopt;
    }

    IMAGE_DATA_DIRECTORY const& debug_directory =
      nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debug_directory.VirtualAddress == 0 || debug_directory.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) {
        return std::nullopt;
    }

    auto const* debug_entries = reinterpret_cast<IMAGE_DEBUG_DIRECTORY const*>(base + debug_directory.VirtualAddress);
    size_t const entry_count = debug_directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (size_t i = 0; i < entry_count; ++i) {
        IMAGE_DEBUG_DIRECTORY const& entry = debug_entries[i];
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 || entry.SizeOfData < 24) {
            continue;
        }

        auto const* code_view = reinterpret_cast<char const*>(base + entry.AddressOfRawData);
        if (std::memcmp(code_view, "RSDS", 4) != 0) {
            continue;
        }

        // RSDS records are signature, GUID, age, then a null-terminated PDB
        // path written by the linker.
        char const* pdb_path = code_view + 24;
        if (pdb_path[0] != '\0') {
            return std::filesystem::path(pdb_path);
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> findPdbPath(ModuleContext const& module) {
    // Prefer the exact linker path embedded in the module. If the binary moved,
    // try the common deployment layouts next: same basename beside the image or
    // the embedded PDB filename beside the image.
    if (auto path = codeViewPdbPath(module.handle); path && std::filesystem::exists(*path)) {
        return path;
    }

    std::filesystem::path fallback = module.path;
    fallback.replace_extension(".pdb");
    if (std::filesystem::exists(fallback)) {
        return fallback;
    }

    if (auto path = codeViewPdbPath(module.handle); path) {
        std::filesystem::path sibling = module.path.parent_path() / path->filename();
        if (std::filesystem::exists(sibling)) {
            return sibling;
        }
    }

    return std::nullopt;
}

std::vector<ModuleContext> loadedModules() {
    // Enumerate every loaded image in this process so globals from DLLs can be
    // snapshotted too. The current module keeps historical unprefixed names;
    // other modules get "module|" prefixes to avoid cross-DLL collisions.
    HMODULE current_module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&getCurrentModuleInfo),
                       &current_module);

    DWORD bytes_needed = 0;
    std::vector<HMODULE> modules(256);
    while (true) {
        if (!EnumProcessModules(GetCurrentProcess(),
                                modules.data(),
                                static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                                &bytes_needed)) {
            return {};
        }
        if (bytes_needed <= modules.size() * sizeof(HMODULE)) {
            modules.resize(bytes_needed / sizeof(HMODULE));
            break;
        }
        modules.resize(bytes_needed / sizeof(HMODULE));
    }

    std::vector<ModuleContext> contexts;
    std::set<HMODULE> seen;
    for (HMODULE module : modules) {
        if (module == nullptr || seen.contains(module)) {
            continue;
        }
        seen.insert(module);

        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(module, path, sizeof(path)) == 0) {
            continue;
        }

        MODULEINFO module_info{};
        if (!GetModuleInformation(GetCurrentProcess(), module, &module_info, sizeof(module_info))) {
            continue;
        }

        std::filesystem::path module_path(path);
        bool const is_current_module = module == current_module;
        contexts.push_back(ModuleContext{
          .handle = module,
          .base_address = reinterpret_cast<MemoryAddress>(module),
          .size = module_info.SizeOfImage,
          .path = module_path,
          .symbol_prefix = is_current_module ? std::string{} : moduleStem(module_path) + "|"});
    }

    return contexts;
}

void storeDataSymbol(std::vector<std::unique_ptr<SymbolDescriptor>>& symbol_descriptors,
                     std::vector<std::unique_ptr<VariantSymbol>>& root_symbols,
                     TypeTable const& type_table,
                     ModuleContext const& module,
                     PDB::ImageSectionStream const& image_sections,
                     SeenSymbols& seen_symbols,
                     char const* name,
                     uint32_t type_index,
                     uint16_t section,
                     uint32_t offset) {
    // A CodeView data symbol gives us only name, type index, and section/offset.
    // This helper performs the shared filtering, address conversion, type
    // expansion, and VariantSymbol root creation for every stream that can
    // contain globals.
    if (name == nullptr || type_index == 0 || shouldSkipSymbolName(name)) {
        return;
    }

    MemoryAddress const address = sectionOffsetToAddress(module, image_sections, section, offset);
    if (address == 0) {
        return;
    }

    std::string symbol_name = module.symbol_prefix + name;
    if (!seen_symbols.emplace(address, symbol_name).second) {
        return;
    }

    auto symbol = std::make_unique<SymbolDescriptor>();
    symbol->name = std::move(symbol_name);
    symbol->address = address;
    if (!resolveType(type_table, type_index, *symbol)) {
        return;
    }

#if defined(DBGGUI_VERIFY_RAWPDB_WITH_DBGHELP)
    verifyRawPdbSymbolWithDbgHelp(dbgHelpModuleContext(module), name, *symbol, address);
#endif

    symbol_descriptors.push_back(std::move(symbol));
    root_symbols.push_back(std::make_unique<VariantSymbol>(
      root_symbols, symbol_descriptors.back().get()));
}

bool isDataRecord(DbiRecordKind kind) {
    return kind == DbiRecordKind::S_GDATA32
        || kind == DbiRecordKind::S_LDATA32;
}

bool isScopeStartRecord(DbiRecordKind kind) {
    return kind == DbiRecordKind::S_LPROC32
        || kind == DbiRecordKind::S_GPROC32
        || kind == DbiRecordKind::S_LPROC32_ID
        || kind == DbiRecordKind::S_GPROC32_ID
        || kind == DbiRecordKind::S_LPROC32_DPC
        || kind == DbiRecordKind::S_LPROC32_DPC_ID
        || kind == DbiRecordKind::S_BLOCK32
        || kind == DbiRecordKind::S_INLINESITE
        || kind == DbiRecordKind::S_INLINESITE2;
}

bool isScopeEndRecord(DbiRecordKind kind) {
    return kind == DbiRecordKind::S_END
        || kind == DbiRecordKind::S_PROC_ID_END
        || kind == DbiRecordKind::S_INLINESITE_END;
}

void storeFunctionAddress(std::unordered_map<MemoryAddress, std::string>& function_addresses,
                          ModuleContext const& module,
                          PDB::ImageSectionStream const& image_sections,
                          char const* name,
                          uint16_t section,
                          uint32_t offset) {
    if (name == nullptr || name[0] == '\0' || shouldSkipSymbolName(name)) {
        return;
    }

    MemoryAddress const address = sectionOffsetToAddress(module, image_sections, section, offset);
    if (address == 0) {
        return;
    }

    std::string function_name = module.symbol_prefix + name;
    size_t const paren = function_name.find('(');
    if (paren != std::string::npos) {
        function_name.resize(paren);
    }
    function_addresses.try_emplace(address, std::move(function_name));
}

void processGlobalSymbols(std::vector<std::unique_ptr<SymbolDescriptor>>& symbol_descriptors,
                          std::vector<std::unique_ptr<VariantSymbol>>& root_symbols,
                          PDB::RawFile const& raw_pdb,
                          PDB::DBIStream const& dbi_stream,
                          TypeTable const& type_table,
                          ModuleContext const& module,
                          PDB::ImageSectionStream const& image_sections,
                          SeenSymbols& seen_symbols,
                          PDB::CoalescedMSFStream const& symbol_records) {
    if (dbi_stream.HasValidGlobalSymbolStream(raw_pdb) != PDB::ErrorCode::Success) {
        return;
    }

    // The global stream is a hash accelerator: each hash record points into the
    // shared symbol record stream. Walking it is much faster than asking
    // DbgHelp to enumerate and materialize every symbol.
    PDB::GlobalSymbolStream global_symbols = dbi_stream.CreateGlobalSymbolStream(raw_pdb);
    for (PDB::HashRecord const& hash_record : global_symbols.GetRecords()) {
        DbiRecord const* record = global_symbols.GetRecord(symbol_records, hash_record);
        switch (record->header.kind) {
            case DbiRecordKind::S_GDATA32:
            case DbiRecordKind::S_LDATA32:
                storeDataSymbol(symbol_descriptors,
                                root_symbols,
                                type_table,
                                module,
                                image_sections,
                                seen_symbols,
                                record->data.S_GDATA32.name,
                                record->data.S_GDATA32.typeIndex,
                                record->data.S_GDATA32.section,
                                record->data.S_GDATA32.offset);
                break;
            default:
                break;
        }
    }
}

void processModuleDataSymbols(std::vector<std::unique_ptr<SymbolDescriptor>>& symbol_descriptors,
                              std::vector<std::unique_ptr<VariantSymbol>>& root_symbols,
                              PDB::RawFile const& raw_pdb,
                              PDB::DBIStream const& dbi_stream,
                              TypeTable const& type_table,
                              ModuleContext const& module,
                              PDB::ImageSectionStream const& image_sections,
                              SeenSymbols& seen_symbols) {
    // Some file-static data records are only present in per-compiland module
    // streams. Scan them as a fallback, but track scope depth so automatic locals
    // inside functions and blocks are not exposed as globals.
    PDB::ModuleInfoStream module_info_stream = dbi_stream.CreateModuleInfoStream(raw_pdb);
    for (PDB::ModuleInfoStream::Module const& pdb_module : module_info_stream.GetModules()) {
        if (!pdb_module.HasSymbolStream()) {
            continue;
        }

        size_t scope_depth = 0;
        PDB::ModuleSymbolStream module_symbols = pdb_module.CreateSymbolStream(raw_pdb);
        module_symbols.ForEachSymbol([&](DbiRecord const* record) {
            DbiRecordKind const kind = record->header.kind;

            if (isScopeEndRecord(kind)) {
                if (scope_depth > 0) {
                    --scope_depth;
                }
                return;
            }

            if (scope_depth == 0 && isDataRecord(kind)) {
                storeDataSymbol(symbol_descriptors,
                                root_symbols,
                                type_table,
                                module,
                                image_sections,
                                seen_symbols,
                                record->data.S_GDATA32.name,
                                record->data.S_GDATA32.typeIndex,
                                record->data.S_GDATA32.section,
                                record->data.S_GDATA32.offset);
            }

            if (isScopeStartRecord(kind)) {
                ++scope_depth;
            }
        });
    }
}

void processModuleFunctions(std::unordered_map<MemoryAddress, std::string>& function_addresses,
                            PDB::RawFile const& raw_pdb,
                            PDB::DBIStream const& dbi_stream,
                            ModuleContext const& module,
                            PDB::ImageSectionStream const& image_sections) {
    // Function addresses are loaded lazily for pointer snapshot support. Module
    // streams contain the typed procedure records with section/offset locations.
    PDB::ModuleInfoStream module_info_stream = dbi_stream.CreateModuleInfoStream(raw_pdb);
    for (PDB::ModuleInfoStream::Module const& pdb_module : module_info_stream.GetModules()) {
        if (!pdb_module.HasSymbolStream()) {
            continue;
        }

        PDB::ModuleSymbolStream module_symbols = pdb_module.CreateSymbolStream(raw_pdb);
        module_symbols.ForEachSymbol([&](DbiRecord const* record) {
            switch (record->header.kind) {
                case DbiRecordKind::S_LPROC32:
                case DbiRecordKind::S_GPROC32:
                case DbiRecordKind::S_LPROC32_ID:
                case DbiRecordKind::S_GPROC32_ID:
                    storeFunctionAddress(function_addresses,
                                         module,
                                         image_sections,
                                         record->data.S_LPROC32.name,
                                         record->data.S_LPROC32.section,
                                         record->data.S_LPROC32.offset);
                    break;
                default:
                    break;
            }
        });
    }
}

void processPublicFunctions(std::unordered_map<MemoryAddress, std::string>& function_addresses,
                            PDB::RawFile const& raw_pdb,
                            PDB::DBIStream const& dbi_stream,
                            ModuleContext const& module,
                            PDB::ImageSectionStream const& image_sections,
                            PDB::CoalescedMSFStream const& symbol_records) {
    if (dbi_stream.HasValidPublicSymbolStream(raw_pdb) != PDB::ErrorCode::Success) {
        return;
    }

    // Public symbols cover extra linker-visible names that may not have typed
    // procedure records. Keep undecorated C-style names useful for display.
    PDB::PublicSymbolStream public_symbols = dbi_stream.CreatePublicSymbolStream(raw_pdb);
    for (PDB::HashRecord const& hash_record : public_symbols.GetRecords()) {
        DbiRecord const* record = public_symbols.GetRecord(symbol_records, hash_record);
        if (record->header.kind != DbiRecordKind::S_PUB32) {
            continue;
        }
        char const* name = record->data.S_PUB32.name;
        if (name == nullptr || name[0] == '?') {
            continue;
        }
        storeFunctionAddress(function_addresses,
                             module,
                             image_sections,
                             name,
                             record->data.S_PUB32.section,
                             record->data.S_PUB32.offset);
    }
}

void processModulePdb(std::vector<std::unique_ptr<SymbolDescriptor>>& symbol_descriptors,
                      std::vector<std::unique_ptr<VariantSymbol>>& root_symbols,
                      ModuleContext const& module) {
    // Load the PDB streams needed for globals:
    // - DBI gives module lists, symbol streams, image sections, and hash streams.
    // - TPI gives the type records referenced by data symbols.
    // - The symbol record stream is the storage that global/public hash records
    //   point back into.
    std::optional<std::filesystem::path> pdb_path = findPdbPath(module);
    if (!pdb_path) {
        return;
    }

    MappedFile mapped_pdb(*pdb_path);
    if (!mapped_pdb.valid() || PDB::ValidateFile(mapped_pdb.data(), mapped_pdb.size()) != PDB::ErrorCode::Success) {
        return;
    }

    PDB::RawFile raw_pdb = PDB::CreateRawFile(mapped_pdb.data());
    if (PDB::HasValidDBIStream(raw_pdb) != PDB::ErrorCode::Success
        || PDB::HasValidTPIStream(raw_pdb) != PDB::ErrorCode::Success) {
        return;
    }

    PDB::DBIStream dbi_stream = PDB::CreateDBIStream(raw_pdb);
    if (dbi_stream.HasValidImageSectionStream(raw_pdb) != PDB::ErrorCode::Success
        || dbi_stream.HasValidSymbolRecordStream(raw_pdb) != PDB::ErrorCode::Success) {
        return;
    }

    PDB::ImageSectionStream image_sections = dbi_stream.CreateImageSectionStream(raw_pdb);
    PDB::CoalescedMSFStream symbol_records = dbi_stream.CreateSymbolRecordStream(raw_pdb);
    PDB::TPIStream tpi_stream = PDB::CreateTPIStream(raw_pdb);
    TypeTable type_table(tpi_stream);
    SeenSymbols seen_symbols;

    processGlobalSymbols(symbol_descriptors, root_symbols, raw_pdb, dbi_stream, type_table, module, image_sections, seen_symbols, symbol_records);
    processModuleDataSymbols(symbol_descriptors, root_symbols, raw_pdb, dbi_stream, type_table, module, image_sections, seen_symbols);
}

void processModuleFunctionAddresses(std::unordered_map<MemoryAddress, std::string>& function_addresses,
                                    ModuleContext const& module) {
    // This is a lighter pass than processModulePdb: it only needs DBI and image
    // sections because function lookup does not expand variable types.
    std::optional<std::filesystem::path> pdb_path = findPdbPath(module);
    if (!pdb_path) {
        return;
    }

    MappedFile mapped_pdb(*pdb_path);
    if (!mapped_pdb.valid() || PDB::ValidateFile(mapped_pdb.data(), mapped_pdb.size()) != PDB::ErrorCode::Success) {
        return;
    }

    PDB::RawFile raw_pdb = PDB::CreateRawFile(mapped_pdb.data());
    if (PDB::HasValidDBIStream(raw_pdb) != PDB::ErrorCode::Success) {
        return;
    }

    PDB::DBIStream dbi_stream = PDB::CreateDBIStream(raw_pdb);
    if (dbi_stream.HasValidImageSectionStream(raw_pdb) != PDB::ErrorCode::Success) {
        return;
    }

    PDB::ImageSectionStream image_sections = dbi_stream.CreateImageSectionStream(raw_pdb);
    processModuleFunctions(function_addresses, raw_pdb, dbi_stream, module, image_sections);
    if (dbi_stream.HasValidSymbolRecordStream(raw_pdb) == PDB::ErrorCode::Success) {
        PDB::CoalescedMSFStream symbol_records = dbi_stream.CreateSymbolRecordStream(raw_pdb);
        processPublicFunctions(function_addresses, raw_pdb, dbi_stream, module, image_sections, symbol_records);
    }
}

} // namespace

void printLastError() {
    DWORD error = GetLastError();
    if (error == 0) {
        return;
    }

    LPSTR message_buffer = nullptr;
    size_t const size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr,
                                       error,
                                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                       reinterpret_cast<LPSTR>(&message_buffer),
                                       0,
                                       nullptr);
    std::string message(message_buffer, size);
    LocalFree(message_buffer);
    std::cerr << "Symbol search error: " << message << std::endl;
}

ModuleInfo getCurrentModuleInfo() {
    HMODULE handle = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&getCurrentModuleInfo),
                            &handle)) {
        printLastError();
        std::abort();
    }

    MODULEINFO module_info{};
    if (!GetModuleInformation(GetCurrentProcess(), handle, &module_info, sizeof(module_info))) {
        printLastError();
        std::abort();
    }

    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(handle, path, sizeof(path)) == 0) {
        printLastError();
        std::abort();
    }

    return ModuleInfo{
      .base_address = reinterpret_cast<MemoryAddress>(handle),
      .size = module_info.SizeOfImage,
      .write_time = std::format("{:%Y-%m-%d %H-%M-%S}", std::filesystem::last_write_time(std::string(path))),
      .path = path};
}

std::string readFile(std::string const& filename) {
    return (std::stringstream() << std::ifstream(filename).rdbuf()).str();
}

std::unique_ptr<SymbolDescriptor> getSymbolFromAddress(MemoryAddress address) {
    std::string const resolved = DbgSymbols::getSymbols().resolveFunctionAddress(address);
    if (resolved.empty()) {
        return nullptr;
    }

    return std::make_unique<SymbolDescriptor>(SymbolDescriptor{
      .name = resolved,
      .address = address,
      .kind = SymbolKind::Function});
}

void DbgSymbols::initSymbolsFromPdb() {
    // Build the root symbol list from every loaded module that has a readable
    // PDB. Missing or invalid PDBs are skipped so one dependency without symbols
    // does not prevent the rest of the process from being inspected.
    for (ModuleContext const& module : loadedModules()) {
        processModulePdb(m_symbol_descriptors, m_root_symbols, module);
    }
}

std::string DbgSymbols::resolveFunctionAddress(MemoryAddress address) const {
    // Function names are only needed when a pointer value is being snapshotted,
    // so build this address map on first use instead of during startup.
    if (!m_function_addresses_loaded) {
        for (ModuleContext const& module : loadedModules()) {
            processModuleFunctionAddresses(m_function_addresses, module);
        }
        m_function_addresses_loaded = true;
    }

    auto it = m_function_addresses.find(address);
    if (it != m_function_addresses.end()) {
        return it->second;
    }
    return "";
}

std::vector<SymbolValue> DbgSymbols::saveSnapshotToMemory() const {
    std::vector<SymbolValue> snapshot;
    std::function<void(VariantSymbol*)> save_symbol_to_snapshot = [&](VariantSymbol* sym) {
        // Add symbol value to snapshot
        VariantSymbol::Type type = sym->getType();
        if (type == VariantSymbol::Type::Arithmetic || type == VariantSymbol::Type::Enum) {
            snapshot.push_back({sym, sym->read()});
        } else if (type == VariantSymbol::Type::Pointer) {
            MemoryAddress pointed_address = sym->getPointedAddress();
            VariantSymbol* pointed_symbol = sym->getPointedSymbol();
            // Set pointer only if it points to some other global
            if (pointed_address == NULL) {
                snapshot.push_back({sym, MemoryAddress(NULL)});
            } else if (pointed_symbol) {
                snapshot.push_back({sym, pointed_address});
            } else if (getSymbolFromAddress(pointed_address)) {
                snapshot.push_back({sym, pointed_address});
            }
        }

        for (auto const& child : sym->getChildren()) {
            save_symbol_to_snapshot(child.get());
        }
    };

    for (std::unique_ptr<VariantSymbol> const& sym : m_root_symbols) {
        save_symbol_to_snapshot(sym.get());
    }

    return snapshot;
}
