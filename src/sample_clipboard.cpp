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

#include "sample_clipboard.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

inline constexpr std::string_view MAGIC = "DBGGUI_SAMPLES_V1";
inline constexpr std::string_view CLIPBOARD_PREFIX = "DBGGUI_SAMPLES_FILE_V1:";
inline constexpr std::string_view TEMP_FILE_PREFIX = "dbggui_samples_";
inline constexpr std::string_view TEMP_FILE_SUFFIX = ".bin";
inline constexpr std::chrono::hours TEMP_FILE_MAX_AGE = std::chrono::hours(24);
std::optional<std::filesystem::path> g_previous_temp_file;

void appendUint32(std::vector<uint8_t>& data, uint32_t value) {
    for (int byte_idx = 0; byte_idx < 4; ++byte_idx) {
        data.push_back(uint8_t((value >> (8 * byte_idx)) & 0xff));
    }
}

void appendUint64(std::vector<uint8_t>& data, uint64_t value) {
    for (int byte_idx = 0; byte_idx < 8; ++byte_idx) {
        data.push_back(uint8_t((value >> (8 * byte_idx)) & 0xff));
    }
}

void appendBytes(std::vector<uint8_t>& data, void const* bytes, size_t byte_count) {
    size_t old_size = data.size();
    data.resize(old_size + byte_count);
    std::memcpy(data.data() + old_size, bytes, byte_count);
}

std::expected<uint32_t, std::string> readUint32(std::span<uint8_t const> data, size_t& offset) {
    if (offset + 4 > data.size()) {
        return std::unexpected("Sample clipboard payload ended unexpectedly");
    }
    uint32_t value = 0;
    for (int byte_idx = 0; byte_idx < 4; ++byte_idx) {
        value |= uint32_t(data[offset + byte_idx]) << (8 * byte_idx);
    }
    offset += 4;
    return value;
}

std::expected<uint64_t, std::string> readUint64(std::span<uint8_t const> data, size_t& offset) {
    if (offset + 8 > data.size()) {
        return std::unexpected("Sample clipboard payload ended unexpectedly");
    }
    uint64_t value = 0;
    for (int byte_idx = 0; byte_idx < 8; ++byte_idx) {
        value |= uint64_t(data[offset + byte_idx]) << (8 * byte_idx);
    }
    offset += 8;
    return value;
}

bool canEncodeSamples(SampleClipboardData const& samples) {
    if (samples.header.size() != samples.data.size() || samples.data.empty() || samples.data[0].empty()) {
        return false;
    }

    size_t row_count = samples.data[0].size();
    return std::ranges::all_of(samples.data, [row_count](std::vector<double> const& column) {
        return column.size() == row_count;
    });
}

std::vector<uint8_t> encodeSamples(SampleClipboardData const& samples) {
    std::vector<uint8_t> payload;
    size_t row_count = samples.data[0].size();

    size_t payload_size = MAGIC.size() + 4 + 8;
    for (std::string const& name : samples.header) {
        payload_size += 4 + name.size();
    }
    payload_size += samples.data.size() * row_count * sizeof(double);
    payload.reserve(payload_size);

    payload.insert(payload.end(), MAGIC.begin(), MAGIC.end());
    appendUint32(payload, uint32_t(samples.header.size()));
    appendUint64(payload, uint64_t(row_count));
    for (std::string const& name : samples.header) {
        appendUint32(payload, uint32_t(name.size()));
        payload.insert(payload.end(), name.begin(), name.end());
    }

    // Clipboard bytes cross a process boundary, so the vector objects themselves
    // are not copied. Each rectangular sample column is copied as one raw double
    // block after the length-prefixed metadata.
    for (std::vector<double> const& column : samples.data) {
        appendBytes(payload, column.data(), column.size() * sizeof(double));
    }

    return payload;
}

std::expected<SampleClipboardData, std::string> decodeSamples(std::span<uint8_t const> payload) {
    if (payload.size() < MAGIC.size()) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }
    if (std::memcmp(payload.data(), MAGIC.data(), MAGIC.size()) != 0) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }

    size_t offset = MAGIC.size();
    std::expected<uint32_t, std::string> column_count_result = readUint32(payload, offset);
    if (!column_count_result.has_value()) {
        return std::unexpected(column_count_result.error());
    }
    std::expected<uint64_t, std::string> row_count_result = readUint64(payload, offset);
    if (!row_count_result.has_value()) {
        return std::unexpected(row_count_result.error());
    }

    uint32_t column_count = column_count_result.value();
    uint64_t row_count = row_count_result.value();
    SampleClipboardData samples;
    samples.header.reserve(column_count);
    samples.data.resize(column_count);

    if (row_count > uint64_t(std::numeric_limits<size_t>::max() / sizeof(double))) {
        return std::unexpected("Sample clipboard payload is too large");
    }
    size_t column_byte_count = size_t(row_count) * sizeof(double);

    for (uint32_t column_idx = 0; column_idx < column_count; ++column_idx) {
        std::expected<uint32_t, std::string> name_size_result = readUint32(payload, offset);
        if (!name_size_result.has_value()) {
            return std::unexpected(name_size_result.error());
        }
        uint32_t name_size = name_size_result.value();
        if (offset + name_size > payload.size()) {
            return std::unexpected("Sample clipboard signal name ended unexpectedly");
        }
        samples.header.emplace_back(reinterpret_cast<char const*>(payload.data() + offset), name_size);
        offset += name_size;
    }

    for (std::vector<double>& column : samples.data) {
        if (offset + column_byte_count > payload.size()) {
            return std::unexpected("Sample clipboard column ended unexpectedly");
        }
        column.resize(size_t(row_count));
        std::memcpy(column.data(), payload.data() + offset, column_byte_count);
        offset += column_byte_count;
    }

    if (offset != payload.size()) {
        return std::unexpected("Sample clipboard payload has trailing data");
    }
    return samples;
}

std::filesystem::path createTempFilePath() {
    std::random_device random;
    std::uniform_int_distribution<uint64_t> distribution;
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
    for (;;) {
        std::filesystem::path path =
          temp_dir / (std::string(TEMP_FILE_PREFIX) + std::to_string(distribution(random)) + std::string(TEMP_FILE_SUFFIX));
        if (!std::filesystem::exists(path)) {
            return path;
        }
    }
}

void pruneStaleClipboardFiles() {
    std::error_code error;
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path(error);
    if (error) {
        return;
    }

    auto const now = std::filesystem::file_time_type::clock::now();
    std::filesystem::directory_iterator entry(temp_dir, std::filesystem::directory_options::skip_permission_denied, error);
    std::filesystem::directory_iterator end;
    while (!error && entry != end) {
        std::filesystem::path path = entry->path();
        std::string filename = path.filename().string();
        if (filename.starts_with(TEMP_FILE_PREFIX) && filename.ends_with(TEMP_FILE_SUFFIX) &&
            entry->is_regular_file(error)) {
            std::filesystem::file_time_type write_time = entry->last_write_time(error);
            if (!error && now - write_time > TEMP_FILE_MAX_AGE) {
                std::filesystem::remove(path, error);
            }
        }
        error.clear();
        entry.increment(error);
    }
}

std::optional<std::filesystem::path> clipboardTempFilePath() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return std::nullopt;
    }
    char const* clipboard_text = ImGui::GetClipboardText();
    if (clipboard_text == nullptr) {
        return std::nullopt;
    }
    std::string_view text(clipboard_text);
    if (!text.starts_with(CLIPBOARD_PREFIX)) {
        return std::nullopt;
    }
    std::string_view path_bytes = text.substr(CLIPBOARD_PREFIX.size());
    if (path_bytes.empty()) {
        return std::nullopt;
    }
    std::u8string path_text(reinterpret_cast<char8_t const*>(path_bytes.data()), path_bytes.size());
    std::filesystem::path path(path_text);
    std::filesystem::path filename = path.filename();
    std::string filename_text = filename.string();
    if (!path.is_absolute() || path.parent_path().lexically_normal() != std::filesystem::temp_directory_path().lexically_normal() ||
        !filename_text.starts_with(TEMP_FILE_PREFIX) || !filename_text.ends_with(TEMP_FILE_SUFFIX)) {
        return std::nullopt;
    }
    return path;
}

bool writeClipboardFile(std::span<uint8_t const> payload) {
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    std::filesystem::path path = createTempFilePath();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.write(reinterpret_cast<char const*>(payload.data()), std::streamsize(payload.size()))) {
        file.close();
        std::error_code error;
        std::filesystem::remove(path, error);
        return false;
    }
    file.close();

    std::u8string path_text = path.u8string();
    std::string clipboard_text(CLIPBOARD_PREFIX);
    clipboard_text.append(reinterpret_cast<char const*>(path_text.data()), path_text.size());
    ImGui::SetClipboardText(clipboard_text.c_str());

    if (g_previous_temp_file) {
        std::error_code error;
        std::filesystem::remove(*g_previous_temp_file, error);
    }
    g_previous_temp_file = path;
    return true;
}

std::expected<SampleClipboardData, std::string> readClipboardFile() {
    std::optional<std::filesystem::path> path = clipboardTempFilePath();
    if (!path) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }
    std::error_code error;
    uintmax_t file_size = std::filesystem::file_size(*path, error);
    if (error || file_size > uintmax_t(std::numeric_limits<size_t>::max())) {
        return std::unexpected("Could not read DbgGui sample clipboard file");
    }
    std::vector<uint8_t> payload(static_cast<size_t>(file_size));
    std::ifstream file(*path, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(payload.data()), std::streamsize(payload.size()))) {
        return std::unexpected("Could not read DbgGui sample clipboard file");
    }
    std::expected<SampleClipboardData, std::string> samples = decodeSamples(payload);
    if (samples) {
        std::filesystem::remove(*path, error);
        if (g_previous_temp_file == path) {
            g_previous_temp_file.reset();
        }
    }
    return samples;
}

} // namespace

bool copySamplesToClipboard(SampleClipboardData const& samples) {
    pruneStaleClipboardFiles();
    if (!canEncodeSamples(samples)) {
        return false;
    }
    return writeClipboardFile(encodeSamples(samples));
}

bool hasSampleClipboardData() {
    pruneStaleClipboardFiles();
    std::optional<std::filesystem::path> path = clipboardTempFilePath();
    return path && std::filesystem::is_regular_file(*path);
}

std::expected<SampleClipboardData, std::string> readSamplesFromClipboard() {
    pruneStaleClipboardFiles();
    return readClipboardFile();
}
