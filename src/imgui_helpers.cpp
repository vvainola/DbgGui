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
