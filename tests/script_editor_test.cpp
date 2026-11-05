#include <catch2/catch_test_macros.hpp>

#include "imgui_internal.h"
#include "script_editor.h"

#include <memory>
#include <string>

TEST_CASE("Lua editor does not draw into an inactive input child's released buffers") {
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 800};
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    std::string source = "return true";
    ImGuiID input_id = 0;
    auto frame = [&](bool skip_items) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize({600, 400});
        ImGui::Begin("Script editor");
        input_id = ImGui::GetID("##source");
        // Hidden popup/layout passes can skip the input while its old child
        // window still exists in ImGui's global window list.
        ImGui::GetCurrentWindow()->SkipItems = skip_items;
        inputScriptTextWithLineNumbers(source, {420, 180}, true);
        ImGui::End();
        ImGui::Render();
    };
    frame(false);
    frame(false);
    frame(false);
    ImGuiWindow* input = nullptr;
    for (auto* window : GImGui->Windows) {
        if (window->ChildId == input_id) {
            input = window;
        }
    }
    REQUIRE(input != nullptr);
    ImGui::GcCompactTransientWindowBuffers(input);
    frame(true);
    CHECK(input->LastFrameActive < GImGui->FrameCount);
    CHECK(input->DrawList->CmdBuffer.empty());
    frame(false);
    CHECK(input->LastFrameActive == GImGui->FrameCount);
    CHECK_FALSE(input->DrawList->CmdBuffer.empty());
}

TEST_CASE("Script editors keep their minimum size and widen for the longest line") {
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 800};
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    ImGui::NewFrame();
    ImGui::Begin("Script editor sizing");
    ImVec2 const minimum = {420, 180};
    SECTION("Short scripts keep the current box size") {
        for (auto text : {"", "return true", "local x = 1\nreturn x"}) {
            auto const size = scriptEditorSizeToFit(text, minimum);
            CHECK(size.x == minimum.x);
            CHECK(size.y == minimum.y);
        }
    }
    SECTION("The longest line determines the width without changing the height") {
        std::string const line(90, 'W');
        auto const size = scriptEditorSizeToFit(line, minimum);
        auto const multiline = scriptEditorSizeToFit("return true\n" + line, minimum);
        CHECK(size.x > minimum.x);
        CHECK(size.x > ImGui::CalcTextSize(line.c_str()).x);
        CHECK(multiline.x == size.x);
        CHECK(size.y == minimum.y);
    }
    SECTION("Very long lines stay within the viewport") {
        auto const size = scriptEditorSizeToFit(std::string(10000, 'W'), minimum);
        CHECK(size.x > minimum.x);
        CHECK(size.x < ImGui::GetWindowViewport()->WorkSize.x);
        CHECK(size.y == minimum.y);
    }
    ImGui::End();
    ImGui::Render();
}

TEST_CASE("Highlighted script text follows horizontal input scrolling") {
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 800};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    std::string source(500, 'a');
    ImGuiID input_id = 0;
    auto frame = [&](bool focus = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize({600, 400});
        ImGui::Begin("Scrolling script editor");
        input_id = ImGui::GetID("##source");
        if (focus) {
            ImGui::SetKeyboardFocusHere();
        }
        inputScriptTextWithLineNumbers(source, {420, 180}, true);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame(true);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_End, true);
    frame();
    io.AddKeyEvent(ImGuiKey_End, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    auto* state = ImGui::GetInputTextState(input_id);
    REQUIRE(state != nullptr);
    REQUIRE(state->GetCursorPos() == static_cast<int>(source.size()));
    REQUIRE(state->Scroll.x > 0);
    ImGuiWindow* input = nullptr;
    for (auto* window : GImGui->Windows) {
        if (window->ChildId == input_id) {
            input = window;
        }
    }
    REQUIRE(input != nullptr);
    REQUIRE_FALSE(input->DrawList->VtxBuffer.empty());
    // The overlay draws its caret last. It must stay beside the visible text,
    // rather than at the unscrolled end of a line far outside the editor.
    auto const caret = input->DrawList->VtxBuffer.back().pos;
    CHECK(caret.x >= input->InnerRect.Min.x);
    CHECK(caret.x <= input->InnerRect.Max.x);
}
