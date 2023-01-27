#pragma once
#include <string>
#include <filesystem>
#include <imgui.h>
#include <map>

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
    int run_number = 0;

    bool operator==(CsvFileData const& other) {
        return name == other.name
            && displayed_name == other.displayed_name
            && write_time == other.write_time
            && run_number == other.run_number;
    }
};

struct GLFWwindow;
class CsvPlotter {
  public:
    CsvPlotter(std::vector<std::string> files,
               std::map<std::string, int> name_and_plot_idx);

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
    bool m_fit_after_drag_and_drop = true;
    double m_x_axis_min;
    double m_x_axis_max;
};
