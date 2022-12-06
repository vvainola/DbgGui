#pragma once
#include <string>
#include "scrolling_buffer.h"

struct CsvSignal {
    std::string name;
    ScrollingBuffer samples;
    bool visible = false;
};

class CsvPlot {
  public:
    CsvPlot(std::string const& csv);

  private:
    std::string m_csv;
    std::vector<CsvSignal> m_signals;
};
