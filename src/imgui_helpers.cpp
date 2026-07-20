// MIT License
//
// Copyright (c) 2026 vvainola
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

#include "imgui_helpers.h"

#include "imgui_stdlib.h"
#include "str_helpers.h"

#include <map>
#include <string>

namespace {

std::optional<char> numberKeyCharacter(ImGuiKey key) {
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
        return static_cast<char>('0' + key - ImGuiKey_0);
    }
    if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
        return static_cast<char>('0' + key - ImGuiKey_Keypad0);
    }
    if (key == ImGuiKey_Minus) {
        return '-';
    }
    return std::nullopt;
}

bool matchesCommand(CommandPaletteCommand const& command,
                    std::string const& filter,
                    CommandHotkeyOverrides const& overrides) {
    return filter.empty()
        || str::fuzzy_match(filter, command.name)
        || str::fuzzy_match(filter, commandHotkeyName(command, overrides))
        || str::fuzzy_match(filter, command.description);
}

struct CommandPaletteState {
    std::string filter;
    std::string selected_command_name;
    std::string edited_command_id;
    ImGuiKeyChord captured_hotkey = ImGuiKey_None;
};

bool isModifierKey(ImGuiKey key) {
    return key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl
        || key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift
        || key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt
        || key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper;
}

std::optional<ImGuiKeyChord> pressedHotkey() {
    for (int key_value = ImGuiKey_NamedKey_BEGIN; key_value < ImGuiKey_GamepadStart; ++key_value) {
        ImGuiKey key = ImGuiKey(key_value);
        if (isModifierKey(key) || !ImGui::IsKeyPressed(key, false)) {
            continue;
        }
        return ImGuiKeyChord(key | ImGui::GetIO().KeyMods);
    }
    return std::nullopt;
}

void setCommandHotkey(CommandPaletteCommand const& command,
                      ImGuiKeyChord hotkey,
                      std::span<CommandPaletteCommand const> commands,
                      CommandHotkeyOverrides& overrides) {
    if (hotkey != ImGuiKey_None) {
        for (CommandPaletteCommand const& other : commands) {
            if (other.id == command.id || effectiveCommandHotkey(other, overrides) != hotkey) {
                continue;
            }
            overrides[std::string(other.id)] = ImGuiKey_None;
        }
    }
    if (hotkey == command.default_hotkey) {
        overrides.erase(std::string(command.id));
    } else {
        overrides[std::string(command.id)] = hotkey;
    }
}

} // namespace

void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool ImRightAlign(const char* str_id) {
    if (ImGui::BeginTable(str_id, 2, ImGuiTableFlags_SizingFixedFit, ImVec2(-1, 0))) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        return true;
    }
    return false;
}

void ImEndRightAlign() {
    ImGui::EndTable();
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

std::optional<ImGuiKey> pressedNumberKey(bool include_minus) {
    for (int key = ImGuiKey_0; key <= ImGuiKey_9; key++) {
        if (ImGui::IsKeyPressed(ImGuiKey(key))) {
            return ImGuiKey(key);
        }
    }
    for (int key = ImGuiKey_Keypad0; key <= ImGuiKey_Keypad9; key++) {
        if (ImGui::IsKeyPressed(ImGuiKey(key))) {
            return ImGuiKey(key);
        }
    }
    if (include_minus && ImGui::IsKeyPressed(ImGuiKey_Minus)) {
        return ImGuiKey_Minus;
    }
    return std::nullopt;
}

int setCursorOnFirstNumberPress(ImGuiInputTextCallbackData* data) {
    auto* pressed_key = static_cast<ImGuiKey*>(data->UserData);
    if (pressed_key == nullptr || *pressed_key == ImGuiKey_None) {
        return 0;
    }

    std::optional<char> key_character = numberKeyCharacter(*pressed_key);
    *pressed_key = ImGuiKey_None;
    if (!key_character || data->BufSize < 2) {
        return 0;
    }

    data->Buf[0] = *key_character;
    data->Buf[1] = '\0';
    data->BufTextLen = 1;
    data->BufDirty = true;
    data->CursorPos = 1;
    data->SelectionStart = 1;
    data->SelectionEnd = 1;
    return 0;
}

bool isValidCommandHotkey(ImGuiKeyChord hotkey) {
    if (hotkey == ImGuiKey_None) {
        return true;
    }
    ImGuiKey key = ImGuiKey(hotkey & ~ImGuiMod_Mask_);
    return key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_GamepadStart && !isModifierKey(key);
}

ImGuiKeyChord effectiveCommandHotkey(CommandPaletteCommand const& command,
                                     CommandHotkeyOverrides const& overrides) {
    auto it = overrides.find(std::string(command.id));
    return it == overrides.end() ? command.default_hotkey : it->second;
}

std::string commandHotkeyName(CommandPaletteCommand const& command,
                              CommandHotkeyOverrides const& overrides) {
    ImGuiKeyChord hotkey = effectiveCommandHotkey(command, overrides);
    if (hotkey == ImGuiKey_None) {
        return "";
    }

    std::string result;
    if (hotkey & ImGuiMod_Ctrl) {
        result += "Ctrl+";
    }
    if (hotkey & ImGuiMod_Shift) {
        result += "Shift+";
    }
    if (hotkey & ImGuiMod_Alt) {
        result += "Alt+";
    }
    if (hotkey & ImGuiMod_Super) {
        result += "Super+";
    }
    result += ImGui::GetKeyName(ImGuiKey(hotkey & ~ImGuiMod_Mask_));
    return result;
}

void triggerCommandHotkeys(char const* title,
                           std::span<CommandPaletteCommand const> commands,
                           CommandHotkeyOverrides const& overrides) {
    if (ImGui::IsPopupOpen(title)) {
        return;
    }
    for (CommandPaletteCommand const& command : commands) {
        ImGuiKeyChord hotkey = effectiveCommandHotkey(command, overrides);
        if (!command.action || hotkey == ImGuiKey_None) {
            continue;
        }
        ImGuiKey key = ImGuiKey(hotkey & ~ImGuiMod_Mask_);
        ImGuiKey modifiers = ImGuiKey(hotkey & ImGuiMod_Mask_);
        if (ImGui::GetIO().KeyMods == modifiers && ImGui::IsKeyPressed(key, command.repeatHotkey)) {
            command.action();
            return;
        }
    }
}

std::optional<size_t> showCommandPaletteTable(char const* title,
                                               std::span<CommandPaletteCommand const> commands,
                                               CommandHotkeyOverrides& overrides) {
    static std::map<std::string, CommandPaletteState> states;
    CommandPaletteState& state = states[title];

    ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return std::nullopt;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        state.filter.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##CommandFilter", &state.filter);
    ImGui::Separator();

    bool const enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    CommandPaletteCommand const* first_matching_action = nullptr;
    CommandPaletteCommand const* selected_action = nullptr;
    CommandPaletteCommand const* command_to_execute = nullptr;
    std::optional<size_t> command_to_execute_index;
    bool open_hotkey_editor = false;

    for (CommandPaletteCommand const& command : commands) {
        if (!command.action || !matchesCommand(command, state.filter, overrides)) {
            continue;
        }
        if (first_matching_action == nullptr) {
            first_matching_action = &command;
        }
        if (!state.selected_command_name.empty()
            && command.name == std::string_view(state.selected_command_name)) {
            selected_action = &command;
            break;
        }
    }
    if (selected_action == nullptr && !state.filter.empty()) {
        selected_action = first_matching_action;
    }

    if (ImGui::BeginTable("##CommandPaletteTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Ctrl+Shift+1").x);

        int visible_count = 0;
        for (size_t i = 0; i < commands.size(); ++i) {
            CommandPaletteCommand const& command = commands[i];
            if (!matchesCommand(command, state.filter, overrides)) {
                continue;
            }
            ++visible_count;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool const has_action = bool(command.action);
            if (has_action) {
                bool const is_selected = &command == selected_action;
                std::string label = std::string(command.name) + "##command_" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    command_to_execute = &command;
                    command_to_execute_index = i;
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Change hotkey")) {
                        state.edited_command_id = command.id;
                        state.captured_hotkey = effectiveCommandHotkey(command, overrides);
                        open_hotkey_editor = true;
                    }
                    ImGui::EndPopup();
                }
                if (is_selected && ImGui::IsWindowAppearing()) {
                    ImGui::SetScrollHereY();
                }
            } else {
                ImGui::TextDisabled("Tip: %.*s", int(command.name.size()), command.name.data());
            }
            if (!command.description.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                ImGui::TextUnformatted(command.description.data(), command.description.data() + command.description.size());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(1);
            std::string hotkey_name = commandHotkeyName(command, overrides);
            ImGui::TextDisabled("%s", hotkey_name.c_str());
        }

        if (visible_count == 0) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("No matching commands");
        }

        ImGui::EndTable();
    }

    if (command_to_execute == nullptr && enter_pressed) {
        command_to_execute = selected_action;
        if (command_to_execute != nullptr) {
            command_to_execute_index = size_t(command_to_execute - commands.data());
        }
    }
    if (command_to_execute != nullptr) {
        state.selected_command_name = command_to_execute->name;
        state.filter.clear();
        ImGui::CloseCurrentPopup();
    }

    if (open_hotkey_editor) {
        ImGui::OpenPopup("Edit hotkey");
    }

    CommandPaletteCommand const* edited_command = nullptr;
    for (CommandPaletteCommand const& command : commands) {
        if (command.id == state.edited_command_id) {
            edited_command = &command;
            break;
        }
    }
    if (edited_command != nullptr && ImGui::BeginPopupModal("Edit hotkey", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            state.edited_command_id.clear();
            ImGui::CloseCurrentPopup();
        } else if (std::optional<ImGuiKeyChord> hotkey = pressedHotkey()) {
            state.captured_hotkey = *hotkey;
        }

        ImGui::Text("%.*s", int(edited_command->name.size()), edited_command->name.data());
        ImGui::TextDisabled("Press a key combination to change the active hotkey.");
        std::string captured_name = commandHotkeyName(
          CommandPaletteCommand{.id = edited_command->id, .default_hotkey = state.captured_hotkey}, {});
        ImGui::Text("Hotkey: %s", captured_name.empty() ? "None" : captured_name.c_str());
        ImGui::Separator();
        if (ImGui::Button("Save")) {
            setCommandHotkey(*edited_command, state.captured_hotkey, commands, overrides);
            state.edited_command_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            state.captured_hotkey = ImGuiKey_None;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            state.captured_hotkey = edited_command->default_hotkey;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.edited_command_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndPopup();
    return command_to_execute_index;
}
