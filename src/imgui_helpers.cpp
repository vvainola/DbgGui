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

bool matchesCommand(CommandPaletteCommand const& command, std::string const& filter) {
    return filter.empty()
        || str::fuzzy_match(filter, command.name)
        || str::fuzzy_match(filter, command.shortcut)
        || str::fuzzy_match(filter, command.description);
}

struct CommandPaletteState {
    std::string filter;
    std::string selected_command_name;
};

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

void showCommandPaletteTable(char const* title, std::span<CommandPaletteCommand const> commands) {
    static std::map<std::string, CommandPaletteState> states;
    CommandPaletteState& state = states[title];

    ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
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

    for (CommandPaletteCommand const& command : commands) {
        if (!command.action || !matchesCommand(command, state.filter)) {
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
            if (!matchesCommand(command, state.filter)) {
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
            ImGui::TextDisabled("%.*s", int(command.shortcut.size()), command.shortcut.data());
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
    }
    if (command_to_execute != nullptr) {
        state.selected_command_name = command_to_execute->name;
        state.filter.clear();
        command_to_execute->action();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
