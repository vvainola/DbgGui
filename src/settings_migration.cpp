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

#include "settings_migration.h"
#include "data_structures.h"

#include <map>
#include <stdexcept>

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

namespace settings_migration {
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
} // namespace

namespace settings_migration {
// Version 0 used both signal-ID arrays and name-to-ID maps. Version 1 always
// uses maps for scalar placements; names are optional labels, IDs are identity.
void normalizeSignals(nlohmann::json& owner) {
    if (!owner.contains("signals") || !owner["signals"].is_array()) {
        return;
    }
    auto signals = nlohmann::json::object();
    for (auto const& id : owner["signals"]) {
        if (id.is_number_unsigned() || id.is_number_integer()) {
            signals[std::to_string(id.get<uint64_t>())] = id;
        }
    }
    owner["signals"] = std::move(signals);
}

// Migrate JSON before registering signals or constructing windows. No live
// Vector2D objects or symbol lookup are needed, so absent symbols stay restorable.
bool migrate(nlohmann::json& settings) {
    int const version = settings.value("format_version", 0);
    if (version > FORMAT_VERSION || version < 0) {
        throw std::runtime_error("Unsupported settings format version " + std::to_string(version));
    }
    if (version == FORMAT_VERSION) {
        return false;
    }

    // Old vector IDs were hashes of either the X name or "X+Y" and the group.
    // Their saved definitions provide both component names and their scalar IDs.
    std::map<uint64_t, nlohmann::json> vector_components;
    for (auto const& definition : settings["vector_symbols"]) {
        if (!definition.is_object()
            || !definition.contains("x") || !definition["x"].is_string()
            || !definition.contains("y") || !definition["y"].is_string()
            || (definition.contains("group") && !definition["group"].is_string())
            || (definition.contains("name") && !definition["name"].is_string())) {
            continue;
        }
        std::string const x = definition.at("x");
        std::string const y = definition.at("y");
        std::string const group = definition.value("group", std::string{"debug"});
        auto pair = nlohmann::json::array({signalId(x, group), signalId(y, group)});
        vector_components[signalId(x, group)] = pair;
        vector_components[signalId(x + "+" + y, group)] = pair;
        vector_components[signalId(definition.value("name", x), group)] = pair;
        for (auto const& name : {x, y}) {
            std::string const key = name + " (" + group + ")";
            if (!settings["scalar_symbols"].contains(key)) {
                settings["scalar_symbols"][key] = {{"name", name}, {"group", group}};
            }
        }
    }

    auto& plots = settings["scalar_plots"];
    if (plots.is_null()) {
        plots = nlohmann::json::object();
    }
    for (auto& plot : plots) {
        if (!plot.contains("subplots") || !plot["subplots"].is_array()) {
            nlohmann::json subplot = nlohmann::json::object();
            for (auto key : {"signals", "autofit_y", "y_min", "y_max"}) {
                if (plot.contains(key)) {
                    subplot[key] = plot[key];
                }
            }
            plot["subplots"] = nlohmann::json::array({std::move(subplot)});
        }
        plot.erase("signals");
        for (auto& subplot : plot["subplots"]) {
            normalizeSignals(subplot);
        }
    }

    auto add_plot = [&](nlohmann::json plot) {
        std::string const key = std::to_string(plot.at("id").get<uint64_t>());
        if (plots.contains(key)) {
            throw std::runtime_error("Cannot migrate duplicate plot ID " + key);
        }
        plots[key] = std::move(plot);
    };
    for (auto plot : settings["vector_plots"]) {
        auto pairs = nlohmann::json::array();
        for (auto const& id : plot["signals"]) {
            if ((!id.is_number_unsigned() && !id.is_number_integer())
                || (id.is_number_integer() && !id.is_number_unsigned() && id.get<int64_t>() < 0)) {
                continue;
            }
            auto const found = vector_components.find(id.get<uint64_t>());
            if (found == vector_components.end()) {
                // A vector ID alone cannot identify both components. Skip this
                // placement without preventing other settings from loading.
                continue;
            }
            if (std::find(pairs.begin(), pairs.end(), found->second) == pairs.end()) {
                pairs.push_back(found->second);
            }
        }
        plot["vector_time_range"] = plot.value("time_range", 0.020);
        plot["rows"] = 1;
        plot["cols"] = 1;
        plot["subplots"] = nlohmann::json::array({{{"type", PlotType::Vector}, {"autofit_y", false}, {"vectors", std::move(pairs)}}});
        plot.erase("signals");
        plot.erase("time_range");
        add_plot(std::move(plot));
    }
    for (auto plot : settings["spec_plots"]) {
        nlohmann::json subplot = {
          {"type", PlotType::Spectrum}, {"autofit_y", false}, {"spectrums", plot.value("signals", nlohmann::json::array())}, {"spectrum_x_min", plot.value("x_axis_min", -1000.0)}, {"spectrum_x_max", plot.value("x_axis_max", 1000.0)}, {"spectrum_y_min", plot.value("y_axis_min", -0.1)}, {"spectrum_y_max", plot.value("y_axis_max", 1.1)}};
        plot["spectrum_time_range"] = plot.value("time_range", 1.0);
        plot["spectrum_window"] = plot.value("window", SpectrumWindow::None);
        plot["spectrum_logarithmic_y_axis"] = plot.value("logarithmic_y_axis", false);
        plot["rows"] = 1;
        plot["cols"] = 1;
        plot["subplots"] = nlohmann::json::array({std::move(subplot)});
        for (auto key : {"signals", "time_range", "window", "logarithmic_y_axis", "x_axis_min", "x_axis_max", "y_axis_min", "y_axis_max"}) {
            plot.erase(key);
        }
        add_plot(std::move(plot));
    }
    for (auto& window : settings["custom_windows"]) {
        normalizeSignals(window);
    }
    for (auto& script : settings["script_windows"]) {
        if (!script.contains("loop_count")) {
            script["loop_count"] = script.value("loop", false) ? 0 : 1;
        }
        script.erase("loop");
    }
    if (settings.contains("layout")) {
        auto layout = settings["layout"].get<std::string>();
        migrateLayoutIniHashes(layout);
        settings["layout"] = std::move(layout);
    }
    for (auto key : {"vector_plots", "vector_symbols", "spec_plots"}) {
        settings.erase(key);
    }
    settings["format_version"] = FORMAT_VERSION;
    return true;
}
} // namespace settings_migration
