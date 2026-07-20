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

#include "imgui_settings_migration.h"

#include "imgui/imgui.h"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
constexpr uint32_t LegacyCrc32Polynomial = 0xEDB88320u;
constexpr uint32_t Crc32cPolynomial = 0x82F63B78u;

uint32_t crcPolynomial() {
#ifdef IMGUI_USE_LEGACY_CRC32_ADLER
    return LegacyCrc32Polynomial;
#else
    return Crc32cPolynomial;
#endif
}

uint32_t crcUpdate(uint32_t crc, unsigned char c) {
    crc ^= c;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 1u) ? (crc >> 1u) ^ crcPolynomial() : crc >> 1u;
    }
    return crc;
}

uint32_t imguiHash(std::string_view data, bool include_triple_hash_marker, uint32_t seed = 0) {
    const uint32_t reset_crc = ~seed;
    uint32_t crc = reset_crc;
    for (size_t i = 0; i < data.size();) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c == '#'
            && i + 2 < data.size()
            && data[i + 1] == '#'
            && data[i + 2] == '#') {
            crc = reset_crc;
            if (!include_triple_hash_marker) {
                i += 3;
                continue;
            }
        }
        crc = crcUpdate(crc, c);
        ++i;
    }
    return ~crc;
}

bool isDecimalId(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

std::string hashHex(uint32_t hash) {
    return std::format("{:08X}", hash);
}

bool replaceIdsAfterPrefix(std::string& text, std::string_view prefix, std::unordered_map<std::string, std::string> const& id_migrations) {
    bool changed = false;
    std::string migrated;
    migrated.reserve(text.size());
    size_t pos = 0;

    while (true) {
        size_t prefix_pos = text.find(prefix, pos);
        if (prefix_pos == std::string::npos) {
            migrated.append(text, pos, std::string::npos);
            break;
        }

        const size_t id_start = prefix_pos + prefix.size();
        if (id_start + 8 > text.size()) {
            migrated.append(text, pos, std::string::npos);
            break;
        }

        migrated.append(text, pos, id_start - pos);
        const std::string old_id = text.substr(id_start, 8);
        if (auto new_id = id_migrations.find(old_id); new_id != id_migrations.end()) {
            migrated.append(new_id->second);
            changed = true;
        } else {
            migrated.append(old_id);
        }
        pos = id_start + 8;
    }

    if (changed) {
        text = std::move(migrated);
    }
    return changed;
}

template <size_t N>
bool replaceIdsAfterPrefixes(std::string& text,
                             std::array<std::string_view, N> prefixes,
                             std::unordered_map<std::string, std::string> const& id_migrations) {
    bool changed = false;
    for (std::string_view prefix : prefixes) {
        changed |= replaceIdsAfterPrefix(text, prefix, id_migrations);
    }
    return changed;
}

struct LayoutIdMigrations {
    std::unordered_map<std::string, std::string> window_ids;
    std::unordered_map<std::string, std::string> dockspace_ids;
};

void addWindowIdMigration(std::string_view line, LayoutIdMigrations& id_migrations) {
    constexpr std::string_view WindowSectionPrefix = "[Window][";
    if (!line.starts_with(WindowSectionPrefix) || !line.ends_with("]")) {
        return;
    }

    std::string_view window_name = line.substr(WindowSectionPrefix.size(), line.size() - WindowSectionPrefix.size() - 1);
    if (!window_name.contains("###")) {
        return;
    }

    const uint32_t old_hash = imguiHash(window_name, true);
    const uint32_t new_hash = imguiHash(window_name, false);
    if (old_hash != new_hash) {
        id_migrations.window_ids[hashHex(old_hash)] = hashHex(new_hash);
    }

    const size_t stable_id_pos = window_name.rfind("###");
    std::string_view stable_id = window_name.substr(stable_id_pos + 3);
    if (!isDecimalId(stable_id)) {
        return;
    }

    const std::string dockspace_id = std::format("Dockspace_{}", stable_id);
    const uint32_t old_dockspace_hash = imguiHash(dockspace_id, true, old_hash);
    const uint32_t new_dockspace_hash = imguiHash(dockspace_id, true, new_hash);
    if (old_dockspace_hash != new_dockspace_hash) {
        id_migrations.dockspace_ids[hashHex(old_dockspace_hash)] = hashHex(new_dockspace_hash);
    }
}

LayoutIdMigrations findIdMigrations(std::string const& layout) {
    LayoutIdMigrations id_migrations;
    for (size_t line_start = 0; line_start < layout.size();) {
        size_t line_end = layout.find_first_of("\r\n", line_start);
        if (line_end == std::string::npos) {
            line_end = layout.size();
        }

        std::string_view line(layout.data() + line_start, line_end - line_start);
        addWindowIdMigration(line, id_migrations);

        if (line_end == layout.size()) {
            break;
        }
        line_start = line_end + 1;
    }
    return id_migrations;
}

bool replaceWindowIds(std::string& layout, std::unordered_map<std::string, std::string> const& id_migrations) {
    const std::array<std::string_view, 2> window_id_prefixes = {
        "Window=0x",
        "Selected=0x",
    };
    return replaceIdsAfterPrefixes(layout, window_id_prefixes, id_migrations);
}

bool replaceDockspaceIds(std::string& layout, std::unordered_map<std::string, std::string> const& id_migrations) {
    const std::array<std::string_view, 3> dockspace_id_prefixes = {
        "ID=0x",
        "Parent=0x",
        "DockId=0x",
    };
    return replaceIdsAfterPrefixes(layout, dockspace_id_prefixes, id_migrations);
}
} // namespace

namespace imgui_settings {
bool migrateLayoutIniHashes(std::string& layout) {
#if IMGUI_VERSION_NUM < 19260
    (void)layout;
    return false;
#else
    bool changed = false;
    const LayoutIdMigrations id_migrations = findIdMigrations(layout);
    changed |= replaceWindowIds(layout, id_migrations.window_ids);
    changed |= replaceDockspaceIds(layout, id_migrations.dockspace_ids);
    return changed;
#endif
}
} // namespace imgui_settings
