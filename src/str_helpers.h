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

#include <string>
#include <expected>
#include <vector>

namespace str {

std::expected<std::string, std::string> readFile(const std::string& filename);

std::vector<std::string_view> splitSv(const std::string& s, char delim, int expected_column_count = 1);
std::vector<std::string> split(const std::string& s, char delim);
std::string replaceAll(const std::string& str,
                       const std::string& find,
                       const std::string& replace);
std::string removeWhitespace(std::string_view str);

std::string& ltrim(std::string& str);
std::string& rtrim(std::string& str);
std::string& trim(std::string& str);

std::expected<double, std::string> evaluateExpression(std::string expression);

} // namespace str
