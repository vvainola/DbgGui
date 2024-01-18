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

#include <iostream>
#include "csvplot.h"
#include "version.h"
#include <cxxopts.hpp>

int main(int argc, char** argv) {
    std::string csv_file;

    cxxopts::Options options(std::format("CSV Plotter version {}", GIT_COMMIT));
    // clang-format off
    options.add_options()
        ("f,files" , "Files to open for plotting"                                                            , cxxopts::value<std::vector<std::string>>()->default_value("-"))
        ("n,names" , "Names of signals to add to plots e.g. \"foo,bar\""                                     , cxxopts::value<std::vector<std::string>>()->default_value("-"))
        ("p,plots" , "Indices of plots to add signals matching order of arguments in \"names\" e.g. \"0,1\"" , cxxopts::value<std::vector<int>>()->default_value("-1"))
        ("xlim"    , "X-axis limits e.g. \"1.0,1.5\""                                                        , cxxopts::value<std::vector<double>>()->default_value("0,1"))
        ("image"   , "Save plot as png image to given path and exit."                                        , cxxopts::value<std::string>()->default_value(""))
        ("h,help"  , "Show help and exit")
        ;
    // clang-format on
    cxxopts::ParseResult parsed_options;
    std::vector<std::string> files;
    std::vector<std::string> names;
    std::vector<int> plots;
    std::vector<double> xlimits;
    std::string image_filepath;
    try {
        parsed_options = options.parse(argc, argv);
        files = parsed_options["files"].as<std::vector<std::string>>();
        names = parsed_options["names"].as<std::vector<std::string>>();
        plots = parsed_options["plots"].as<std::vector<int>>();
        xlimits = parsed_options["xlim"].as<std::vector<double>>();
        image_filepath = parsed_options["image"].as<std::string>();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        std::abort();
    }

    if (parsed_options.count("help")) {
        std::cout << options.help() << std::endl;
        std::abort();
    }
    // cxxopts does not seem to support empty vector as default so use these magic constants instead
    if (files == std::vector<std::string>{"-"}) {
        files.clear();
    }
    if (names == std::vector<std::string>{"-"}) {
        names.clear();
    }
    if (plots == std::vector<int>{-1}) {
        plots.clear();
    }
    if (names.size() != plots.size()) {
        std::cerr << std::format("Number of names and plots does not match: {}!={}", names.size(), plots.size());
        std::abort();
    }
    if (xlimits.size() != 2) {
        std::cerr << std::format("Wrong amount of x-axis limits: {}, expected 2", xlimits.size());
    }

    // Signal name <-> plot idx mapping given from cmd line
    std::map<std::string, int> name_and_plot_idx;
    for (int i = 0; i < names.size(); ++i) {
        name_and_plot_idx[names[i]] = plots[i];
    }

    CsvPlotter plotter(files, name_and_plot_idx, {xlimits[0], xlimits[1]}, image_filepath);
}
