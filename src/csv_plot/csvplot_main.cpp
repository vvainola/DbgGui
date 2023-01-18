
#include <iostream>
#include "csvplot.h"
#include <cxxopts.hpp>

int main(int argc, char** argv) {
    std::string csv_file;
    
    cxxopts::Options options("CSV Plotter");
    // clang-format off
    options.add_options()
        ("f,files", "Files to open for plotting", cxxopts::value<std::vector<std::string>>()->default_value("-"))
        ("n,names", "Names of signals to add to plots e.g. \"foo,bar\"", cxxopts::value<std::vector<std::string>>()->default_value("-"))
        ("p,plots", "Indices of plots to add signals matching order of arguments in \"names\" e.g. \"0,1\"", cxxopts::value<std::vector<int>>()->default_value("-1"))
        ("h,help", "Show help and exit")
        ;
    // clang-format on
    cxxopts::ParseResult parsed_options;
    std::vector<std::string> files;
    std::vector<std::string> names;
    std::vector<int> plots;
    try {
        parsed_options = options.parse(argc, argv);
        files = parsed_options["files"].as<std::vector<std::string>>();
        names = parsed_options["names"].as<std::vector<std::string>>();
        plots = parsed_options["plots"].as<std::vector<int>>();
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

    // Signal name <-> plot idx mapping given from cmd line
    std::map<std::string, int> name_and_plot_idx; 
    for (int i = 0; i < names.size(); ++i) 
    {
        name_and_plot_idx[names[i]] = plots[i];
    }

    CsvPlotter plotter(files, name_and_plot_idx);
}
