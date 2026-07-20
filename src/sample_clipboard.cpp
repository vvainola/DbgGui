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

#if WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif LINUX
#include <gtk/gtk.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace {

inline constexpr std::string_view Magic = "DBGGUI_SAMPLES_V1";

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

    size_t payload_size = Magic.size() + 4 + 8;
    for (std::string const& name : samples.header) {
        payload_size += 4 + name.size();
    }
    payload_size += samples.data.size() * row_count * sizeof(double);
    payload.reserve(payload_size);

    payload.insert(payload.end(), Magic.begin(), Magic.end());
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
    if (payload.size() < Magic.size()) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }
    if (std::memcmp(payload.data(), Magic.data(), Magic.size()) != 0) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }

    size_t offset = Magic.size();
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

#if WINDOWS

UINT sampleClipboardFormat() {
    static UINT format = RegisterClipboardFormatA("DbgGui Samples V1");
    return format;
}

bool writeNativeClipboard(std::span<uint8_t const> payload) {
    UINT format = sampleClipboardFormat();
    if (format == 0) {
        return false;
    }

    HGLOBAL clipboard_handle = GlobalAlloc(GMEM_MOVEABLE, payload.size());
    if (clipboard_handle == nullptr) {
        return false;
    }

    void* clipboard_memory = GlobalLock(clipboard_handle);
    if (clipboard_memory == nullptr) {
        GlobalFree(clipboard_handle);
        return false;
    }
    std::memcpy(clipboard_memory, payload.data(), payload.size());
    GlobalUnlock(clipboard_handle);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(clipboard_handle);
        return false;
    }

    EmptyClipboard();
    if (SetClipboardData(format, clipboard_handle) == nullptr) {
        GlobalFree(clipboard_handle);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool hasNativeSampleClipboardData() {
    UINT format = sampleClipboardFormat();
    return format != 0 && IsClipboardFormatAvailable(format);
}

std::expected<SampleClipboardData, std::string> readNativeClipboard() {
    UINT format = sampleClipboardFormat();
    if (format == 0 || !IsClipboardFormatAvailable(format)) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }
    if (!OpenClipboard(nullptr)) {
        return std::unexpected("Could not open clipboard");
    }

    HANDLE clipboard_handle = GetClipboardData(format);
    if (clipboard_handle == nullptr) {
        CloseClipboard();
        return std::unexpected("Could not read DbgGui sample clipboard data");
    }

    void* clipboard_memory = GlobalLock(clipboard_handle);
    if (clipboard_memory == nullptr) {
        CloseClipboard();
        return std::unexpected("Could not lock DbgGui sample clipboard data");
    }

    size_t clipboard_size = GlobalSize(clipboard_handle);
    auto payload = std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(clipboard_memory), clipboard_size);
    std::expected<SampleClipboardData, std::string> samples = decodeSamples(payload);
    GlobalUnlock(clipboard_handle);
    CloseClipboard();
    return samples;
}

#elif LINUX

inline constexpr char GtkClipboardTarget[] = "application/x-dbggui-samples-v1";
std::vector<uint8_t> g_clipboard_payload;

bool ensureGtkClipboard() {
    static bool initialized = gtk_init_check(nullptr, nullptr);
    return initialized;
}

GdkAtom sampleClipboardAtom() {
    return gdk_atom_intern_static_string(GtkClipboardTarget);
}

void getGtkClipboardData(GtkClipboard*, GtkSelectionData* selection_data, guint, gpointer) {
    if (g_clipboard_payload.empty()) {
        return;
    }
    gtk_selection_data_set(selection_data,
                           sampleClipboardAtom(),
                           8,
                           reinterpret_cast<guchar const*>(g_clipboard_payload.data()),
                           int(g_clipboard_payload.size()));
}

void clearGtkClipboardData(GtkClipboard*, gpointer) {
    g_clipboard_payload.clear();
}

bool writeNativeClipboard(std::span<uint8_t const> payload) {
    if (!ensureGtkClipboard()) {
        return false;
    }

    g_clipboard_payload.assign(payload.begin(), payload.end());
    GtkTargetEntry target{
      const_cast<char*>(GtkClipboardTarget),
      0,
      0,
    };
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (!gtk_clipboard_set_with_data(clipboard, &target, 1, getGtkClipboardData, clearGtkClipboardData, nullptr)) {
        g_clipboard_payload.clear();
        return false;
    }
    gtk_clipboard_store(clipboard);
    return true;
}

bool hasNativeSampleClipboardData() {
    if (!ensureGtkClipboard()) {
        return false;
    }
    return gtk_clipboard_wait_is_target_available(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), sampleClipboardAtom());
}

std::expected<SampleClipboardData, std::string> readNativeClipboard() {
    if (!ensureGtkClipboard()) {
        return std::unexpected("Could not initialize GTK clipboard");
    }

    GtkSelectionData* selection_data =
      gtk_clipboard_wait_for_contents(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), sampleClipboardAtom());
    if (selection_data == nullptr) {
        return std::unexpected("Clipboard does not contain DbgGui samples");
    }

    gint length = gtk_selection_data_get_length(selection_data);
    guchar const* data = gtk_selection_data_get_data(selection_data);
    if (length <= 0 || data == nullptr) {
        gtk_selection_data_free(selection_data);
        return std::unexpected("Could not read DbgGui sample clipboard data");
    }

    auto payload = std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(data), size_t(length));
    std::expected<SampleClipboardData, std::string> samples = decodeSamples(payload);
    gtk_selection_data_free(selection_data);
    return samples;
}

#else

bool writeNativeClipboard(std::span<uint8_t const>) {
    return false;
}

bool hasNativeSampleClipboardData() {
    return false;
}

std::expected<SampleClipboardData, std::string> readNativeClipboard() {
    return std::unexpected("Binary sample clipboard is not supported on this platform");
}

#endif

} // namespace

bool copySamplesToClipboard(SampleClipboardData const& samples) {
    if (!canEncodeSamples(samples)) {
        return false;
    }
    return writeNativeClipboard(encodeSamples(samples));
}

bool hasSampleClipboardData() {
    return hasNativeSampleClipboardData();
}

std::expected<SampleClipboardData, std::string> readSamplesFromClipboard() {
    return readNativeClipboard();
}
