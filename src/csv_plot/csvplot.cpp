#define _CRT_SECURE_NO_WARNINGS

#include "csvplot.h"
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "scrolling_buffer.h"

#include <nlohmann/json.hpp>
#include <vector>
#include <iterator>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

void setTheme();
void updateSavedSettings(GLFWwindow& window);
void loadPreviousSessionSettings(GLFWwindow& window);
void showScalarPlot(std::vector<CsvSignal>& signals);
std::vector<CsvSignal> parseCsvData(std::string const& csv_filename);

void exit(std::string const& err) {
    std::cerr << err;
    exit(-1);
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

CsvPlot::CsvPlot(std::string const& csv) {
    m_signals = parseCsvData(csv);

    //---------- Initializations ----------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        std::abort();
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "CSV Plotter", NULL, NULL);
    if (window == NULL)
        std::abort();
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowPos(window, 0, 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport
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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    io.Fonts->AddFontFromFileTTF("../Cousine-Regular.ttf", 13.0f);
    setTheme();
    loadPreviousSessionSettings(*window);

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
        ImGui::ShowDemoWindow();
        ImPlot::ShowDemoWindow();

        //---------- Main windows ----------
        showScalarPlot(m_signals);
        updateSavedSettings(*window);

        //---------- Rendering ----------
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
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
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void loadPreviousSessionSettings(GLFWwindow& window) {
    std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.csvplot\\");
    ImGui::LoadIniSettingsFromDisk((settings_dir + "imgui.ini").c_str());

    std::ifstream f(settings_dir + "settings.json");
    if (f.is_open()) {
        try {
            auto settings = nlohmann::json::parse(f);
            int xpos = std::max(0, int(settings["window"]["xpos"]));
            int ypos = std::max(0, int(settings["window"]["ypos"]));
            glfwSetWindowPos(&window, xpos, ypos);
            glfwSetWindowSize(&window, settings["window"]["width"], settings["window"]["height"]);
        } catch (nlohmann::json::exception err) {
            std::cerr << "Failed to load previous session settings" << std::endl;
            std::cerr << err.what();
        }
    }
}

void updateSavedSettings(GLFWwindow& window) {
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
    int width, height;
    glfwGetWindowSize(&window, &width, &height);
    int xpos, ypos;
    glfwGetWindowPos(&window, &xpos, &ypos);
    settings["window"]["width"] = width;
    settings["window"]["height"] = height;
    settings["window"]["xpos"] = xpos;
    settings["window"]["ypos"] = ypos;
    static nlohmann::json settings_saved = settings;
    if (settings != settings_saved) {
        std::ofstream(settings_dir + "settings.json") << std::setw(4) << settings;
    }
}

void setTheme() {
    constexpr auto ColorFromBytes = [](uint8_t r, uint8_t g, uint8_t b) { return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f); };

    auto& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 bgColor = ColorFromBytes(25, 25, 25);
    const ImVec4 lightBgColor = ColorFromBytes(82, 82, 85);
    const ImVec4 veryLightBgColor = ColorFromBytes(90, 90, 95);

    const ImVec4 panelColor = ColorFromBytes(40, 40, 40);
    const ImVec4 panelHoverColor = ColorFromBytes(29, 151, 236);
    const ImVec4 panelActiveColor = ColorFromBytes(0, 119, 200);

    const ImVec4 textColor = ColorFromBytes(255, 255, 255);
    const ImVec4 textDisabledColor = ColorFromBytes(151, 151, 151);
    const ImVec4 borderColor = ColorFromBytes(78, 78, 78);

    colors[ImGuiCol_Text] = textColor;
    colors[ImGuiCol_TextDisabled] = textDisabledColor;
    colors[ImGuiCol_TextSelectedBg] = panelActiveColor;
    colors[ImGuiCol_WindowBg] = bgColor;
    colors[ImGuiCol_ChildBg] = bgColor;
    colors[ImGuiCol_PopupBg] = bgColor;
    colors[ImGuiCol_Border] = borderColor;
    colors[ImGuiCol_BorderShadow] = borderColor;
    colors[ImGuiCol_FrameBg] = panelColor;
    colors[ImGuiCol_FrameBgHovered] = panelHoverColor;
    colors[ImGuiCol_FrameBgActive] = panelActiveColor;
    colors[ImGuiCol_TitleBg] = bgColor;
    colors[ImGuiCol_TitleBgActive] = bgColor;
    colors[ImGuiCol_TitleBgCollapsed] = bgColor;
    colors[ImGuiCol_MenuBarBg] = panelColor;
    colors[ImGuiCol_ScrollbarBg] = panelColor;
    colors[ImGuiCol_ScrollbarGrab] = lightBgColor;
    colors[ImGuiCol_ScrollbarGrabHovered] = veryLightBgColor;
    colors[ImGuiCol_ScrollbarGrabActive] = veryLightBgColor;
    colors[ImGuiCol_CheckMark] = panelActiveColor;
    colors[ImGuiCol_SliderGrab] = panelHoverColor;
    colors[ImGuiCol_SliderGrabActive] = panelActiveColor;
    colors[ImGuiCol_Button] = panelColor;
    colors[ImGuiCol_ButtonHovered] = panelHoverColor;
    colors[ImGuiCol_ButtonActive] = panelHoverColor;
    colors[ImGuiCol_Header] = panelColor;
    colors[ImGuiCol_HeaderHovered] = panelHoverColor;
    colors[ImGuiCol_HeaderActive] = panelActiveColor;
    colors[ImGuiCol_Separator] = borderColor;
    colors[ImGuiCol_SeparatorHovered] = borderColor;
    colors[ImGuiCol_SeparatorActive] = borderColor;
    colors[ImGuiCol_ResizeGrip] = bgColor;
    colors[ImGuiCol_ResizeGripHovered] = panelColor;
    colors[ImGuiCol_ResizeGripActive] = lightBgColor;
    colors[ImGuiCol_PlotLines] = panelActiveColor;
    colors[ImGuiCol_PlotLinesHovered] = panelHoverColor;
    colors[ImGuiCol_PlotHistogram] = panelActiveColor;
    colors[ImGuiCol_PlotHistogramHovered] = panelHoverColor;
    colors[ImGuiCol_DragDropTarget] = bgColor;
    colors[ImGuiCol_NavHighlight] = lightBgColor;
    colors[ImGuiCol_DockingPreview] = panelActiveColor;
    colors[ImGuiCol_Tab] = bgColor;
    colors[ImGuiCol_TabActive] = panelActiveColor;
    colors[ImGuiCol_TabUnfocused] = bgColor;
    colors[ImGuiCol_TabUnfocusedActive] = panelActiveColor;
    colors[ImGuiCol_TabHovered] = panelHoverColor;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding = 0.0f;
}

template <typename Out>
void split(const std::string& s, char delim, Out result) {
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        *result++ = item;
    }
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

std::vector<CsvSignal> parseCsvData(std::string const& csv_filename) {
    std::ifstream csv(csv_filename);
    if (!csv.is_open()) {
        exit("Unable to open file " + csv_filename);
    }
    std::stringstream buffer;
    buffer << csv.rdbuf();
    std::vector<std::string> lines = split(buffer.str(), '\n');
    if (lines.size() == 0) {
        exit("No data in file " + csv_filename);
    }

    if (lines[0].ends_with(',')) {
        lines[0].pop_back();
    }
    std::vector<std::string> signal_names = split(lines[0], ',');

    size_t sample_cnt = lines.size() - 1;
    std::vector<CsvSignal> csv_signals;
    for (std::string signal_name : signal_names) {
        csv_signals.push_back(CsvSignal{
            .name = signal_name,
            .samples = ScrollingBuffer(sample_cnt)});
    }
    for (size_t i = 0; i < sample_cnt; ++i) {
        std::vector<std::string> values = split(lines[i + 1], ',');
        for (size_t j = 0; j < signal_names.size(); ++j) {
            csv_signals[j].samples.addPoint(double(i), std::stod(values[j]));
        }
    }

    return csv_signals;
}

template <typename T>
inline T RandomRange(T min, T max) {
    T scale = rand() / (T)RAND_MAX;
    return min + scale * (max - min);
}

ImVec4 RandomColor() {
    ImVec4 col;
    col.x = RandomRange(0.0f, 1.0f);
    col.y = RandomRange(0.0f, 1.0f);
    col.z = RandomRange(0.0f, 1.0f);
    col.w = 1.0f;
    return col;
}

void showScalarPlot(std::vector<CsvSignal>& signals) {
    ImGui::Begin("Plot##ScalarCsv");

    // child window to serve as initial source for our DND items
    ImGui::BeginChild("DND_LEFT", ImVec2(300, 400));
    static bool first_signal_as_x = true;
    ImGui::Selectable("Use first signal as x-axis", &first_signal_as_x);
    for (CsvSignal& signal : signals) {
        ImGui::Selectable(signal.name.c_str(), &signal.visible);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    // plot 1 (time series)
    ImPlotAxisFlags flags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoHighlight;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0, 0.1f));
    if (ImPlot::BeginPlot("##DND1", ImVec2(-1, -1))) {
        ImPlot::SetupAxis(ImAxis_X1, NULL, flags);

        for (CsvSignal& signal : signals) {
            if (!signal.visible) {
                continue;
            }
            double* x_data;
            if (first_signal_as_x) {
                x_data = signals[0].samples.data.data();
            } else {
                x_data = signal.samples.time.data();
            }
            ImPlot::PlotLine(signal.name.c_str(),
                             x_data,
                             signal.samples.data.data(),
                             int(signal.samples.data.size() / 2),
                             ImPlotLineFlags_None);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    ImGui::End();
}