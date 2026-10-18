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
#include "signal_cleanup.h"
#include "plot_settings.h"
#include "settings_migration.h"
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
std::vector<Scalar*> restoreScalarSettings(Scalar* scalar,
                                           nlohmann::json& settings,
                                           std::vector<PlotWindow>& plot_windows,
                                           std::vector<std::unique_ptr<Scalar>> const& scalars,
                                           std::vector<CustomWindow>& custom_windows) {
    std::vector<Scalar*> scalars_to_sample;
    auto start_sampling = [&scalars_to_sample](Scalar* restored_scalar) {
        if (!contains(scalars_to_sample, restored_scalar)) {
            scalars_to_sample.push_back(restored_scalar);
        }
    };
    // Restore settings of the scalar signal
    TRY(
        auto const& scalar_settings = settings["scalars"];
        auto const scalar_data = scalar_settings.find(scalar->name_and_group);
        if (scalar_data != scalar_settings.end()) {
            scalar->fromJson(*scalar_data);
        }
    )

    // Restore scalar to plots
    TRY(for (auto plot_data : settings["scalar_plots"]) {
        PlotWindow* plot = nullptr;
        for (auto& plot_window : plot_windows) {
            if (plot_window.id == plot_data["id"]) {
                plot = &plot_window;
                break;
            }
        }
        if (plot == nullptr) {
            continue;
        }
        for (Scalar* component : plot_settings::restorePlot(*plot, plot_data, scalars, scalar->id)) {
            start_sampling(component);
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
            plot_settings::forEachSignalId(custom_window_data["signals"], [&](uint64_t id) {
                if (id == scalar->id) {
                    custom->addScalar(scalar);
                }
            });
        }
    })
    return scalars_to_sample;
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

Scalar* DbgGui::findScalarByName(std::string_view name) {
    auto indexed = m_scalars_by_name.find(std::string(name));
    if (indexed != m_scalars_by_name.end()) {
        if (!indexed->second->deleted) {
            return indexed->second;
        }
        m_scalars_by_name.erase(indexed);
    }

    // Scalars are indexed lazily. Find the first live match when no cached
    // entry exists or the cached scalar was deleted.
    for (auto const& scalar : m_scalars) {
        if (!scalar->deleted && scalar->name == name) {
            m_scalars_by_name[scalar->name] = scalar.get();
            return scalar.get();
        }
    }
    return nullptr;
}


constexpr int SETTINGS_CHECK_INTERVAL_MS = 500;

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
    constexpr double SYNC_INTERVAL = 30e-3;
    static steady_clock::time_point next_sync_time = steady_clock::now();

    auto now = steady_clock::now();
    // Continue without blocking until either simulation time reaches its next
    // checkpoint or the wall-clock interval expires.
    if (m_sample_timestamp <= m_next_sync_timestamp
        && now < next_sync_time) {
        return;
    }

    // If simulation time reached its checkpoint early, wait out the
    // remaining wall-clock time. sleep_until returns immediately when the
    // application is already running slower than the requested speed.
    std::this_thread::sleep_until(next_sync_time);
    now = steady_clock::now();

    next_sync_time = now + milliseconds(30);
    m_next_sync_timestamp = m_sample_timestamp + SYNC_INTERVAL * m_simulation_speed;
}

void DbgGui::sample() {
    sampleWithTimestamp(m_sample_timestamp + m_sampling_time);
}

void DbgGui::sampleWithTimestamp(double timestamp) {
    // No point sampling if window has been closed
    if (isClosed()) {
        return;
    }

    {
        // Sample scalars
        std::scoped_lock<std::mutex> lock(m_sampling_mutex);
        if (timestamp < m_sample_timestamp) {
            double const time_offset = timestamp - m_sample_timestamp;
            m_sampler.shiftTime(time_offset);
            for (ScriptWindow& script_window : m_script_windows) {
                script_window.shiftScriptSchedule(time_offset);
            }
            for (auto const& action : m_panel.actions) {
                if (auto state = action.lock()) {
                    state->runner.shiftSchedule(time_offset);
                }
            }
            m_next_sync_timestamp = 0;
        }
        m_sample_timestamp = timestamp;

        for (ScriptWindow& script_window : m_script_windows) {
            if (std::string const error = script_window.processScript(m_sample_timestamp); !error.empty()) {
                logMessage(error);
            }
        }
        processPanelScripts(m_sample_timestamp);
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
        {
            std::scoped_lock lock(m_sampling_mutex);
            processPanelScripts(m_sample_timestamp);
        }
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
        start_pause_action = [&] {
            m_paused = !m_paused;
        };
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
      {"add-plot", "Add plot", "Open the add-plot dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_1, [&] { ImGui::OpenPopup(str::ADD_PLOT); }},
      {"add-custom-window", "Add custom window", "Open the add-custom-window dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_4, [&] { ImGui::OpenPopup(str::ADD_CUSTOM_WINDOW); }},
      {"add-dockspace", "Add dockspace", "Open the add-dockspace dialog.", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_6, [&] { ImGui::OpenPopup(str::ADD_DOCKSPACE); }},
      {"add-panel", "Add panel", "Open the add-panel dialog.", ImGuiKey_None, [&] { ImGui::OpenPopup(str::ADD_PANEL); }},
      {"add-grid-window", "Add grid window", "Open the add-grid-window dialog.", ImGuiKey_None, [&] { ImGui::OpenPopup(str::ADD_GRID_WINDOW); }},
      {"copy-visible-samples", "Copy visible samples to clipboard", "Copy visible scalar samples for import into CsvPlotter.", ImGuiMod_Ctrl | ImGuiKey_T, [&] { copyAllScalarSamplesToClipboard(); }},
      {"save-plots-csv", "Save all plots as CSV", "Export visible scalar samples from all plots to a CSV file.", ImGuiKey_None, save_all_plots_as_csv},
      {"reset-plot-colors", "Reset plot colors", "Reassign colors for all plot items from the current colormap.", ImGuiKey_None, [&] { ImPlot::BustColorCache(); }},
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

    // Arm the pause only on start-up.
    m_pause_at_time = m_options.pause_at_time;

    extern unsigned int calibri_compressed_size;
    extern unsigned int calibri_compressed_data[];
    io.Fonts->AddFontFromMemoryCompressedTTF(calibri_compressed_data, calibri_compressed_size, MIN_FONT_SIZE);
    ImGui::PushFont(ImGui::GetDefaultFont(), m_options.font_size);

    {
        std::scoped_lock lock(m_pending_gui_operations_mutex);
        m_accepting_gui_operations = true;
    }
    m_initialized = true;

    //---------- Actual update loop ----------
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Runtime additions must mutate signal containers on the GUI thread
        // before any window traverses them during this frame.
        processPendingGuiOperations();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        // ImGui::ShowDemoWindow();
        // ImPlot::ShowDemoWindow();

        //---------- Hotkeys ----------
        std::vector<CommandPaletteCommand> commands = commandPaletteCommands(!ImGui::IsAnyItemActive());
        triggerCommandHotkeys("Command Palette", commands, m_hotkey_overrides);
        showCommandPalette();
        addPopupModal(str::ADD_PLOT);
        addPopupModal(str::ADD_CUSTOM_WINDOW);
        addPopupModal(str::ADD_DOCKSPACE);
        addPopupModal(str::ADD_GRID_WINDOW);
        addPopupModal(str::ADD_PANEL);
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
        showCustomWindow();
        showSymbolsWindow();
        showScriptWindow();
        showGridWindow();
        showPanels();
        showPlots();
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

    stopPendingGuiOperations();

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

std::shared_ptr<DbgGui::PendingGuiOperation> DbgGui::runOnGuiThread(std::function<void()> operation) {
    auto request = std::make_shared<PendingGuiOperation>();
    request->operation = std::move(operation);

    if (std::this_thread::get_id() == m_gui_thread.get_id()) {
        try {
            request->operation();
        } catch (...) {
            request->exception = std::current_exception();
        }
        request->completed.store(true);
        request->completed.notify_one();
        return request;
    }
    if (!m_gui_thread.joinable()) {
        if (!m_initialized.load()) {
            try {
                request->operation();
            } catch (...) {
                request->exception = std::current_exception();
            }
        } else {
            request->exception = std::make_exception_ptr(
              std::runtime_error("DbgGui GUI thread is shutting down"));
        }
        request->completed.store(true);
        request->completed.notify_one();
        return request;
    }

    std::scoped_lock lock(m_pending_gui_operations_mutex);
    if (m_accepting_gui_operations) {
        m_pending_gui_operations.push_back(request);
    } else {
        request->exception = std::make_exception_ptr(
          std::runtime_error("DbgGui GUI thread is shutting down"));
        request->completed.store(true);
        request->completed.notify_one();
    }
    return request;
}

void DbgGui::runOnGuiThreadAndWait(std::function<void()> operation) {
    auto request = runOnGuiThread(std::move(operation));
    request->completed.wait(false);
    if (request->exception) {
        std::rethrow_exception(request->exception);
    }
}

void DbgGui::processPendingGuiOperations() {
    std::unique_lock lock(m_pending_gui_operations_mutex);
    for (auto const& request : m_pending_gui_operations) {
        try {
            request->operation();
        } catch (...) {
            request->exception = std::current_exception();
        }

        request->completed.store(true);
        request->completed.notify_one();
    }
    m_pending_gui_operations.clear();
}

void DbgGui::stopPendingGuiOperations() {
    std::scoped_lock lock(m_pending_gui_operations_mutex);
    m_accepting_gui_operations = false;
    for (auto const& request : m_pending_gui_operations) {
        request->exception = std::make_exception_ptr(
          std::runtime_error("DbgGui GUI thread is shutting down"));
        request->completed.store(true);
        request->completed.notify_one();
    }
    m_pending_gui_operations.clear();
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
        try {
            auto settings = nlohmann::json::parse(f);
            f.close();
            if (settings_migration::migrate(settings)) {
                // Replace the file only after the complete migration succeeds.
                std::string const temporary_path = settings_path + ".migration.tmp";
                std::ofstream output;
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output.open(temporary_path);
                output << std::setw(4) << settings;
                output.close();
                std::filesystem::rename(temporary_path, settings_path);
                m_last_settings_write_time = std::filesystem::last_write_time(settings_path);
            }
            m_settings = std::move(settings);
            m_settings_saved = m_settings;
            m_settings_load_failed = false;
        } catch (std::exception const& error) {
            m_settings_load_failed = true;
            logMessage(std::format("Could not load settings: {}. Automatic settings saving is disabled.", error.what()));
            return;
        }

        if (m_settings.contains("layout")) {
            std::string layout = m_settings["layout"];
            ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
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

        // Restore buffer size and window geometry only at startup, not from parallel processes.
        static bool once = false;
        if (!once) {
            once = true;
            m_sampler.setCapacity(m_options.sampling_buffer_size);
            TRY(int xpos = std::max(0, int(m_settings["window"]["xpos"]));
                int ypos = std::max(0, int(m_settings["window"]["ypos"]));
                glfwRestoreWindow(m_window);
                glfwSetWindowPos(m_window, xpos, ypos);
                glfwSetWindowSize(m_window, m_settings["window"]["width"], m_settings["window"]["height"]);
                if (m_settings["window"].value("maximized", false)) {
                    glfwMaximizeWindow(m_window);
                })
        }

        TRY(m_window_focus.scalars.initial_focus = m_settings["initial_focus"]["scalars"];)
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

        m_plot_windows.clear();
        for (auto& plot_data : m_settings["scalar_plots"]) {
            PlotWindow& plot = m_plot_windows.emplace_back(plot_data);
            for (Scalar* component : plot_settings::restorePlot(plot, plot_data, m_scalars)) {
                m_sampler.startSampling(component);
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
            plot_settings::forEachSignalId(custom_window_data["signals"], [&](uint64_t id) {
                Scalar* scalar = findScalar(m_scalars, id);
                if (scalar) {
                    custom_window.addScalar(scalar);
                }
            });
        }

        m_script_windows.clear();
        for (auto script_window_data : m_settings["script_windows"]) {
            ScriptWindow& script_window = m_script_windows.emplace_back(this, script_window_data);
            if (script_window.id == 0) {
                script_window.id = hashWithTime(script_window.name);
            }
            if (script_window.run_on_startup) {
                logMessage(script_window.startScript(m_sample_timestamp, m_scalars));
            }
        }
        m_selected_script_id.reset();
        if (m_settings.contains("selected_script_id") && m_settings["selected_script_id"].is_number_unsigned()) {
            uint64_t const selected_script_id = m_settings["selected_script_id"].get<uint64_t>();
            auto const selected_script = std::ranges::find(m_script_windows, selected_script_id, &ScriptWindow::id);
            if (selected_script != m_script_windows.end()) {
                m_selected_script_id = selected_script_id;
            }
        }

        m_panels.clear();
        for (auto const& panel_data : m_settings["panels"]) {
            m_panels.emplace_back(panel_data);
        }
        restorePanelPlotSettings();

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
}

void DbgGui::reconcilePlotSettings(PlotWindow& plot, nlohmann::json& saved) {
    for (int subplot_idx = 0; subplot_idx < plot.subplotCount(); ++subplot_idx) {
        removeDeletedVectorComponents(plot.subplots[subplot_idx]);
        auto& subplot = plot.subplots[subplot_idx];
        removeDeletedSpectrumComponents(subplot);
        std::erase_if(subplot.scalars, [](Scalar* scalar) { return !isLiveScalar(scalar); });
        auto& saved_subplot = saved["subplots"][subplot_idx];
        plot_settings::reconcileVectors(subplot, saved_subplot, m_scalars);
        plot_settings::reconcileSpectra(subplot, saved_subplot, m_scalars);
        plot_settings::reconcileScalars(subplot, saved_subplot, m_scalars);
    }
}

void DbgGui::restorePanelPlotSettings(std::optional<uint64_t> registered_id) {
    for (auto const& saved_panel : m_settings["panels"]) {
        auto panel = std::ranges::find(m_panels, saved_panel.value("id", uint64_t{0}), &PanelWindow::id);
        if (panel == m_panels.end()) {
            continue;
        }
        for (auto const& saved_widget : saved_panel.value("widgets", nlohmann::json::array())) {
            auto widget = std::ranges::find(panel->widgets, saved_widget.value("id", uint64_t{0}), &PanelWidget::id);
            if (widget == panel->widgets.end() || !widget->plot || !saved_widget.contains("plot")) {
                continue;
            }
            for (auto* scalar : plot_settings::restorePlot(*widget->plot, saved_widget["plot"], m_scalars, registered_id)) {
                m_sampler.startSampling(scalar);
            }
        }
    }
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
        m_settings_load_failed = false;
        // Maximized/minimized windows cannot supply their normal restore geometry.
        auto window_settings = m_settings["window"];
        m_settings.clear();
        m_settings["window"] = std::move(window_settings);
        m_settings_saved.clear();
        // Restore symbols
        for (auto const& scalar : m_scalars) {
            VariantSymbol* scalar_sym = m_symbols.getSymbol(scalar->name);
            if (scalar_sym) {
                m_settings["scalar_symbols"][scalar->name_and_group]["name"] = scalar->name;
                m_settings["scalar_symbols"][scalar->name_and_group]["group"] = scalar->group;
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
                auto window_settings = m_settings["window"];
                loadPreviousSessionSettings();
                m_settings["window"] = std::move(window_settings);
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

    bool const maximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED);
    bool const iconified = glfwGetWindowAttrib(m_window, GLFW_ICONIFIED);
    if (!iconified) {
        m_settings["window"]["maximized"] = maximized;
    }
    if (!iconified && !maximized) {
        m_settings["window"]["width"] = width;
        m_settings["window"]["height"] = height;
        m_settings["window"]["xpos"] = xpos;
        m_settings["window"]["ypos"] = ypos;
    }
    m_settings["format_version"] = settings_migration::FORMAT_VERSION;
    m_settings["options"] = m_options.toJson();
    m_settings["hotkeys"] = nlohmann::json::object();
    for (auto const& [command_id, hotkey] : m_hotkey_overrides) {
        m_settings["hotkeys"][command_id] = hotkey;
    }
    m_settings["initial_focus"]["scalars"] = m_window_focus.scalars.focused;
    m_settings["initial_focus"]["symbols"] = m_window_focus.symbols.focused;
    m_settings["initial_focus"]["log"] = m_window_focus.log.focused;

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

    for (PlotWindow& plot_window : m_plot_windows) {
        if (!plot_window.open) {
            m_settings["scalar_plots"].erase(std::to_string(plot_window.id));
            continue;
        }
        if (plot_window.id == 0) {
            plot_window.id = hashWithTime(plot_window.name);
        }
        nlohmann::json& j = m_settings["scalar_plots"][std::to_string(plot_window.id)];
        plot_window.updateJson(j);
        reconcilePlotSettings(plot_window, j);
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
        for (Scalar* scalar : custom_window.scalars) {
            if (scalar->deleted) {
                j["signals"].erase(scalar->group + " " + scalar->name);
            }
        }
        std::erase_if(custom_window.scalars, [](Scalar* scalar) { return !isLiveScalar(scalar); });
        for (Scalar* scalar : custom_window.scalars) {
            // use group first in key so that the signals are sorted alphabetically by group
            j["signals"][scalar->group + " " + scalar->name] = scalar->id;
        }
    }

    {
        std::scoped_lock<std::mutex> lock(m_sampling_mutex);
        // Arrays preserve the order chosen in the Scripts window. Loading still
        // accepts the legacy object format because both are iterable as scripts.
        nlohmann::json script_windows = nlohmann::json::array();
        for (ScriptWindow const& script_window : m_script_windows) {
            nlohmann::json script_window_json;
            script_window.updateJson(script_window_json);
            script_windows.push_back(std::move(script_window_json));
        }
        m_settings["script_windows"] = std::move(script_windows);
        if (m_selected_script_id.has_value()) {
            m_settings["selected_script_id"] = *m_selected_script_id;
        } else {
            m_settings.erase("selected_script_id");
        }
    }

    auto saved_panels = nlohmann::json::array();
    for (PanelWindow& panel : m_panels) {
        if (!panel.open) {
            continue;
        }
        // Preserve widget/canvas settings until those parts are restored too.
        auto panel_data = nlohmann::json::object();
        for (auto const& saved : m_settings["panels"]) {
            if (saved.value("id", uint64_t{0}) == panel.id) {
                panel_data = saved;
                break;
            }
        }
        panel.updateJson(panel_data);
        for (size_t index = 0; index < panel.widgets.size(); ++index) {
            if (panel.widgets[index].plot) {
                reconcilePlotSettings(*panel.widgets[index].plot, panel_data["widgets"][index]["plot"]);
            }
        }
        saved_panels.push_back(std::move(panel_data));
    }
    m_settings["panels"] = std::move(saved_panels);

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
            // Erase only this scalar's cached entry. If another live scalar has
            // the same name, the next Lua lookup finds and caches it lazily.
            auto indexed = m_scalars_by_name.find(scalar->name);
            if (indexed != m_scalars_by_name.end() && indexed->second == scalar.get()) {
                m_scalars_by_name.erase(indexed);
            }
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
    if (!m_settings_load_failed
        && !closing
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
    for (PlotWindow& plot_window : m_plot_windows) {
        ImGui::Begin(plot_window.title().c_str());
        if (plot_window.focus.initial_focus) {
            ImGui::SetWindowFocus(plot_window.title().c_str());
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
    for (PanelWindow& panel : m_panels) {
        ImGui::Begin(panel.title().c_str());
        if (panel.focus.initial_focus) {
            ImGui::SetWindowFocus(panel.title().c_str());
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
    auto scale_it = m_symbol_scale_settings.find(sym->getFullName());
    std::string const scale = scale_it == m_symbol_scale_settings.end() ? "1" : scale_it->second;
    Scalar* scalar = addScalarExpression(sym->getValueSource(), group, sym->getFullName(), scale, "0");
    scalar->read_only = sym->isConst();
    m_settings["scalar_symbols"][scalar->name_and_group]["name"] = scalar->name;
    m_settings["scalar_symbols"][scalar->name_and_group]["group"] = scalar->group;
    return scalar;
}

bool DbgGui::isClosed() {
    return m_initialized && (m_window == nullptr);
}

void DbgGui::close() {
    m_next_sync_timestamp = 0;
    m_closing = true;
    stopPendingGuiOperations();
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
    // Once running, the GUI thread owns the signal containers and settings
    // touched by the complete add operation.
    if (m_gui_thread.joinable() && std::this_thread::get_id() != m_gui_thread.get_id()) {
        Scalar* result = nullptr;
        runOnGuiThreadAndWait([this, &result, symbol_name, group = std::move(group), alias, scale, offset] {
            result = addSymbol(symbol_name, group, alias, scale, offset);
        });
        return result;
    }

    VariantSymbol* sym = m_symbols.getSymbol(symbol_name);
    if (sym) {
        Scalar* ptr = addScalar(sym->getValueSource(), group, symbol_name, scale, offset);
        ptr->read_only = sym->isConst();
        ptr->alias = alias;
        ptr->updateDisplayNames();
        m_settings["scalar_symbols"][ptr->name_and_group]["name"] = ptr->name;
        m_settings["scalar_symbols"][ptr->name_and_group]["group"] = ptr->group;
        return ptr;
    }
    return nullptr;
}

Scalar* DbgGui::addScalar(ValueSource const& src, std::string group, std::string const& name, double scale, double offset) {
    if (m_gui_thread.joinable() && std::this_thread::get_id() != m_gui_thread.get_id()) {
        Scalar* result = nullptr;
        runOnGuiThreadAndWait([this, &result, src, group = std::move(group), name, scale, offset] {
            result = addScalar(src, group, name, scale, offset);
        });
        return result;
    }

    return addScalarExpression(src,
                               std::move(group),
                               name,
                               std::format("{:g}", scale),
                               std::format("{:g}", offset));
}

void DbgGui::addScalarAsync(ValueSource src, std::string group, std::string name, double scale, double offset) {
    runOnGuiThread([this,
                    src = std::move(src),
                    group = std::move(group),
                    name = std::move(name),
                    scale,
                    offset] {
        addScalar(src, std::move(group), name, scale, offset);
    });
}

Scalar* DbgGui::addScalarExpression(ValueSource const& src,
                                    std::string group,
                                    std::string const& name,
                                    std::string const& scale,
                                    std::string const& offset) {
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
    new_scalar->updateDisplayNames();
    new_scalar->id = id;
    new_scalar->setScaleStr(scale);
    new_scalar->setOffsetStr(offset);
    if (m_initialized.load()) {
        std::vector<Scalar*> const scalars_to_sample = restoreScalarSettings(new_scalar.get(),
                                                                             m_settings,
                                                                             m_plot_windows,
                                                                             m_scalars,
                                                                             m_custom_windows);
        for (Scalar* scalar_to_sample : scalars_to_sample) {
            m_sampler.startSampling(scalar_to_sample);
        }
        restorePanelPlotSettings(new_scalar->id);
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
    std::sort(added_group->signals.begin(), added_group->signals.end(), [](Scalar* a, Scalar* b) { return a->name < b->name; });
    return new_scalar.get();
}

void DbgGui::moveScalarToGroup(Scalar* scalar, SignalGroup<Scalar>& source, SignalGroup<Scalar>& destination) {
    if (&source == &destination) {
        return;
    }
    uint64_t const new_id = signalId(scalar->name, destination.full_name);
    if (findScalar(m_scalars, new_id)) {
        return;
    }
    std::scoped_lock lock(m_sampling_mutex);
    uint64_t const old_id = scalar->id;
    std::string const old_group = scalar->group;
    // Remove browser membership before changing the group. The scalar itself
    // stays alive, so sampled data and pointer-based placements remain valid.
    remove(source.signals, scalar);
    scalar->group = destination.full_name;
    scalar->id = new_id;
    scalar->updateDisplayNames();
    destination.signals.push_back(scalar);
    std::sort(destination.signals.begin(), destination.signals.end(), [](Scalar* a, Scalar* b) { return a->name < b->name; });

    plot_settings::remapScalarId(m_settings, old_id, old_group, *scalar);
    for (auto& panel : m_panels) {
        for (auto& widget : panel.widgets) {
            for (auto& cell : widget.sliders.cells) {
                if (cell.scalar_id == old_id) {
                    cell.scalar_id = new_id;
                }
            }
            for (auto& cell : widget.values.cells) {
                if (cell.scalar_id == old_id) {
                    cell.scalar_id = new_id;
                }
            }
        }
    }
    for (auto& window : m_custom_windows) {
        window.sortSignals();
    }
    for (auto& window : m_grid_windows) {
        for (auto& row : window.scalars) {
            for (auto& id : row) {
                if (id == old_id) {
                    id = new_id;
                }
            }
        }
    }
}

VectorPair DbgGui::addVector(ValueSource const& x, ValueSource const& y, std::string group, std::string const& name_x, std::string const& name_y, double scale, double offset) {
    if (m_gui_thread.joinable() && std::this_thread::get_id() != m_gui_thread.get_id()) {
        VectorPair result{};
        runOnGuiThreadAndWait([this, &result, x, y, group = std::move(group), name_x, name_y, scale, offset] {
            result = addVector(x, y, group, name_x, name_y, scale, offset);
        });
        return result;
    }
    if (group.empty()) {
        group = "debug";
    }
    VectorPair pair{
        addScalarExpression(x, group, name_x, std::format("{:g}", scale), std::format("{:g}", offset)),
        addScalarExpression(y, group, name_y, std::format("{:g}", scale), std::format("{:g}", offset))
    };
    return pair;
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
