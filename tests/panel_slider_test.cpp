#include <catch2/catch_test_macros.hpp>

#include "panel_slider.h"

#include <memory>

namespace {
struct SliderTest {
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{
      ImGui::CreateContext(), &ImGui::DestroyContext};
    double value = 20;
    int writes = 0;
    ImGuiID slider_id = 0;
    ImRect slider_rect;
    ImRect button_rect;

    SliderTest() {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.0f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        frame();
        frame();
    }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize({400, 200});
        ImGui::Begin("Panel slider");
        slider_id = ImGui::GetID("##slider");
        double candidate = value;
        ImGui::SetNextItemWidth(300);
        if (panelSliderDouble("##slider", &candidate, 0, 100)) {
            value = candidate;
            ++writes;
        }
        slider_rect = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        ImGui::Button("Other control");
        button_rect = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        ImGui::End();
        ImGui::Render();
    }

    void startTyping() {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddMousePosEvent(slider_rect.GetCenter().x, slider_rect.GetCenter().y);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        REQUIRE(ImGui::TempInputIsActive(slider_id));
    }
};
}

TEST_CASE("Panel sliders defer typed changes until Enter") {
    SliderTest slider;
    slider.startTyping();
    auto& io = ImGui::GetIO();
    io.AddInputCharactersUTF8("7");
    slider.frame();
    CHECK(slider.value == 20);
    CHECK(slider.writes == 0);
    io.AddInputCharactersUTF8("5");
    slider.frame();
    slider.frame();
    CHECK(slider.value == 20);
    CHECK(slider.writes == 0);

    SECTION("Enter commits the complete typed value") {
        io.AddKeyEvent(ImGuiKey_Enter, true);
        slider.frame();
        CHECK(slider.value == 75);
        CHECK(slider.writes == 1);
    }
    SECTION("Keypad Enter commits the complete typed value") {
        io.AddKeyEvent(ImGuiKey_KeypadEnter, true);
        slider.frame();
        CHECK(slider.value == 75);
        CHECK(slider.writes == 1);
    }
    SECTION("Enter clamps the submitted value to the slider range") {
        io.AddInputCharactersUTF8("9");
        slider.frame();
        CHECK(slider.value == 20);
        CHECK(slider.writes == 0);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        slider.frame();
        CHECK(slider.value == 100);
        CHECK(slider.writes == 1);
    }
    SECTION("Escape discards the pending value") {
        io.AddKeyEvent(ImGuiKey_Escape, true);
        slider.frame();
        slider.frame();
        CHECK(slider.value == 20);
        CHECK(slider.writes == 0);
    }
    SECTION("Clicking away discards the pending value") {
        io.AddMousePosEvent(slider.button_rect.GetCenter().x, slider.button_rect.GetCenter().y);
        io.AddMouseButtonEvent(0, true);
        slider.frame();
        io.AddMouseButtonEvent(0, false);
        slider.frame();
        CHECK(slider.value == 20);
        CHECK(slider.writes == 0);
    }
}

TEST_CASE("Panel sliders apply dragging immediately") {
    SliderTest slider;
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(slider.slider_rect.GetCenter().x, slider.slider_rect.GetCenter().y);
    io.AddMouseButtonEvent(0, true);
    slider.frame();
    CHECK(slider.value > 20);
    CHECK(slider.writes == 1);
    double const previous = slider.value;
    io.AddMousePosEvent(slider.slider_rect.Max.x - 20, slider.slider_rect.GetCenter().y);
    slider.frame();
    CHECK(slider.value > previous);
    CHECK(slider.writes == 2);
}
