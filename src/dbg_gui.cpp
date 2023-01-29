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

#include "dbg_gui.h"
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

DbgGui::DbgGui(double sampling_time)
    : m_sampling_time(sampling_time) {
    assert(sampling_time > 0);
}

DbgGui::~DbgGui() {
    close();
}

void DbgGui::startUpdateLoop() {
    m_gui_thread = std::jthread(&DbgGui::updateLoop, std::ref(*this));
}

void DbgGui::synchronizeSpeed() {
    using namespace std::chrono;
    static double sync_interval = 30e-3;
    static auto last_real_timestamp = std::chrono::system_clock::now();
    static double last_timestamp = m_timestamp;
    static std::future<void> tick;

    if (m_timestamp > m_next_sync_timestamp) {
        // Wait until next tick
        if (tick.valid()) {
            tick.wait();
        }
        tick = std::async(std::launch::async,
                          []() {
                              std::this_thread::sleep_for(std::chrono::milliseconds(30));
                          });

        m_next_sync_timestamp = m_timestamp + sync_interval * m_simulation_speed;
        // Limit sync interval to 1 second in case simulation speed is set very high
        m_next_sync_timestamp = std::min(m_timestamp + 1, m_next_sync_timestamp);

        auto now = std::chrono::system_clock::now();
        auto real_time_us = std::chrono::duration_cast<microseconds>(now - last_real_timestamp).count();
        real_time_us = std::max(real_time_us, 1ll);
        double real_time_s = real_time_us * 1e-6;
        last_real_timestamp = now;

        // Adjust the sync interval for more accurate synchronization
        double simulation_speed = (m_timestamp - last_timestamp) / real_time_s;
        double sync_interval_ki = 1e-2;
        sync_interval += sync_interval_ki * (m_simulation_speed - simulation_speed);
        sync_interval = std::clamp(sync_interval, 1e-3, 100e-3);

        last_timestamp = m_timestamp;
    }
}

void DbgGui::sample() {
    m_timestamp += m_sampling_time;
    sampleWithTimestamp(m_timestamp);
}

void DbgGui::sampleWithTimestamp(double timestamp) {
    m_timestamp = timestamp;
    // Wait in infinitely loop while paused
    while (m_paused || !m_initialized) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Set sync time to 0 so that if speed is changed while paused, it will
        // be effective immediately. Otherwise simulation could run for e.g. 10ms
        // before new speed is taken into use
        m_next_sync_timestamp = 0;
    }

    if (m_time_until_pause > 0) {
        m_time_until_pause -= m_sampling_time;
        m_paused = m_time_until_pause <= 0;
        m_time_until_pause = std::max(m_time_until_pause, 0.0);
    }

    {
        std::scoped_lock<std::mutex> lock(m_sampling_mutex);
        for (auto& signal : m_scalars) {
            if (signal->buffer != nullptr) {
                // Sampling is done with unscaled value and the signal is scaled when retrieving samples
                signal->buffer->addPoint(m_timestamp, signal->getValue());
            }
            if (signal->m_pause_triggers.size() > 0) {
                double value = signal->getScaledValue();
                m_paused = m_paused || signal->checkTriggers(value);
            }
        }
    }

    synchronizeSpeed();
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
    m_window = glfwCreateWindow(1280, 720, "DbgGui", NULL, NULL);
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

    if (std::filesystem::exists(COUSINE_REGULAR_FONT)) {
        io.Fonts->AddFontFromFileTTF(COUSINE_REGULAR_FONT, 13.0f);
    }
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
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();

        //---------- Hotkeys ----------
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::IsAnyItemActive()) {
            m_paused = !m_paused;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) && !ImGui::IsAnyItemActive()) {
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
            showSpectrumPlots();
        }
        updateSavedSettings();
        setInitialFocus();

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
            m_settings = nlohmann::json::parse(f);
            int xpos = std::max(0, int(m_settings["window"]["xpos"]));
            int ypos = std::max(0, int(m_settings["window"]["ypos"]));
            glfwSetWindowPos(m_window, xpos, ypos);
            glfwSetWindowSize(m_window, m_settings["window"]["width"], m_settings["window"]["height"]);
            m_options.x_tick_labels = m_settings["options"]["x_tick_labels"];
            m_options.pause_on_close = m_settings["options"]["pause_on_close"];
            m_options.link_scalar_x_axis = m_settings["options"]["link_scalar_x_axis"];
            m_linked_scalar_x_axis_range = m_settings["options"]["linked_scalar_x_axis_range"];

            m_scalar_window_focus.initial_focus = m_settings["initial_focus"]["scalars"];
            m_vector_window_focus.initial_focus = m_settings["initial_focus"]["vectors"];
            m_configuration_window_focus.focused = m_settings["initial_focus"]["configuration"];

            for (auto symbol : m_settings["scalar_symbols"]) {
                VariantSymbol* sym = m_dbghelp_symbols.getSymbol(symbol["name"]);
                if (sym && (sym->getType() == VariantSymbol::Type::Arithmetic || sym->getType() == VariantSymbol::Type::Enum)) {
                    addScalarSymbol(sym, symbol["group"]);
                }
            }

            for (auto symbol : m_settings["vector_symbols"]) {
                VariantSymbol* sym_x = m_dbghelp_symbols.getSymbol(symbol["x"]);
                VariantSymbol* sym_y = m_dbghelp_symbols.getSymbol(symbol["y"]);
                if (sym_x && sym_y) {
                    addVectorSymbol(sym_x, sym_y, symbol["group"]);
                }
            }

            for (auto scalar_plot_data : m_settings["scalar_plots"]) {
                ScalarPlot& plot = m_scalar_plots.emplace_back();
                plot.name = scalar_plot_data["name"];
                plot.x_axis.min = 0;
                plot.x_axis.max = scalar_plot_data["x_range"];
                plot.autofit_y = scalar_plot_data["autofit_y"];
                plot.show_tooltip = scalar_plot_data["show_tooltip"];
                if (!plot.autofit_y) {
                    plot.y_axis.min = scalar_plot_data["y_min"];
                    plot.y_axis.max = scalar_plot_data["y_max"];
                }
                plot.x_range = scalar_plot_data["x_range"];

                for (size_t id : scalar_plot_data["signals"]) {
                    Scalar* scalar = getScalar(id);
                    if (scalar) {
                        plot.addSignalToPlot(scalar);
                    }
                }
                plot.initial_focus = scalar_plot_data["initial_focus"];
            }

            for (auto vector_plot_data : m_settings["vector_plots"]) {
                VectorPlot& plot = m_vector_plots.emplace_back();
                plot.name = vector_plot_data["name"];
                plot.time_range = vector_plot_data["time_range"];
                for (size_t id : vector_plot_data["signals"]) {
                    Vector2D* vec = getVector(id);
                    if (vec) {
                        plot.addSignalToPlot(vec);
                    }
                }
                plot.initial_focus = vector_plot_data["initial_focus"];
            }

            for (auto spec_plot_data : m_settings["spec_plots"]) {
                SpectrumPlot& plot = m_spectrum_plots.emplace_back();
                plot.name = spec_plot_data["name"];
                plot.time_range = spec_plot_data["time_range"];
                plot.logarithmic_y_axis = spec_plot_data["logarithmic_y_axis"];
                plot.window = spec_plot_data["window"];
                plot.x_axis.min = spec_plot_data["x_axis_min"];
                plot.x_axis.max = spec_plot_data["x_axis_max"];
                plot.y_axis.min = spec_plot_data["y_axis_min"];
                plot.y_axis.max = spec_plot_data["y_axis_max"];
                if (spec_plot_data.contains("id")) {
                    size_t id = spec_plot_data["id"];
                    Scalar* scalar = getScalar(id);
                    Vector2D* vector = getVector(id);
                    if (scalar) {
                        plot.addSignalToPlot(scalar);
                    } else if (vector) {
                        plot.addSignalToPlot(vector);
                    }
                }
                plot.initial_focus = spec_plot_data["initial_focus"];
            }

            for (auto& scalar_data : m_settings["scalars"]) {
                size_t id = scalar_data["id"];
                Scalar* scalar = getScalar(id);
                if (scalar) {
                    scalar->scale = scalar_data["scale"];
                    scalar->offset = scalar_data["offset"];
                    if (scalar_data.contains("alias")) {
                        scalar->alias = scalar_data["alias"];
                        scalar->alias_and_group = scalar->alias + " (" + scalar->group + ")";
                    }
                }
            }

            for (auto custom_window_data : m_settings["custom_windows"]) {
                CustomWindow& custom_window = m_custom_windows.emplace_back();
                custom_window.name = custom_window_data["name"];
                for (size_t id : custom_window_data["signals"]) {
                    Scalar* scalar = getScalar(id);
                    if (scalar) {
                        custom_window.scalars.push_back(scalar);
                    }
                }
                custom_window.initial_focus = custom_window_data["initial_focus"];
            }

            std::string group_to_add_symbols = m_settings["group_to_add_symbols"];
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
    m_settings["window"]["width"] = width;
    m_settings["window"]["height"] = height;
    m_settings["window"]["xpos"] = xpos;
    m_settings["window"]["ypos"] = ypos;
    m_settings["options"]["x_tick_labels"] = m_options.x_tick_labels;
    m_settings["options"]["pause_on_close"] = m_options.pause_on_close;
    m_settings["options"]["link_scalar_x_axis"] = m_options.link_scalar_x_axis;
    m_settings["options"]["linked_scalar_x_axis_range"] = m_linked_scalar_x_axis_range;
    m_settings["initial_focus"]["scalars"] = m_scalar_window_focus.focused;
    m_settings["initial_focus"]["vectors"] = m_vector_window_focus.focused;
    m_settings["initial_focus"]["configuration"] = m_configuration_window_focus.focused;

    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        if (!scalar_plot.open) {
            m_settings["scalar_plots"].erase(scalar_plot.name);
            continue;
        }
        m_settings["scalar_plots"][scalar_plot.name]["initial_focus"] = scalar_plot.focused;
        m_settings["scalar_plots"][scalar_plot.name]["name"] = scalar_plot.name;
        m_settings["scalar_plots"][scalar_plot.name]["x_range"] = scalar_plot.x_range;
        m_settings["scalar_plots"][scalar_plot.name]["autofit_y"] = scalar_plot.autofit_y;
        m_settings["scalar_plots"][scalar_plot.name]["show_tooltip"] = scalar_plot.show_tooltip;
        for (Scalar* signal : scalar_plot.signals) {
            // Update range only if autofit is not on because otherwise the file
            // could be continously rewritten when autofit range changes
            if (!scalar_plot.autofit_y) {
                m_settings["scalar_plots"][scalar_plot.name]["y_min"] = scalar_plot.y_axis.min;
                m_settings["scalar_plots"][scalar_plot.name]["y_max"] = scalar_plot.y_axis.max;
            }
            m_settings["scalar_plots"][scalar_plot.name]["signals"][signal->name_and_group] = signal->id;
        }
    }

    for (VectorPlot& vector_plot : m_vector_plots) {
        if (!vector_plot.open) {
            m_settings["vector_plots"].erase(vector_plot.name);
            continue;
        }
        m_settings["vector_plots"][vector_plot.name]["initial_focus"] = vector_plot.focused;
        m_settings["vector_plots"][vector_plot.name]["name"] = vector_plot.name;
        m_settings["vector_plots"][vector_plot.name]["time_range"] = vector_plot.time_range;
        for (Vector2D* signal : vector_plot.signals) {
            m_settings["vector_plots"][vector_plot.name]["signals"][signal->name_and_group] = signal->id;
        }
    }

    for (SpectrumPlot& spec_plot : m_spectrum_plots) {
        if (!spec_plot.open) {
            m_settings["spec_plots"].erase(spec_plot.name);
            continue;
        }
        m_settings["spec_plots"][spec_plot.name]["initial_focus"] = spec_plot.focused;
        m_settings["spec_plots"][spec_plot.name]["name"] = spec_plot.name;
        m_settings["spec_plots"][spec_plot.name]["time_range"] = spec_plot.time_range;
        m_settings["spec_plots"][spec_plot.name]["logarithmic_y_axis"] = spec_plot.logarithmic_y_axis;
        m_settings["spec_plots"][spec_plot.name]["window"] = spec_plot.window;
        m_settings["spec_plots"][spec_plot.name]["x_axis_min"] = spec_plot.x_axis.min;
        m_settings["spec_plots"][spec_plot.name]["x_axis_max"] = spec_plot.x_axis.max;
        m_settings["spec_plots"][spec_plot.name]["y_axis_min"] = spec_plot.y_axis.min;
        m_settings["spec_plots"][spec_plot.name]["y_axis_max"] = spec_plot.y_axis.max;
        if (spec_plot.scalar) {
            m_settings["spec_plots"][spec_plot.name]["id"] = spec_plot.scalar->id;
        } else if (spec_plot.vector) {
            m_settings["spec_plots"][spec_plot.name]["id"] = spec_plot.vector->id;
        }
    }

    for (CustomWindow& custom_window : m_custom_windows) {
        if (!custom_window.open) {
            m_settings["custom_windows"].erase(custom_window.name);
            continue;
        }
        m_settings["custom_windows"][custom_window.name]["initial_focus"] = custom_window.focused;
        m_settings["custom_windows"][custom_window.name]["name"] = custom_window.name;
        for (Scalar* scalar : custom_window.scalars) {
            // use group first in key so that the signals are sorted alphabetically by group
            m_settings["custom_windows"][custom_window.name]["signals"][scalar->group + " " + scalar->name] = scalar->id;
        }
    }

    for (auto& scalar : m_scalars) {
        if (!scalar->deleted) {
            m_settings["scalars"][scalar->name_and_group]["id"] = scalar->id;
            m_settings["scalars"][scalar->name_and_group]["scale"] = scalar->scale;
            m_settings["scalars"][scalar->name_and_group]["offset"] = scalar->offset;
            m_settings["scalars"][scalar->name_and_group]["alias"] = scalar->alias;
        }
    }

    size_t ini_settings_size = 0;
    std::string ini_settings = ImGui::SaveIniSettingsToMemory(&ini_settings_size);
    m_settings["group_to_add_symbols"] = m_group_to_add_symbols;
    static nlohmann::json m_settings_saved = m_settings;
    static std::string m_ini_settings_saved = ini_settings;
    if (m_settings != m_settings_saved
        || ini_settings != m_ini_settings_saved) {
        m_ini_settings_saved = ini_settings;
        m_settings_saved = m_settings;

        std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.dbg_gui\\");
        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        ImGui::SaveIniSettingsToDisk((settings_dir + "imgui.ini").c_str());
        std::ofstream(settings_dir + "settings.json") << std::setw(4) << m_settings;
    }
}

void DbgGui::setInitialFocus() {
    // Set same tabs active as in previous session because the windows do not yet exists when previous session
    // settings are loaded and focus cannot be set immediately on first creation
    // Related github issues
    // https://github.com/ocornut/imgui/issues/5005 How to set active docked window?
    // https://github.com/ocornut/imgui/issues/5289 ImGui::SetWindowFocus does nothing the first frame after a window has been created
    static bool first_time = true;
    if (!first_time) {
        return;
    }
    first_time = false;

    if (m_configuration_window_focus.initial_focus) {
        ImGui::Begin("Configuration");
        ImGui::SetWindowFocus("Configuration");
        ImGui::End();
    }
    if (m_scalar_window_focus.initial_focus) {
        ImGui::Begin("Scalars");
        ImGui::SetWindowFocus("Scalars");
        ImGui::End();
    }
    if (m_vector_window_focus.initial_focus) {
        ImGui::Begin("Vectors");
        ImGui::SetWindowFocus("Vectors");
        ImGui::End();
    }

    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        ImGui::Begin(scalar_plot.name.c_str());
        if (scalar_plot.initial_focus) {
            ImGui::SetWindowFocus(scalar_plot.name.c_str());
        }
        ImGui::End();
    }
    for (VectorPlot& vector_plot : m_vector_plots) {
        ImGui::Begin(vector_plot.name.c_str());
        if (vector_plot.initial_focus) {
            ImGui::SetWindowFocus(vector_plot.name.c_str());
        }
        ImGui::End();
    }
    for (SpectrumPlot& spec_plot : m_spectrum_plots) {
        ImGui::Begin(spec_plot.name.c_str());
        if (spec_plot.initial_focus) {
            ImGui::SetWindowFocus(spec_plot.name.c_str());
        }
        ImGui::End();
    }
    for (CustomWindow& custom_window : m_custom_windows) {
        ImGui::Begin(custom_window.name.c_str());
        if (custom_window.initial_focus) {
            ImGui::SetWindowFocus(custom_window.name.c_str());
        }
        ImGui::End();
    }
}

Scalar* DbgGui::addScalarSymbol(VariantSymbol* sym, std::string const& group) {
    Scalar* scalar = addScalar(sym->getValueSource(), group, sym->getFullName());
    m_settings["scalar_symbols"][scalar->name_and_group]["name"] = scalar->name;
    m_settings["scalar_symbols"][scalar->name_and_group]["group"] = scalar->group;
    return scalar;
}

Vector2D* DbgGui::addVectorSymbol(VariantSymbol* x, VariantSymbol* y, std::string const& group) {
    Vector2D* vector = addVector(x->getValueSource(), y->getValueSource(), group, x->getFullName());
    m_settings["vector_symbols"][vector->name_and_group]["name"] = vector->name;
    m_settings["vector_symbols"][vector->name_and_group]["group"] = vector->group;
    m_settings["vector_symbols"][vector->name_and_group]["x"] = x->getFullName();
    m_settings["vector_symbols"][vector->name_and_group]["y"] = y->getFullName();
    return vector;
}

bool DbgGui::isClosed() {
    return m_initialized && (m_window == nullptr);
}

void DbgGui::close() {
    if (m_window && m_options.pause_on_close) {
        m_paused = true;
        while (m_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (m_window) {
        glfwSetWindowShouldClose(m_window, 1);
    }
    m_paused = false;
    if (m_gui_thread.joinable()) {
        m_gui_thread.join();
    }
}

Scalar* DbgGui::addScalar(ValueSource const& src, std::string group, std::string const& name) {
    if (group.empty()) {
        group = "debug";
    } else {
        group = group;
    }
    size_t id = hasher(name + " (" + group + ")");
    Scalar* existing_scalar = getScalar(id);
    if (existing_scalar != nullptr) {
        return existing_scalar;
    }
    auto& new_scalar = m_scalars.emplace_back(std::make_unique<Scalar>());
    new_scalar->src = src;
    new_scalar->name = name;
    new_scalar->group = group;
    new_scalar->alias = new_scalar->name;
    new_scalar->name_and_group = name + " (" + new_scalar->group + ")";
    new_scalar->alias_and_group = new_scalar->name_and_group;
    new_scalar->id = id;
    m_scalar_groups[new_scalar->group].push_back(new_scalar.get());
    // Sort items within the inserted group
    auto& inserted_group = m_scalar_groups[new_scalar->group];
    std::sort(inserted_group.begin(), inserted_group.end(), [](Scalar* a, Scalar* b) { return a->name < b->name; });
    return new_scalar.get();
}

Vector2D* DbgGui::addVector(ValueSource const& x, ValueSource const& y, std::string group, std::string const& name) {
    if (group.empty()) {
        group = "debug";
    } else {
        group = group;
    }
    size_t id = hasher(name + " (" + group + ")");
    Vector2D* existing_vector = getVector(id);
    if (existing_vector != nullptr) {
        return existing_vector;
    }
    auto& new_vector = m_vectors.emplace_back(std::make_unique<Vector2D>());
    new_vector->name = name;
    new_vector->group = group;
    new_vector->name_and_group = name + " (" + group + ")";
    new_vector->id = id;
    new_vector->x = addScalar(x, group, name + ".x");
    new_vector->x->hide_from_scalars_window = true;
    new_vector->y = addScalar(y, group, name + ".y");
    new_vector->y->hide_from_scalars_window = true;
    m_vector_groups[group].push_back(new_vector.get());
    // Sort items within the inserted group
    auto& inserted_group = m_vector_groups[group];
    std::sort(inserted_group.begin(), inserted_group.end(), [](Vector2D* a, Vector2D* b) { return a->name < b->name; });
    return new_vector.get();
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

    auto& implot_style = ImPlot::GetStyle();
    implot_style.Colors[ImPlotCol_LegendBg] = ImVec4(0, 0, 0, 0);
}

void Scalar::addTrigger(double pause_level) {
    double current_value = getScaledValue();
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
