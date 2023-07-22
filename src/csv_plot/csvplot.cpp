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

#define _CRT_SECURE_NO_WARNINGS

#pragma warning(push, 0)
#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "symbols/fts_fuzzy_match.h"
#pragma warning(pop)

#include "csvplot.h"
#include "dark_theme.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "save_image.h"

#include <nfd.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <iterator>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <span>

inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.7f, 0.7f, 0.7f, 1);
inline constexpr ImVec4 COLOR_WHITE = ImVec4(1, 1, 1, 1);
// Render few frames before saving image because plot are not immediately autofitted correctly
inline int IMAGE_SAVE_FRAME_COUNT = 3;

int32_t binarySearch(std::span<double> values, double searched_value, int32_t start, int32_t end) {
    int32_t original_start = start;
    int32_t mid = std::midpoint(start, end);
    while (start <= end) {
        mid = std::midpoint(start, end);
        double val = values[mid];
        if (val < searched_value) {
            start = mid + 1;
        } else if (val > searched_value) {
            end = mid - 1;
        } else {
            return mid;
        }
    }
    return std::max(original_start, end);
}

std::pair<int32_t, int32_t> getTimeIndices(std::span<double> time, double start_time, double end_time) {
    int32_t start_idx = 0;
    int32_t end_idx = int32_t(time.size() - 1);
    start_idx = binarySearch(time, start_time, start_idx, end_idx);
    end_idx = binarySearch(time, end_time, start_idx, end_idx);
    end_idx = std::max(end_idx, start_idx);
    return {start_idx, end_idx};
}

inline constexpr unsigned MAX_NAME_LENGTH = 255;

std::vector<double> ASCENDING_NUMBERS;

std::optional<CsvFileData> parseCsvData(std::string filename, std::map<std::string, int> name_and_plot_idx);
std::vector<CsvFileData> openCsvFromFileDialog();
bool pscadInfToCsv(std::string const& inf_filename);

template <typename T>
inline void remove(std::vector<T>& v, const T& item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

void exit(std::string const& err) {
    std::cerr << err;
    exit(-1);
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

CsvPlotter::CsvPlotter(std::vector<std::string> files,
                       std::map<std::string, int> name_and_plot_idx,
                       MinMax const& xlimits,
                       std::string const& image_filepath) {
    if (xlimits != AUTOFIT_AXIS) {
        m_x_axis.min = std::min(xlimits.min, xlimits.max);
        m_x_axis.max = std::max(xlimits.min, xlimits.max);
    }

    for (std::string file : files) {
        std::optional<CsvFileData> data = parseCsvData(file, name_and_plot_idx);
        if (data) {
            m_csv_data.push_back(*data);
        }
    }

    //---------- Initializations ----------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        std::abort();
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    m_window = glfwCreateWindow(1280, 720, "CSV Plotter", NULL, NULL);
    if (m_window == NULL)
        std::abort();
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    glfwSetWindowPos(m_window, 0, 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport
    io.IniFilename = NULL;                                // Set to NULL because ini file file is loaded manually
    ImGui::StyleColorsDark();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform
    // windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
    style.WindowPadding.x = 1;
    style.WindowPadding.y = 5;
    style.FramePadding.x = 1;
    style.FramePadding.y = 1;
    style.CellPadding.y = 1;
    style.IndentSpacing = 20;
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(5, 5));

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    extern unsigned int cousine_regular_compressed_size;
    extern unsigned int cousine_regular_compressed_data[];
    io.Fonts->AddFontFromMemoryCompressedTTF(cousine_regular_compressed_data, cousine_regular_compressed_size, 13.0f);
    setDarkTheme(m_window);
    loadPreviousSessionSettings();
    // Move window out of sight if creating image to avoid popups
    if (!image_filepath.empty()) {
        glfwSetWindowPos(m_window, 10000, 10000);
    }

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();

        //---------- Main windows ----------
        showSignalWindow();
        showPlots();
        // Settings are not saved when creating image because the window
        // is moved out of sight and should not be restored there
        if (image_filepath.empty()) {
            updateSavedSettings();
        }

        //---------- Rendering ----------
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(m_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we
        // save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call
        //  glfwMakeContextCurrent(window) directly)
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        glfwSwapBuffers(m_window);

        if (!image_filepath.empty() && ImGui::GetFrameCount() > IMAGE_SAVE_FRAME_COUNT) {
            saveImage(image_filepath.c_str(), m_window);
            glfwSetWindowShouldClose(m_window, true);
        }
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void CsvPlotter::loadPreviousSessionSettings() {
    std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.csvplot\\");
    ImGui::LoadIniSettingsFromDisk((settings_dir + "imgui.ini").c_str());

    std::ifstream f(settings_dir + "settings.json");
    if (f.is_open()) {
        try {
            auto settings = nlohmann::json::parse(f);
            int xpos = std::max(0, int(settings["window"]["xpos"]));
            int ypos = std::max(0, int(settings["window"]["ypos"]));
            glfwSetWindowPos(m_window, xpos, ypos);
            glfwSetWindowSize(m_window, settings["window"]["width"], settings["window"]["height"]);
            m_plot_cnt = int(settings["window"]["plot_cnt"]);
            m_link_axis = settings["window"]["link_axis"];
            m_fit_after_drag_and_drop = settings["window"]["fit_on_drag_and_drop"];
            m_keep_old_signals_on_reload = settings["window"]["keep_old_signals_on_reload"];
            for (auto scale : settings["scales"].items()) {
                m_signal_scales[scale.key()] = scale.value();
            }
        } catch (nlohmann::json::exception err) {
            std::cerr << "Failed to load previous session settings" << std::endl;
            std::cerr << err.what();
        }
    }
}

void CsvPlotter::updateSavedSettings() {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    int xpos, ypos;
    glfwGetWindowPos(m_window, &xpos, &ypos);
    // Early return in case something is wrong
    if (width == 0 || height == 0) {
        return;
    }

    std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.csvplot\\");

    size_t ini_settings_size = 0;
    std::string ini_settings = ImGui::SaveIniSettingsToMemory(&ini_settings_size);
    static std::string ini_settings_saved = ini_settings;
    if (ini_settings != ini_settings_saved) {
        ini_settings_saved = ini_settings;

        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        ImGui::SaveIniSettingsToDisk((settings_dir + "imgui.ini").c_str());
    }

    nlohmann::json settings;
    settings["window"]["width"] = width;
    settings["window"]["height"] = height;
    settings["window"]["xpos"] = xpos;
    settings["window"]["ypos"] = ypos;
    settings["window"]["plot_cnt"] = m_plot_cnt;
    settings["window"]["link_axis"] = m_link_axis;
    settings["window"]["fit_on_drag_and_drop"] = m_fit_after_drag_and_drop;
    settings["window"]["keep_old_signals_on_reload"] = m_keep_old_signals_on_reload;
    for (auto& [name, scale] : m_signal_scales) {
        settings["scales"][name] = scale;
    }
    static nlohmann::json settings_saved = settings;
    if (settings != settings_saved) {
        std::ofstream(settings_dir + "settings.json") << std::setw(4) << settings;
    }
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::optional<CsvFileData> parseCsvData(std::string filename,
                                        std::map<std::string, int> name_and_plot_idx = {}) {
    std::string csv_filename = filename;
    if (filename.ends_with(".inf")) {
        bool csv_file_created = pscadInfToCsv(filename);
        if (!csv_file_created) {
            return std::nullopt;
        }
        std::string basename = filename.substr(0, filename.find_last_of("."));
        csv_filename = basename + ".csv";
        // Set the _01.out file as the filename instead of inf because PSCAD writes the inf file immediately
        // but the out files do not have all the data yet. The out file is watched for changes so that the
        // files are not re-read too early
        filename = basename + "_01.out";
    } else if (filename.ends_with(".out")) {
        // A inf file has been read earlier. Re-read the inf file and re-create the csv file.
        std::string basename = filename.substr(0, filename.find_last_of(".") - 3);
        std::string inf_filename = basename + ".inf";
        bool csv_file_created = pscadInfToCsv(inf_filename);
        if (!csv_file_created) {
            return std::nullopt;
        }
        csv_filename = basename + ".csv";
    }

    std::ifstream csv(csv_filename);
    if (!csv.is_open()) {
        std::cerr << "Unable to open file " + csv_filename << std::endl;
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << csv.rdbuf();
    std::vector<std::string> lines = split(buffer.str(), '\n');
    if (lines.size() == 0) {
        std::cerr << "No data in file " + csv_filename << std::endl;
        return std::nullopt;
    }

    // Try detect delimiter from the end of file as that part likely doesn't contain extra information
    // Use second last line in case the last line gets modified suddenly
    char delimiter = '\0';
    size_t element_count = 0;
    size_t last_line = 0;
    for (size_t i = lines.size() - 2; i > 0; --i) {
        if (!lines[i].empty()) {
            last_line = i;
            std::vector<std::string> values_comma = split(lines[i], ',');
            std::vector<std::string> values_semicolon = split(lines[i], ';');
            std::vector<std::string> values_tab = split(lines[i], '\t');
            if (values_comma.size() > values_semicolon.size() && values_comma.size() > values_tab.size()) {
                delimiter = ',';
                element_count = values_comma.size();
            } else if (values_semicolon.size() > values_comma.size() && values_semicolon.size() > values_tab.size()) {
                delimiter = ';';
                element_count = values_semicolon.size();
            } else if (values_tab.size() > values_comma.size() && values_tab.size() > values_semicolon.size()) {
                delimiter = '\t';
                element_count = values_tab.size();
            }
            break;
        }
    }
    if (delimiter == '\0') {
        exit(std::format("Unable to detect delimiter from second last line of the file \"{}\"\n", csv_filename));
    }

    // Find first line where header begins
    size_t first_line = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (split(lines[i], delimiter).size() == element_count) {
            first_line = i;
            break;
        }
    }

    std::vector<std::string> signal_names = split(lines[first_line], delimiter);
    size_t sample_cnt = last_line - first_line;
    for (size_t i = ASCENDING_NUMBERS.size(); i < sample_cnt; ++i) {
        ASCENDING_NUMBERS.push_back(double(i));
    }

    // Count number of instances with same name
    std::map<std::string, int> signal_name_count;
    for (std::string const& signal_name : signal_names) {
        if (signal_name_count.contains(signal_name)) {
            ++signal_name_count[signal_name];
        } else {
            signal_name_count[signal_name] = 1;
        }
    }

    std::vector<CsvSignal> csv_signals;
    csv_signals.reserve(signal_names.size());
    std::map<std::string, int> signal_name_counter;
    for (std::string const& signal_name : signal_names) {
        // Add counter to name if same name is included multiple times
        bool has_duplicate_names = signal_name_count[signal_name] > 1;
        if (has_duplicate_names) {
            csv_signals.push_back(CsvSignal{
                .name = std::format("{}#{}", signal_name, signal_name_counter[signal_name])});
            ++signal_name_counter[signal_name];
        } else {
            csv_signals.push_back(CsvSignal{.name = signal_name});
        }
        // Add signal to plot automatically if name was given from cmd line
        if (name_and_plot_idx.contains(signal_name)) {
            csv_signals.back().plot_idx = name_and_plot_idx[signal_name];
        }
        csv_signals.back().samples.reserve(sample_cnt);
    }
    for (size_t i = 0; i < sample_cnt; ++i) {
        std::vector<std::string> values = split(lines[first_line + i + 1], delimiter);
        for (size_t j = 0; j < values.size(); ++j) {
            csv_signals[j].samples.push_back(std::stod(values[j]));
        }
    }

    // Sort signals alphabetically, skip first since that is usually time
    std::sort(csv_signals.begin() + 1, csv_signals.end(), [](CsvSignal const& l, CsvSignal const& r) {
        std::string l_name = l.name;
        std::string r_name = r.name;
        std::transform(l_name.begin(), l_name.end(), l_name.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
        std::transform(r_name.begin(), r_name.end(), r_name.begin(), [](unsigned char c) { return (unsigned char)std::tolower(c); });
        return l_name < r_name;
    });

    return CsvFileData{
        .name = std::filesystem::relative(filename).string(),
        .displayed_name = std::filesystem::relative(filename).string(),
        .signals = csv_signals,
        .write_time = std::filesystem::last_write_time(filename)};
}

void writeLineToCsv(std::ofstream& csv_file, std::string const& line, bool include_first_column) {
    std::istringstream iss(line);
    double signal_data;
    if (!include_first_column) {
        iss >> signal_data;
    }
    while (iss >> signal_data) {
        csv_file << signal_data << ",";
    }
}

// Opens PSCAD .inf file, reads the signal names, parses the .out files for data and creates single
// csv file with same basename. Returns true if csv file was created, false if something went wrong.
bool pscadInfToCsv(std::string const& inf_filename) {
    std::ifstream inf_file(inf_filename);
    if (!inf_file.is_open()) {
        std::cerr << "Unable to open file " + inf_filename << std::endl;
        return false;
    }

    // Collect signal names from the .inf file
    std::vector<std::string> signal_names;
    std::string line;
    while (std::getline(inf_file, line)) {
        std::string desc_tag = "Desc=\"";
        size_t desc_start_idx = line.find(desc_tag);
        if (desc_start_idx != std::string::npos) {
            desc_start_idx += desc_tag.length();
            size_t desc_end_idx = line.find("\"", desc_start_idx);
            if (desc_end_idx != std::string::npos) {
                std::string desc_content = line.substr(desc_start_idx, desc_end_idx - desc_start_idx);
                signal_names.push_back(desc_content);
            }
        }
    }

    // Open the .out files
    std::string inf_basename = inf_filename.substr(0, inf_filename.find_last_of("."));
    std::vector<std::ifstream> out_files;
    int out_file_cnt = int(ceil(signal_names.size() / 10.0));
    for (int i = 1; i < out_file_cnt + 1; ++i) {
        std::stringstream ss;
        ss << std::setw(2) << std::setfill('0') << i << ".out";
        std::string out_filename = inf_basename + "_" + ss.str();
        std::ifstream& out_file = out_files.emplace_back(out_filename);
        if (!out_file.is_open()) {
            std::cerr << "Unable to open file " + out_filename << std::endl;
            return false;
        }
        // Ignore first line that contains the project name
        std::getline(out_file, line);
    }

    // Open the .csv file for writing
    std::string csv_filename = inf_basename + ".csv";
    std::ofstream csv_file(csv_filename);
    if (!csv_file.is_open()) {
        std::cerr << "Unable to open file " + csv_filename << std::endl;
        return false;
    }

    // Write signal names to header row
    csv_file << "Time,";
    for (int i = 0; i < signal_names.size(); i++) {
        csv_file << signal_names[i];
        if (i < signal_names.size() - 1) {
            csv_file << ",";
        }
    }
    csv_file << ",\n";

    // Go through the .out files line by line to write out the data. Time column is included
    // only from the first file
    std::ifstream& out_file1 = out_files[0];
    while (std::getline(out_file1, line)) {
        writeLineToCsv(csv_file, line, true);
        for (int i = 1; i < out_files.size(); ++i) {
            std::getline(out_files[i], line);
            writeLineToCsv(csv_file, line, false);
        }
        csv_file << "\n";
    }
    csv_file.close();
    return true;
}

void CsvPlotter::showSignalWindow() {
    ImGui::Begin("Signals");

    ImGui::BeginChild("Signal selection", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
    if (ImGui::Button("Open")) {
        std::vector<CsvFileData> csv_datas = openCsvFromFileDialog();
        for (CsvFileData& csv_data : csv_datas) {
            m_csv_data.push_back(csv_data);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        for (CsvFileData& file : m_csv_data) {
            for (CsvSignal& signal : file.signals) {
                signal.plot_idx = NOT_VISIBLE;
                signal.color = NO_COLOR;
            }
        }
    }
    if (ImGui::Button("Add plot")) {
        m_plot_cnt++;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove plot")) {
        m_plot_cnt--;
        m_plot_cnt = std::max(m_plot_cnt, 1);
    }

    if (ImGui::Checkbox("Use first signal as x-axis", &m_first_signal_as_x)) {
        ImPlot::SetNextAxesToFit();
    }
    ImGui::Checkbox("Link x-axis", &m_link_axis);
    ImGui::Checkbox("Fit after drag and drop", &m_fit_after_drag_and_drop);
    ImGui::Checkbox("Keep old signals on reload", &m_keep_old_signals_on_reload);
    ImGui::Checkbox("Cursor measurements", &m_cursor_measurements);

    static char signal_name_filter[256] = "";
    ImGui::InputText("Filter", signal_name_filter, IM_ARRAYSIZE(signal_name_filter));

    CsvFileData* file_to_remove = nullptr;
    for (CsvFileData& file : m_csv_data) {
        // Reload file if it has been rewritten. Wait that file has not been modified in the last 2 seconds
        // in case it is still being written
        if (std::filesystem::exists(file.name)) {
            auto last_write_time = std::filesystem::last_write_time(file.name);
            auto write_time_plus_2s = std::chrono::clock_cast<std::chrono::system_clock>(last_write_time) + std::chrono::seconds(2);
            auto now = std::chrono::system_clock::now();

            if (last_write_time != file.write_time
                && now > write_time_plus_2s
                && file.write_time != std::filesystem::file_time_type()) {
                std::optional<CsvFileData> csv_data = parseCsvData(file.name);
                if (csv_data) {
                    if (csv_data->signals.size() == file.signals.size()) {
                        for (int i = 0; i < file.signals.size(); ++i) {
                            csv_data->signals[i].plot_idx = file.signals[i].plot_idx;
                            if (!m_keep_old_signals_on_reload) {
                                csv_data->signals[i].color = file.signals[i].color;
                                file.signals[i].plot_idx = NOT_VISIBLE;
                                file.signals[i].color = NO_COLOR;
                            }
                        }
                    }
                    // Set write time to default so that the file gets reloaded again for the latest dataset
                    file.write_time = std::filesystem::file_time_type();
                    file.run_number++;
                    csv_data->run_number = file.run_number;
                    file.displayed_name += " " + std::to_string(file.run_number);
                    m_csv_data.push_back(*csv_data);
                    break;
                }
            }
        }

        bool opened = ImGui::TreeNode(file.displayed_name.c_str());
        // Make displayed name editable
        if (ImGui::BeginPopupContextItem((file.displayed_name + "context_menu").c_str())) {
            static std::string displayed_name_edit = file.displayed_name;
            displayed_name_edit.reserve(MAX_NAME_LENGTH);
            displayed_name_edit = file.displayed_name;
            if (ImGui::InputText("Name##scalar_context_menu",
                                 displayed_name_edit.data(),
                                 MAX_NAME_LENGTH,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                file.displayed_name = std::string(displayed_name_edit.data());
                if (file.displayed_name.empty()) {
                    file.displayed_name = file.name;
                }
            }
            if (ImGui::Button("Remove")) {
                file_to_remove = &file;
            }
            ImGui::EndPopup();
        }

        if (opened) {
            for (CsvSignal& signal : file.signals) {
                // Skip signal if it doesn't match the filter
                if (std::string(signal_name_filter) != ""
                    && !fts::fuzzy_match_simple(signal_name_filter, signal.name.c_str())) {
                    continue;
                }

                bool selected = signal.plot_idx != NOT_VISIBLE;
                double& signal_scale = m_signal_scales[signal.name];
                // Set default scale if signal has no scale
                if (signal_scale == 0) {
                    signal_scale = 1;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, signal_scale == 1 ? COLOR_WHITE : COLOR_GRAY);
                if (ImGui::Selectable(signal.name.c_str(), &selected)) {
                    signal.plot_idx = NOT_VISIBLE;
                    signal.color = NO_COLOR;
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    CsvSignal* p = &signal;
                    ImGui::SetDragDropPayload("CSV", &p, sizeof(CsvSignal*));
                    ImGui::Text("Drag to plot");
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginPopupContextItem((file.displayed_name + signal.name + "context_menu").c_str())) {
                    ImGui::InputDouble("Scale", &signal_scale);
                    if (ImGui::Button("Copy name")) {
                        ImGui::SetClipboardText(signal.name.c_str());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::End();

    if (file_to_remove) {
        remove(m_csv_data, *file_to_remove);
    }
}

void CsvPlotter::showPlots() {
    for (int plot_idx = 0; plot_idx < m_plot_cnt; ++plot_idx) {
        ImGui::Begin(std::format("Plot {}", plot_idx).c_str());
        bool autofit_x_axis = (m_x_axis == AUTOFIT_AXIS);
        bool fit_data = (plot_idx == m_fit_plot_idx);
        if (fit_data || autofit_x_axis) {
            ImPlot::SetNextAxesToFit();
            m_fit_plot_idx = -1;
        }

        // Get signals in the plot
        size_t longest_name_length = 1;
        size_t longest_file_length = 1;
        struct FileAndSignal {
            CsvFileData* file;
            CsvSignal* signal;
        };
        std::vector<FileAndSignal> signals;
        for (CsvFileData& file : m_csv_data) {
            for (CsvSignal& signal : file.signals) {
                if (signal.plot_idx == plot_idx) {
                    longest_name_length = std::max(longest_name_length, signal.name.size());
                    longest_file_length = std::max(longest_file_length, file.displayed_name.size());
                    signals.push_back(FileAndSignal{
                        .file = &file,
                        .signal = &signal});
                }
            }
        }

        if (m_cursor_measurements) {
            if (ImGui::BeginTable("Delta", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("X").x * (longest_name_length + longest_file_length + 5));
                const float num_width = ImGui::CalcTextSize("0xDDDDDDDDDDDDDDDDDD").x;
                ImGui::TableSetupColumn("y1", ImGuiTableColumnFlags_WidthFixed, num_width);
                ImGui::TableSetupColumn("y2", ImGuiTableColumnFlags_WidthFixed, num_width);
                ImGui::TableSetupColumn("delta (y2 - y1)", ImGuiTableColumnFlags_WidthFixed, num_width);
                ImGui::TableHeadersRow();

                // Time row
                ImGui::TableNextColumn();
                ImGui::Text("Time");
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{:g}", m_drag_x1).c_str());
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{:g}", m_drag_x2).c_str());
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{:g}", m_drag_x2 - m_drag_x1).c_str());

                for (FileAndSignal& sig : signals) {
                    std::vector<double>& all_x_values = m_first_signal_as_x ? sig.file->signals[0].samples : ASCENDING_NUMBERS;
                    std::vector<double>& all_y_values = sig.signal->samples;
                    int idx1 = binarySearch(all_x_values, m_drag_x1, 0, int(all_x_values.size() - 1));
                    int idx2 = binarySearch(all_x_values, m_drag_x2, 0, int(all_x_values.size() - 1));
                    double signal_scale = m_signal_scales[sig.signal->name];
                    double y1 = all_y_values[idx1] * signal_scale;
                    double y2 = all_y_values[idx2] * signal_scale;

                    std::stringstream ss;
                    ss << std::left << std::setw(longest_name_length) << sig.signal->name << " | " << sig.file->displayed_name;
                    std::string displayed_signal_name = ss.str();

                    ImGui::TableNextColumn();
                    ImGui::TextColored(sig.signal->color, displayed_signal_name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(sig.signal->color, std::format("{:g}", y1).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(sig.signal->color, std::format("{:g}", y2).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(sig.signal->color, std::format("{:g}", y2 - y1).c_str());
                }
                ImGui::EndTable();
            }
        }

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
        if (ImPlot::BeginPlot("##DND", ImVec2(-1, -1))) {
            ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_None);
            if (m_link_axis) {
                ImPlot::SetupAxisLinks(ImAxis_X1, &m_x_axis.min, &m_x_axis.max);
                if (!autofit_x_axis) {
                    ImPlot::SetupAxisLimits(ImAxis_X1, m_x_axis.min, m_x_axis.max);
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CSV")) {
                    CsvSignal* sig = *(CsvSignal**)payload->Data;
                    sig->plot_idx = plot_idx;
                    if (m_fit_after_drag_and_drop) {
                        m_fit_plot_idx = plot_idx;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Reset plot colors if there are no signals in it
            if (signals.size() == 0) {
                ImVec4 last_color = ImPlot::GetColormapColor(ImPlot::GetColormapSize() - 1);
                ImVec4 next_color = ImPlot::NextColormapColor();
                while (next_color.x != last_color.x
                       || next_color.y != last_color.y
                       || next_color.z != last_color.z
                       || next_color.w != last_color.w) {
                    next_color = ImPlot::NextColormapColor();
                }
            }

            for (FileAndSignal& sig : signals) {
                // Collect only values that are within plot range so that the autofit fits to the plotted values instead
                // of the entire data
                std::vector<double>& all_x_values = m_first_signal_as_x ? sig.file->signals[0].samples : ASCENDING_NUMBERS;
                std::vector<double>& all_y_values = sig.signal->samples;
                ImPlotRect limits = ImPlot::GetPlotLimits();
                std::pair<int32_t, int32_t> indices = getTimeIndices(all_x_values, limits.X.Min, limits.X.Max);
                // Add 1 extra point to both ends not have blanks at the edges. +2 because end range is exclusive
                indices.first = std::max(0, indices.first - 1);
                indices.second = std::min(int32_t(all_x_values.size()), indices.second + 2);

                std::vector<double> plotted_x(all_x_values.begin() + indices.first, all_x_values.begin() + indices.second);
                std::vector<double> plotted_y(all_y_values.begin() + indices.first, all_y_values.begin() + indices.second);
                if (fit_data) {
                    plotted_x = all_x_values;
                    plotted_y = all_y_values;
                }
                // Scale samples. Set default scale if signal has no scale
                double& signal_scale = m_signal_scales[sig.signal->name];
                if (signal_scale == 0) {
                    signal_scale = 1;
                }
                for (double& sample : plotted_y) {
                    sample *= signal_scale;
                }

                std::stringstream ss;
                ss << std::left << std::setw(longest_name_length) << sig.signal->name << " | " << sig.file->displayed_name;
                std::string displayed_signal_name = ss.str();
                // Store color so that it does not change if legend length changes
                if (sig.signal->color.x == -1) {
                    sig.signal->color = ImPlot::NextColormapColor();
                }
                ImPlot::PushStyleColor(ImPlotCol_Line, sig.signal->color);
                ImPlot::PlotLine(displayed_signal_name.c_str(),
                                 plotted_x.data(),
                                 plotted_y.data(),
                                 int(plotted_x.size()),
                                 ImPlotLineFlags_None);
                ImPlot::PopStyleColor();

                // Tooltip
                if (ImPlot::IsPlotHovered() && plotted_x.size() > 0) {
                    ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                    ImPlot::PushStyleColor(ImPlotCol_Line, {255, 255, 255, 0.25});
                    ImPlot::PlotInfLines("##", &mouse.x, 1);
                    ImPlot::PopStyleColor();
                    ImGui::BeginTooltip();
                    int idx = binarySearch(plotted_x, mouse.x, 0, int(plotted_x.size() - 1));
                    ss.str("");
                    ss << sig.signal->name << " : " << plotted_y[idx];
                    ImGui::PushStyleColor(ImGuiCol_Text, sig.signal->color);
                    ImGui::Text(ss.str().c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndTooltip();
                }

                // Legend right click
                if (ImPlot::BeginLegendPopup(displayed_signal_name.c_str())) {
                    if (ImGui::Button("Remove")) {
                        sig.signal->plot_idx = NOT_VISIBLE;
                    }
                    ImPlot::EndLegendPopup();
                }

                // Fit to the entire data with mouse middle button
                if (ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                    m_fit_plot_idx = plot_idx;
                }
            }

            // Vertical lines for cursor measurements
            if (m_cursor_measurements) {
                ImPlotRect plot_limits = ImPlot::GetPlotLimits();
                ImPlotRange x_range = plot_limits.X;
                if (m_drag_x1 == 0 && m_drag_x2 == 0) {
                    m_drag_x1 = x_range.Min + 0.1 * x_range.Size();
                    m_drag_x2 = x_range.Max - 0.1 * x_range.Size();
                }
                ImPlot::DragLineX(0, &m_drag_x1, ImVec4(1, 1, 1, 1));
                ImPlot::DragLineX(1, &m_drag_x2, ImVec4(1, 1, 1, 1));
                ImPlot::PlotText(std::format("x1 : {:g}", m_drag_x1).c_str(), m_drag_x1, plot_limits.Y.Min + 0.2 * plot_limits.Y.Size());
                ImPlot::PlotText(std::format("x2 : {:g}", m_drag_x2).c_str(), m_drag_x2, plot_limits.Y.Min + 0.2 * plot_limits.Y.Size());
            } else {
                m_drag_x1 = 0;
                m_drag_x2 = 0;
            }

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
        ImGui::End();
    }
}

std::vector<CsvFileData> openCsvFromFileDialog() {
    nfdpathset_t path_set;
    std::vector<CsvFileData> csv_datas;
    auto cwd = std::filesystem::current_path();
    nfdresult_t result = NFD_OpenDialogMultiple("csv,inf", cwd.string().c_str(), &path_set);
    if (result == NFD_OKAY) {
        for (int i = 0; i < path_set.count; ++i) {
            std::string out(NFD_PathSet_GetPath(&path_set, i));
            auto csv_data = parseCsvData(out);
            if (csv_data) {
                csv_datas.push_back(*csv_data);
            }
        }
        NFD_PathSet_Free(&path_set);
    } else if (result == NFD_ERROR) {
        std::cerr << NFD_GetError() << std::endl;
    }
    return csv_datas;
}
