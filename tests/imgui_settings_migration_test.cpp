#include <catch2/catch_test_macros.hpp>

#include "imgui/imgui.h"
#include "imgui_settings_migration.h"

TEST_CASE("ImGui layout migration updates old triple-hash dock window references") {
    std::string layout =
      "[Window][###123]\n"
      "Pos=1,2\n"
      "DockId=0x731539FF,0\n"
      "\n"
      "[Docking][Data]\n"
      "DockSpace ID=0x00000001 Window=0x731539FF Pos=0,0 Size=100,100 Selected=0x731539FF\n"
      "DockNode  ID=0x00000002 Parent=0x00000001 SizeRef=50,100 Selected=0xDEADBEEF\n";

#if IMGUI_VERSION_NUM >= 19260
    CHECK(imgui_settings::migrateLayoutIniHashes(layout));
    CHECK(layout.find("Window=0x884863D2") != std::string::npos);
    CHECK(layout.find("Selected=0x884863D2") != std::string::npos);
    CHECK(layout.find("DockId=0x731539FF,0") != std::string::npos);
    CHECK(layout.find("Window=0x731539FF") == std::string::npos);
    CHECK(layout.find("Selected=0x731539FF") == std::string::npos);
    CHECK_FALSE(imgui_settings::migrateLayoutIniHashes(layout));
#else
    CHECK_FALSE(imgui_settings::migrateLayoutIniHashes(layout));
    CHECK(layout.find("Window=0x731539FF") != std::string::npos);
    CHECK(layout.find("Selected=0x731539FF") != std::string::npos);
#endif
}

TEST_CASE("ImGui layout migration leaves layouts without triple-hash windows untouched") {
    std::string layout =
      "[Window][Scalars]\n"
      "Pos=1,2\n"
      "\n"
      "[Docking][Data]\n"
      "DockSpace ID=0x00000001 Window=0x731539FF Pos=0,0 Size=100,100 Selected=0x731539FF\n";
    std::string original = layout;

    CHECK_FALSE(imgui_settings::migrateLayoutIniHashes(layout));
    CHECK(layout == original);
}
