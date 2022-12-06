
#include <iostream>
#include "csvplot.h"

int main(int argc, char** argv) {
    std::string csv_file;
    if (argc == 1) {
        std::cerr << "No csv file given";
        return -1;
    }
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        files.push_back(argv[i]);
    }
    CsvPlot({files});
}