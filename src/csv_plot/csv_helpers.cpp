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

#include "csv_helpers.h"

#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <format>
#include <map>

template <typename T>
T inline min(T a, T b) {
    return a < b ? a : b;
}

template <typename T>
T inline max(T a, T b) {
    return a > b ? a : b;
}

std::vector<std::string_view> splitWhitespace(std::string const& s, int expected_column_count) {
    std::vector<std::string_view> elems;
    elems.reserve(expected_column_count);
    int32_t pos_start = 0;
    for (int i = 0; i < s.size(); ++i) {
        if (std::isspace(s[i])) {
            elems.push_back(std::string_view(&s[pos_start], &s[i]));
            pos_start = i + 1;
            // Skip any intermediate spaces
            while (i < s.size() && std::isspace(s[i])) {
                ++i;
                pos_start = i;
            }
        }
    }
    // Add the last value
    elems.push_back(std::string_view(&s[pos_start]));
    return elems;
}

// Opens PSCAD .inf file, reads the signal names, parses the .out files for data and creates single
// csv file with same basename. Returns true if csv file was created, false if something went wrong.
bool pscadInfToCsv(std::string const& inf_filename) {
    std::ifstream inf_file(inf_filename);
    if (!inf_file.is_open()) {
        std::cerr << "Unable to open file " + inf_filename << std::endl;
        return false;
    }

    // Collect signal names from the .inf file
    std::vector<std::string> signal_names;
    std::string line;
    while (std::getline(inf_file, line)) {
        std::string desc_tag = "Desc=\"";
        size_t desc_start_idx = line.find(desc_tag);
        if (desc_start_idx != std::string::npos) {
            desc_start_idx += desc_tag.length();
            size_t desc_end_idx = line.find("\"", desc_start_idx);
            if (desc_end_idx != std::string::npos) {
                std::string desc_content = line.substr(desc_start_idx, desc_end_idx - desc_start_idx);
                signal_names.push_back(desc_content);
            }
        }
    }

    // Open the .out files
    std::string inf_basename = inf_filename.substr(0, inf_filename.find_last_of("."));
    std::vector<std::ifstream> out_files;
    int out_file_cnt = int(ceil(signal_names.size() / 10.0));
    for (int i = 1; i < out_file_cnt + 1; ++i) {
        std::stringstream ss;
        ss << std::setw(2) << std::setfill('0') << i << ".out";
        std::string out_filename = inf_basename + "_" + ss.str();
        std::ifstream& out_file = out_files.emplace_back(out_filename);
        if (!out_file.is_open()) {
            std::cerr << "Unable to open file " + out_filename << std::endl;
            return false;
        }
        // Ignore first line that contains the project name
        std::getline(out_file, line);
    }

    // Open the .csv file for writing
    std::string csv_filename = inf_basename + ".csv";
    std::ofstream csv_file(csv_filename);
    if (!csv_file.is_open()) {
        std::cerr << "Unable to open file " + csv_filename << std::endl;
        return false;
    }

    // Write signal names to header row
    csv_file << "Time,";
    for (int i = 0; i < signal_names.size(); i++) {
        csv_file << signal_names[i];
        if (i < signal_names.size() - 1) {
            csv_file << ",";
        }
    }
    csv_file << ",\n";

    // Go through the .out files line by line to write out the data. Time column is included
    // only from the first file
    std::ifstream& out_file1 = out_files[0];
    while (std::getline(out_file1, line)) {
        str::trim(line);
        std::vector<std::string_view> values = splitWhitespace(line, 11);
        for (int i = 0; i < values.size(); ++i) {
            csv_file << values[i] << ",";
        }
        for (int i = 1; i < out_files.size(); ++i) {
            std::getline(out_files[i], line);
            str::trim(line);
            values = splitWhitespace(line, 11);
            for (int j = 1; j < values.size(); ++j) {
                csv_file << values[j] << ",";
            }
        }
        csv_file << "\n";
    }
    csv_file.close();
    return true;
}

std::string formatCsvColumns(std::vector<std::string> const& header,
                             std::vector<std::vector<double>> const& data) {
    std::stringstream csv;

    for (std::string const& signal_name : header) {
        csv << signal_name << ",";
    }
    csv << "\n";

    size_t row_count = 0;
    for (std::vector<double> const& column : data) {
        row_count = std::max(row_count, column.size());
    }
    for (size_t row = 0; row < row_count; ++row) {
        for (std::vector<double> const& column : data) {
            if (row < column.size()) {
                csv << std::format("{:g}", column[row]);
            }
            csv << ",";
        }
        csv << "\n";
    }

    return csv.str();
}

std::vector<std::string> makeUniqueCsvSignalNames(std::vector<std::string> const& signal_names) {
    std::map<std::string, int> signal_name_count;
    for (std::string const& signal_name : signal_names) {
        ++signal_name_count[signal_name];
    }

    std::vector<std::string> unique_names;
    unique_names.reserve(signal_names.size());
    std::map<std::string, int> signal_name_counter;
    for (std::string const& signal_name : signal_names) {
        if (signal_name_count[signal_name] > 1) {
            unique_names.push_back(std::format("{}#{}", signal_name, signal_name_counter[signal_name]));
            ++signal_name_counter[signal_name];
        } else {
            unique_names.push_back(signal_name);
        }
    }
    return unique_names;
}

DecimatedValues decimateValues(std::vector<double> const& x, std::vector<double> const& y, int count) {
    DecimatedValues decimated_values;
    if (x.empty() || y.empty() || count <= 0) {
        return decimated_values;
    }

    size_t sample_count = std::min(x.size(), y.size());
    decimated_values.x.reserve(count + 2);
    decimated_values.y_min.reserve(count + 2);
    decimated_values.y_max.reserve(count + 2);

    int32_t decimation = static_cast<int32_t>(std::max(std::floor(double(sample_count) / count) - 1, 0.0));

    double current_min = INFINITY;
    double current_max = -INFINITY;
    int64_t counter = 0;
    for (int32_t i = 0; i < sample_count; i++) {
        if (counter < 0) {
            decimated_values.x.push_back(x[i - 1]);
            decimated_values.y_min.push_back(current_min);
            decimated_values.y_max.push_back(current_max);

            current_min = INFINITY;
            current_max = -INFINITY;
            counter = decimation;
        }
        current_min = min(y[i], current_min);
        current_max = max(y[i], current_max);
        counter--;
    }
    // Add last value
    decimated_values.x.push_back(x[sample_count - 1]);
    decimated_values.y_min.push_back(y[sample_count - 1]);
    decimated_values.y_max.push_back(y[sample_count - 1]);
    return decimated_values;
}

double getPlotValueAtX(CsvPlotStyle plot_style,
                       std::span<double const> x,
                       std::span<double const> y,
                       double x_position,
                       bool interpolate_linear) {
    size_t sample_count = std::min(x.size(), y.size());
    if (sample_count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (sample_count == 1 || x_position <= x[0]) {
        return y[0];
    }
    if (x_position >= x[sample_count - 1]) {
        return y[sample_count - 1];
    }

    auto x_begin = x.begin();
    auto x_end = x.begin() + sample_count;
    if (plot_style == CsvPlotStyle::LeadingStairs) {
        auto it = std::lower_bound(x_begin, x_end, x_position);
        size_t idx = size_t(std::distance(x_begin, it));
        return y[std::min(idx, sample_count - 1)];
    }

    auto it = std::upper_bound(x_begin, x_end, x_position);
    size_t idx = size_t(std::distance(x_begin, it) - 1);
    if (plot_style == CsvPlotStyle::LaggingStairs || !interpolate_linear) {
        return y[idx];
    }

    size_t next_idx = std::min(idx + 1, sample_count - 1);
    double x0 = x[idx];
    double x1 = x[next_idx];
    if (next_idx == idx || x1 == x0) {
        return y[idx];
    }
    return std::lerp(y[idx], y[next_idx], (x_position - x0) / (x1 - x0));
}

void saveAsCsv(std::string const& filename,
               std::vector<std::string> const& header,
               std::vector<std::vector<double>> const& data) {
    std::ofstream csv_file(filename);
    if (!csv_file.is_open()) {
        std::cerr << "Unable to open file " + filename << std::endl;
        return;
    }

    csv_file << formatCsvColumns(header, data);

    csv_file.close();
}
