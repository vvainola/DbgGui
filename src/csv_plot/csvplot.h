#pragma once
#include <string>
#include <filesystem>
#include <imgui.h>

inline constexpr int NOT_VISIBLE = -1;
inline constexpr ImVec4 NO_COLOR = {-1, -1, -1, -1};

struct CsvSignal {
    std::string name;
    std::vector<double> samples;
    int plot_idx = NOT_VISIBLE;
    ImVec4 color{NO_COLOR};
};

struct CsvFileData {
    std::string name;
    std::string displayed_name;
    std::vector<CsvSignal> signals;
    std::filesystem::file_time_type write_time;
};

struct GLFWwindow;
class CsvPlotter {
  public:
    CsvPlotter(std::vector<std::string> files);

  private:
    void showSignalWindow();
    void showPlots();

    void updateSavedSettings();
    void loadPreviousSessionSettings();
    GLFWwindow* m_window;

    std::vector<CsvFileData> m_csv_data;
    int m_plot_cnt = 1;
    int m_fit_plot_idx = -1;
    bool m_first_signal_as_x = true;
    bool m_link_axis = true;
    double m_x_axis_min;
    double m_x_axis_max;
};
