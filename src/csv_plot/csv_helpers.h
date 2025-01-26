#pragma once
// MIT License
//
// Copyright (c) 2023 vvainola
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
#include <vector>
#include <string>

#include "str_helpers.h"

std::vector<std::string_view> splitWhitespace(std::string const& s, int expected_column_count = 1);

// Opens PSCAD .inf file, reads the signal names, parses the .out files for data and creates single
// csv file with same basename. Returns true if csv file was created, false if something went wrong.
bool pscadInfToCsv(std::string const& inf_filename);

template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

template <typename T>
inline bool contains(std::vector<T>& v, const T& item_to_search) {
    for (auto const& item : v) {
        if (item == item_to_search) {
            return true;
        }
    }
    return false;
}

struct DecimatedValues {
    std::vector<double> x;
    std::vector<double> y_min;
    std::vector<double> y_max;
};
DecimatedValues decimateValues(std::vector<double> const& x, std::vector<double> const& y, int count);

void saveAsCsv(std::string const& filename,
               std::vector<std::string> const& header,
               std::vector<std::vector<double>> const& data);
