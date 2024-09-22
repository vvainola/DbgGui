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
#include "themes.h"
#include "str_helpers.h"
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

constexpr int SETTINGS_CHECK_INTERVAL_MS = 500;

#define TRY(expression)                       \
    try {                                     \
        expression                            \
    } catch (nlohmann::json::exception err) { \
        std::cerr << err.what() << std::endl; \
    } catch (std::exception err) {            \
        std::cerr << err.what() << std::endl; \
    }

uint64_t hash(const std::string& str) {
    uint64_t hash = 5381;
    for (size_t i = 0; i < str.size(); ++i)
        hash = 33 * hash + (uint64_t)str[i];
    return hash;
}

uint64_t hashWithTime(const std::string& str) {
    return hash(std::format("{}{}",
                            std::chrono::system_clock::now().time_since_epoch().count(),
                            str));
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

DbgGui::DbgGui(double sampling_time)
    : m_sampling_time(sampling_time),
      m_dbghelp_symbols(DbgHelpSymbols::getSymbolsFromPdb()) {
    assert(sampling_time >= 0);
}

DbgGui::~DbgGui() {
    close();
}

void DbgGui::startUpdateLoop() {
    m_gui_thread = std::jthread(&DbgGui::updateLoop, std::ref(*this));
    while (!m_initialized) {
    }
}

void DbgGui::synchronizeSpeed() {
    using namespace std::chrono;
    static double sync_interval = 30e-3;
    static auto last_real_timestamp = std::chrono::system_clock::now();
    static double last_timestamp = m_sample_timestamp;
    static std::future<void> tick;

    if (m_sample_timestamp > m_next_sync_timestamp || (tick.valid() && tick._Is_ready())) {
        // Wait until next tick
        if (tick.valid()) {
            tick.wait();
        }
        tick = std::async(std::launch::async,
                          []() {
                              std::this_thread::sleep_for(std::chrono::milliseconds(30));
                          });
        m_next_sync_timestamp = m_sample_timestamp + sync_interval * m_simulation_speed;

        auto now = std::chrono::system_clock::now();
        auto real_time_us = std::chrono::duration_cast<microseconds>(now - last_real_timestamp).count();
        real_time_us = std::max(real_time_us, 1ll);
        double real_time_s = real_time_us * 1e-6;
        last_real_timestamp = now;

        // Adjust the sync interval for more accurate synchronization
        double simulation_speed = (m_sample_timestamp - last_timestamp) / real_time_s;
        double sync_interval_ki = 1e-2;
        sync_interval += sync_interval_ki * (m_simulation_speed - simulation_speed);
        sync_interval = std::clamp(sync_interval, 1e-3, 100e-3);

        last_timestamp = m_sample_timestamp;
    }
}

void DbgGui::sample() {
    sampleWithTimestamp(m_sample_timestamp + m_sampling_time);
}

void DbgGui::sampleWithTimestamp(double timestamp) {
    // No point sampling if window has been closed
    if (isClosed()) {
        return;
    }

    { // Sample scalars
        std::scoped_lock<std::mutex> lock(m_sampling_mutex);
        if (timestamp < m_sample_timestamp) {
            m_sampler.shiftTime(timestamp - m_sample_timestamp);
            m_next_sync_timestamp = 0;
        }
        m_sample_timestamp = timestamp;
        m_sampler.sample(m_sample_timestamp);

        // Check pause triggers
        for (PauseTrigger& trigger : m_pause_triggers) {
            bool pause_triggered = trigger.check();
            if (pause_triggered) {
                remove(m_pause_triggers, trigger);
                m_paused = true;
                break;
            }
        }
    }

    if (m_pause_at_time > 0 && m_sample_timestamp >= m_pause_at_time) {
        m_pause_at_time = 0;
        m_paused = true;
    }

    // Wait in infinitely loop while paused
    while (m_paused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Set sync time to 0 so that if speed is changed while paused, it will
        // be effective immediately. Otherwise simulation could run for e.g. 10ms
        // before new speed is taken into use
        m_next_sync_timestamp = 0;
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
    extern unsigned int calibri_compressed_size;
    extern unsigned int calibri_compressed_data[];
    io.Fonts->AddFontFromMemoryCompressedTTF(calibri_compressed_data, calibri_compressed_size, m_options.font_size);
    ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->Fonts[m_options.font_selection];

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
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::IsKeyDown(ImGuiKey_LeftShift) && !ImGui::IsAnyItemActive()) {
            m_paused = !m_paused;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Space) && ImGui::IsKeyDown(ImGuiKey_LeftShift) && !ImGui::IsAnyItemActive()) {
            m_pause_at_time = std::numeric_limits<double>::epsilon();
            m_paused = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) && !ImGui::IsAnyItemActive()) {
            m_paused = !m_paused;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            m_simulation_speed *= 2.;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            m_simulation_speed /= 2.;
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadDivide)) {
            ImGui::OpenPopup(str::PAUSE_AFTER);
        } else if (ImGui::IsKeyPressed(ImGuiKey_KeypadMultiply)) {
            ImGui::OpenPopup(str::PAUSE_AT);
        } else if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_1)) {
            ImGui::OpenPopup(str::ADD_SCALAR_PLOT);
        } else if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_2)) {
            ImGui::OpenPopup(str::ADD_VECTOR_PLOT);
        } else if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_3)) {
            ImGui::OpenPopup(str::ADD_SPECTRUM_PLOT);
        } else if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_4)) {
            ImGui::OpenPopup(str::ADD_CUSTOM_WINDOW);
        } else if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_5)) {
            ImGui::OpenPopup(str::ADD_SCRIPT_WINDOW);
        } else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            saveSnapshot();
        } else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R)) {
            loadSnapshot();
        }
        addPopupModal(str::ADD_SCALAR_PLOT);
        addPopupModal(str::ADD_VECTOR_PLOT);
        addPopupModal(str::ADD_SPECTRUM_PLOT);
        addPopupModal(str::ADD_CUSTOM_WINDOW);
        addPopupModal(str::ADD_SCRIPT_WINDOW);
        addPopupModal(str::PAUSE_AFTER);
        addPopupModal(str::PAUSE_AT);

        //---------- Main windows ----------
        {
            std::scoped_lock<std::mutex> lock(m_sampling_mutex);
            m_plot_timestamp = m_sample_timestamp;
            m_sampler.emptyTempBuffers();
        }
        showDockSpaces();
        showErrorModal();
        showMainMenuBar();
        showLogWindow();
        showScalarWindow();
        showVectorWindow();
        showCustomWindow();
        showSymbolsWindow();
        showScriptWindow();
        showScalarPlots();
        showVectorPlots();
        showSpectrumPlots();
        setInitialFocus();
        updateSavedSettings();

        //---------- Rendering ----------
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(m_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        // Update and Render additional Platform Windows
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(m_window);
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

void DbgGui::restoreScalarSettings(Scalar* scalar) {
    if (!m_initialized) {
        return;
    }

    // Restore settings of the scalar signal
    TRY(for (auto& scalar_data
             : m_settings["scalars"]) {
        uint64_t id = scalar_data["id"];
        if (id == scalar->id) {
            std::string scale = scalar_data["scale"];
            scalar->setScaleStr(scale);
            std::string offset = scalar_data["offset"];
            scalar->setOffsetStr(offset);
            scalar->alias = scalar_data["alias"];
            scalar->alias_and_group = scalar->alias + " (" + scalar->group + ")";
            break;
        }
    })

    // Restore scalar to plots
    TRY(for (auto scalar_plot_data
             : m_settings["scalar_plots"]) {
        ScalarPlot* plot = nullptr;
        for (auto& scalar_plot : m_scalar_plots) {
            if (scalar_plot.name == scalar_plot_data["name"]) {
                plot = &scalar_plot;
                break;
            }
        }
        for (uint64_t id : scalar_plot_data["signals"]) {
            if (plot != nullptr && id == scalar->id) {
                m_sampler.startSampling(scalar);
                plot->addScalarToPlot(scalar);
            }
        }
    })
}

void DbgGui::loadPreviousSessionSettings() {
    std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.dbg_gui\\");
    std::ifstream f(settings_dir + "settings.json");
    if (f.is_open()) {
        TRY(m_settings = nlohmann::json::parse(f);
            m_settings_saved = m_settings;)

        if (m_settings.contains("layout")) {
            std::string layout = m_settings["layout"];
            ImGui::LoadIniSettingsFromMemory(layout.data());
        } else {
            ImGui::LoadIniSettingsFromDisk((settings_dir + "imgui.ini").c_str());
        }

        TRY(m_options.x_tick_labels = m_settings["options"]["x_tick_labels"];)
        TRY(m_options.pause_on_close = m_settings["options"]["pause_on_close"];)
        TRY(m_options.link_scalar_x_axis = m_settings["options"]["link_scalar_x_axis"];)
        TRY(m_options.scalar_plot_tooltip = m_settings["options"]["scalar_plot_tooltip"];)
        TRY(m_options.font_selection = m_settings["options"]["font_selection"];)
        TRY(m_options.show_latest_message_on_main_menu_bar = m_settings["options"]["show_latest_message_on_main_menu_bar"];)
        TRY(m_linked_scalar_x_axis_range = m_settings["options"]["linked_scalar_x_axis_range"];)
        TRY(m_options.sampling_buffer_size = m_settings["options"]["sampling_buffer_size"];)
        TRY(m_options.font_size = m_settings["options"]["font_size"];)
        TRY(m_options.theme = m_settings["options"]["theme"];)

        m_sampler.setBufferSize(m_options.sampling_buffer_size);
        setTheme(m_options.theme, m_window);

        TRY(int xpos = std::max(0, int(m_settings["window"]["xpos"]));
            int ypos = std::max(0, int(m_settings["window"]["ypos"]));
            glfwSetWindowPos(m_window, xpos, ypos);
            glfwSetWindowSize(m_window, m_settings["window"]["width"], m_settings["window"]["height"]);)

        TRY(m_window_focus.scalars.initial_focus = m_settings["initial_focus"]["scalars"];)
        TRY(m_window_focus.vectors.initial_focus = m_settings["initial_focus"]["vectors"];)
        TRY(m_window_focus.symbols.initial_focus = m_settings["initial_focus"]["symbols"];)
        TRY(m_window_focus.log.initial_focus = m_settings["initial_focus"]["log"];)

        for (auto symbol : m_settings["scalar_symbols"]) {
            TRY(
              VariantSymbol* sym = m_dbghelp_symbols.getSymbol(symbol["name"]);
              if (sym
                  && (sym->getType() == VariantSymbol::Type::Arithmetic
                      || sym->getType() == VariantSymbol::Type::Enum
                      || sym->getType() == VariantSymbol::Type::Pointer)) {
                  addScalarSymbol(sym, symbol["group"]);
              })
        }

        for (auto symbol : m_settings["vector_symbols"]) {
            TRY(
              VariantSymbol* sym_x = m_dbghelp_symbols.getSymbol(symbol["x"]);
              VariantSymbol* sym_y = m_dbghelp_symbols.getSymbol(symbol["y"]);
              if (sym_x && sym_y) {
                  addVectorSymbol(sym_x, sym_y, symbol["group"]);
              };)
        }

        for (auto dockspace_data : m_settings["dockspaces"]) {
            TRY(
              DockSpace& dockspace = m_dockspaces.emplace_back();
              dockspace.name = dockspace_data["name"];
              dockspace.id = dockspace_data["id"];
              dockspace.focus.initial_focus = dockspace_data["initial_focus"];)
        }

        for (auto scalar_plot_data : m_settings["scalar_plots"]) {
            TRY(
              ScalarPlot& plot = m_scalar_plots.emplace_back();
              plot.name = scalar_plot_data["name"];

              plot.x_axis.min = 0;
              plot.x_axis.max = scalar_plot_data["x_range"];
              plot.autofit_y = scalar_plot_data["autofit_y"];
              if (!plot.autofit_y) {
                  plot.y_axis.min = scalar_plot_data["y_min"];
                  plot.y_axis.max = scalar_plot_data["y_max"];
              };
              plot.x_range = scalar_plot_data["x_range"];

              for (uint64_t id
                   : scalar_plot_data["signals"]) {
                  Scalar* scalar = getScalar(id);
                  if (scalar) {
                      m_sampler.startSampling(scalar);
                      plot.addScalarToPlot(scalar);
                  }
              };
              plot.focus.initial_focus = scalar_plot_data["initial_focus"];
              plot.id = scalar_plot_data["id"];)
        }

        for (auto vector_plot_data : m_settings["vector_plots"]) {
            TRY(
              VectorPlot& plot = m_vector_plots.emplace_back();
              plot.name = vector_plot_data["name"];
              plot.time_range = vector_plot_data["time_range"];
              for (uint64_t id
                   : vector_plot_data["signals"]) {
                  Vector2D* vec = getVector(id);
                  if (vec) {
                      m_sampler.startSampling(vec);
                      plot.addVectorToPlot(vec);
                  }
              };
              plot.focus.initial_focus = vector_plot_data["initial_focus"];
              plot.id = vector_plot_data["id"];)
        }

        for (auto spec_plot_data : m_settings["spec_plots"]) {
            TRY(
              SpectrumPlot& plot = m_spectrum_plots.emplace_back();
              plot.name = spec_plot_data["name"];
              plot.time_range = spec_plot_data["time_range"];
              plot.logarithmic_y_axis = spec_plot_data["logarithmic_y_axis"];
              plot.window = spec_plot_data["window"];
              plot.x_axis.min = spec_plot_data["x_axis_min"];
              plot.x_axis.max = spec_plot_data["x_axis_max"];
              plot.y_axis.min = spec_plot_data["y_axis_min"];
              plot.y_axis.max = spec_plot_data["y_axis_max"];
              if (spec_plot_data.contains("signal_id")) {
                  uint64_t id = spec_plot_data["signal_id"];
                  Scalar* scalar = getScalar(id);
                  Vector2D* vector = getVector(id);
                  if (scalar) {
                      m_sampler.startSampling(scalar);
                      plot.addScalarToPlot(scalar);
                  } else if (vector) {
                      m_sampler.startSampling(vector);
                      plot.addVectorToPlot(vector);
                  }
              };
              plot.focus.initial_focus = spec_plot_data["initial_focus"];
              plot.id = spec_plot_data["id"];)
        }

        for (auto& scalar_data : m_settings["scalars"]) {
            TRY(
              uint64_t id = scalar_data["id"];
              Scalar* scalar = getScalar(id);
              if (scalar) {
                  std::string scale;
                  if (scalar_data["scale"].is_number()) {
                      scale = std::format("{:g}", double(scalar_data["scale"]));
                  } else {
                      scale = scalar_data["scale"];
                  }
                  scalar->setScaleStr(scale);
                  std::string offset;
                  if (scalar_data["offset"].is_number()) {
                      offset = std::format("{:g}", double(scalar_data["offset"]));
                  } else {
                      offset = scalar_data["offset"];
                  }
                  scalar->setOffsetStr(offset);
                  scalar->alias = scalar_data["alias"];
                  scalar->alias_and_group = scalar->alias + " (" + scalar->group + ")";
              };)
        }

        for (auto custom_window_data : m_settings["custom_windows"]) {
            TRY(
              CustomWindow& custom_window = m_custom_windows.emplace_back();
              custom_window.name = custom_window_data["name"];
              for (uint64_t id
                   : custom_window_data["signals"]) {
                  Scalar* scalar = getScalar(id);
                  if (scalar) {
                      custom_window.scalars.push_back(scalar);
                  }
              };
              custom_window.focus.initial_focus = custom_window_data["initial_focus"];
              custom_window.id = custom_window_data["id"];)
        }

        TRY(for (auto script_window_data : m_settings["script_windows"]) {
            ScriptWindow& script_window = m_script_windows.emplace_back();
            script_window.name = script_window_data["name"];
            script_window.focus.initial_focus = script_window_data["initial_focus"];
            script_window.id = script_window_data["id"];
            script_window.loop = script_window_data["loop"];
            std::string text = script_window_data["text"];
            std::memcpy((void*)script_window.text, (void*)text.data(), text.size());
            script_window.text[text.size()] = '\0';
        })

        TRY(for (std::string hidden_symbol
                 : m_settings["hidden_symbols"]) {
            m_hidden_symbols.insert(hidden_symbol);
        };)

        TRY(std::string group_to_add_symbols = m_settings["group_to_add_symbols"];
            strcpy_s(m_group_to_add_symbols, group_to_add_symbols.data());)
    }
    f.close();
}

void DbgGui::updateSavedSettings() {
    // Checking settings on every frame can be slow if there are a lot of signals. However,
    // adding a flag when to update them is noisy on the rest of the code so check with some
    // small interval that settings are fairly guaranteed to get updated if user changes
    // something but not too often to slow the GUI down.
    static auto last_check_time = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    if (now - last_check_time < std::chrono::milliseconds(SETTINGS_CHECK_INTERVAL_MS)) {
        return;
    }
    last_check_time = now;

    if (m_options.clear_saved_settings) {
        m_options.clear_saved_settings = false;
        m_settings.clear();
        m_settings_saved.clear();
        // Restore symbols
        for (auto const& scalar : m_scalars) {
            VariantSymbol* scalar_sym = m_dbghelp_symbols.getSymbol(scalar->name);
            if (scalar_sym) {
                m_settings["scalar_symbols"][scalar->name_and_group]["name"] = scalar->name;
                m_settings["scalar_symbols"][scalar->name_and_group]["group"] = scalar->group;
            }
        }
        for (auto const& vector : m_vectors) {
            VariantSymbol* x = m_dbghelp_symbols.getSymbol(vector->x->name);
            VariantSymbol* y = m_dbghelp_symbols.getSymbol(vector->y->name);
            if (x && y) {
                m_settings["vector_symbols"][vector->name_and_group]["name"] = vector->name;
                m_settings["vector_symbols"][vector->name_and_group]["group"] = vector->group;
                m_settings["vector_symbols"][vector->name_and_group]["x"] = x->getFullName();
                m_settings["vector_symbols"][vector->name_and_group]["y"] = y->getFullName();
            }
        }
    }

    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    int xpos, ypos;
    glfwGetWindowPos(m_window, &xpos, &ypos);
    if (width == 0 || height == 0) {
        return;
    }

    m_settings["window"]["width"] = width;
    m_settings["window"]["height"] = height;
    m_settings["window"]["xpos"] = xpos;
    m_settings["window"]["ypos"] = ypos;
    m_settings["options"]["x_tick_labels"] = m_options.x_tick_labels;
    m_settings["options"]["pause_on_close"] = m_options.pause_on_close;
    m_settings["options"]["link_scalar_x_axis"] = m_options.link_scalar_x_axis;
    m_settings["options"]["scalar_plot_tooltip"] = m_options.scalar_plot_tooltip;
    m_settings["options"]["show_latest_message_on_main_menu_bar"] = m_options.show_latest_message_on_main_menu_bar;
    m_settings["options"]["font_selection"] = m_options.font_selection;
    m_settings["options"]["linked_scalar_x_axis_range"] = m_linked_scalar_x_axis_range;
    m_settings["options"]["sampling_buffer_size"] = m_options.sampling_buffer_size;
    m_settings["options"]["font_size"] = m_options.font_size;
    m_settings["options"]["theme"] = m_options.theme;
    m_settings["initial_focus"]["scalars"] = m_window_focus.scalars.focused;
    m_settings["initial_focus"]["vectors"] = m_window_focus.vectors.focused;
    m_settings["initial_focus"]["symbols"] = m_window_focus.symbols.focused;
    m_settings["initial_focus"]["log"] = m_window_focus.log.focused;

    // If vector is deleted, mark the scalars as also deleted but don't delete them for real
    // yet before they are removed from all other structures
    for (int i = int(m_vectors.size() - 1); i >= 0; --i) {
        auto& vector = m_vectors[i];
        if (vector->deleted) {
            vector->x->deleted = true;
            vector->y->deleted = true;
        }
    }

    for (int i = int(m_dockspaces.size() - 1); i >= 0; --i) {
        DockSpace const& dockspace = m_dockspaces[i];
        if (!dockspace.open) {
            m_settings["dockspaces"].erase(std::to_string(i));
            remove(m_dockspaces, dockspace);
            continue;
        }
        m_settings["dockspaces"][std::to_string(i)]["name"] = dockspace.name;
        m_settings["dockspaces"][std::to_string(i)]["id"] = dockspace.id;
        m_settings["dockspaces"][std::to_string(i)]["initial_focus"] = dockspace.focus.focused;
    }

    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        if (!scalar_plot.open) {
            m_settings["scalar_plots"].erase(std::to_string(scalar_plot.id));
            continue;
        }
        if (scalar_plot.id == 0) {
            scalar_plot.id = hashWithTime(scalar_plot.name);
        }
        m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["name"] = scalar_plot.name;
        m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["id"] = scalar_plot.id;
        m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["initial_focus"] = scalar_plot.focus.focused;
        m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["x_range"] = scalar_plot.x_range;
        m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["autofit_y"] = scalar_plot.autofit_y;
        // Update range only if autofit is not on because otherwise the file
        // could be continously rewritten when autofit range changes
        if (!scalar_plot.autofit_y) {
            m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["y_min"] = scalar_plot.y_axis.min;
            m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["y_max"] = scalar_plot.y_axis.max;
        }
        for (int i = int(scalar_plot.scalars.size() - 1); i >= 0; --i) {
            Scalar* scalar = scalar_plot.scalars[i];
            if (scalar->deleted) {
                m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["signals"].erase(scalar->name_and_group);
                if (scalar->replacement != nullptr) {
                    scalar_plot.addScalarToPlot(scalar->replacement);
                }
                remove(scalar_plot.scalars, scalar);
            } else {
                m_settings["scalar_plots"][std::to_string(scalar_plot.id)]["signals"][scalar->name_and_group] = scalar->id;
            }
        }
    }

    for (VectorPlot& vector_plot : m_vector_plots) {
        if (!vector_plot.open) {
            m_settings["vector_plots"].erase(std::to_string(vector_plot.id));
            continue;
        }
        if (vector_plot.id == 0) {
            vector_plot.id = hashWithTime(vector_plot.name);
        }
        m_settings["vector_plots"][std::to_string(vector_plot.id)]["name"] = vector_plot.name;
        m_settings["vector_plots"][std::to_string(vector_plot.id)]["id"] = vector_plot.id;
        m_settings["vector_plots"][std::to_string(vector_plot.id)]["initial_focus"] = vector_plot.focus.focused;
        m_settings["vector_plots"][std::to_string(vector_plot.id)]["time_range"] = vector_plot.time_range;
        for (int i = int(vector_plot.vectors.size() - 1); i >= 0; --i) {
            Vector2D* vector = vector_plot.vectors[i];
            if (vector->deleted || vector->x->deleted || vector->y->deleted) {
                if (vector->deleted && vector->replacement != nullptr) {
                    vector_plot.addVectorToPlot(vector->replacement);
                }
                m_settings["vector_plots"][std::to_string(vector_plot.id)]["signals"].erase(vector->name_and_group);
                remove(vector_plot.vectors, vector);
            } else {
                m_settings["vector_plots"][std::to_string(vector_plot.id)]["signals"][vector->name_and_group] = vector->id;
            }
        }
    }

    for (SpectrumPlot& spec_plot : m_spectrum_plots) {
        if (!spec_plot.open) {
            m_settings["spec_plots"].erase(std::to_string(spec_plot.id));
            continue;
        }
        if (spec_plot.id == 0) {
            spec_plot.id = hashWithTime(spec_plot.name);
        }
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["name"] = spec_plot.name;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["id"] = spec_plot.id;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["initial_focus"] = spec_plot.focus.focused;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["time_range"] = spec_plot.time_range;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["logarithmic_y_axis"] = spec_plot.logarithmic_y_axis;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["window"] = spec_plot.window;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["x_axis_min"] = spec_plot.x_axis.min;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["x_axis_max"] = spec_plot.x_axis.max;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["y_axis_min"] = spec_plot.y_axis.min;
        m_settings["spec_plots"][std::to_string(spec_plot.id)]["y_axis_max"] = spec_plot.y_axis.max;
        if (spec_plot.scalar) {
            if (spec_plot.scalar->deleted) {
                spec_plot.scalar = spec_plot.scalar->replacement;
            } else {
                m_settings["spec_plots"][std::to_string(spec_plot.id)]["signal_id"] = spec_plot.scalar->id;
            }
        } else if (spec_plot.vector) {
            if (spec_plot.vector->deleted) {
                spec_plot.vector = spec_plot.vector->replacement;
            } else {
                m_settings["spec_plots"][std::to_string(spec_plot.id)]["signal_id"] = spec_plot.vector->id;
            }
        }
    }

    for (CustomWindow& custom_window : m_custom_windows) {
        if (!custom_window.open) {
            m_settings["custom_windows"].erase(std::to_string(custom_window.id));
            continue;
        }
        if (custom_window.id == 0) {
            custom_window.id = hashWithTime(custom_window.name);
        }
        m_settings["custom_windows"][std::to_string(custom_window.id)]["name"] = custom_window.name;
        m_settings["custom_windows"][std::to_string(custom_window.id)]["id"] = custom_window.id;
        m_settings["custom_windows"][std::to_string(custom_window.id)]["initial_focus"] = custom_window.focus.focused;
        for (int i = int(custom_window.scalars.size() - 1); i >= 0; --i) {
            Scalar* scalar = custom_window.scalars[i];
            if (scalar->deleted) {
                if (scalar->replacement != nullptr) {
                    custom_window.scalars.push_back(scalar->replacement);
                }
                remove(custom_window.scalars, scalar);
                m_settings["custom_windows"][std::to_string(custom_window.id)]["signals"].erase(scalar->group + " " + scalar->name);
            } else {
                // use group first in key so that the signals are sorted alphabetically by group
                m_settings["custom_windows"][std::to_string(custom_window.id)]["signals"][scalar->group + " " + scalar->name] = scalar->id;
            }
        }
    }

    for (ScriptWindow& script_window : m_script_windows) {
        if (!script_window.open) {
            m_settings["script_windows"].erase(std::to_string(script_window.id));
            continue;
        }
        if (script_window.id == 0) {
            script_window.id = hashWithTime(script_window.name);
        }
        m_settings["script_windows"][std::to_string(script_window.id)]["name"] = script_window.name;
        m_settings["script_windows"][std::to_string(script_window.id)]["id"] = script_window.id;
        m_settings["script_windows"][std::to_string(script_window.id)]["loop"] = script_window.loop;
        m_settings["script_windows"][std::to_string(script_window.id)]["initial_focus"] = script_window.focus.focused;
        m_settings["script_windows"][std::to_string(script_window.id)]["text"] = script_window.text;
    }

    // Remove deleted scalars from scalar groups
    for (auto& scalar_group : m_scalar_groups) {
        std::function<void(SignalGroup<Scalar>&)> remove_scalar_group_deleted_signals = [&](SignalGroup<Scalar>& group) {
            std::vector<Scalar*>& scalars = group.signals;
            for (int i = int(scalars.size() - 1); i >= 0; --i) {
                auto scalar = scalars[i];
                if (scalar->deleted) {
                    remove(scalars, scalar);
                }
            }
            for (auto& subgroup : group.subgroups) {
                remove_scalar_group_deleted_signals(subgroup.second);
            }
        };
        remove_scalar_group_deleted_signals(scalar_group.second);
    }

    // Remove deleted vectors from vector groups
    for (auto& vector_group : m_vector_groups) {
        std::function<void(SignalGroup<Vector2D>&)> remove_vector_group_deleted_signals = [&](SignalGroup<Vector2D>& group) {
            std::vector<Vector2D*>& vectors = group.signals;
            for (int i = int(vectors.size() - 1); i >= 0; --i) {
                auto vector = vectors[i];
                if (vector->deleted) {
                    remove(vectors, vector);
                }
            }
            for (auto& subgroup : group.subgroups) {
                remove_vector_group_deleted_signals(subgroup.second);
            }
        };
        remove_vector_group_deleted_signals(vector_group.second);
    }

    for (int i = int(m_vectors.size() - 1); i >= 0; --i) {
        auto& vector = m_vectors[i];
        if (vector->deleted) {
            m_settings["vector_symbols"].erase(vector->name_and_group);
            remove(m_vectors, vector);
        }
    }

    for (int i = int(m_scalars.size() - 1); i >= 0; --i) {
        std::unique_ptr<Scalar>& scalar = m_scalars[i];
        if (scalar->deleted) {
            std::scoped_lock<std::mutex> lock(m_sampling_mutex);
            m_sampler.stopSampling(scalar.get());
            m_settings["scalars"].erase(scalar->name_and_group);
            m_settings["scalar_symbols"].erase(scalar->name_and_group);
            remove(m_scalars, scalar);
        } else {
            m_settings["scalars"][scalar->name_and_group]["id"] = scalar->id;
            m_settings["scalars"][scalar->name_and_group]["scale"] = scalar->getScaleStr();
            m_settings["scalars"][scalar->name_and_group]["offset"] = scalar->getOffsetStr();
            m_settings["scalars"][scalar->name_and_group]["alias"] = scalar->alias;
        }
    }

    m_settings["layout"] = ImGui::SaveIniSettingsToMemory(nullptr);
    m_settings["group_to_add_symbols"] = m_group_to_add_symbols;
    bool closing = glfwWindowShouldClose(m_window);
    bool settings_changed = (m_settings != m_settings_saved);
    if (!closing
        && m_initial_focus_set
        && settings_changed) {
        m_settings_saved = m_settings;

        std::string settings_dir = std::getenv("USERPROFILE") + std::string("\\.dbg_gui\\");
        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        // Write settings to tmp file first to avoid corrupting the file if program is closed mid-write
        std::ofstream(settings_dir + "settings.json.tmp") << std::setw(4) << m_settings;
        std::filesystem::copy_file(settings_dir + "settings.json.tmp",
                                   settings_dir + "settings.json",
                                   std::filesystem::copy_options::overwrite_existing);
    }
}

void DbgGui::setInitialFocus() {
    // Set same tabs active as in previous session because the windows do not yet exists when previous session
    // settings are loaded and focus cannot be set immediately on first creation
    // Related github issues
    // https://github.com/ocornut/imgui/issues/5005 How to set active docked window?
    // https://github.com/ocornut/imgui/issues/5289 ImGui::SetWindowFocus does nothing the first frame after a window has been created
    if (m_initial_focus_set) {
        return;
    }
    m_initial_focus_set = true;

    if (m_window_focus.scalars.initial_focus) {
        ImGui::Begin("Scalars");
        ImGui::SetWindowFocus("Scalars");
        ImGui::End();
    }
    if (m_window_focus.vectors.initial_focus) {
        ImGui::Begin("Vectors");
        ImGui::SetWindowFocus("Vectors");
        ImGui::End();
    }
    if (m_window_focus.symbols.initial_focus) {
        ImGui::Begin("Symbols");
        ImGui::SetWindowFocus("Symbols");
        ImGui::End();
    }
    if (m_window_focus.log.initial_focus) {
        ImGui::Begin("Log");
        ImGui::SetWindowFocus("Log");
        ImGui::End();
    }

    for (DockSpace& dockspace : m_dockspaces) {
        ImGui::Begin(dockspace.title().c_str());
        if (dockspace.focus.initial_focus) {
            ImGui::SetWindowFocus(dockspace.title().c_str());
        }
        ImGui::End();
    }
    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        ImGui::Begin(scalar_plot.title().c_str());
        if (scalar_plot.focus.initial_focus) {
            ImGui::SetWindowFocus(scalar_plot.title().c_str());
        }
        ImGui::End();
    }
    for (VectorPlot& vector_plot : m_vector_plots) {
        ImGui::Begin(vector_plot.title().c_str());
        if (vector_plot.focus.initial_focus) {
            ImGui::SetWindowFocus(vector_plot.title().c_str());
        }
        ImGui::End();
    }
    for (SpectrumPlot& spec_plot : m_spectrum_plots) {
        ImGui::Begin(spec_plot.title().c_str());
        if (spec_plot.focus.initial_focus) {
            ImGui::SetWindowFocus(spec_plot.title().c_str());
        }
        ImGui::End();
    }
    for (CustomWindow& custom_window : m_custom_windows) {
        ImGui::Begin(custom_window.title().c_str());
        if (custom_window.focus.initial_focus) {
            ImGui::SetWindowFocus(custom_window.title().c_str());
        }
        ImGui::End();
    }
    for (ScriptWindow& script_window : m_script_windows) {
        ImGui::Begin(script_window.title().c_str());
        if (script_window.focus.initial_focus) {
            ImGui::SetWindowFocus(script_window.title().c_str());
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
    Vector2D* vector = addVector(x->getValueSource(), y->getValueSource(), group, x->getFullName(), y->getFullName());
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
    m_next_sync_timestamp = 0;
    m_closing = true;
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

void DbgGui::pause() {
    m_paused = true;
    m_next_sync_timestamp = 0;
    while (m_paused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

Scalar* DbgGui::addSymbol(std::string const& symbol_name, std::string group, std::string const& alias, double scale, double offset) {
    VariantSymbol* sym = m_dbghelp_symbols.getSymbol(symbol_name);
    if (sym) {
        Scalar* ptr = addScalar(sym->getValueSource(), group, symbol_name, scale, offset);
        ptr->alias = alias;
        ptr->alias_and_group = ptr->alias + "(" + ptr->group + ")";
        m_settings["scalar_symbols"][ptr->name_and_group]["name"] = ptr->name;
        m_settings["scalar_symbols"][ptr->name_and_group]["group"] = ptr->group;
        return ptr;
    }
    return nullptr;
}

Scalar* DbgGui::addScalar(ValueSource const& src, std::string group, std::string const& name, double scale, double offset) {
    if (group.empty()) {
        group = "debug";
    } else {
        group = group;
    }
    uint64_t id = hash(name + " (" + group + ")");
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
    new_scalar->setScaleStr(std::format("{:g}", scale));
    new_scalar->setOffsetStr(std::format("{:g}", offset));
    restoreScalarSettings(new_scalar.get());
    std::vector<std::string> groups = str::split(new_scalar->group, '|');
    SignalGroup<Scalar>* added_group = &m_scalar_groups[groups[0]];
    added_group->name = groups[0];
    added_group->full_name = added_group->name;
    std::string full_group_name = added_group->name;
    for (int i = 1; i < groups.size(); ++i) {
        added_group = &added_group->subgroups[groups[i]];
        added_group->name = groups[i];
        full_group_name += "|" + added_group->name;
        added_group->full_name = full_group_name;
    }

    added_group->signals.push_back(new_scalar.get());
    // Sort items within the inserted group
    std::sort(added_group->signals.begin(), added_group->signals.end(), [](Scalar* a, Scalar* b) { return a->name < b->name; });
    return new_scalar.get();
}

Vector2D* DbgGui::addVector(ValueSource const& x, ValueSource const& y, std::string group, std::string const& name_x, std::string const& name_y, double scale, double offset) {
    if (group.empty()) {
        group = "debug";
    } else {
        group = group;
    }
    uint64_t id = hash(name_x + " (" + group + ")");
    Vector2D* existing_vector = getVector(id);
    if (existing_vector != nullptr) {
        return existing_vector;
    }
    auto& new_vector = m_vectors.emplace_back(std::make_unique<Vector2D>());
    new_vector->name = name_x;
    new_vector->group = group;
    new_vector->name_and_group = name_x + " (" + group + ")";
    new_vector->id = id;
    new_vector->x = addScalar(x, group, name_x);
    new_vector->x->hide_from_scalars_window = true;
    new_vector->y = addScalar(y, group, name_y);
    new_vector->y->hide_from_scalars_window = true;
    new_vector->x->setScaleStr(std::format("{:g}", scale));
    new_vector->x->setOffsetStr(std::format("{:g}", offset));
    new_vector->y->setScaleStr(std::format("{:g}", scale));
    new_vector->y->setOffsetStr(std::format("{:g}", offset));
    std::vector<std::string> groups = str::split(new_vector->group, '|');
    SignalGroup<Vector2D>* added_group = &m_vector_groups[groups[0]];
    added_group->name = groups[0];
    added_group->full_name = added_group->name;
    std::string full_group_name = added_group->name;
    for (int i = 1; i < groups.size(); ++i) {
        added_group = &added_group->subgroups[groups[i]];
        added_group->name = groups[i];
        full_group_name += "|" + added_group->name;
        added_group->full_name = full_group_name;
    }

    added_group->signals.push_back(new_vector.get());
    // Sort items within the inserted group
    std::sort(added_group->signals.begin(), added_group->signals.end(), [](Vector2D* a, Vector2D* b) { return a->name < b->name; });
    return new_vector.get();
}

void DbgGui::logMessage(const char* msg) {
    m_all_messages += msg;
    if (m_message_queue.size() > 20) {
        m_message_queue.pop_front();
    }
    m_message_queue.push_back(msg);
}
