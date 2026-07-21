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

#include "lua_syntax_highlighter.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace {

inline constexpr ImVec4 KeywordColor = ImVec4(0.86f, 0.55f, 0.95f, 1.0f);
inline constexpr ImVec4 StringColor = ImVec4(0.95f, 0.72f, 0.40f, 1.0f);
inline constexpr ImVec4 NumberColor = ImVec4(0.48f, 0.82f, 0.55f, 1.0f);
inline constexpr ImVec4 CommentColor = ImVec4(0.47f, 0.60f, 0.48f, 1.0f);
inline constexpr ImVec4 BuiltinColor = ImVec4(0.38f, 0.72f, 0.96f, 1.0f);

bool isIdentifierStart(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
}

bool isIdentifierCharacter(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

bool isLuaKeyword(std::string_view word) {
    return word == "and" || word == "break" || word == "do" || word == "else" || word == "elseif"
        || word == "end" || word == "false" || word == "for" || word == "function" || word == "goto"
        || word == "if" || word == "in" || word == "local" || word == "nil" || word == "not"
        || word == "or" || word == "repeat" || word == "return" || word == "then" || word == "true"
        || word == "until" || word == "while";
}

bool isLuaBuiltin(std::string_view word) {
    return word == "math" || word == "string" || word == "table" || word == "utf8"
        || word == "read" || word == "read_u" || word == "write" || word == "write_u"
        || word == "exists" || word == "wait" || word == "pause" || word == "save_csv";
}

float textWidth(ImFont* font, float font_size, std::string_view text) {
    float width = 0;
    size_t segment_start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\t') {
            continue;
        }
        width += font->CalcTextSizeA(font_size,
                                     std::numeric_limits<float>::max(),
                                     0.0f,
                                     text.data() + segment_start,
                                     text.data() + i)
                   .x;
        width += font->CalcTextSizeA(font_size, std::numeric_limits<float>::max(), 0.0f, "    ").x;
        segment_start = i + 1;
    }
    width += font->CalcTextSizeA(font_size,
                                 std::numeric_limits<float>::max(),
                                 0.0f,
                                 text.data() + segment_start,
                                 text.data() + text.size())
               .x;
    return width;
}

void drawToken(ImDrawList* draw_list, ImFont* font, float font_size, ImVec2 position, std::string_view token, ImVec4 color) {
    draw_list->AddText(font, font_size, position, ImGui::ColorConvertFloat4ToU32(color), token.data(), token.data() + token.size());
}

template <typename Function>
void forEachLuaToken(std::string_view line, bool& inside_block_comment, Function&& function) {
    size_t index = 0;
    while (index < line.size()) {
        size_t const token_start = index;
        ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

        // Continue a block comment opened on an earlier line. If it closes here,
        // resume normal tokenization after the closing brackets.
        if (inside_block_comment) {
            size_t const close = line.find("]]", index);
            index = close == std::string_view::npos ? line.size() : close + 2;
            if (close != std::string_view::npos) {
                inside_block_comment = false;
            }
            color = CommentColor;
        } else if (index + 1 < line.size() && line[index] == '-' && line[index + 1] == '-') {
            // Lua comments beginning with --[[ may span lines, while any other
            // comment beginning with -- extends only to the end of this line.
            if (line.substr(index, 4) == "--[[") {
                size_t const close = line.find("]]", index + 4);
                index = close == std::string_view::npos ? line.size() : close + 2;
                if (close == std::string_view::npos) {
                    inside_block_comment = true;
                }
            } else {
                index = line.size();
            }
            color = CommentColor;
        } else if (line[index] == '\'' || line[index] == '"') {
            char const quote = line[index++];
            while (index < line.size()) {
                if (line[index] == '\\' && index + 1 < line.size()) {
                    index += 2;
                } else if (line[index++] == quote) {
                    break;
                }
            }
            color = StringColor;
        } else if (isIdentifierStart(line[index])) {
            ++index;
            while (index < line.size() && isIdentifierCharacter(line[index])) {
                ++index;
            }
            std::string_view const word = line.substr(token_start, index - token_start);
            if (isLuaKeyword(word)) {
                color = KeywordColor;
            } else if (isLuaBuiltin(word)) {
                color = BuiltinColor;
            }
        } else if (std::isdigit(static_cast<unsigned char>(line[index]))
                   || (line[index] == '.' && index + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[index + 1])))) {
            ++index;
            while (index < line.size() && (std::isalnum(static_cast<unsigned char>(line[index])) || line[index] == '.' || line[index] == '_')) {
                ++index;
            }
            color = NumberColor;
        } else {
            ++index;
        }

        std::string_view const token = line.substr(token_start, index - token_start);
        function(token, color);
    }
}

void drawLuaLine(ImDrawList* draw_list,
                 ImFont* font,
                 float font_size,
                 std::string_view line,
                 ImVec2 position,
                 bool& inside_block_comment) {
    float x = position.x;
    forEachLuaToken(line, inside_block_comment, [&](std::string_view token, ImVec4 color) {
        drawToken(draw_list, font, font_size, ImVec2(x, position.y), token, color);
        x += textWidth(font, font_size, token);
    });
}

} // namespace

void showLuaHighlightedText(std::string_view text, bool& inside_block_comment) {
    bool first_token = true;
    forEachLuaToken(text, inside_block_comment, [&](std::string_view token, ImVec4 color) {
        if (!first_token) {
            ImGui::SameLine(0, 0);
        }
        ImGui::TextColored(color, "%.*s", static_cast<int>(token.size()), token.data());
        first_token = false;
    });
    if (first_token) {
        ImGui::TextUnformatted("");
    }
}

void drawLuaSyntaxHighlightOverlay(ImDrawList* draw_list,
                                   ImFont* font,
                                   float font_size,
                                   std::string_view text,
                                   ImVec2 editor_min,
                                   ImVec2 editor_max,
                                   ImVec2 scroll,
                                   int cursor_position) {
    ImGuiStyle const& style = ImGui::GetStyle();
    ImVec2 position = ImVec2(editor_min.x + style.FramePadding.x - scroll.x,
                             editor_min.y + style.FramePadding.y - scroll.y);
    float const line_height = font_size;

    draw_list->PushClipRect(editor_min, editor_max, true);
    bool inside_block_comment = false;
    size_t line_start = 0;
    while (line_start <= text.size()) {
        size_t const line_end = text.find('\n', line_start);
        size_t const line_length = (line_end == std::string_view::npos ? text.size() : line_end) - line_start;
        std::string_view const line = text.substr(line_start, line_length);
        if (position.y + line_height >= editor_min.y && position.y <= editor_max.y) {
            drawLuaLine(draw_list, font, font_size, line, position, inside_block_comment);
        } else {
            // Off-screen lines still affect whether visible lines are inside a long comment.
            forEachLuaToken(line, inside_block_comment, [](std::string_view, ImVec4) {});
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
        position.y += line_height;
    }
    if (cursor_position >= 0) {
        size_t const cursor = std::min<size_t>(static_cast<size_t>(cursor_position), text.size());
        size_t const previous_line_end = cursor == 0 ? std::string_view::npos : text.rfind('\n', cursor - 1);
        size_t const cursor_line_start = previous_line_end == std::string_view::npos ? 0 : previous_line_end + 1;
        int const cursor_line = static_cast<int>(std::ranges::count(text.substr(0, cursor), '\n'));
        ImVec2 const cursor_position_on_screen = ImVec2(position.x + textWidth(font, font_size, text.substr(cursor_line_start, cursor - cursor_line_start)),
                                                        editor_min.y + style.FramePadding.y - scroll.y + cursor_line * line_height);
        draw_list->AddLine(cursor_position_on_screen,
                           ImVec2(cursor_position_on_screen.x, cursor_position_on_screen.y + line_height),
                           ImGui::GetColorU32(ImGuiCol_Text));
    }
    draw_list->PopClipRect();
}
