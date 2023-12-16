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

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string_view> splitSv(const std::string& s, char delim, int expected_column_count) {
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

void writeLineToCsv(std::ofstream& csv_file, std::string const& line, bool include_first_column) {
    std::istringstream iss(line);
    double signal_data;
    if (!include_first_column) {
        iss >> signal_data;
    }
    while (iss >> signal_data) {
        csv_file << signal_data << ",";
    }
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
        writeLineToCsv(csv_file, line, true);
        for (int i = 1; i < out_files.size(); ++i) {
            std::getline(out_files[i], line);
            writeLineToCsv(csv_file, line, false);
        }
        csv_file << "\n";
    }
    csv_file.close();
    return true;
}

std::string getLineFromEnd(std::ifstream& file, size_t line_count) {
    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();

    if (file_size <= 1) {
        file.seekg(0);
        return ""; // Return an empty string for empty or single-character files
    }

    // Go back in the line until correct amount of newlines found
    std::string line;
    size_t new_line_count = 0;
    for (std::streampos pos = file_size - std::streampos(1); pos >= 0; pos -= std::streampos(1)) {
        file.seekg(pos);
        char c = (char)file.get();

        if (c == '\n') {
            ++new_line_count;
            if (new_line_count == line_count) {
                break;
            }
        }
        line = c + line;
    }
    file.seekg(0);

    if (new_line_count < line_count) {
        return ""; // Return an empty string if there are not enough lines
    }

    return split(line, '\n')[0];
}
