
#include <iostream>
#include "csvplot.h"

int main(int argc, char** argv) {
    std::string csv_file;
    if (argc == 1) {
        std::cerr << "No csv file given";
        return -1;
    }
    if (argc == 2) {
        CsvPlot(std::string(argv[1]));
    } else {
        CsvPlot();
    }
}