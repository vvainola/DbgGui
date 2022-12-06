#pragma once
#include <string>
#include "scrolling_buffer.h"
#include <filesystem>

struct CsvSignal {
    std::string name;
    ScrollingBuffer samples;
    bool visible = false;
};

struct FileCsvData {
    std::string name;
    std::string displayed_name;
    std::vector<CsvSignal> signals;
    std::filesystem::file_time_type write_time;
};


class CsvPlot {
  public:
    CsvPlot(std::vector<std::string> files);

  private:
    std::vector<FileCsvData> m_csv_data;
};
