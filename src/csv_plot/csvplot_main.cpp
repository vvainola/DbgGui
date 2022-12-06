
#include <iostream>
#include "csvplot.h"

int main(int argc, char** argv) {
    std::string csv_file;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        files.push_back(argv[i]);
    }
    CsvPlot plotter(files);
}