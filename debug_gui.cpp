// Dear ImGui: standalone example application for GLFW + OpenGL 3, using
// programmable pipeline (GLFW is a cross-platform general purpose library for
// handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation,
// etc.) If you are new to Dear ImGui, read documentation from the docs/ folder
// + read the top of imgui.cpp. Read online:
// https://github.com/ocornut/imgui/tree/master/docs

#define _CRT_SECURE_NO_WARNINGS

#include "debug_gui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdio.h>
#include <type_traits>

void setTheme();

std::hash<std::string> hasher;

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

DbgGui::DbgGui() {
    strcpy_s(m_group_to_add_symbols, "debug");
}

DbgGui::~DbgGui() {
    m_gui_thread.join();
}

void DbgGui::startUpdateLoop() {
    m_gui_thread = std::thread(&DbgGui::updateLoop, std::ref(*this));
}

void DbgGui::sample(double timestamp) {
    // Wait in infinitely loop while paused
    while (m_paused || !m_initialized) {
    }

    {
        std::scoped_lock<std::mutex> lock(m_sampling_mutex);
        m_timestamp = timestamp;
        for (auto& signal : m_scalars) {
            if (signal.second->buffer != nullptr) {
                double value = getSourceValue(signal.second->src);
                signal.second->buffer->addPoint(timestamp, value);
            }
            if (signal.second->m_pause_triggers.size() > 0) {
                double value = getSourceValue(signal.second->src);
                m_paused = m_paused || signal.second->checkTriggers(value);
            }
        }
    }

    const double sync_interval = 10e-3;
    static double next_sync_timestamp = sync_interval;
    if (timestamp > next_sync_timestamp) {
        next_sync_timestamp += sync_interval * m_simulation_speed;
        // Limit sync interval to 1 second in case simulation speed is set very high
        next_sync_timestamp = std::min(timestamp + 1, next_sync_timestamp);
        auto now = std::chrono::system_clock::now();
        static auto last_timestamp = std::chrono::system_clock::now();
        auto real_elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_timestamp).count();
        std::this_thread::sleep_for(std::chrono::milliseconds(int(sync_interval * 1000) - real_elapsed_time));
        last_timestamp = std::chrono::system_clock::now();
    }
}

void DbgGui::updateLoop() {
    //---------- Initializations ----------
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        std::abort();
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    m_window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+OpenGL3 example", NULL, NULL);
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

    io.Fonts->AddFontFromFileTTF("../Cousine-Regular.ttf", 13.0f);
    setTheme();

    loadPreviousSessionSettings();
    m_initialized = true;

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
        ImGui::ShowDemoWindow();
        ImPlot::ShowDemoWindow();

        //---------- Hotkeys ----------
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::IsAnyItemActive()) {
            m_paused = !m_paused;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            m_simulation_speed *= 2.;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            m_simulation_speed /= 2.;
        }

        //---------- Main windows ----------
        {
            std::scoped_lock<std::mutex> lock(m_sampling_mutex);
            showConfigurationWindow();
            showScalarWindow();
            showVectorWindow();
            showCustomWindow();
            showSymbolsWindow();
            showScalarPlots();
            showVectorPlots();
        }
        updateSavedSettings();

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
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = nullptr;
    m_paused = false;
}

void DbgGui::loadPreviousSessionSettings() {
    std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.dbg_gui\\");
    ImGui::LoadIniSettingsFromDisk((settings_dir + "imgui.ini").c_str());
    std::ifstream f(settings_dir + "settings.json");
    if (f.is_open()) {
        try {
            m_saved_settings = nlohmann::json::parse(f);
            int xpos = std::max(0, int(m_saved_settings["window"]["xpos"]));
            int ypos = std::max(0, int(m_saved_settings["window"]["ypos"]));
            glfwSetWindowPos(m_window, xpos, ypos);
            glfwSetWindowSize(m_window, m_saved_settings["window"]["width"], m_saved_settings["window"]["height"]);

            for (auto symbol : m_saved_settings["scalar_symbols"]) {
                VariantSymbol* sym = m_dbghelp_symbols.getSymbol(symbol["name"]);
                if (sym) {
                    addScalarSymbol(sym, symbol["group"]);
                }
            }

            for (auto symbol : m_saved_settings["vector_symbols"]) {
                VariantSymbol* sym_x = m_dbghelp_symbols.getSymbol(symbol["x"]);
                VariantSymbol* sym_y = m_dbghelp_symbols.getSymbol(symbol["y"]);
                if (sym_x && sym_y) {
                    addVectorSymbol(sym_x, sym_y, symbol["group"]);
                }
            }

            for (auto scalar_plot_data : m_saved_settings["scalar_plots"]) {
                ScalarPlot& plot = m_scalar_plots.emplace_back();
                plot.name = scalar_plot_data["name"];
                plot.x_axis_min = 0;
                plot.x_axis_max = scalar_plot_data["x_range"];
                plot.autofit_y = scalar_plot_data["autofit_y"];
                if (!plot.autofit_y) {
                    plot.y_axis_min = scalar_plot_data["y_min"];
                    plot.y_axis_max = scalar_plot_data["y_max"];
                }
                plot.x_range = scalar_plot_data["x_range"];

                for (size_t id : scalar_plot_data["signals"]) {
                    if (m_scalars.contains(id)) {
                        Scalar* scalar = m_scalars[id].get();
                        plot.addSignalToPlot(scalar);
                    }
                }
            }

            for (auto vector_plot_data : m_saved_settings["vector_plots"]) {
                VectorPlot& plot = m_vector_plots.emplace_back();
                plot.name = vector_plot_data["name"];
                plot.time_range = vector_plot_data["time_range"];
                for (size_t id : vector_plot_data["signals"]) {
                    if (m_vectors.contains(id)) {
                        Vector* vec = m_vectors[id].get();
                        plot.addSignalToPlot(vec);
                    }
                }
            }

            for (auto& scalar_data : m_saved_settings["scalars"]) {
                size_t id = scalar_data["id"];
                if (m_scalars.contains(id)) {
                    Scalar* scalar = m_scalars[id].get();
                    scalar->scale = scalar_data["scale"];
                    scalar->offset = scalar_data["offset"];
                    if (scalar_data.contains("alias")) {
                        scalar->alias = scalar_data["alias"];
                        scalar->alias_and_group = scalar->alias + " (" + scalar->group + ")";
                    }
                }
            }

            for (size_t id : m_saved_settings["custom_window_signals"]) {
                if (m_scalars.contains(id)) {
                    m_custom_window_scalars.push_back(m_scalars[id].get());
                }
            }

            std::string group_to_add_symbols = m_saved_settings["group_to_add_symbols"];
            strcpy_s(m_group_to_add_symbols, group_to_add_symbols.data());
        } catch (nlohmann::json::exception err) {
            std::cerr << "Failed to load previous session settings" << std::endl;
            std::cerr << err.what();
        }
    }
    f.close();
}

void DbgGui::updateSavedSettings() {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    int xpos, ypos;
    glfwGetWindowPos(m_window, &xpos, &ypos);
    nlohmann::json settings = m_saved_settings;
    settings["window"]["width"] = width;
    settings["window"]["height"] = height;
    settings["window"]["xpos"] = xpos;
    settings["window"]["ypos"] = ypos;

    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        if (!scalar_plot.open) {
            settings["scalar_plots"].erase(scalar_plot.name);
            continue;
        }
        for (Scalar* signal : scalar_plot.signals) {
            settings["scalar_plots"][scalar_plot.name]["name"] = scalar_plot.name;
            settings["scalar_plots"][scalar_plot.name]["x_range"] = scalar_plot.x_range;
            settings["scalar_plots"][scalar_plot.name]["autofit_y"] = scalar_plot.autofit_y;
            // Update range only if autofit is not on because otherwise the file
            // could be continously rewritten when autofit range changes
            if (!scalar_plot.autofit_y) {
                settings["scalar_plots"][scalar_plot.name]["y_min"] = scalar_plot.y_axis_min;
                settings["scalar_plots"][scalar_plot.name]["y_max"] = scalar_plot.y_axis_max;
            }
            settings["scalar_plots"][scalar_plot.name]["signals"][signal->str_id] = signal->id;
        }
    }

    for (VectorPlot& vector_plot : m_vector_plots) {
        if (!vector_plot.open) {
            settings["vector_plots"].erase(vector_plot.name);
            continue;
        }
        for (Vector* signal : vector_plot.signals) {
            settings["vector_plots"][vector_plot.name]["name"] = vector_plot.name;
            settings["vector_plots"][vector_plot.name]["time_range"] = vector_plot.time_range;
            settings["vector_plots"][vector_plot.name]["signals"][signal->name_and_group] = signal->id;
        }
    }

    for (Scalar* scalar : m_custom_window_scalars) {
        settings["custom_window_signals"][scalar->group_and_name] = scalar->id;
    }

    for (auto& scalar : m_scalars) {
        if (!scalar.second->hide_from_scalars_window) {
            settings["scalars"][scalar.second->str_id]["id"] = scalar.second->id;
            settings["scalars"][scalar.second->str_id]["scale"] = scalar.second->scale;
            settings["scalars"][scalar.second->str_id]["offset"] = scalar.second->offset;
            settings["scalars"][scalar.second->str_id]["alias"] = scalar.second->alias;
        }
    }

    settings["group_to_add_symbols"] = m_group_to_add_symbols;
    if (m_saved_settings != settings || m_manual_save_settings) {
        m_saved_settings = settings;
        std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.dbg_gui\\");
        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        ImGui::SaveIniSettingsToDisk((settings_dir + "imgui.ini").c_str());
        std::ofstream(settings_dir + "settings.json") << std::setw(4) << m_saved_settings;
        m_manual_save_settings = false;
    }
}

Scalar* DbgGui::addScalarSymbol(VariantSymbol* sym, std::string const& group) {
    size_t id = addScalar(sym->getValueSource(), group, sym->getFullName());
    Scalar* scalar = m_scalars[id].get();
    m_saved_settings["scalar_symbols"][scalar->str_id]["name"] = scalar->name;
    m_saved_settings["scalar_symbols"][scalar->str_id]["group"] = scalar->group;
    m_manual_save_settings = true;
    return scalar;
}

Vector* DbgGui::addVectorSymbol(VariantSymbol* x, VariantSymbol* y, std::string const& group) {
    size_t id = addVector(x->getValueSource(), y->getValueSource(), group, x->getFullName());
    Vector* vector = m_vectors[id].get();
    m_saved_settings["vector_symbols"][vector->name_and_group]["name"] = vector->name;
    m_saved_settings["vector_symbols"][vector->name_and_group]["group"] = vector->group;
    m_saved_settings["vector_symbols"][vector->name_and_group]["x"] = x->getFullName();
    m_saved_settings["vector_symbols"][vector->name_and_group]["y"] = y->getFullName();
    m_manual_save_settings = true;
    return vector;
}

bool DbgGui::isClosed() {
    return m_initialized && (m_window == nullptr);
}

size_t DbgGui::addScalar(ValueSource const& src, std::string const& group, std::string const& name) {
    std::unique_ptr<Scalar> ptr = std::make_unique<Scalar>();
    ptr->src = src;
    if (group.empty()) {
        ptr->group = "debug";
    } else {
        ptr->group = group;
    }
    ptr->name = name;
    ptr->alias = ptr->name;
    ptr->str_id = name + " (" + ptr->group + ")";
    ptr->alias_and_group = ptr->str_id;
    ptr->group_and_name = ptr->group + " " + ptr->name;
    size_t id = hasher(ptr->str_id);
    if (m_scalars.contains(id)) {
        return id;
    }
    ptr->id = id;
    m_scalar_groups[ptr->group].push_back(ptr.get());
    // Sort items within the inserted group
    auto& inserted_group = m_scalar_groups[ptr->group];
    std::sort(inserted_group.begin(), inserted_group.end(), [](Scalar* a, Scalar* b) { return a->name < b->name; });
    m_scalars[ptr->id] = std::move(ptr);
    return id;
}

size_t DbgGui::addVector(ValueSource const& x, ValueSource const& y, std::string const& group, std::string const& name) {
    std::unique_ptr<Vector> ptr = std::make_unique<Vector>();
    ptr->name = name;
    ptr->group = group;
    ptr->name_and_group = name + " (" + group + ")";
    size_t id = hasher(ptr->name_and_group);
    if (m_vectors.contains(id)) {
        return id;
    }
    ptr->id = id;
    size_t id_x = addScalar(x, group, name + ".x");
    size_t id_y = addScalar(y, group, name + ".y");
    ptr->x = m_scalars[id_x].get();
    ptr->x->hide_from_scalars_window = true;
    ptr->y = m_scalars[id_y].get();
    ptr->y->hide_from_scalars_window = true;
    m_vector_groups[group].push_back(ptr.get());
    // Sort items within the inserted group
    auto& inserted_group = m_vector_groups[group];
    std::sort(inserted_group.begin(), inserted_group.end(), [](Vector* a, Vector* b) { return a->name < b->name; });
    m_vectors[ptr->id] = std::move(ptr);
    return id;
}

void setTheme() {
    constexpr auto ColorFromBytes = [](uint8_t r, uint8_t g, uint8_t b) { return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f); };

    auto& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 bgColor = ColorFromBytes(37, 37, 38);
    const ImVec4 lightBgColor = ColorFromBytes(82, 82, 85);
    const ImVec4 veryLightBgColor = ColorFromBytes(90, 90, 95);

    const ImVec4 panelColor = ColorFromBytes(51, 51, 55);
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

void Scalar::addTrigger(double pause_level) {
    double current_value = getSourceValue(src);
    m_pause_triggers.push_back(Trigger{
        .initial_value = current_value,
        .previous_sample = current_value,
        .pause_level = pause_level});
}

bool Scalar::checkTriggers(double value) {
    for (Trigger& trigger : m_pause_triggers) {
        // Pause when value changes if trigger value is same as initial value
        bool zero_crossed = (value - trigger.pause_level) * (trigger.previous_sample - trigger.pause_level) <= 0;
        if (value != trigger.initial_value && zero_crossed) {
            remove(m_pause_triggers, trigger);
            return true;
        }
        trigger.previous_sample = value;
    }
    return false;
}
