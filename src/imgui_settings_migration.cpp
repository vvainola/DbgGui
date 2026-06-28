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

uint32_t imguiHash(std::string_view data, bool include_triple_hash_marker) {
    constexpr uint32_t Seed = 0;
    const uint32_t reset_crc = ~Seed;
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

std::string hashHex(uint32_t hash) {
    return std::format("{:08X}", hash);
}

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return;
    }

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

void addWindowIdMigration(std::string_view line, std::unordered_map<std::string, std::string>& id_migrations) {
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
        id_migrations[hashHex(old_hash)] = hashHex(new_hash);
    }
}

std::unordered_map<std::string, std::string> findWindowIdMigrations(std::string const& layout) {
    std::unordered_map<std::string, std::string> id_migrations;
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
} // namespace

namespace imgui_settings {
bool migrateLayoutIniHashes(std::string& layout) {
#if IMGUI_VERSION_NUM < 19260
    (void)layout;
    return false;
#else
    bool changed = false;
    const std::array<std::string_view, 2> dock_window_id_prefixes = {
        "Window=0x",
        "Selected=0x",
    };

    for (auto const& [old_id, new_id] : findWindowIdMigrations(layout)) {
        for (std::string_view prefix : dock_window_id_prefixes) {
            const std::string old_token = std::format("{}{}", prefix, old_id);
            const std::string new_token = std::format("{}{}", prefix, new_id);
            if (layout.contains(old_token)) {
                replaceAll(layout, old_token, new_token);
                changed = true;
            }
        }
    }
    return changed;
#endif
}
} // namespace imgui_settings
