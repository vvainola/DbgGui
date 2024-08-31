// MIT License
//
// Copyright (c) 2024 vvainola
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

#include "str_helpers.h"
#include <windows.h>
#include <format>
#include <sstream>
#include <iomanip>

namespace str {

std::expected<std::string, std::string> str::readFile(const std::string& filename) {
    HANDLE file_handle = CreateFileA(
      filename.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

    if (file_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(std::format("Error opening file: {}", filename));
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle, &file_size)) {
        CloseHandle(file_handle);
        return std::unexpected(std::format("Error getting file size: {}", filename));
    }

    HANDLE file_mapping = CreateFileMapping(file_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (file_mapping == nullptr) {
        CloseHandle(file_handle);
        return std::unexpected(std::format("Error creating file mapping: {}", filename));
    }

    char* file_contents = static_cast<char*>(MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, file_size.QuadPart));
    if (file_contents == nullptr) {
        CloseHandle(file_mapping);
        CloseHandle(file_handle);
        return std::unexpected(std::format("Error mapping file to memory: {}", filename));
    }

    std::string result(file_contents, file_size.QuadPart);

    if (!UnmapViewOfFile(file_contents)) {
        return std::unexpected(std::format("Error unmapping file: {}", filename));
    }

    CloseHandle(file_mapping);
    CloseHandle(file_handle);

    return result;
}

std::vector<std::string_view> str::splitSv(const std::string& s, char delim, int expected_column_count) {
    std::vector<std::string_view> elems;
    elems.reserve(expected_column_count);
    int32_t pos_start = 0;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            elems.push_back(std::string_view(&s[pos_start], &s[i]));
            pos_start = i + 1;
        }
    }
    // Add the last value if there is no trailing delimiter
    if (s.back() != delim) {
        elems.push_back(std::string_view(&s[pos_start]));
    }
    return elems;
}

std::vector<std::string> str::split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::string str::replaceAll(
  const std::string& str,    // where to work
  const std::string& find,   // substitute 'find'
  const std::string& replace // by 'replace'
) {
    using namespace std;
    string result;
    size_t find_len = find.size();
    size_t pos, from = 0;
    while (string::npos != (pos = str.find(find, from))) {
        result.append(str, from, pos - from);
        result.append(replace);
        from = pos + find_len;
    }
    result.append(str, from, string::npos);
    return result;
}

std::string& str::ltrim(std::string& str) {
    auto it2 = std::find_if(str.begin(), str.end(), [](char ch) { return !std::isspace<char>(ch, std::locale::classic()); });
    str.erase(str.begin(), it2);
    return str;
}

std::string& str::rtrim(std::string& str) {
    auto it1 = std::find_if(str.rbegin(), str.rend(), [](char ch) { return !std::isspace<char>(ch, std::locale::classic()); });
    str.erase(it1.base(), str.end());
    return str;
}

std::string& str::trim(std::string& str) {
    return ltrim(rtrim(str));
}


} // namespace str
