#pragma once

#include "imgui_internal.h"

// The caller supplies the current scalar value each frame. ImGui owns the
// pending text buffer; only dragging or pressing Enter may write it back.
inline bool panelSliderDouble(char const* label, double* value, double minimum, double maximum) {
    ImGuiID const id = ImGui::GetID(label);
    bool const was_typing = ImGui::TempInputIsActive(id);
    bool const changed = ImGui::SliderScalar(label, ImGuiDataType_Double, value, &minimum, &maximum,
                                            "%.6g", ImGuiSliderFlags_AlwaysClamp);
    // Check both sides: entering text mode and losing focus must not commit.
    bool const typing = was_typing || ImGui::TempInputIsActive(id);
    bool const submitted = was_typing
                        && (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
                            || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    return changed && (!typing || submitted);
}
