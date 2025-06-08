#include "dbg_gui.h"
#include "custom_signal.hpp"

void DbgGui::showCustomSignalCreator() {
    if (!m_show_custom_signal_creator) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Custom Signal Creator", &m_show_custom_signal_creator)) {
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
        HelpMarker("Curly brackets in the equation are replaced with the selected signals in the same order. Same signal can be selected multiple times.\nSupports sqrt,+-*/ and parenthesis. Example:\n-({} + sqrt({}))");
        ImGui::InputText("Name", custom_signal_name.data(), MAX_CUSTOM_EQ_NAME);
        if (ImGui::Button("Add")) {
            custom_signal_eq = custom_signal_eq.data();
            custom_signal_name = custom_signal_name.data();
            if (custom_signal_eq.empty()) {
                m_error_message = "Equation cannot be empty";
                ImGui::End();
                return;
            }

            if (custom_signal_name.empty()) {
                m_error_message = "New custom signal name cannot be empty";
                ImGui::End();
                return;
            }

            if (m_selected_symbols.empty() || m_selected_symbols.size() > MAX_CUSTOM_SIGNALS_IN_EQ) {
                m_error_message = std::format("Select between 1 and {} signals", MAX_CUSTOM_SIGNALS_IN_EQ);
                ImGui::End();
                return;
            }

            // Try evaluate the equation
            std::vector<double> zeros(m_selected_symbols.size(), 0.0);
            std::expected<double, std::string> expr_value = str::evaluateExpression(getFormattedEqForSample(custom_signal_eq, zeros));
            if (!expr_value.has_value()) {
                m_error_message = std::format("Invalid equation: {}", expr_value.error());
                ImGui::End();
                return;
            }

            // Capture m_selected_symbols by copy in the lambda
            ReadWriteFn eq = [selected_symbols = m_selected_symbols, eq_str = custom_signal_eq](std::optional<double> /*write*/) {
                std::vector<double> values;
                for (VariantSymbol* symbol : selected_symbols) {
                    values.push_back(getSourceValue(symbol->getValueSource()));
                }
                auto expr_value = str::evaluateExpression(getFormattedEqForSample(eq_str, values));
                assert(expr_value.has_value());
                return expr_value.value();
            };
            Scalar* scalar = addScalar(eq, m_group_to_add_symbols, custom_signal_name);
            std::vector<std::string> selected_symbol_names;
            for (const auto& symbol : m_selected_symbols) {
                selected_symbol_names.push_back(symbol->getFullName());
            }
            m_settings["custom_signals"][scalar->name_and_group]["equation"] = custom_signal_eq;
            m_settings["custom_signals"][scalar->name_and_group]["name"] = custom_signal_name;
            m_settings["custom_signals"][scalar->name_and_group]["group"] = m_group_to_add_symbols;
            m_settings["custom_signals"][scalar->name_and_group]["symbols"] = selected_symbol_names;

            custom_signal_eq = "";
            custom_signal_name = "";
            m_selected_symbols.clear();
        }

        ImGui::Text("Selected signals:");
        for (int i = 0; i < MAX_CUSTOM_SIGNALS_IN_EQ; ++i) {
            if (i < m_selected_symbols.size()) {
                ImGui::Text(std::format("  {}. {}", i, m_selected_symbols[i]->getFullName()).c_str());
                if (ImGui::BeginPopupContextItem((m_selected_symbols[i]->getFullName() + "_context_menu").c_str())) {
                    if (ImGui::Button("Copy name")) {
                        ImGui::SetClipboardText(m_selected_symbols[i]->getFullName().c_str());
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            } else {
                ImGui::Text(std::format("  {}. -", i).c_str());
            }
        }

        ImGui::End();
    } else {
        m_show_custom_signal_creator = false;
    }
}
