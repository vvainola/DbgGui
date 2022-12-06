#pragma once
#include <string>
#include "scrolling_buffer.h"

struct CsvSignal {
    std::string name;
    ScrollingBuffer samples;
    bool visible = false;
};

struct FileCsvData {
    std::string name;
    std::vector<CsvSignal> signals;
};


class CsvPlot {
  public:
    CsvPlot(std::string const& csv = "");

  private:
    std::vector<FileCsvData> m_csv_data;
};
