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

#include "script_editor.h"

#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "lua_syntax_highlighter.h"

#include <algorithm>
#include <ranges>
#include <string_view>

namespace {

int scriptLineCount(std::string_view text) {
    return static_cast<int>(std::ranges::count(text, '\n')) + 1;
}

float scriptLineNumberGutterWidth(int line_count) {
    ImGuiStyle const& style = ImGui::GetStyle();
    return ImGui::CalcTextSize(std::to_string(std::max(1, line_count)).c_str()).x + 2.0f * style.FramePadding.x;
}

void drawScriptLineNumberGutter(std::string const& text, ImVec2 min, ImVec2 max, float scroll_y) {
    ImGuiStyle const& style = ImGui::GetStyle();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 const text_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    float const line_height = ImGui::GetFontSize();
    int const line_count = scriptLineCount(text);

    draw_list->PushClipRect(min, max, true);
    for (int line = 1; line <= line_count; ++line) {
        // InputTextMultiline scrolls internally, so draw the gutter with the
        // same vertical scroll instead of making line numbers their own child.
        float const y = min.y + style.FramePadding.y + static_cast<float>(line - 1) * line_height - scroll_y;
        if (y + line_height < min.y) {
            continue;
        }
        if (y > max.y) {
            break;
        }
        std::string line_number = std::to_string(line);
        float const x = max.x - style.FramePadding.x - ImGui::CalcTextSize(line_number.c_str()).x;
        draw_list->AddText(ImVec2(x, y), text_color, line_number.c_str());
    }
    draw_list->PopClipRect();
}

ImGuiWindow* findChildWindowByChildId(ImGuiID child_id) {
    // Only children submitted by this parent in the current frame have a
    // draw list ready to use. Inactive windows may have released their buffers.
    for (ImGuiWindow* window : ImGui::GetCurrentWindow()->DC.ChildWindows) {
        if (window->ChildId == child_id) {
            return window;
        }
    }
    return nullptr;
}

} // namespace

void showRunningScriptWithLineNumbers(std::vector<std::string_view> const& lines, int current_line, bool highlight_lua) {
    int const line_count = static_cast<int>(lines.size());
    float const gutter_width = scriptLineNumberGutterWidth(line_count);
    if (ImGui::BeginTable("##running_script_lines", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##line", ImGuiTableColumnFlags_WidthFixed, gutter_width);
        ImGui::TableSetupColumn("##source", ImGuiTableColumnFlags_WidthStretch);
        bool inside_block_comment = false;
        for (int i = 0; i < line_count; ++i) {
            if (i == current_line) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::Separator();
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::string line_number = std::to_string(i + 1);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gutter_width - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize(line_number.c_str()).x);
            ImGui::TextDisabled("%s", line_number.c_str());
            ImGui::TableNextColumn();
            if (highlight_lua) {
                showLuaHighlightedText(lines[i], inside_block_comment);
            } else {
                ImGui::TextUnformatted(lines[i].data(), lines[i].data() + lines[i].size());
            }
        }
        ImGui::EndTable();
    }
}

ImVec2 scriptEditorSizeToFit(std::string_view text, ImVec2 minimum_size) {
    auto const& style = ImGui::GetStyle();
    float const text_width = ImGui::CalcTextSize(text.data(), text.data() + text.size(), false).x;
    // Leave room for line numbers, input padding, the scrollbar, and the caret.
    float const width = text_width + scriptLineNumberGutterWidth(scriptLineCount(text))
                      + 2 * style.FramePadding.x + style.ScrollbarSize + 1;
    float const viewport_width = ImGui::GetWindowViewport()->WorkSize.x
                               - 2 * (style.WindowPadding.x + style.WindowBorderSize);
    return {std::max(minimum_size.x, std::min(width, viewport_width)), minimum_size.y};
}

void inputScriptTextWithLineNumbers(std::string& text, ImVec2 size, bool highlight_lua) {
    ImGuiID const input_id = ImGui::GetID("##source");
    float const gutter_width = scriptLineNumberGutterWidth(scriptLineCount(text));
    float const editor_width = std::max(1.0f, size.x - gutter_width);

    // Layout order is intentional:
    // 1. Reserve gutter space with Dummy().
    // 2. Submit InputTextMultiline() so ImGui updates its internal scroll state.
    // 3. Draw line numbers into the reserved gutter using that scroll offset.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
    ImVec2 const gutter_min = ImGui::GetCursorScreenPos();
    ImVec2 const gutter_size = ImVec2(gutter_width, size.y);
    ImGui::Dummy(gutter_size);
    ImVec2 const gutter_max = ImVec2(gutter_min.x + gutter_size.x, gutter_min.y + gutter_size.y);
    ImGui::SameLine();
    ImVec2 const editor_min = ImGui::GetCursorScreenPos();
    ImFont* const editor_font = ImGui::GetFont();
    float const editor_font_size = ImGui::GetFontSize();

    if (highlight_lua) {
        // The highlighter redraws every glyph after InputTextMultiline().
        // Hiding InputText's glyphs avoids double anti-aliased text, while its
        // editing, selection, and scrolling behavior remains intact.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    }
    ImGui::InputTextMultiline("##source", &text, ImVec2(editor_width, size.y), ImGuiInputTextFlags_AllowTabInput);
    if (highlight_lua) {
        ImGui::PopStyleColor();
    }

    // InputTextMultiline is implemented as a child window. Its ChildId is the
    // input ID, but its window ID is generated from the parent/window name.
    ImGuiWindow* input_window = findChildWindowByChildId(input_id);
    ImGuiInputTextState* input_state = ImGui::GetInputTextState(input_id);
    float const scroll_y = input_window ? input_window->Scroll.y :
                           input_state  ? input_state->Scroll.y :
                                          0.0f;
    // Horizontal text scrolling belongs to the input state, not its child window.
    // ImGui also retains it while dragging the editor's vertical scrollbar.
    ImGuiID const active_id = ImGui::GetActiveID();
    bool const input_active = active_id == input_id;
    bool const scrollbar_active = input_window && active_id == ImGui::GetWindowScrollbarID(input_window, ImGuiAxis_Y);
    bool const editing = input_active || scrollbar_active;
    float const scroll_x = input_state && editing ? input_state->Scroll.x : 0.0f;
    drawScriptLineNumberGutter(text, gutter_min, gutter_max, scroll_y);
    if (highlight_lua && input_window && !input_window->SkipItems) {
        ImDrawList* draw_list = input_window->DrawList;
        int const cursor_position = input_state && input_active ? input_state->GetCursorPos() : -1;
        drawLuaSyntaxHighlightOverlay(draw_list,
                                      editor_font,
                                      editor_font_size,
                                      text,
                                      editor_min,
                                      ImVec2(editor_min.x + editor_width, editor_min.y + size.y),
                                      ImVec2(scroll_x, scroll_y),
                                      cursor_position);
    }
    ImGui::PopStyleVar();
}
