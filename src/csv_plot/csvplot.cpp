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

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "resource.h"

#include "csvplot.h"
#include "themes.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "save_image.h"
#include "csv_helpers.h"
#include "stb_image.h"

#include <nfd.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <iterator>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <span>
#include <stdexcept>

inline constexpr int32_t MIN_FONT_SIZE = 8;
inline constexpr int32_t MAX_FONT_SIZE = 100;
inline constexpr ImVec4 COLOR_TOOLTIP_LINE = ImVec4(0.7f, 0.7f, 0.7f, 0.6f);
inline constexpr ImVec4 COLOR_GRAY = ImVec4(0.7f, 0.7f, 0.7f, 1);
inline constexpr ImVec4 COLOR_WHITE = ImVec4(1, 1, 1, 1);
// Render few frames before saving image because plot are not immediately autofitted correctly
inline int IMAGE_SAVE_FRAME_COUNT = 3;
inline constexpr unsigned MAX_NAME_LENGTH = 255;
inline int MAX_PLOT_SAMPLE_COUNT = 3000;
constexpr int CUSTOM_SIGNAL_CAPACITY = 10;
std::vector<double> ASCENDING_NUMBERS;

std::optional<int> pressedNumber();
std::unique_ptr<CsvFileData> parseCsvData(std::string filename);
std::vector<std::unique_ptr<CsvFileData>> openCsvFromFileDialog();
void setLayout(ImGuiID main_dock, int rows, int cols, float signals_window_width);
std::tuple<int, int> getAutoLayout(int signal_count);
std::pair<int32_t, int32_t> getTimeIndices(std::span<double const> time, double start_time, double end_time);

int32_t binarySearch(std::span<double const> values, double searched_value, int32_t start, int32_t end) {
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

std::pair<int32_t, int32_t> getTimeIndices(std::span<double const> time, double start_time, double end_time) {
    int32_t start_idx = 0;
    int32_t end_idx = int32_t(time.size() - 1);
    // Add 1 extra point to both ends not have blanks at the edges. +2 because end range is exclusive
    start_idx = binarySearch(time, start_time, start_idx, end_idx) - 1;
    end_idx = binarySearch(time, end_time, start_idx, end_idx) + 2;
    end_idx = std::max(end_idx, start_idx);
    // Prevent overflows
    start_idx = std::max(start_idx, 0);
    end_idx = std::min(end_idx, (int32_t)time.size() - 1);
    return {start_idx, end_idx};
}

std::vector<double> CsvPlotter::getVisibleSamples(CsvSignal const& signal) {
    std::vector<double> const& all_x_values = m_options.first_signal_as_x ? signal.file->signals[0].samples : ASCENDING_NUMBERS;
    std::vector<double> const& all_samples = signal.samples;
    double x_offset = m_options.shift_samples_to_start_from_zero ? all_x_values[0] : 0;
    x_offset -= signal.file->x_axis_shift;

    std::pair<int32_t, int32_t> indices;
    if (!m_options.first_signal_as_x) {
        indices = {std::max(0, (int)std::floor(m_x_axis.min)),
                   std::min((int)all_samples.size(), (int)std::ceil(m_x_axis.max))};
    } else {
        indices = getTimeIndices(signal.file->signals[0].samples, m_x_axis.min + x_offset, m_x_axis.max + x_offset);
    }

    std::vector<double> plotted_samples(all_samples.begin() + indices.first, all_samples.begin() + indices.second);
    // Scale samples. Use default scale if signal has no scale
    double signal_scale = 1;
    if (m_signal_scales.contains(signal.name)) {
        signal_scale = *str::evaluateExpression(m_signal_scales.at(signal.name));
    }

    for (double& sample : plotted_samples) {
        sample *= signal_scale;
    }

    return plotted_samples;
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

CsvPlotter::CsvPlotter(std::vector<std::string> files,
                       std::map<std::string, int> name_and_plot_idx,
                       MinMax const& xlimits,
                       int rows,
                       int cols,
                       std::string const& image_filepath) {
    if (xlimits != AUTOFIT_AXIS) {
        m_x_axis.min = std::min(xlimits.min, xlimits.max);
        m_x_axis.max = std::max(xlimits.min, xlimits.max);
    }

    for (std::string file : files) {
        std::unique_ptr<CsvFileData> data = parseCsvData(file);
        if (data) {
            m_csv_data.push_back(std::move(data));
            for (CsvSignal& signal : m_csv_data.back()->signals) {
                if (name_and_plot_idx.contains(signal.name)) {
                    int plot_idx = name_and_plot_idx[signal.name];
                    m_scalar_plots[plot_idx].addSignal(&signal);
                }
            }
        } else {
            std::abort();
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

    // Load icon
    HRSRC resource_handle = FindResource(nullptr, MAKEINTRESOURCEA(ICON_PNG), "PNG");
    HGLOBAL resource_memory_handle = LoadResource(nullptr, resource_handle);
    size_t size_bytes = SizeofResource(nullptr, resource_handle);
    void* resource_buffer = LockResource(resource_memory_handle);
    GLFWimage image;
    image.pixels = stbi_load_from_memory((stbi_uc*)resource_buffer, (int)size_bytes, &image.width, &image.height, 0, STBI_rgb_alpha);
    glfwSetWindowIcon(m_window, 1, &image);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport
    io.IniFilename = NULL;                                // Set to NULL because ini file file is loaded manually

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    loadPreviousSessionSettings();

    extern unsigned int cousine_regular_compressed_size;
    extern unsigned int cousine_regular_compressed_data[];
    io.Fonts->AddFontFromMemoryCompressedTTF(cousine_regular_compressed_data, cousine_regular_compressed_size, m_options.font_size);
    ImGui::PushFont(ImGui::GetDefaultFont(), m_options.font_size);

    // Move window out of sight if creating image to avoid popups. Docking layout sometimes does not get applied
    // if window is not visible so only 1 pixel is visible to have correct layout.
    if (!image_filepath.empty()) {
        int xpos = 0;
        int ypos = 0;
        if (rows > 0) {
            m_rows = rows;
        }
        if (cols > 0) {
            m_cols = cols;
        }
        glfwGetWindowSize(m_window, &xpos, &ypos);
        glfwSetWindowPos(m_window, 0, -ypos + 1);
        // Set window minimum size to 1 to hide the signals window which is docked
        // to the main dock. Signals window has to be included because docking some
        // plot to the main dock will make it the wrong size https://github.com/ocornut/imgui/issues/6095
        auto& style = ImGui::GetStyle();
        style.WindowMinSize.x = 1;
        m_signals_window_width = 0;
    }

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiID main_dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();

        //---------- Main windows ----------
        if (image_filepath.empty()) {
            showVectorPlots();
            showSpectrumPlots();
        }
        showErrorModal();
        showSignalWindow();
        showScalarPlots();

        // Settings are not saved when creating image because the window
        // is moved out of sight and should not be restored there
        if (image_filepath.empty()) {
            updateSavedSettings();
        }

        setLayout(main_dock, m_rows, m_cols, m_signals_window_width);

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

    std::ifstream f(settings_dir + "settings.json");
    if (f.is_open()) {
        try {
            auto settings = nlohmann::json::parse(f);

            if (settings.contains("layout")) {
                std::string layout = settings["layout"];
                ImGui::LoadIniSettingsFromMemory(layout.data());
            } else {
                ImGui::LoadIniSettingsFromDisk((settings_dir + "imgui.ini").c_str());
            }

            m_rows = int(settings["window"]["rows"]);
            m_cols = int(settings["window"]["cols"]);
            m_vector_plot_cnt = int(settings["window"]["vector_plot_cnt"]);
            m_spectrum_plot_cnt = int(settings["window"]["spectrum_plot_cnt"]);
            m_options.first_signal_as_x = settings["window"]["first_signal_as_x"];
            m_options.link_axis = settings["window"]["link_axis"];
            m_options.autofit_y_axis = settings["window"]["autofit_y_axis"];
            m_options.show_vertical_line_in_all_plots = settings["window"]["show_vertical_line_in_all_plots"];
            m_options.shift_samples_to_start_from_zero = settings["window"]["shift_samples_to_start_from_zero"];
            m_options.keep_old_signals_on_reload = settings["window"]["keep_old_signals_on_reload"];
            m_options.theme = settings["window"]["theme"];
            m_options.font_size = settings["window"]["font_size"];
            setTheme(m_options.theme, m_window);
            for (auto scale : settings["scales"].items()) {
                if (scale.value().is_number()) {
                    double value = scale.value();
                    m_signal_scales[scale.key()] = std::format("{:g}", value);
                } else {
                    m_signal_scales[scale.key()] = scale.value();
                }
            }
            m_signals_window_width = settings["window"]["signals_window_width"];

            int xpos = std::max(0, int(settings["window"]["xpos"]));
            int ypos = std::max(0, int(settings["window"]["ypos"]));
            glfwSetWindowPos(m_window, xpos, ypos);
            glfwSetWindowSize(m_window, settings["window"]["width"], settings["window"]["height"]);
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

    nlohmann::json settings;
    settings["window"]["width"] = width;
    settings["window"]["height"] = height;
    settings["window"]["xpos"] = xpos;
    settings["window"]["ypos"] = ypos;
    settings["window"]["rows"] = m_rows;
    settings["window"]["cols"] = m_cols;
    settings["window"]["vector_plot_cnt"] = m_vector_plot_cnt;
    settings["window"]["spectrum_plot_cnt"] = m_spectrum_plot_cnt;
    settings["window"]["first_signal_as_x"] = m_options.first_signal_as_x;
    settings["window"]["link_axis"] = m_options.link_axis;
    settings["window"]["autofit_y_axis"] = m_options.autofit_y_axis;
    settings["window"]["show_vertical_line_in_all_plots"] = m_options.show_vertical_line_in_all_plots;
    settings["window"]["shift_samples_to_start_from_zero"] = m_options.shift_samples_to_start_from_zero;
    settings["window"]["keep_old_signals_on_reload"] = m_options.keep_old_signals_on_reload;
    settings["window"]["theme"] = m_options.theme;
    settings["window"]["font_size"] = m_options.font_size;
    settings["layout"] = ImGui::SaveIniSettingsToMemory(nullptr);
    settings["window"]["signals_window_width"] = m_signals_window_width;
    for (auto& [name, scale] : m_signal_scales) {
        settings["scales"][name] = scale;
    }
    static nlohmann::json settings_saved = settings;
    if (settings != settings_saved) {
        settings_saved = settings;

        std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.csvplot\\");
        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        // Write settings to tmp file first to avoid corrupting the file if program is closed mid-write
        std::ofstream(settings_dir + "settings.json.tmp") << std::setw(4) << settings;
        std::filesystem::copy_file(settings_dir + "settings.json.tmp",
                                   settings_dir + "settings.json",
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

std::unique_ptr<CsvFileData> parseCsvData(std::string filename) {
    std::string csv_filename = filename;
    if (filename.ends_with(".inf")) {
        bool csv_file_created = pscadInfToCsv(filename);
        if (!csv_file_created) {
            return nullptr;
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
            return nullptr;
        }
        csv_filename = basename + ".csv";
    }

    std::expected<std::string, std::string> csv_str = str::readFile(csv_filename);
    if (!csv_str.has_value()) {
        std::cerr << csv_str.error() << std::endl;
        return nullptr;
    }
    std::vector<std::string_view> csv_lines = str::splitSv(csv_str.value(), '\n');
    if (csv_lines.size() < 3) {
        std::cerr << "File " + csv_filename << " has less than 3 lines of data" << std::endl;
        return nullptr;
    }
    std::string third_last_line = std::string(csv_lines[csv_lines.size() - 3]);
    str::trim(third_last_line);
    if (third_last_line.empty()) {
        std::cerr << "No data in file " + csv_filename << std::endl;
        return nullptr;
    }

    // Try detect delimiter from the end of file as that part likely doesn't contain extra information
    // Use third last line in case the last line gets modified suddenly
    char delimiter = '\0';
    size_t element_count = 0;
    std::vector<std::string> values_comma = str::split(third_last_line, ',');
    std::vector<std::string> values_semicolon = str::split(third_last_line, ';');
    std::vector<std::string> values_tab = str::split(third_last_line, '\t');
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
    if (delimiter == '\0') {
        std::cerr << std::format("Unable to detect delimiter from third last line of the file \"{}\"\n", csv_filename);
        return nullptr;
    }

    // Find first line where header begins
    int header_line_idx = 0;
    for (std::string_view line_sv : csv_lines) {
        std::string line(line_sv);
        str::trim(line);
        if (str::splitSv(line, delimiter).size() == element_count) {
            break;
        }
        ++header_line_idx;
    }
    std::string header_line(csv_lines[header_line_idx]);
    str::trim(header_line);
    std::vector<std::string> signal_names = str::split(header_line, delimiter);

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
    }
    for (int i = header_line_idx + 1; i < csv_lines.size(); ++i) {
        std::string line = str::removeWhitespace(csv_lines[i]);
        std::vector<std::string_view> values = str::splitSv(line, delimiter, (int)csv_signals.size());
        if (values.size() != signal_names.size()) {
            break;
        }
        for (size_t j = 0; j < values.size(); ++j) {
            double value;
            std::from_chars_result result = std::from_chars(values[j].data(), values[j].data() + values[j].size(), value);
            if (result.ec != std::errc()) {
                value = NAN;
            }
            csv_signals[j].samples.push_back(value);
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

    for (size_t i = ASCENDING_NUMBERS.size(); i < csv_signals[0].samples.size(); ++i) {
        ASCENDING_NUMBERS.push_back(double(i));
    }

    std::unique_ptr<CsvFileData> csv_data = std::make_unique<CsvFileData>();
    csv_data->name = std::filesystem::relative(filename).string();
    csv_data->displayed_name = std::filesystem::relative(filename).string();
    csv_data->signals = std::move(csv_signals);
    csv_data->write_time = std::filesystem::last_write_time(filename);
    for (CsvSignal& signal : csv_data->signals) {
        signal.file = csv_data.get();
    }
    // Add extra capacity to the vector so that it does not get reallocated when adding custom signals which invalidates
    // pointers to the signals in vector & spectrum plots
    csv_data->signals.reserve(csv_data->signals.size() + CUSTOM_SIGNAL_CAPACITY);
    return std::move(csv_data);
}

void CsvPlotter::showErrorModal() {
    if (!m_error_message.empty()) {
        ImGui::OpenPopup("Error");
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); // Center modal
    if (ImGui::BeginPopupModal("Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_error_message.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::Text(m_error_message.c_str());
        ImGui::EndPopup();
    }
}

void CsvPlotter::showSignalWindow() {
    ImGui::Begin("Signals");

    // Adjust window size only when something is being changed and when the actual size is close to the
    // wanted size because in full screen mode the requested size is not fulfilled and if the requested
    // is updated to match actual, the signals window will always expand to take the full screen.
    if (ImGui::IsAnyMouseDown()) {
        ImVec2 size = ImGui::GetWindowSize();
        int width;
        int height;
        glfwGetWindowSize(m_window, &width, &height);
        // The width has to be rounded because otherwise the window size will slide down to zero when
        // holding mouse down
        float rel_width = std::round(size.x * 100.0f / width) / 100.0f;
        if (abs(m_signals_window_width - rel_width) < 0.03) {
            m_signals_window_width = std::min(rel_width, 0.5f);
        }
    }

    ImGui::BeginChild("Signal selection", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
    if (ImGui::Button("Open")) {
        std::vector<std::unique_ptr<CsvFileData>> csv_datas = openCsvFromFileDialog();
        for (auto& csv_data : csv_datas) {
            m_csv_data.push_back(std::move(csv_data));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        for (ScalarPlot& plot : m_scalar_plots) {
            plot.clear();
        }
    }

    // The flag is active for only a single frame
    m_flags.reset_colors = false;
    if (ImGui::Button("Reset colors")) {
        m_flags.reset_colors = true;
    }

    if (ImGui::Button("Copy signals to clipboard")) {
        std::stringstream ss_signals;
        std::stringstream ss_plots;
        ss_signals << "\"";
        ss_plots << "\"";
        for (int i = 0; i < m_scalar_plots.size(); ++i) {
            ScalarPlot& plot = m_scalar_plots[i];
            for (CsvSignal* signal : plot.signals) {
                ss_signals << signal->name << ",";
                ss_plots << i << ",";
            }
        }
        ss_signals << "\"";
        ss_plots << "\"";
        ImGui::SetClipboardText(std::format("--names {} --plots {} --rows {} --cols {}",
                                            ss_signals.str(),
                                            ss_plots.str(),
                                            std::to_string(m_rows),
                                            std::to_string(m_cols))
                                  .c_str());
    }

    if (ImGui::CollapsingHeader("Options")) {
        ImGui::SetNextItemWidth(75);
        ImGui::InputInt("Rows", &m_rows, 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(75);
        ImGui::InputInt("Columns", &m_cols, 1);
        ImGui::SetNextItemWidth(185);
        ImGui::InputInt("Vector plots", &m_vector_plot_cnt, 1);
        ImGui::SetNextItemWidth(185);
        ImGui::InputInt("Spectrum plots", &m_spectrum_plot_cnt, 1);
        m_vector_plot_cnt = std::clamp(m_vector_plot_cnt, 0, MAX_PLOTS);
        m_spectrum_plot_cnt = std::clamp(m_spectrum_plot_cnt, 0, MAX_PLOTS);
        m_rows = std::clamp(m_rows, 1, MAX_PLOTS);
        m_cols = std::clamp(m_cols, 1, MAX_PLOTS);

        themeCombo(m_options.theme, m_window);
        if (ImGui::Checkbox("Use first signal as x-axis", &m_options.first_signal_as_x)) {
            ImPlot::SetNextAxesToFit();
        }
        ImGui::Checkbox("Shift samples to start from zero", &m_options.shift_samples_to_start_from_zero);
        ImGui::Checkbox("Link x-axis", &m_options.link_axis);
        ImGui::Checkbox("Autofix y-axis", &m_options.autofit_y_axis);
        ImGui::Checkbox("Keep old signals on reload", &m_options.keep_old_signals_on_reload);
        ImGui::Checkbox("Cursor measurements", &m_options.cursor_measurements);
        ImGui::Checkbox("Show vertical line in all plots", &m_options.show_vertical_line_in_all_plots);
        if (ImGui::InputInt("Font size", &m_options.font_size, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
            m_options.font_size = std::clamp((int)m_options.font_size, MIN_FONT_SIZE, MAX_FONT_SIZE);
            ImGui::GetStyle()._NextFrameFontSizeBase = m_options.font_size;
        }
    }

    static char signal_name_filter[256] = "";
    if (ImGui::CollapsingHeader("Create custom signal")) {
        showCustomSignalCreator();
    }
    ImGui::InputText("Filter", signal_name_filter, IM_ARRAYSIZE(signal_name_filter));
    ImGui::Separator();

    std::unique_ptr<CsvFileData>* file_to_remove = nullptr;
    for (auto& file : m_csv_data) {
        if (file->signals.size() == 0) {
            continue;
        }

        // Reload file if it has been rewritten. Wait that file has not been modified in the last 2 seconds
        // in case it is still being written
        if (std::filesystem::exists(file->name)) {
            auto last_write_time = std::filesystem::last_write_time(file->name);
            auto write_time_plus_2s = std::chrono::clock_cast<std::chrono::system_clock>(last_write_time) + std::chrono::seconds(2);
            auto now = std::chrono::system_clock::now();

            if (last_write_time != file->write_time
                && now > write_time_plus_2s
                && file->write_time != std::filesystem::file_time_type()) {
                std::unique_ptr<CsvFileData> csv_data = parseCsvData(file->name);
                if (csv_data && csv_data->signals.size() == file->signals.size()) {
                    for (int i = 0; i < file->signals.size(); ++i) {
                        for (ScalarPlot& plot : m_scalar_plots) {
                            if (contains(plot.signals, &file->signals[i])) {
                                plot.addSignal(&csv_data->signals[i]);
                                if (!m_options.keep_old_signals_on_reload) {
                                    plot.removeSignal(&file->signals[i]);
                                }
                            }
                        }
                    }
                    // Set write time to default so that the file gets reloaded again for the latest dataset
                    file->write_time = std::filesystem::file_time_type();
                    file->run_number++;
                    csv_data->run_number = file->run_number;
                    file->displayed_name += " " + std::to_string(file->run_number);
                    m_csv_data.push_back(std::move(csv_data));
                    break;
                }
            }
        }

        bool opened = ImGui::TreeNode(file->displayed_name.c_str());
        // Make displayed name editable
        if (ImGui::BeginPopupContextItem((file->displayed_name + "context_menu").c_str())) {
            static std::string displayed_name_edit = file->displayed_name;
            displayed_name_edit.reserve(MAX_NAME_LENGTH);
            displayed_name_edit = file->displayed_name;
            if (ImGui::InputText("Name##scalar_context_menu",
                                 displayed_name_edit.data(),
                                 MAX_NAME_LENGTH,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                file->displayed_name = std::string(displayed_name_edit.data());
                if (file->displayed_name.empty()) {
                    file->displayed_name = file->name;
                }
            }
            ImGui::InputDouble("X-axis shift", &file->x_axis_shift, 0, 0, "%g");
            if (ImGui::Button("Add same signals to plots")) {
                for (auto& signal : file->signals) {
                    for (ScalarPlot& plot : m_scalar_plots) {
                        for (CsvSignal* plot_signal : plot.signals) {
                            if (plot_signal->name == signal.name) {
                                plot.addSignal(&signal);
                                break;
                            }
                        }
                    }
                }
            }
            if (ImGui::Button("Remove signals from plots")) {
                for (auto& signal : file->signals) {
                    for (ScalarPlot& plot : m_scalar_plots) {
                        plot.removeSignal(&signal);
                    }
                    for (VectorPlot& plot : m_vector_plots) {
                        plot.removeSignal(&signal);
                    }
                    for (SpectrumPlot& plot : m_spectrum_plots) {
                        plot.removeSignal(&signal);
                    }
                }
            }
            if (ImGui::Button("Save as CSV")) {
                nfdchar_t* out_path = NULL;
                auto cwd = std::filesystem::current_path();
                nfdresult_t result = NFD_SaveDialog("csv", cwd.string().c_str(), &out_path);
                if (result == NFD_OKAY) {
                    std::string out(out_path);
                    if (!out.ends_with(".csv")) {
                        out += ".csv";
                    }
                    free(out_path);
                    std::vector<std::string> header;
                    std::vector<std::vector<double>> samples;
                    for (CsvSignal& signal : file->signals) {
                        header.push_back(signal.name);
                        samples.push_back(signal.samples);
                    }
                    saveAsCsv(out, header, samples);
                }
            }
            if (ImGui::Button("Remove file")) {
                file_to_remove = &file;
            }
            ImGui::EndPopup();
        }

        if (opened) {
            for (CsvSignal& signal : file->signals) {
                // Skip signal if it doesn't match the filter
                if (!std::string(signal_name_filter).empty()
                    && !fts::fuzzy_match_simple(signal_name_filter, signal.name.c_str())) {
                    continue;
                }

                // Use default scale if signal has no scale
                double signal_scale = 1;
                if (m_signal_scales.contains(signal.name)) {
                    signal_scale = *str::evaluateExpression(m_signal_scales.at(signal.name));
                }
                ImGui::PushStyleColor(ImGuiCol_Text, signal_scale == 1 ? ImGui::GetStyle().Colors[ImGuiCol_Text] : COLOR_GRAY);
                // Highlight selected/plotted signals
                bool selected = contains(m_selected_signals, &signal);
                for (ScalarPlot& plot : m_scalar_plots) {
                    if (contains(plot.signals, &signal)) {
                        selected = true;
                        break;
                    }
                }
                if (ImGui::Selectable(signal.name.c_str(), &selected)) {
                    // Always drag and dropping is tedious. Add signal to plot if it is selected while
                    // pressing a number to make it easier to add signals with same name from different
                    // files to same plot by selecting them while pressing the number of the plot
                    std::optional<int> pressed_number = pressedNumber();
                    if (pressed_number) {
                        m_scalar_plots[*pressed_number].addSignal(&signal);
                    }

                    // Select two signals with ctrl-click for dragging to vector plot
                    if (ImGui::GetIO().KeyCtrl) {
                        m_selected_signals.push_back(&signal);
                    } else if (!pressed_number) {
                        m_selected_signals.clear();
                        for (ScalarPlot& plot : m_scalar_plots) {
                            plot.removeSignal(&signal);
                        }
                    }
                }
                ImGui::PopStyleColor();

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    CsvSignal* p = &signal;
                    ImGui::SetDragDropPayload("CSV", &p, sizeof(CsvSignal*));
                    ImGui::Text("Drag to plot");
                    ImGui::EndDragDropSource();
                }

                if (m_selected_signals.size() == 2
                    && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    // Payload is not used
                    ImGui::SetDragDropPayload("CSV_Vector", NULL, 0);
                    ImGui::Text("Drag to vector plot");
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginPopupContextItem((file->displayed_name + signal.name + "context_menu").c_str())) {
                    std::string scale_str = "1";
                    if (m_signal_scales.contains(signal.name)) {
                        scale_str = m_signal_scales.at(signal.name);
                    }
                    scale_str.reserve(1024);
                    if (ImGui::InputText("Scale", scale_str.data(), scale_str.capacity(), ImGuiInputTextFlags_EnterReturnsTrue)) {
                        scale_str = std::string(scale_str.data());
                        auto scale = str::evaluateExpression(scale_str);
                        if (scale.has_value()) {
                            m_signal_scales[signal.name] = scale_str;
                        } else {
                            m_error_message = scale.error();
                        }
                    }

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
        // Remove signal from all scalar, vector and spectrum plots
        for (ScalarPlot& plot : m_scalar_plots) {
            for (CsvSignal& signal : (*file_to_remove)->signals) {
                plot.removeSignal(&signal);
            }
        }
        for (VectorPlot& plot : m_vector_plots) {
            for (CsvSignal& signal : (*file_to_remove)->signals) {
                plot.removeSignal(&signal);
            }
        }
        for (SpectrumPlot& plot : m_spectrum_plots) {
            for (CsvSignal& signal : (*file_to_remove)->signals) {
                plot.removeSignal(&signal);
            }
        }

        remove(m_csv_data, *file_to_remove);
    }
}

void CsvPlotter::showScalarPlots() {
    // Show vertical line at same time in all plots if mouse is hovered in any plot
    static double vertical_line_time_next = 0;
    double vertical_line_time = vertical_line_time_next;
    vertical_line_time_next = NAN;
    bool aligned = ImPlot::BeginAlignedPlots("AlignedGroup");
    for (int plot_idx = 0; plot_idx < m_rows * m_cols; ++plot_idx) {
        ScalarPlot& plot = m_scalar_plots[plot_idx];
        ImGui::Begin(std::format("Plot {}", plot_idx).c_str());
        bool autofit_x_axis = (m_x_axis == AUTOFIT_AXIS);
        bool fit_data = plot.autofit_next_frame;
        if (plot.autofit_next_frame || autofit_x_axis) {
            ImPlot::SetNextAxesToFit();
            plot.autofit_next_frame = false;
        }
        if (m_options.autofit_y_axis) {
            ImPlot::SetNextAxisToFit(ImAxis_Y1);
        }

        // Calculate the longest name for table column width
        size_t longest_name_length = 1;
        size_t longest_file_length = 1;
        for (CsvSignal* signal : plot.signals) {
            longest_name_length = std::max(longest_name_length, signal->name.size());
            longest_file_length = std::max(longest_file_length, signal->file->displayed_name.size());
        }

        if (m_options.cursor_measurements) {
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

                for (int i = 0; i < plot.signals.size(); ++i) {
                    CsvSignal* signal = plot.signals[i];
                    std::vector<double> const& all_x_values = m_options.first_signal_as_x ? signal->file->signals[0].samples : ASCENDING_NUMBERS;
                    std::vector<double> const& all_y_values = signal->samples;
                    int idx1 = binarySearch(all_x_values, m_drag_x1 - signal->file->x_axis_shift, 0, int(all_x_values.size() - 1));
                    int idx2 = binarySearch(all_x_values, m_drag_x2 - signal->file->x_axis_shift, 0, int(all_x_values.size() - 1));
                    double signal_scale = 1;
                    if (m_signal_scales.contains(signal->name)) {
                        signal_scale = *str::evaluateExpression(m_signal_scales.at(signal->name));
                    }
                    double y1 = all_y_values[idx1] * signal_scale;
                    double y2 = all_y_values[idx2] * signal_scale;

                    std::stringstream ss;
                    ss << std::left << std::setw(longest_name_length) << signal->name << " | " << signal->file->displayed_name;
                    std::string displayed_signal_name = ss.str();

                    ImVec4 color = ImPlot::GetColormapColor(i);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(color, displayed_signal_name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(color, std::format("{:g}", y1).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(color, std::format("{:g}", y2).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(color, std::format("{:g}", y2 - y1).c_str());
                }
                ImGui::EndTable();
            }
        }

        // Reset plot colors if there are no signals in it
        if (plot.signals.size() == 0 || m_flags.reset_colors) {
            ImPlot::BustColorCache("##DND");
        }

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
        if (ImPlot::BeginPlot("##DND", ImVec2(-1, -1))) {
            ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_None);
            if (m_options.link_axis) {
                ImPlot::SetupAxisLinks(ImAxis_X1, &m_x_axis.min, &m_x_axis.max);
                if (!autofit_x_axis) {
                    ImPlot::SetupAxisLimits(ImAxis_X1, m_x_axis.min, m_x_axis.max);
                }
            }

            if (ImPlot::BeginDragDropTargetPlot()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CSV")) {
                    CsvSignal* sig = *(CsvSignal**)payload->Data;
                    plot.addSignal(sig);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("LEGEND")) {
                    std::pair<ScalarPlot*, CsvSignal*> plot_and_signal = *(std::pair<ScalarPlot*, CsvSignal*>*)payload->Data;
                    ScalarPlot* original_plot = plot_and_signal.first;
                    CsvSignal* signal = plot_and_signal.second;
                    if (&plot != original_plot) {
                        plot.addSignal(signal);
                        original_plot->removeSignal(signal);
                    }
                }
                ImPlot::EndDragDropTarget();
            }

            CsvSignal* signal_to_remove = nullptr;
            for (CsvSignal* signal : plot.signals) {
                // Collect only values that are within plot range so that the autofit fits to the plotted values instead
                // of the entire data
                ImPlotRect limits = ImPlot::GetPlotLimits();
                if (autofit_x_axis) {
                    limits.X.Min = -INFINITY;
                    limits.X.Max = INFINITY;
                }
                std::vector<double> const& all_x_values = m_options.first_signal_as_x ? signal->file->signals[0].samples : ASCENDING_NUMBERS;
                std::vector<double> const& all_y_values = signal->samples;
                double x_offset = m_options.shift_samples_to_start_from_zero ? all_x_values[0] : 0;
                x_offset -= signal->file->x_axis_shift;
                std::pair<int32_t, int32_t> indices = getTimeIndices(all_x_values, limits.X.Min + x_offset, limits.X.Max + x_offset);
                indices.second = std::min(all_y_values.size() - 1, (size_t)indices.second);
                std::vector<double> x_samples_in_range(all_x_values.begin() + indices.first, all_x_values.begin() + indices.second);
                std::vector<double> y_samples_in_range(all_y_values.begin() + indices.first, all_y_values.begin() + indices.second);
                if (fit_data) {
                    x_samples_in_range = all_x_values;
                    y_samples_in_range = all_y_values;
                    if (y_samples_in_range.size() < x_samples_in_range.size()) {
                        x_samples_in_range.resize(y_samples_in_range.size());
                    }
                }

                // Scale samples. Set default scale if signal has no scale
                double signal_scale = 1;
                if (m_signal_scales.contains(signal->name)) {
                    signal_scale = *str::evaluateExpression(m_signal_scales.at(signal->name));
                }
                // Shift x-axis and scale y-axis
                for (int i = 0; i < y_samples_in_range.size(); ++i) {
                    x_samples_in_range[i] -= x_offset;
                    y_samples_in_range[i] *= signal_scale;
                }

                // Decimate values because plotting very large amount of samples is slow and the GUI becomes unresponsive
                DecimatedValues plotted_values = decimateValues(x_samples_in_range, y_samples_in_range, MAX_PLOT_SAMPLE_COUNT);

                std::stringstream ss;
                ss << std::left << std::setw(longest_name_length) << signal->name << " | " << signal->file->displayed_name;
                std::string label_id = std::format("{}###{}", ss.str(), signal->name + signal->file->displayed_name);
                ImPlot::PlotLine(label_id.c_str(),
                                 plotted_values.x.data(),
                                 plotted_values.y_min.data(),
                                 int(plotted_values.x.size()),
                                 ImPlotLineFlags_None);
                ImVec4 line_color = ImPlot::GetLastItemColor();
                ImPlot::PlotLine(label_id.c_str(),
                                 plotted_values.x.data(),
                                 plotted_values.y_max.data(),
                                 int(plotted_values.x.size()),
                                 ImPlotLineFlags_None);
                ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.4f);
                ImPlot::PlotShaded(label_id.c_str(),
                                   plotted_values.x.data(),
                                   plotted_values.y_min.data(),
                                   plotted_values.y_max.data(),
                                   int(plotted_values.x.size()),
                                   ImPlotLineFlags_None);

                // Tooltip
                if (ImPlot::IsPlotHovered() && y_samples_in_range.size() > 0) {
                    ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                    ImPlot::PushStyleColor(ImPlotCol_Line, COLOR_TOOLTIP_LINE);
                    ImPlot::PlotInfLines("##", &mouse.x, 1);
                    ImPlot::PopStyleColor();
                    vertical_line_time_next = mouse.x;
                    ImGui::BeginTooltip();
                    int idx = binarySearch(x_samples_in_range, mouse.x, 0, int(y_samples_in_range.size() - 1));
                    ss.str("");
                    ss << signal->name << " : " << y_samples_in_range[idx];
                    ImGui::PushStyleColor(ImGuiCol_Text, line_color);
                    ImGui::Text(ss.str().c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndTooltip();
                } else if (m_options.show_vertical_line_in_all_plots && !std::isnan(vertical_line_time)) {
                    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
                    ImPlot::PlotInfLines("##", &vertical_line_time, 1);
                    ImPlot::PopStyleColor(1);
                }

                // Legend right click
                if (ImPlot::BeginLegendPopup(label_id.c_str())) {
                    if (ImGui::Button("Remove")) {
                        signal_to_remove = signal;
                    }
                    ImPlot::EndLegendPopup();
                }

                // Fit to the entire data with mouse middle button
                if (ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                    plot.autofit_next_frame = true;
                }

                // Legend items can be dragged to other plots to move the signal.
                if (ImPlot::BeginDragDropSourceItem(label_id.c_str(), ImGuiDragDropFlags_None)) {
                    std::pair<ScalarPlot*, CsvSignal*> plot_and_signal = {&plot, signal};
                    ImGui::SetDragDropPayload("LEGEND", &plot_and_signal, sizeof(plot_and_signal));
                    ImGui::Text("Drag to plot");
                    ImPlot::EndDragDropSource();
                }
            }
            if (signal_to_remove) {
                plot.removeSignal(signal_to_remove);
            }

            // Vertical lines for cursor measurements
            if (m_options.cursor_measurements) {
                ImPlotRect plot_limits = ImPlot::GetPlotLimits();
                ImPlotRange x_range = plot_limits.X;
                if (m_drag_x1 == 0 && m_drag_x2 == 0) {
                    m_drag_x1 = x_range.Min + 0.1 * x_range.Size();
                    m_drag_x2 = x_range.Max - 0.1 * x_range.Size();
                }
                ImPlot::DragLineX(0, &m_drag_x1, COLOR_TOOLTIP_LINE);
                ImPlot::DragLineX(1, &m_drag_x2, COLOR_TOOLTIP_LINE);
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
    if (aligned) {
        ImPlot::EndAlignedPlots();
    }
}

std::vector<std::unique_ptr<CsvFileData>> openCsvFromFileDialog() {
    nfdpathset_t path_set;
    std::vector<std::unique_ptr<CsvFileData>> csv_datas;
    static std::filesystem::path dir = std::filesystem::current_path();
    nfdresult_t result = NFD_OpenDialogMultiple("csv,inf", dir.string().c_str(), &path_set);
    if (result == NFD_OKAY) {
        for (int i = 0; i < path_set.count; ++i) {
            std::string out(NFD_PathSet_GetPath(&path_set, i));
            dir = std::filesystem::path(out).parent_path();
            auto csv_data = parseCsvData(out);
            if (csv_data) {
                csv_datas.push_back(std::move(csv_data));
            }
        }
        NFD_PathSet_Free(&path_set);
    } else if (result == NFD_ERROR) {
        std::cerr << NFD_GetError() << std::endl;
    }
    return csv_datas;
}

void setLayout(ImGuiID main_dock, int rows, int cols, float signals_window_width) {
    // Remove the existing main dock node and all its subnodes to be able to split it
    ImGui::DockBuilderRemoveNode(main_dock);
    // Create new main dock
    main_dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    ImGuiID docks[MAX_PLOTS][MAX_PLOTS]{};
    ImGuiID dock_signals = 0;
    ImGui::DockBuilderSplitNode(main_dock, ImGuiDir_Right, 1.0f - signals_window_width, &docks[0][0], &dock_signals);
    // Split the grid nodes
    for (int row = 0; row < rows; ++row) {
        float row_height = 1.0f / (rows - row);
        // Dont split the last row
        if (row < rows - 1) {
            ImGui::DockBuilderSplitNode(docks[row][0], ImGuiDir_Up, row_height, &docks[row][0], &docks[row + 1][0]);
        }
        for (int col = 0; col < cols - 1; ++col) {
            float col_width = 1.0f / (cols - col);
            ImGui::DockBuilderSplitNode(docks[row][col], ImGuiDir_Left, col_width, &docks[row][col], &docks[row][col + 1]);
        }
    }

    // Dock the windows to correct node
    if (dock_signals != 0) {
        ImGui::DockBuilderDockWindow("Signals", dock_signals);
    }
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            ImGui::DockBuilderDockWindow(std::format("Plot {}", row * cols + col).c_str(), docks[row][col]);
        }
    }
    ImGui::DockBuilderFinish(main_dock);
}

std::optional<int> pressedNumber() {
    for (int key = ImGuiKey_0; key <= ImGuiKey_9; key++) {
        if (ImGui::IsKeyDown(ImGuiKey(key))) {
            return key - ImGuiKey_0;
        }
    }
    for (int key = ImGuiKey_Keypad0; key <= ImGuiKey_Keypad9; key++) {
        if (ImGui::IsKeyDown(ImGuiKey(key))) {
            return key - ImGuiKey_Keypad0;
        }
    }
    return std::nullopt;
}

std::tuple<int, int> getAutoLayout(int signal_count) {
    switch (signal_count) {
        case 1: return {1, 1};
        case 2: return {2, 1};
        case 3: return {3, 1};
        case 4: return {2, 2};
        case 5: return {5, 1};
        case 6: return {3, 2};
        case 7: return {4, 2};
        case 8: return {4, 2};
        case 9: return {3, 3};
        case 10: return {5, 2};
        case 11: return {6, 2};
        case 12: return {6, 2};
        case 13: return {5, 3};
        case 14: return {5, 3};
        case 15: return {5, 3};
        case 16: return {4, 4};
        case 17: return {6, 3};
        case 18: return {6, 3};
        case 19: return {5, 4};
        case 20: return {5, 4};
        case 21: return {7, 3};
        case 22: return {6, 4};
        case 23: return {6, 4};
        case 24: return {6, 4};
        case 25: return {5, 5};
        case 26: return {7, 4};
        case 27: return {7, 4};
        case 28: return {7, 4};
        case 29: return {6, 5};
        case 30: return {6, 5};
        case 32: return {8, 4};
        case 35: return {7, 5};
        case 36: return {9, 4};
        case 40: return {8, 5};
        case 42: return {7, 4};
        case 45: return {9, 5};
        default:
            return {(int)std::ceil(signal_count / 6.0), 6};
    }
}
