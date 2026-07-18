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

#include "dbg_gui_internal.h"
#include "themes.h"
#include "str_helpers.h"
#include "custom_signal.hpp"
#include "imgui_settings_migration.h"
#include "signal_cleanup.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {
template <typename Fn>
void forEachSignalId(nlohmann::json const& signals, Fn fn) {
    // Saved signal maps use names as keys, while older/imported shapes may be
    // arrays. Iterating values supports both without duplicating restore loops.
    if (!signals.is_array() && !signals.is_object()) {
        return;
    }
    for (auto const& signal : signals) {
        if (signal.is_number_unsigned() || signal.is_number_integer()) {
            fn(signal.get<uint64_t>());
        }
    }
}

void loadImguiLayout(std::string layout) {
    imgui_settings::migrateLayoutIniHashes(layout);
    ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
}

bool restoreScalarSettings(Scalar* scalar,
                           nlohmann::json& settings,
                           std::vector<ScalarPlot>& scalar_plots,
                           std::vector<CustomWindow>& custom_windows) {
    bool restored_to_plot = false;
    // Restore settings of the scalar signal
    TRY(for (auto& scalar_data : settings["scalars"]) {
        uint64_t id = scalar_data["id"];
        if (id == scalar->id) {
            std::string scale = scalar_data["scale"];
            scalar->setScaleStr(scale);
            std::string offset = scalar_data["offset"];
            scalar->setOffsetStr(offset);
            scalar->alias = std::string(scalar_data["alias"]);
            scalar->alias_and_group = scalar->alias + " (" + scalar->group + ")";
            break;
        }
    })

    // Restore scalar to plots
    TRY(for (auto scalar_plot_data : settings["scalar_plots"]) {
        ScalarPlot* plot = nullptr;
        for (auto& scalar_plot : scalar_plots) {
            if (scalar_plot.id == scalar_plot_data["id"]) {
                plot = &scalar_plot;
                break;
            }
        }
        if (plot == nullptr) {
            continue;
        }
        // New settings store signal placement per subplot. Old settings only
        // have a top-level signals object, which restores into subplot 0.
        if (scalar_plot_data.contains("subplots")) {
            int subplot_idx = 0;
            for (auto const& subplot_data : scalar_plot_data["subplots"]) {
                if (subplot_data.contains("signals")) {
                    forEachSignalId(subplot_data["signals"], [&](uint64_t id) {
                        if (id == scalar->id) {
                            plot->addScalarToPlot(scalar, subplot_idx);
                            restored_to_plot = true;
                        }
                    });
                }
                ++subplot_idx;
            }
        } else if (scalar_plot_data.contains("signals")) {
            forEachSignalId(scalar_plot_data["signals"], [&](uint64_t id) {
                if (id == scalar->id) {
                    plot->addScalarToPlot(scalar);
                    restored_to_plot = true;
                }
            });
        }
    })

    // Restore scalar to custom window
    TRY(for (auto custom_window_data : settings["custom_windows"]) {
        CustomWindow* custom = nullptr;
        for (auto& custom_window : custom_windows) {
            if (custom_window.id == custom_window_data["id"]) {
                custom = &custom_window;
                break;
            }
        }
        if (custom != nullptr) {
            forEachSignalId(custom_window_data["signals"], [&](uint64_t id) {
                if (id == scalar->id) {
                    custom->addScalar(scalar);
                }
            });
        }
    })
    return restored_to_plot;
}
} // namespace

Scalar* findScalar(std::vector<std::unique_ptr<Scalar>> const& scalars, uint64_t id) {
    for (std::unique_ptr<Scalar> const& scalar : scalars) {
        if (scalar->id == id && !scalar->deleted) {
            return scalar.get();
        }
    }
    return nullptr;
}

Vector2D* findVector(std::vector<std::unique_ptr<Vector2D>> const& vectors, uint64_t id) {
    for (std::unique_ptr<Vector2D> const& vector : vectors) {
        if (vector->id == id && !vector->deleted) {
            return vector.get();
        }
    }
    return nullptr;
}

constexpr int SETTINGS_CHECK_INTERVAL_MS = 500;

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
      m_symbols(DbgSymbols::getSymbols()) {
    assert(sampling_time >= 0);
    for (std::string const& error : m_symbols.symbolLoadErrors()) {
        logMessage(error);
    }
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

    if (m_sample_timestamp > m_next_sync_timestamp || (tick.valid() && tick.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
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
        real_time_us = std::max<long>(real_time_us, 1);
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
            double const time_offset = timestamp - m_sample_timestamp;
            m_sampler.shiftTime(time_offset);
            for (ScriptWindow& script_window : m_script_windows) {
                script_window.shiftScriptSchedule(time_offset);
            }
            m_next_sync_timestamp = 0;
        }
        m_sample_timestamp = timestamp;

        for (ScriptWindow& script_window : m_script_windows) {
            if (std::string const error = script_window.processScript(m_sample_timestamp); !error.empty()) {
                logMessage(error);
            }
        }
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

std::vector<CommandPaletteCommand> DbgGui::commandPaletteCommands(bool enable_sampling_hotkeys) {
    auto save_all_plots_as_csv = [&] {
        std::vector<Scalar*> scalars;
        scalars.reserve(m_scalars.size());
        for (auto const& scalar : m_scalars) {
            scalars.push_back(scalar.get());
        }
        saveScalarsAsCsv(getFilenameToSave(), scalars, m_linked_scalar_x_axis_limits);
    };

    std::function<void()> start_pause_action;
    std::function<void()> step_action;
    if (enable_sampling_hotkeys) {
        start_pause_action = [&] { m_paused = !m_paused; };
        step_action = [&] {
            m_pause_at_time = std::numeric_limits<double>::epsilon();
            m_paused = false;
        };
    }

    return {
      {"command-palette", "Command palette", "Search available commands and configure their hotkeys.", ImGuiMod_Ctrl | ImGuiKey_P, [&] { ImGui::OpenPopup("Command Palette"); }},
      {"start-pause", "Start / pause sampling", "Toggle DbgGui between running and paused.", ImGuiKey_Space, start_pause_action},
      {"step", "Step one sample", "Resume briefly to advance the target one sample.", ImGuiMod_Shift | ImGuiKey_Space, step_action, true},
      {"double-speed", "Double simulation speed", "Increase simulated speed relative to real time.", ImGuiKey_KeypadAdd, [&] { m_simulation_speed *= 2.; }},
      {"halve-speed", "Halve simulation speed", "Decrease simulated speed relative to real time.", ImGuiKey_KeypadSubtract, [&] { m_simulation_speed /= 2.; }},
      {"pause-after", "Pause after", "Open the pause-after-time dialog.", ImGuiKey_KeypadDivide, [&] { ImGui::OpenPopup(str::PAUSE_AFTER); }},
      {"pause-at", "Pause at", "Open the pause-at-time dialog.", ImGuiKey_KeypadMultiply, [&] { ImGui::OpenPopup(str::PAUSE_AT); }},
      {"add-scalar-plot", "Add scalar plot", "Open the add-scalar-plot dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_1, [&] { ImGui::OpenPopup(str::ADD_SCALAR_PLOT); }},
      {"add-vector-plot", "Add vector plot", "Open the add-vector-plot dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_2, [&] { ImGui::OpenPopup(str::ADD_VECTOR_PLOT); }},
      {"add-spectrum-plot", "Add spectrum plot", "Open the add-spectrum-plot dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_3, [&] { ImGui::OpenPopup(str::ADD_SPECTRUM_PLOT); }},
      {"add-custom-window", "Add custom window", "Open the add-custom-window dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_4, [&] { ImGui::OpenPopup(str::ADD_CUSTOM_WINDOW); }},
      {"add-dockspace", "Add dockspace", "Open the add-dockspace dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_6, [&] { ImGui::OpenPopup(str::ADD_DOCKSPACE); }},
      {"add-grid-window", "Add grid window", "Open the add-grid-window dialog.", ImGuiKey_None, [&] { ImGui::OpenPopup(str::ADD_GRID_WINDOW); }},
      {"copy-visible-samples", "Copy visible samples to clipboard", "Copy visible scalar samples for import into CsvPlotter.", ImGuiMod_Ctrl | ImGuiKey_T, [&] { copyAllScalarSamplesToClipboard(); }},
      {"save-plots-csv", "Save all plots as CSV", "Export visible scalar samples from all plots to a CSV file.", ImGuiKey_None, save_all_plots_as_csv},
      {"save-snapshot", "Save snapshot", "Save current global variable values.", ImGuiMod_Ctrl | ImGuiKey_S, [&] { saveSnapshot(); }},
      {"load-snapshot", "Load snapshot", "Restore global variable values from a snapshot.", ImGuiMod_Ctrl | ImGuiKey_R, [&] { loadSnapshot(); }},
      {"save-settings", "Save settings", "Save the current DbgGui configuration to a JSON file.", ImGuiKey_None, [&] { saveSettings(); }},
      {"load-settings", "Load settings", "Load a DbgGui configuration from a JSON file.", ImGuiKey_None, [&] { loadSettings(); }},
      {"custom-signal-tip", "Create custom signal", "Select symbols, then Ctrl+Shift-click a symbol in the Symbols tree to open the Custom Signal Creator.", ImGuiKey_None, {}},
    };
}

std::string DbgGui::commandHotkeyName(std::string_view command_id, ImGuiKeyChord default_hotkey) const {
    CommandPaletteCommand command{.id = command_id, .default_hotkey = default_hotkey};
    return ::commandHotkeyName(command, m_hotkey_overrides);
}

void DbgGui::showCommandPalette() {
    std::vector<CommandPaletteCommand> commands = commandPaletteCommands();
    std::erase_if(commands, [](CommandPaletteCommand const& command) { return command.id == "command-palette"; });
    if (std::optional<size_t> command_index = showCommandPaletteTable("Command Palette", commands, m_hotkey_overrides)) {
        commands[*command_index].action();
    }
}

void DbgGui::saveSettings() {
    const char* env = std::getenv(USER_SETTINGS_LOCATION);
    if (env == nullptr) {
        return;
    }

    std::string out_path = getFilenameToSave("json", std::format("{}/.dbg_gui/", env));
    if (!out_path.empty()) {
        std::ofstream(out_path) << std::setw(4) << m_settings;
    }
}

void DbgGui::loadSettings() {
    const char* env = std::getenv(USER_SETTINGS_LOCATION);
    if (env == nullptr) {
        return;
    }

    std::string settings_dir = std::format("{}/.dbg_gui/", env);
    std::string in_path = getFilenameToOpen("json", settings_dir);
    if (!in_path.empty()) {
        // Overwrite existing settings. The file will be reloaded in updateSavedSettings.
        std::filesystem::copy_file(in_path, settings_dir + "settings.json", std::filesystem::copy_options::overwrite_existing);
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

    TRY(loadPreviousSessionSettings();)

    extern unsigned int calibri_compressed_size;
    extern unsigned int calibri_compressed_data[];
    io.Fonts->AddFontFromMemoryCompressedTTF(calibri_compressed_data, calibri_compressed_size, MIN_FONT_SIZE);
    ImGui::PushFont(ImGui::GetDefaultFont(), m_options.font_size);

    m_initialized = true;

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();

        //---------- Hotkeys ----------
        std::vector<CommandPaletteCommand> commands = commandPaletteCommands(!ImGui::IsAnyItemActive());
        triggerCommandHotkeys("Command Palette", commands, m_hotkey_overrides);
        showCommandPalette();
        addPopupModal(str::ADD_SCALAR_PLOT);
        addPopupModal(str::ADD_VECTOR_PLOT);
        addPopupModal(str::ADD_SPECTRUM_PLOT);
        addPopupModal(str::ADD_CUSTOM_WINDOW);
        addPopupModal(str::ADD_DOCKSPACE);
        addPopupModal(str::ADD_GRID_WINDOW);
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
        showGridWindow();
        showScalarPlots();
        showVectorPlots();
        showSpectrumPlots();
        showCustomSignalCreator();
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

void DbgGui::loadPreviousSessionSettings() {
    m_initial_focus_set = false;
    const char* env = std::getenv(USER_SETTINGS_LOCATION);
    if (env == nullptr) {
        logMessage(std::format("The {} environment variable is not set. Settings cannot be saved or loaded.", USER_SETTINGS_LOCATION));
        return;
    }
    std::string settings_dir = std::string(env) + "/.dbg_gui/";
    std::string settings_path = settings_dir + "settings.json";
    std::ifstream f(settings_path);
    if (f.is_open()) {
        m_last_settings_write_time = std::filesystem::last_write_time(settings_path);
        TRY(m_settings = nlohmann::json::parse(f);
            m_settings_saved = m_settings;)

        if (m_settings.contains("layout")) {
            std::string layout = m_settings["layout"];
            loadImguiLayout(layout);
        }

        m_options.fromJson(m_settings["options"]);
        setTheme(m_options.theme, m_window);

        m_hotkey_overrides.clear();
        if (m_settings.contains("hotkeys") && m_settings["hotkeys"].is_object()) {
            for (auto const& [command_id, hotkey] : m_settings["hotkeys"].items()) {
                if (!hotkey.is_number_unsigned() && !hotkey.is_number_integer()) {
                    continue;
                }
                ImGuiKeyChord chord = hotkey.get<ImGuiKeyChord>();
                if (isValidCommandHotkey(chord)) {
                    m_hotkey_overrides[command_id] = chord;
                }
            }
        }

        // Buffer size and window position are set only once and not synchronized with multiple processes
        static bool once = false;
        if (!once) {
            once = true;
            m_sampler.setBufferSize(m_options.sampling_buffer_size);
            TRY(int xpos = std::max(0, int(m_settings["window"]["xpos"]));
                int ypos = std::max(0, int(m_settings["window"]["ypos"]));
                glfwSetWindowPos(m_window, xpos, ypos);)
        }
        TRY(glfwSetWindowSize(m_window, m_settings["window"]["width"], m_settings["window"]["height"]);)

        TRY(m_window_focus.scalars.initial_focus = m_settings["initial_focus"]["scalars"];)
        TRY(m_window_focus.vectors.initial_focus = m_settings["initial_focus"]["vectors"];)
        TRY(m_window_focus.symbols.initial_focus = m_settings["initial_focus"]["symbols"];)
        TRY(m_window_focus.log.initial_focus = m_settings["initial_focus"]["log"];)

        m_symbol_scale_settings.clear();
        TRY(for (auto const& [symbol_name, scale] : m_settings["symbol_scales"].items()) {
            std::string scale_str = scale.is_number() ? std::format("{:g}", scale.get<double>()) : scale.get<std::string>();
            if (str::evaluateExpression(scale_str).has_value()) {
                m_symbol_scale_settings[symbol_name] = scale_str;
            }
        })

        for (auto symbol : m_settings["scalar_symbols"]) {
            TRY(
              VariantSymbol* sym = m_symbols.getSymbol(symbol["name"]);
              if (sym
                  && (sym->getType() == VariantSymbol::Type::Arithmetic
                      || sym->getType() == VariantSymbol::Type::Enum
                      || sym->getType() == VariantSymbol::Type::Pointer)) {
                  addScalarSymbol(sym, symbol["group"]);
              })
        }

        for (auto symbol : m_settings["vector_symbols"]) {
            TRY(
              VariantSymbol* sym_x = m_symbols.getSymbol(symbol["x"]);
              VariantSymbol* sym_y = m_symbols.getSymbol(symbol["y"]);
              if (sym_x && sym_y) {
                  std::string group = symbol.value("group", "debug");
                  // Prefer referencing existing visible scalars instead of creating hidden
                  // duplicates via addVectorSymbol, which would hide the user's existing ones.
                  Scalar* existing_x = findScalar(m_scalars, signalId((std::string)symbol["x"], group));
                  Scalar* existing_y = findScalar(m_scalars, signalId((std::string)symbol["y"], group));
                  if (existing_x && existing_y) {
                      addVectorFromScalars(existing_x, existing_y);
                  } else {
                      addVectorSymbol(sym_x, sym_y, group);
                  }
              };)
        }

        for (auto custom_signal : m_settings["custom_signals"]) {
            std::string eq = custom_signal["equation"];
            std::string name = custom_signal["name"];
            std::string group = custom_signal["group"];
            std::vector<VariantSymbol*> selected_symbols;
            bool all_symbols_exist = true;
            for (auto& symbol_name : custom_signal["symbols"]) {
                VariantSymbol* sym = m_symbols.getSymbol(symbol_name);
                if (sym) {
                    selected_symbols.push_back(sym);
                } else {
                    all_symbols_exist = false;
                }
            }
            if (!all_symbols_exist) {
                continue;
            }

            ReadWriteFn eq_fn = [selected_symbols, eq](std::optional<double> /*write*/) {
                std::vector<double> values;
                for (VariantSymbol* symbol : selected_symbols) {
                    values.push_back(getSourceValue(symbol->getValueSource()));
                }
                std::expected<double, std::string> expr_value = str::evaluateExpression(getFormattedEqForSample(eq, values));
                assert(expr_value.has_value());
                return expr_value.value();
            };
            addScalar(eq_fn, group, name);
        }

        m_dockspaces.clear();
        for (auto dockspace_data : m_settings["dockspaces"]) {
            TRY(
              DockSpace& dockspace = m_dockspaces.emplace_back(dockspace_data["name"], dockspace_data["id"]);
              dockspace.focus.initial_focus = dockspace_data["initial_focus"];)
        }

        m_scalar_plots.clear();
        for (auto scalar_plot_data : m_settings["scalar_plots"]) {
            ScalarPlot& plot = m_scalar_plots.emplace_back(scalar_plot_data);
            // Prefer the subplot-aware format, but keep old top-level signals
            // readable for existing settings files.
            if (scalar_plot_data.contains("subplots")) {
                int subplot_idx = 0;
                for (auto const& subplot_data : scalar_plot_data["subplots"]) {
                    if (subplot_data.contains("signals")) {
                        forEachSignalId(subplot_data["signals"], [&](uint64_t id) {
                            Scalar* scalar = findScalar(m_scalars, id);
                            if (scalar) {
                                m_sampler.startSampling(scalar);
                                plot.addScalarToPlot(scalar, subplot_idx);
                            }
                        });
                    }
                    ++subplot_idx;
                }
            } else if (scalar_plot_data.contains("signals")) {
                forEachSignalId(scalar_plot_data["signals"], [&](uint64_t id) {
                    Scalar* scalar = findScalar(m_scalars, id);
                    if (scalar) {
                        m_sampler.startSampling(scalar);
                        plot.addScalarToPlot(scalar);
                    }
                });
            }
        }

        m_vector_plots.clear();
        for (auto vector_plot_data : m_settings["vector_plots"]) {
            VectorPlot& plot = m_vector_plots.emplace_back(vector_plot_data);
            forEachSignalId(vector_plot_data["signals"], [&](uint64_t id) {
                Vector2D* vec = findVector(m_vectors, id);
                if (vec) {
                    m_sampler.startSampling(vec);
                    plot.addVectorToPlot(vec);
                }
            });
        }

        m_spectrum_plots.clear();
        for (auto spec_plot_data : m_settings.at("spec_plots")) {
            SpectrumPlot& plot = m_spectrum_plots.emplace_back(spec_plot_data);
            if (spec_plot_data.contains("signals")) {
                for (auto xy : spec_plot_data.at("signals")) {
                    if (xy.is_array() && xy.size() == 2) {
                        Scalar* real = findScalar(m_scalars, xy[0]);
                        Scalar* imag = findScalar(m_scalars, xy[1]);
                        if (real && imag) {
                            m_sampler.startSampling(real);
                            m_sampler.startSampling(imag);
                            plot.addToPlot(real, imag);
                        } else if (real) {
                            m_sampler.startSampling(real);
                            plot.addToPlot(real, nullptr);
                        }
                    }
                }
            }
        }

        for (auto& scalar_data : m_settings["scalars"]) {
            uint64_t id = scalar_data["id"];
            Scalar* scalar = findScalar(m_scalars, id);
            if (scalar) {
                scalar->fromJson(scalar_data);
            };
        }

        m_custom_windows.clear();
        for (auto custom_window_data : m_settings["custom_windows"]) {
            CustomWindow& custom_window = m_custom_windows.emplace_back(custom_window_data);
            forEachSignalId(custom_window_data["signals"], [&](uint64_t id) {
                Scalar* scalar = findScalar(m_scalars, id);
                if (scalar) {
                    custom_window.addScalar(scalar);
                }
            });
        }

        {
            std::scoped_lock<std::mutex> lock(m_sampling_mutex);
            m_script_windows.clear();
            for (auto script_window_data : m_settings["script_windows"]) {
                m_script_windows.emplace_back(this, script_window_data);
            }
        }
        m_selected_script_id.reset();

        m_grid_windows.clear();
        for (auto grid_window_data : m_settings["grid_windows"]) {
            GridWindow& grid_window = m_grid_windows.emplace_back(grid_window_data);
            std::vector<uint64_t> signal_ids = grid_window_data["signals"];
            for (int i = 0; i < std::min((int)signal_ids.size(), GridWindow::MAX_ROWS * GridWindow::MAX_COLUMNS); ++i) {
                grid_window.scalars[i / GridWindow::MAX_COLUMNS][i % GridWindow::MAX_COLUMNS] = signal_ids[i];
            }
        }

        TRY(for (std::string hidden_symbol : m_settings["hidden_symbols"]) {
            m_hidden_symbols.insert(hidden_symbol);
        };)

        TRY(m_group_to_add_symbols = m_settings["group_to_add_symbols"].get<std::string>();)
        TRY(m_symbol_search_depth = std::max(0, m_settings.value("symbol_search_depth", m_symbol_search_depth));)
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

    if (m_clear_saved_settings) {
        m_clear_saved_settings = false;
        m_settings.clear();
        m_settings_saved.clear();
        // Restore symbols
        for (auto const& scalar : m_scalars) {
            VariantSymbol* scalar_sym = m_symbols.getSymbol(scalar->name);
            if (scalar_sym) {
                m_settings["scalar_symbols"][scalar->name_and_group]["name"] = scalar->name;
                m_settings["scalar_symbols"][scalar->name_and_group]["group"] = scalar->group;
            }
        }
        for (auto const& vector : m_vectors) {
            VariantSymbol* x = m_symbols.getSymbol(vector->x->name);
            VariantSymbol* y = m_symbols.getSymbol(vector->y->name);
            if (x && y) {
                m_settings["vector_symbols"][vector->name_and_group]["name"] = vector->name;
                m_settings["vector_symbols"][vector->name_and_group]["group"] = vector->group;
                m_settings["vector_symbols"][vector->name_and_group]["x"] = x->getFullName();
                m_settings["vector_symbols"][vector->name_and_group]["y"] = y->getFullName();
            }
        }
    }

    // Read current settings from json if there is a parallel process in which they have changed
    const char* env = std::getenv(USER_SETTINGS_LOCATION);
    std::string settings_dir;
    std::string settings_path;
    if (env != nullptr) {
        settings_dir = std::string(env) + "/.dbg_gui/";
        settings_path = settings_dir + "settings.json";
        if (std::filesystem::exists(settings_path)) {
            auto current_write_time = std::filesystem::last_write_time(settings_path);
            if (current_write_time != m_last_settings_write_time) {
                loadPreviousSessionSettings();
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
    m_settings["options"] = m_options.toJson();
    m_settings["hotkeys"] = nlohmann::json::object();
    for (auto const& [command_id, hotkey] : m_hotkey_overrides) {
        m_settings["hotkeys"][command_id] = hotkey;
    }
    m_settings["initial_focus"]["scalars"] = m_window_focus.scalars.focused;
    m_settings["initial_focus"]["vectors"] = m_window_focus.vectors.focused;
    m_settings["initial_focus"]["symbols"] = m_window_focus.symbols.focused;
    m_settings["initial_focus"]["log"] = m_window_focus.log.focused;

    // If a component scalar is deleted, delete the vector too so vector groups
    // cannot keep pointers to scalars that are removed below. If vector uses
    // hidden component scalars, mark them as also deleted but don't delete them
    // for real yet before they are removed from all other structures.
    markVectorsDeletedByDeletedComponents(m_vectors);

    // Remove first deleted dockspaces because are saved by index and index changes if any is removed
    for (int i = int(m_dockspaces.size() - 1); i >= 0; --i) {
        DockSpace const& dockspace = m_dockspaces[i];
        if (!dockspace.open) {
            m_settings["dockspaces"].clear();
            remove(m_dockspaces, dockspace);
            continue;
        }
    }
    // Update the json for now existing dockspaces
    for (int i = int(m_dockspaces.size() - 1); i >= 0; --i) {
        DockSpace const& dockspace = m_dockspaces[i];
        dockspace.updateJson(m_settings["dockspaces"][std::to_string(i)]);
    }

    for (ScalarPlot& scalar_plot : m_scalar_plots) {
        if (!scalar_plot.open) {
            m_settings["scalar_plots"].erase(std::to_string(scalar_plot.id));
            continue;
        }
        if (scalar_plot.id == 0) {
            scalar_plot.id = hashWithTime(scalar_plot.name);
        }
        nlohmann::json& j = m_settings["scalar_plots"][std::to_string(scalar_plot.id)];
        scalar_plot.updateJson(j);
        // Top-level scalar plot signals are a legacy read-only migration path.
        // New settings write signal placement only under subplots.
        j.erase("signals");
        for (int subplot_idx = 0; subplot_idx < scalar_plot.subplotCount(); ++subplot_idx) {
            auto& scalars = scalar_plot.subplots[subplot_idx].scalars;
            j["subplots"][subplot_idx]["signals"] = nlohmann::json::object();
            for (int i = int(scalars.size() - 1); i >= 0; --i) {
                Scalar* scalar = scalars[i];
                if (scalar->deleted) {
                    if (isLiveScalar(scalar->replacement)) {
                        scalars[i] = scalar->replacement;
                    } else {
                        remove(scalars, scalar);
                    }
                } else {
                    j["subplots"][subplot_idx]["signals"][scalar->name_and_group] = scalar->id;
                }
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
        nlohmann::json& j = m_settings["vector_plots"][std::to_string(vector_plot.id)];
        vector_plot.updateJson(j);
        for (int i = int(vector_plot.vectors.size() - 1); i >= 0; --i) {
            Vector2D* vector = vector_plot.vectors[i];
            if (!isLiveVector(vector)) {
                j["signals"].erase(vector->name_and_group);
                replaceDeletedVectorInPlot(vector_plot, vector);
            } else {
                j["signals"][vector->name_and_group] = vector->id;
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
        nlohmann::json& j = m_settings["spec_plots"][std::to_string(spec_plot.id)];
        spec_plot.updateJson(j);
        for (int i = int(spec_plot.spectrums.size() - 1); i >= 0; --i) {
            Spectrum<Scalar>& spec = spec_plot.spectrums[i];
            if (spec.imag == nullptr) {
                if (spec.real->deleted) {
                    spec_plot.removeFromPlot(spec.real);
                    j["signals"].erase(spec.real->name_and_group);
                    if (isLiveScalar(spec.real->replacement)) {
                        spec_plot.addToPlot(spec.real->replacement, nullptr);
                    }
                } else {
                    j["signals"][spec.real->name_and_group] = {spec.real->id, 0};
                }
            } else {
                if (spec.real->deleted || spec.imag->deleted) {
                    spec_plot.removeFromPlot(spec.real);
                    spec_plot.removeFromPlot(spec.imag);
                    j["signals"].erase(spec.real->name_and_group);
                    if (isLiveScalar(spec.real->replacement) && isLiveScalar(spec.imag->replacement)) {
                        spec_plot.addToPlot(spec.real->replacement, spec.imag->replacement);
                    }
                } else {
                    j["signals"][spec.real->name_and_group] = {spec.real->id, spec.imag->id};
                }
            }
        }
        // If scalars are deleted but not yet removed from spectrums, remove them from json
        // so that they don't get saved to settings and added back to plot after restart
        for (Scalar* removed_scalar : spec_plot.removed_scalars) {
            if (removed_scalar == nullptr) {
                continue;
            }
            j["signals"].erase(removed_scalar->name_and_group);
        }
        spec_plot.removed_scalars.clear();
    }

    for (CustomWindow& custom_window : m_custom_windows) {
        if (!custom_window.open) {
            m_settings["custom_windows"].erase(std::to_string(custom_window.id));
            continue;
        }
        if (custom_window.id == 0) {
            custom_window.id = hashWithTime(custom_window.name);
        }
        nlohmann::json& j = m_settings["custom_windows"][std::to_string(custom_window.id)];
        custom_window.updateJson(j);
        for (int i = int(custom_window.scalars.size() - 1); i >= 0; --i) {
            Scalar* scalar = custom_window.scalars[i];
            if (scalar->deleted) {
                if (isLiveScalar(scalar->replacement)) {
                    custom_window.addScalar(scalar->replacement);
                }
                remove(custom_window.scalars, scalar);
                j["signals"].erase(scalar->group + " " + scalar->name);
            } else {
                // use group first in key so that the signals are sorted alphabetically by group
                j["signals"][scalar->group + " " + scalar->name] = scalar->id;
            }
        }
    }

    {
        std::scoped_lock<std::mutex> lock(m_sampling_mutex);
        // Arrays preserve the order chosen in the Scripts window. Loading still
        // accepts the legacy object format because both are iterable as scripts.
        nlohmann::json script_windows = nlohmann::json::array();
        for (ScriptWindow& script_window : m_script_windows) {
            if (script_window.id == 0) {
                script_window.id = hashWithTime(script_window.name);
            }
            nlohmann::json script_window_json;
            script_window.updateJson(script_window_json);
            script_windows.push_back(std::move(script_window_json));
        }
        m_settings["script_windows"] = std::move(script_windows);
    }

    for (GridWindow& grid_window : m_grid_windows) {
        if (!grid_window.open) {
            m_settings["grid_windows"].erase(std::to_string(grid_window.id));
            continue;
        }
        if (grid_window.id == 0) {
            grid_window.id = hashWithTime(grid_window.name);
        }
        nlohmann::json& j = m_settings["grid_windows"][std::to_string(grid_window.id)];
        grid_window.updateJson(j);
        std::vector<uint64_t> signal_ids;
        for (int row = 0; row < GridWindow::MAX_ROWS; ++row) {
            for (int col = 0; col < GridWindow::MAX_COLUMNS; ++col) {
                signal_ids.push_back(grid_window.scalars[row][col]);
            }
        }
        j["signals"] = signal_ids;
    }

    // Remove deleted scalars from scalar groups
    for (auto& scalar_group : m_scalar_groups) {
        removeDeletedSignalsFromGroup(scalar_group.second);
    }

    // Remove deleted vectors from vector groups
    for (auto& vector_group : m_vector_groups) {
        removeDeletedSignalsFromGroup(vector_group.second);
    }

    for (int i = int(m_vectors.size() - 1); i >= 0; --i) {
        auto& vector = m_vectors[i];
        if (vector->deleted) {
            remove(m_selected_vectors, vector.get());
            bool const has_live_duplicate = std::any_of(m_vectors.begin(), m_vectors.end(), [&](auto const& candidate) {
                return candidate.get() != vector.get()
                    && !candidate->deleted
                    && candidate->name_and_group == vector->name_and_group;
            });
            if (!has_live_duplicate) {
                m_settings["vector_symbols"].erase(vector->name_and_group);
            }
            remove(m_vectors, vector);
        }
    }

    for (int i = int(m_scalars.size() - 1); i >= 0; --i) {
        std::unique_ptr<Scalar>& scalar = m_scalars[i];
        if (m_settings["scalars"].contains(scalar->name_and_group)
            || scalar->alias != scalar->name
            || scalar->getScale() != 1
            || scalar->getOffset() != 0) {
            scalar->updateJson(m_settings["scalars"][scalar->name_and_group]);
        }

        if (scalar->deleted) {
            std::scoped_lock<std::mutex> lock(m_sampling_mutex);
            m_sampler.stopSampling(scalar.get());
            remove(m_selected_scalars, scalar.get());
            bool const has_live_duplicate = std::any_of(m_scalars.begin(), m_scalars.end(), [&](auto const& candidate) {
                return candidate.get() != scalar.get()
                    && !candidate->deleted
                    && candidate->name_and_group == scalar->name_and_group;
            });
            if (!has_live_duplicate) {
                m_settings["scalars"].erase(scalar->name_and_group);
                m_settings["scalar_symbols"].erase(scalar->name_and_group);
            }
            if (!has_live_duplicate && m_settings["custom_signals"].contains(scalar->name_and_group)) {
                m_settings["custom_signals"].erase(scalar->name_and_group);
            }
            remove(m_scalars, scalar);
        }
    }

    m_settings["layout"] = ImGui::SaveIniSettingsToMemory(nullptr);
    m_settings["group_to_add_symbols"] = m_group_to_add_symbols;
    m_settings["symbol_search_depth"] = m_symbol_search_depth;
    m_settings["symbol_scales"] = nlohmann::json::object();
    for (auto const& [symbol_name, scale] : m_symbol_scale_settings) {
        m_settings["symbol_scales"][symbol_name] = scale;
    }
    // Settings are only saved if window is focused so that there is no competition which process is writing
    bool closing = glfwWindowShouldClose(m_window);
    bool focused = (bool)glfwGetWindowAttrib(m_window, GLFW_FOCUSED);
    bool settings_changed = (m_settings != m_settings_saved);
    if (!closing
        && focused
        && m_initial_focus_set
        && settings_changed
        && !settings_dir.empty()) {
        m_settings_saved = m_settings;

        if (!std::filesystem::exists(settings_dir)) {
            std::filesystem::create_directories(settings_dir);
        }
        // Write settings to tmp file first to avoid corrupting the file if program is closed mid-write
        std::ofstream(settings_path + ".tmp") << std::setw(4) << m_settings;
        std::filesystem::copy_file(settings_path + ".tmp",
                                   settings_path,
                                   std::filesystem::copy_options::overwrite_existing);
        m_last_settings_write_time = std::filesystem::last_write_time(settings_path);
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
    for (GridWindow& grid_window : m_grid_windows) {
        ImGui::Begin(grid_window.title().c_str());
        if (grid_window.focus.initial_focus) {
            ImGui::SetWindowFocus(grid_window.title().c_str());
        }
        ImGui::End();
    }
}

Scalar* DbgGui::addScalarSymbol(VariantSymbol* sym, std::string const& group) {
    Scalar* scalar = addScalar(sym->getValueSource(), group, sym->getFullName(), getSymbolScale(*sym, m_symbol_scale_settings));
    scalar->read_only = sym->isConst();
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
    VariantSymbol* sym = m_symbols.getSymbol(symbol_name);
    if (sym) {
        Scalar* ptr = addScalar(sym->getValueSource(), group, symbol_name, scale, offset);
        ptr->read_only = sym->isConst();
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
    }
    uint64_t id = signalId(name, group);
    Scalar* existing_scalar = findScalar(m_scalars, id);
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
    if (m_initialized.load()
        && restoreScalarSettings(new_scalar.get(), m_settings, m_scalar_plots, m_custom_windows)) {
        m_sampler.startSampling(new_scalar.get());
    }
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
    }
    uint64_t id = signalId(name_x, group);
    Vector2D* existing_vector = findVector(m_vectors, id);
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

Vector2D* DbgGui::addVectorFromScalars(Scalar* x, Scalar* y) {
    if (x->group != y->group) {
        return nullptr;
    }
    uint64_t id = signalId(x->name + "+" + y->name, x->group);
    Vector2D* existing_vector = findVector(m_vectors, id);
    if (existing_vector != nullptr) {
        return existing_vector;
    }
    auto& new_vector = m_vectors.emplace_back(std::make_unique<Vector2D>());
    new_vector->name = x->name;
    new_vector->group = x->group;
    new_vector->name_and_group = new_vector->name + " (" + x->group + ")";
    new_vector->id = id;
    new_vector->x = x;
    new_vector->y = y;
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
    std::sort(added_group->signals.begin(), added_group->signals.end(), [](Vector2D* a, Vector2D* b) { return a->name < b->name; });
    // Reuse vector_symbols persistence intentionally. This restores only
    // symbol-backed scalar pairs; vectors built from custom scalars are session-only.
    m_settings["vector_symbols"][new_vector->name_and_group]["name"] = new_vector->name;
    m_settings["vector_symbols"][new_vector->name_and_group]["group"] = new_vector->group;
    m_settings["vector_symbols"][new_vector->name_and_group]["x"] = x->name;
    m_settings["vector_symbols"][new_vector->name_and_group]["y"] = y->name;
    return new_vector.get();
}

void DbgGui::logMessage(std::string message, MessageType type) {
    if (message.empty()) {
        return;
    }

    std::scoped_lock<std::mutex> lock(m_message_mutex);
    m_all_messages += message;
    if (!message.ends_with('\n')) {
        m_all_messages += '\n';
    }
    if (m_message_queue.size() > 20) {
        m_message_queue.pop_front();
    }
    m_message_queue.push_back(message);

    if (m_message) {
        m_message->text += '\n';
        m_message->text += message;
    } else {
        m_message = Message{.text = std::move(message), .type = type};
    }
}

std::optional<DbgGui::Message> DbgGui::getMessage() {
    std::scoped_lock<std::mutex> lock(m_message_mutex);
    return m_message;
}

void DbgGui::clearMessage() {
    std::scoped_lock<std::mutex> lock(m_message_mutex);
    m_message.reset();
}
