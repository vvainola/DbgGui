#include <catch2/catch_test_macros.hpp>

#include "imgui_internal.h"
#include "lua_script_editor.h"

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
