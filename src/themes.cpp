// MIT License
//
// Copyright (c) 2023 vvainola
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

#include "themes.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "imgui.h"
#include "implot.h"
#include "magic_enum.hpp"
#include <format>

void setDarkTitleBar(GLFWwindow* window);
void setTheme(Theme theme);

void themeCombo(Theme& theme, GLFWwindow* window) {
    // Collect names of themes
    std::vector<std::string> themes;
    for (int i = 0; i < (int)Theme::THEME_COUNT; ++i) {
        themes.push_back(std::string(magic_enum::enum_name<Theme>((Theme)i)));
    }
    // Limit theme value to avoid accessing out of bounds
    theme = (Theme)std::min(std::max((int)theme, 0), (int)Theme::THEME_COUNT - 1);

    if (ImGui::BeginCombo("Theme", themes[(int)theme].c_str())) {
        for (int n = 0; n < themes.size(); n++) {
            bool is_selected = (n == (int)theme);
            if (ImGui::Selectable(themes[n].c_str(), is_selected)) {
                theme = (Theme)n;
                setTheme(theme, window);
            }
            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void setTheme(Theme theme, GLFWwindow* window) {
    static bool once = true;
    if (once) {
        setDarkTitleBar(window);
        once = false;
    }
    setTheme(theme);

    auto& style = ImGui::GetStyle();
    style.WindowPadding.x = 1;
    style.WindowPadding.y = 5;
    style.FramePadding.x = 6;
    style.FramePadding.y = 2;
    style.CellPadding.x = 4;
    style.CellPadding.y = 1;
    style.ItemSpacing.x = 4;
    style.ItemSpacing.y = 4;
    style.IndentSpacing = 20;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    // When viewports are enabled we tweak WindowRounding/WindowBg so platform
    // windows can look identical to regular ones.
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(5, 5));
    auto& implot_style = ImPlot::GetStyle();
    implot_style.Colors[ImPlotCol_LegendBg] = ImVec4(0, 0, 0, 0);
}

void setDarkTitleBar(GLFWwindow* window) {
    // https://github.com/ysc3839/win32-darkmode/blob/cc26549b65b25d6f3168a80238792545bd401271/win32-darkmode/DarkMode.h#L107-L123
    // https://gaellalire.fr/gitlab/vestige/vestige/-/commit/a069f425efc11fd078bb05087fb8ce4e37665a55
    // https://github.com/mintty/wsltty/issues/157
    enum WINDOWCOMPOSITIONATTRIB {
        WCA_UNDEFINED = 0,
        WCA_NCRENDERING_ENABLED = 1,
        WCA_NCRENDERING_POLICY = 2,
        WCA_TRANSITIONS_FORCEDISABLED = 3,
        WCA_ALLOW_NCPAINT = 4,
        WCA_CAPTION_BUTTON_BOUNDS = 5,
        WCA_NONCLIENT_RTL_LAYOUT = 6,
        WCA_FORCE_ICONIC_REPRESENTATION = 7,
        WCA_EXTENDED_FRAME_BOUNDS = 8,
        WCA_HAS_ICONIC_BITMAP = 9,
        WCA_THEME_ATTRIBUTES = 10,
        WCA_NCRENDERING_EXILED = 11,
        WCA_NCADORNMENTINFO = 12,
        WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
        WCA_VIDEO_OVERLAY_ACTIVE = 14,
        WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
        WCA_DISALLOW_PEEK = 16,
        WCA_CLOAK = 17,
        WCA_CLOAKED = 18,
        WCA_ACCENT_POLICY = 19,
        WCA_FREEZE_REPRESENTATION = 20,
        WCA_EVER_UNCLOAKED = 21,
        WCA_VISUAL_OWNER = 22,
        WCA_HOLOGRAPHIC = 23,
        WCA_EXCLUDED_FROM_DDA = 24,
        WCA_PASSIVEUPDATEMODE = 25,
        WCA_USEDARKMODECOLORS = 26,
        WCA_LAST = 27
    };
    struct WINDOWCOMPOSITIONATTRIBDATA {
        WINDOWCOMPOSITIONATTRIB Attrib;
        PVOID pvData;
        SIZE_T cbData;
    };

    HWND hWnd = glfwGetWin32Window(window);
    if (hWnd == NULL) {
        return;
    }
    HMODULE hUser = GetModuleHandleA("user32.dll");
    if (hUser == NULL) {
        return;
    }

    using pfnSetWindowCompositionAttribute = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
    pfnSetWindowCompositionAttribute fnSetWindowCompositionAttribute = NULL;
    fnSetWindowCompositionAttribute = (BOOL(WINAPI*)(HWND, struct WINDOWCOMPOSITIONATTRIBDATA*))GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (fnSetWindowCompositionAttribute == NULL) {
        return;
    }
    BOOL dark = TRUE;
    WINDOWCOMPOSITIONATTRIBDATA data = {WCA_USEDARKMODECOLORS, &dark, sizeof(dark)};
    (*fnSetWindowCompositionAttribute)(hWnd, &data);
}

void setTheme(Theme theme) {
    switch (theme) {
        default:
        case Theme::DefaultDark: {
            constexpr auto ColorFromBytes = [](uint8_t r, uint8_t g, uint8_t b, float alpha = 1.0f) {
                return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, alpha);
            };

            auto& style = ImGui::GetStyle();
            ImVec4* colors = style.Colors;

            const ImVec4 bgColor = ColorFromBytes(25, 25, 25);
            const ImVec4 lightBgColor = ColorFromBytes(82, 82, 85);
            const ImVec4 veryLightBgColor = ColorFromBytes(90, 90, 95);

            const ImVec4 panelColor = ColorFromBytes(40, 40, 40, 0.4f);
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
            break;
        }
        case Theme::ImGuiLight: {
            // Light style by dougbinks from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 3.0f);
            style.FrameRounding = 0.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 21.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 14.0f;
            style.ScrollbarRounding = 9.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.9372549057006836f, 0.9372549057006836f, 0.9372549057006836f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.9800000190734863f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.300000011920929f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.4000000059604645f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.6700000166893005f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.95686274766922f, 0.95686274766922f, 0.95686274766922f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.8196078538894653f, 0.8196078538894653f, 0.8196078538894653f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.0f, 1.0f, 1.0f, 0.5099999904632568f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.9764705896377563f, 0.9764705896377563f, 0.9764705896377563f, 0.5299999713897705f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.686274528503418f, 0.686274528503418f, 0.686274528503418f, 0.800000011920929f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4862745106220245f, 0.4862745106220245f, 0.4862745106220245f, 0.800000011920929f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4862745106220245f, 0.4862745106220245f, 0.4862745106220245f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.7799999713897705f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.4588235318660736f, 0.5372549295425415f, 0.800000011920929f, 0.6000000238418579f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.4000000059604645f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3100000023841858f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 0.6200000047683716f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1372549086809158f, 0.4392156898975372f, 0.800000011920929f, 0.7799999713897705f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1372549086809158f, 0.4392156898975372f, 0.800000011920929f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.3490196168422699f, 0.1700000017881393f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.6700000166893005f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.7607843279838562f, 0.7960784435272217f, 0.8352941274642944f, 0.9309999942779541f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.5921568870544434f, 0.7254902124404907f, 0.8823529481887817f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.9176470637321472f, 0.9254902005195618f, 0.9333333373069763f, 0.9861999750137329f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.7411764860153198f, 0.8196078538894653f, 0.9137254953384399f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.4470588266849518f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.7764706015586853f, 0.8666666746139526f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.5686274766921997f, 0.5686274766921997f, 0.6392157077789307f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.6784313917160034f, 0.6784313917160034f, 0.7372549176216125f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.2980392277240753f, 0.2980392277240753f, 0.2980392277240753f, 0.09000000357627869f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.3499999940395355f);
            break;
        }
        case Theme::Gold: {
            // Gold style by CookiePLMonster from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 4.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(1.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Right;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 4.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 2.0f);
            style.FrameRounding = 4.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(10.0f, 2.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 12.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 10.0f;
            style.ScrollbarRounding = 6.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 4.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.9176470637321472f, 0.9176470637321472f, 0.9176470637321472f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4392156898975372f, 0.4392156898975372f, 0.4392156898975372f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1098039224743843f, 0.1098039224743843f, 0.1098039224743843f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.7764706015586853f, 0.5490196347236633f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1098039224743843f, 0.1098039224743843f, 0.1098039224743843f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 0.5299999713897705f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2078431397676468f, 0.2078431397676468f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.8078431487083435f, 0.8274509906768799f, 0.8078431487083435f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.7764706015586853f, 0.5490196347236633f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.7764706015586853f, 0.5490196347236633f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.929411768913269f, 0.6470588445663452f, 0.1372549086809158f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.2078431397676468f, 0.2078431397676468f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.7764706015586853f, 0.5490196347236633f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2078431397676468f, 0.2078431397676468f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.7764706015586853f, 0.5490196347236633f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.9098039269447327f, 0.6392157077789307f, 0.1294117718935013f, 1.0f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.7764706015586853f, 0.5490196347236633f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.09803921729326248f, 0.1490196138620377f, 0.9700000286102295f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1372549086809158f, 0.2588235437870026f, 0.4196078479290009f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);

            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.42f, 0.29f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.42f, 0.29f, 0.12f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.5098039507865906f, 0.3568627536296844f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.73f, 0.50f, 0.18f, 1.00f);

            break;
        }
        case Theme::Cherry: {
            // Cherry style by r-lyeh from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(6.0f, 3.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(5.0f, 1.0f);
            style.FrameRounding = 3.0f;
            style.FrameBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(7.0f, 1.0f);
            style.ItemInnerSpacing = ImVec2(1.0f, 1.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 6.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 13.0f;
            style.ScrollbarRounding = 16.0f;
            style.GrabMinSize = 20.0f;
            style.GrabRounding = 2.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 1.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 0.8799999952316284f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 0.2800000011920929f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1294117718935013f, 0.1372549086809158f, 0.168627455830574f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 0.8999999761581421f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.5372549295425415f, 0.47843137383461f, 0.2549019753932953f, 0.1620000004768372f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.7799999713897705f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.2313725501298904f, 0.2000000029802322f, 0.2705882489681244f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2313725501298904f, 0.2000000029802322f, 0.2705882489681244f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.501960813999176f, 0.07450980693101883f, 0.2549019753932953f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 0.75f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 0.4699999988079071f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.08627451211214066f, 0.1490196138620377f, 0.1568627506494522f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.7799999713897705f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.7098039388656616f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.4666666686534882f, 0.7686274647712708f, 0.8274509906768799f, 0.1400000005960464f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.7098039388656616f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.4666666686534882f, 0.7686274647712708f, 0.8274509906768799f, 0.1400000005960464f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.8600000143051147f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.7599999904632568f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.8600000143051147f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.501960813999176f, 0.07450980693101883f, 0.2549019753932953f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 0.7799999713897705f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.4666666686534882f, 0.7686274647712708f, 0.8274509906768799f, 0.03999999910593033f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.7799999713897705f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 0.8619999885559082f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 0.6299999952316284f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 0.6299999952316284f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 0.4300000071525574f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);

            break;
        }
        case Theme::ImGuiClassic: {
            // Classic style by ocornut from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 3.0f);
            style.FrameRounding = 0.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 21.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 14.0f;
            style.ScrollbarRounding = 9.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.8980392217636108f, 0.8980392217636108f, 0.8980392217636108f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8500000238418579f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.1098039224743843f, 0.1098039224743843f, 0.1372549086809158f, 0.9200000166893005f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4274509847164154f, 0.3899999856948853f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.686274528503418f, 0.4000000059604645f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4196078479290009f, 0.407843142747879f, 0.6392157077789307f, 0.6899999976158142f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.2666666805744171f, 0.2666666805744171f, 0.5372549295425415f, 0.8299999833106995f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2666666805744171f, 0.2666666805744171f, 0.5372549295425415f, 0.8299999833106995f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.3176470696926117f, 0.3176470696926117f, 0.6274510025978088f, 0.8700000047683716f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.5490196347236633f, 0.800000011920929f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.2000000029802322f, 0.2470588237047195f, 0.2980392277240753f, 0.6000000238418579f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.800000011920929f, 0.300000011920929f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.800000011920929f, 0.4000000059604645f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.407843142747879f, 0.3882353007793427f, 0.800000011920929f, 0.6000000238418579f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.8980392217636108f, 0.8980392217636108f, 0.8980392217636108f, 0.5f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.300000011920929f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.407843142747879f, 0.3882353007793427f, 0.800000011920929f, 0.6000000238418579f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.3490196168422699f, 0.4000000059604645f, 0.6078431606292725f, 0.6200000047683716f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.4000000059604645f, 0.47843137383461f, 0.7098039388656616f, 0.7900000214576721f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4588235318660736f, 0.5372549295425415f, 0.800000011920929f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.8980392217636108f, 0.449999988079071f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4470588266849518f, 0.4470588266849518f, 0.8980392217636108f, 0.800000011920929f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.529411792755127f, 0.529411792755127f, 0.8666666746139526f, 0.800000011920929f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 0.6000000238418579f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6980392336845398f, 1.0f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.8980392217636108f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.1000000014901161f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.7764706015586853f, 0.8196078538894653f, 1.0f, 0.6000000238418579f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.7764706015586853f, 0.8196078538894653f, 1.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.3333333432674408f, 0.3333333432674408f, 0.6823529601097107f, 0.7860000133514404f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.4470588266849518f, 0.4470588266849518f, 0.8980392217636108f, 0.800000011920929f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.4039215743541718f, 0.4039215743541718f, 0.7254902124404907f, 0.8420000076293945f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.2823529541492462f, 0.2823529541492462f, 0.5686274766921997f, 0.8212000131607056f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.6509804129600525f, 0.8371999859809875f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.2666666805744171f, 0.2666666805744171f, 0.3764705955982208f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.4470588266849518f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2588235437870026f, 0.2588235437870026f, 0.2784313857555389f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.07000000029802322f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 1.0f, 0.3499999940395355f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.4470588266849518f, 0.4470588266849518f, 0.8980392217636108f, 0.800000011920929f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.3499999940395355f);
            break;
        }
        case Theme::Comfy: {
            // Comfy style by Giuseppe from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.1000000014901161f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 10.0f;
            style.WindowBorderSize = 0.0f;
            style.WindowMinSize = ImVec2(30.0f, 30.0f);
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Right;
            style.ChildRounding = 5.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 10.0f;
            style.PopupBorderSize = 0.0f;
            style.FramePadding = ImVec2(5.0f, 3.5f);
            style.FrameRounding = 5.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(5.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(5.0f, 5.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 5.0f;
            style.ColumnsMinSpacing = 5.0f;
            style.ScrollbarSize = 15.0f;
            style.ScrollbarRounding = 9.0f;
            style.GrabMinSize = 15.0f;
            style.GrabRounding = 5.0f;
            style.TabRounding = 5.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(1.0f, 1.0f, 1.0f, 0.3605149984359741f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.4235294163227081f, 0.3803921639919281f, 0.572549045085907f, 0.54935622215271f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3803921639919281f, 0.4235294163227081f, 0.572549045085907f, 0.5490196347236633f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.2588235437870026f, 0.2588235437870026f, 0.2588235437870026f, 0.0f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 0.0f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.2352941185235977f, 0.2352941185235977f, 0.2352941185235977f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.294117659330368f, 0.294117659330368f, 0.294117659330368f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.294117659330368f, 0.294117659330368f, 0.294117659330368f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.8156862854957581f, 0.772549033164978f, 0.9647058844566345f, 0.5490196347236633f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.8156862854957581f, 0.772549033164978f, 0.9647058844566345f, 0.5490196347236633f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.8156862854957581f, 0.772549033164978f, 0.9647058844566345f, 0.5490196347236633f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.8156862854957581f, 0.772549033164978f, 0.9647058844566345f, 0.5490196347236633f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.8156862854957581f, 0.772549033164978f, 0.9647058844566345f, 0.5490196347236633f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.34f, 0.20f, 0.81f, 0.55f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.34f, 0.20f, 0.81f, 0.55f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.0f, 0.4509803950786591f, 1.0f, 0.0f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.74f, 0.69f, 0.89f, 0.59f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.294117659330368f, 0.294117659330368f, 0.294117659330368f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.6196078658103943f, 0.5764706134796143f, 0.7686274647712708f, 0.5490196347236633f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.4235294163227081f, 0.3803921639919281f, 0.572549045085907f, 0.5490196347236633f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.4235294163227081f, 0.3803921639919281f, 0.572549045085907f, 0.2918455004692078f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03433477878570557f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.7372549176216125f, 0.6941176652908325f, 0.886274516582489f, 0.5490196347236633f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);

            break;
        }
        case Theme::ImGuiDark: {
            // Dark style by dougbinks from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 3.0f);
            style.FrameRounding = 0.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 21.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 14.0f;
            style.ScrollbarRounding = 9.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 0.9399999976158142f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1568627506494522f, 0.2862745225429535f, 0.47843137383461f, 0.5400000214576721f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.4000000059604645f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.6700000166893005f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1568627506494522f, 0.2862745225429535f, 0.47843137383461f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.239215686917305f, 0.5176470875740051f, 0.8784313797950745f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.4000000059604645f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3100000023841858f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 0.7799999713897705f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.6700000166893005f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 0.8619999885559082f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);
            break;
        }
        case Theme::Microsoft: {
            // Microsoft style by usernameiwantedwasalreadytaken from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(4.0f, 6.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 0.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(8.0f, 6.0f);
            style.FrameRounding = 0.0f;
            style.FrameBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 20.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 20.0f;
            style.ScrollbarRounding = 0.0f;
            style.GrabMinSize = 5.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.9490196108818054f, 0.9490196108818054f, 0.9490196108818054f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.9490196108818054f, 0.9490196108818054f, 0.9490196108818054f, 1.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f, 1.0f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.0f, 0.4666666686534882f, 0.8392156958580017f, 0.2000000029802322f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.4666666686534882f, 0.8392156958580017f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1568627506494522f, 0.2862745225429535f, 0.47843137383461f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.686274528503418f, 0.686274528503418f, 0.686274528503418f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.0f, 0.0f, 0.0f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.686274528503418f, 0.686274528503418f, 0.686274528503418f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.4666666686534882f, 0.8392156958580017f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.4666666686534882f, 0.8392156958580017f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.4666666686534882f, 0.8392156958580017f, 0.2000000029802322f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.4666666686534882f, 0.8392156958580017f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 0.7799999713897705f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.6700000166893005f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 0.8619999885559082f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);

            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.52f, 0.53f, 0.55f, 0.97f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.23f, 0.47f, 0.77f, 1.00f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);

            break;
        }
        case Theme::Moonlight: {
            // Moonlight style by Madam-Herta from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 1.0f;
            style.WindowPadding = ImVec2(12.0f, 12.0f);
            style.WindowRounding = 11.5f;
            style.WindowBorderSize = 0.0f;
            style.WindowMinSize = ImVec2(20.0f, 20.0f);
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Right;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(20.0f, 3.400000095367432f);
            style.FrameRounding = 11.89999961853027f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(4.300000190734863f, 5.5f);
            style.ItemInnerSpacing = ImVec2(7.099999904632568f, 1.799999952316284f);
            style.CellPadding = ImVec2(12.10000038146973f, 9.199999809265137f);
            style.IndentSpacing = 0.0f;
            style.ColumnsMinSpacing = 4.900000095367432f;
            style.ScrollbarSize = 11.60000038146973f;
            style.ScrollbarRounding = 15.89999961853027f;
            style.GrabMinSize = 3.700000047683716f;
            style.GrabRounding = 20.0f;
            style.TabRounding = 0.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.2745098173618317f, 0.3176470696926117f, 0.4509803950786591f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09411764889955521f, 0.1019607856869698f, 0.1176470592617989f, 1.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1137254908680916f, 0.125490203499794f, 0.1529411822557449f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.7960784435272217f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.1803921610116959f, 0.1882352977991104f, 0.196078434586525f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1529411822557449f, 0.1529411822557449f, 0.1529411822557449f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.1411764770746231f, 0.1647058874368668f, 0.2078431397676468f, 1.0f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.105882354080677f, 0.105882354080677f, 0.105882354080677f, 1.0f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.1294117718935013f, 0.1490196138620377f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.125490203499794f, 0.2745098173618317f, 0.572549045085907f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.5215686559677124f, 0.6000000238418579f, 0.7019608020782471f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.03921568766236305f, 0.9803921580314636f, 0.9803921580314636f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8823529481887817f, 0.7960784435272217f, 0.5607843399047852f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.95686274766922f, 0.95686274766922f, 0.95686274766922f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.9372549057006836f, 0.9372549057006836f, 0.9372549057006836f, 1.0f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2666666805744171f, 0.2901960909366608f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);

            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
            break;
        }
        case Theme::Photoshop: {
            // Photoshop style by Derydoca from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 4.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 4.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 2.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 3.0f);
            style.FrameRounding = 2.0f;
            style.FrameBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 21.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 13.0f;
            style.ScrollbarRounding = 12.0f;
            style.GrabMinSize = 7.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 0.0f;
            style.TabBorderSize = 1.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.2784313857555389f, 0.2784313857555389f, 0.2784313857555389f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.2627451121807098f, 0.2627451121807098f, 0.2627451121807098f, 1.0f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2784313857555389f, 0.2784313857555389f, 0.2784313857555389f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1450980454683304f, 1.0f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2745098173618317f, 0.2745098173618317f, 0.2745098173618317f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.2980392277240753f, 0.2980392277240753f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.1560000032186508f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.3910000026226044f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.2627451121807098f, 0.2627451121807098f, 0.2627451121807098f, 1.0f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.6700000166893005f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1921568661928177f, 0.1921568661928177f, 0.1921568661928177f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.4666666686534882f, 0.4666666686534882f, 0.4666666686534882f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.5843137502670288f, 0.5843137502670288f, 0.5843137502670288f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.1560000032186508f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 0.3882353007793427f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5860000252723694f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5860000252723694f);
            break;
        }
        case Theme::SoftCherry: {
            // Soft Cherry style by Patitotective from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.4000000059604645f;
            style.WindowPadding = ImVec2(10.0f, 10.0f);
            style.WindowRounding = 4.0f;
            style.WindowBorderSize = 0.0f;
            style.WindowMinSize = ImVec2(50.0f, 50.0f);
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(5.0f, 3.0f);
            style.FrameRounding = 3.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(6.0f, 6.0f);
            style.ItemInnerSpacing = ImVec2(3.0f, 2.0f);
            style.CellPadding = ImVec2(3.0f, 3.0f);
            style.IndentSpacing = 6.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 13.0f;
            style.ScrollbarRounding = 16.0f;
            style.GrabMinSize = 20.0f;
            style.GrabRounding = 4.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 1.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5215686559677124f, 0.5490196347236633f, 0.5333333611488342f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1294117718935013f, 0.1372549086809158f, 0.168627455830574f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.1490196138620377f, 0.1568627506494522f, 0.1882352977991104f, 1.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.1372549086809158f, 0.1137254908680916f, 0.1333333402872086f, 1.0f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.168627455830574f, 0.1843137294054031f, 0.2313725501298904f, 1.0f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.2313725501298904f, 0.2000000029802322f, 0.2705882489681244f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2313725501298904f, 0.2000000029802322f, 0.2705882489681244f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.501960813999176f, 0.07450980693101883f, 0.2549019753932953f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.239215686917305f, 0.239215686917305f, 0.2196078449487686f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3725490272045135f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.6941176652908325f, 0.6941176652908325f, 0.686274528503418f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.6941176652908325f, 0.6941176652908325f, 0.686274528503418f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.658823549747467f, 0.1372549086809158f, 0.1764705926179886f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.7098039388656616f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.501960813999176f, 0.07450980693101883f, 0.2549019753932953f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 1.0f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.3098039329051971f, 0.7764706015586853f, 0.196078434586525f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.3843137323856354f, 0.6274510025978088f, 0.9176470637321472f, 1.0f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 1.0f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.300000011920929f);

            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2313725501298904f, 0.2000000029802322f, 0.2705882489681244f, 1.0f);
            break;
        }
        case Theme::Unreal: {
            // Unreal style by dev0-1 from ImThemes
            ImGuiStyle& style = ImGui::GetStyle();

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.6000000238418579f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Left;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(4.0f, 3.0f);
            style.FrameRounding = 0.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
            style.CellPadding = ImVec2(4.0f, 2.0f);
            style.IndentSpacing = 21.0f;
            style.ColumnsMinSpacing = 6.0f;
            style.ScrollbarSize = 14.0f;
            style.ScrollbarRounding = 9.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabMinWidthForCloseButton = 0.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392158031464f, 0.4980392158031464f, 0.4980392158031464f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 0.9399999976158142f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.0784313753247261f, 0.0784313753247261f, 0.9399999976158142f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2000000029802322f, 0.2078431397676468f, 0.2196078449487686f, 0.5400000214576721f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f, 0.4000000059604645f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.1764705926179886f, 0.1764705926179886f, 0.1764705926179886f, 0.6700000166893005f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 1.0f);
            //style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2862745225429535f, 0.2862745225429535f, 0.2862745225429535f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5099999904632568f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.9372549057006836f, 0.9372549057006836f, 0.9372549057006836f, 1.0f);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.8588235378265381f, 0.8588235378265381f, 0.8588235378265381f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.4392156898975372f, 0.4392156898975372f, 0.4392156898975372f, 0.4000000059604645f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.4588235318660736f, 0.4666666686534882f, 0.47843137383461f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4196078479290009f, 0.4196078479290009f, 0.4196078479290009f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.3100000023841858f);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.800000011920929f);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.47843137383461f, 0.4980392158031464f, 0.5176470875740051f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.7176470756530762f, 0.7176470756530762f, 0.7176470756530762f, 0.7799999713897705f);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.9098039269447327f, 0.9098039269447327f, 0.9098039269447327f, 0.25f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.8078431487083435f, 0.8078431487083435f, 0.8078431487083435f, 0.6700000166893005f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4588235318660736f, 0.4588235318660736f, 0.4588235318660736f, 0.949999988079071f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 0.8619999885559082f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 0.9724000096321106f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(0.6078431606292725f, 0.6078431606292725f, 0.6078431606292725f, 1.0f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.729411780834198f, 0.6000000238418579f, 0.1490196138620377f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6000000238418579f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.8666666746139526f, 0.8666666746139526f, 0.8666666746139526f, 0.3499999940395355f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.6000000238418579f, 0.6000000238418579f, 0.6000000238418579f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);
            break;
        }
    }
}
