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
#pragma once

#include <vector>
#include <algorithm>
#include <imgui.h>

// Apply ImGui multi-select requests (SetAll / SetRange) to a selection vector,
// using a flattened visible-items list to map indices back to item pointers.
// Shared between DbgGui symbol tree and CsvPlotter signal list.
template <typename T>
void applyMultiSelectRequests(ImGuiMultiSelectIO* ms_io,
                              std::vector<T>& selected,
                              std::vector<T> const& visible) {
    for (const ImGuiSelectionRequest& req : ms_io->Requests) {
        if (req.Type == ImGuiSelectionRequestType_SetAll) {
            if (req.Selected) {
                selected = visible;
            } else {
                selected.clear();
            }
        } else if (req.Type == ImGuiSelectionRequestType_SetRange) {
            int first = (int)req.RangeFirstItem;
            int last = (int)req.RangeLastItem;
            if (first > last) {
                std::swap(first, last);
            }
            first = std::max(first, 0);
            last = std::min(last, (int)visible.size() - 1);
            for (int i = first; i <= last; ++i) {
                const T& item = visible[i];
                if (req.Selected) {
                    if (std::find(selected.begin(), selected.end(), item) == selected.end()) {
                        selected.push_back(item);
                    }
                } else {
                    selected.erase(std::remove(selected.begin(), selected.end(), item), selected.end());
                }
            }
        }
    }
}

template <typename T>
void beginMultiSelect(std::vector<T>& selected,
                      std::vector<T> const& visible,
                      int items_count = -1,
                      ImGuiMultiSelectFlags extra_flags = ImGuiMultiSelectFlags_None) {
    ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape
                                | ImGuiMultiSelectFlags_SelectOnClickRelease
                                | extra_flags;
    ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(flags,
                                                        (int)selected.size(),
                                                        items_count);
    applyMultiSelectRequests(ms_io, selected, visible);
}

template <typename T>
void endMultiSelect(std::vector<T>& selected, std::vector<T> const& visible) {
    applyMultiSelectRequests(ImGui::EndMultiSelect(), selected, visible);
}
