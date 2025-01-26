// MIT License
//
// Copyright (c) 2024 vvainola
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

#include "csvplot.h"
#include "str_helpers.h"

#include <stack>
#include <stdexcept>
#include <format>

inline int MAX_CUSTOM_SIGNALS_IN_EQ = 10;
inline int MAX_CUSTOM_EQ_LENGTH = 1000;
inline int MAX_CUSTOM_EQ_NAME = 256;

static std::string getFormattedEqForSample(std::string_view fmt, std::vector<CsvSignal*> const& signals, int i) {
    switch (signals.size()) {
        case 0:
            return std::vformat(fmt, std::make_format_args());
        case 1:
            return std::vformat(fmt, std::make_format_args(signals[0]->samples[i]));
        case 2:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i]));
        case 3:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i]));
        case 4:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i]));
        case 5:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i],
                                                      signals[4]->samples[i]));
        case 6:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i],
                                                      signals[4]->samples[i],
                                                      signals[5]->samples[i]));
        case 7:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i],
                                                      signals[4]->samples[i],
                                                      signals[5]->samples[i],
                                                      signals[6]->samples[i]));
        case 8:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i],
                                                      signals[4]->samples[i],
                                                      signals[5]->samples[i],
                                                      signals[6]->samples[i],
                                                      signals[7]->samples[i]));
        case 9:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i],
                                                      signals[4]->samples[i],
                                                      signals[5]->samples[i],
                                                      signals[6]->samples[i],
                                                      signals[7]->samples[i],
                                                      signals[8]->samples[i]));
        case 10:
            return std::vformat(fmt,
                                std::make_format_args(signals[0]->samples[i],
                                                      signals[1]->samples[i],
                                                      signals[2]->samples[i],
                                                      signals[3]->samples[i],
                                                      signals[4]->samples[i],
                                                      signals[5]->samples[i],
                                                      signals[6]->samples[i],
                                                      signals[7]->samples[i],
                                                      signals[8]->samples[i],
                                                      signals[9]->samples[i]));
        default:
            throw std::runtime_error("Too many selected signals");
            break;
    }
    return "";
}

void CsvPlotter::showCustomSignalCreator() {
    static std::string custom_signal_eq;
    static std::string custom_signal_name;
    static bool once = true;
    if (once) {
        once = false;
        custom_signal_eq.reserve(MAX_CUSTOM_EQ_LENGTH);
        custom_signal_name.reserve(MAX_CUSTOM_EQ_NAME);
    }

    ImGui::InputText("Equation", custom_signal_eq.data(), MAX_CUSTOM_EQ_LENGTH);
    ImGui::SameLine();
    HelpMarker("Curly brackets in the equation are replaced with the selected signals in the same order. Same signal cann be selected multiple times.\nSupports sqrt,+-*/ and parenthesis. Example:\n-({} + sqrt({}))");
    ImGui::InputText("Name", custom_signal_name.data(), MAX_CUSTOM_EQ_NAME);
    if (ImGui::Button("Add")) {
        custom_signal_eq = custom_signal_eq.data();
        custom_signal_name = custom_signal_name.data();
        if (custom_signal_eq.empty()) {
            m_error_message = "Invalid custom equation";
            return;
        }

        if (custom_signal_name.empty()) {
            m_error_message = "Invalid custom name";
            return;
        }

        if (m_selected_signals.empty()) {
            m_error_message = "At least one signal has to be selected";
            return;
        }

        // Check signals are from same file
        for (CsvSignal* signal : m_selected_signals) {
            if (signal->file != m_selected_signals[0]->file) {
                m_error_message = "Signals must be from same file";
                return;
            }
        }

        try {
            CsvSignal c;
            c.name = custom_signal_name;
            c.file = m_selected_signals[0]->file;
            for (int i = 0; i < m_selected_signals[0]->samples.size(); ++i) {
                std::string expr = getFormattedEqForSample(custom_signal_eq, m_selected_signals, i);
                auto expr_value = str::evaluateExpression(expr);
                if (!expr_value.has_value()) {
                    m_error_message = expr_value.error();
                    return;
                }
                c.samples.push_back(expr_value.value());
            }
            m_selected_signals[0]->file->signals.push_back(std::move(c));
        } catch (std::runtime_error e) {
            m_error_message = e.what();
            return;
        }

        custom_signal_eq = "";
        custom_signal_name = "";
        m_selected_signals.clear();
    }

    ImGui::Text("Selected signals:");
    for (int i = 0; i < MAX_CUSTOM_SIGNALS_IN_EQ; ++i) {
        if (i < m_selected_signals.size()) {
            ImGui::Text(std::format("  {}. {}", i, m_selected_signals[i]->name).c_str());
            if (ImGui::BeginPopupContextItem((m_selected_signals[i]->name + "_context_menu").c_str())) {
                if (ImGui::Button("Copy name")) {
                    ImGui::SetClipboardText(m_selected_signals[i]->name.c_str());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::Text(std::format("  {}. -", i).c_str());
        }
    }
}
