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

#include <stack>
#include <stdexcept>
#include <format>

inline int MAX_CUSTOM_SIGNALS_IN_EQ = 10;
inline int MAX_CUSTOM_EQ_LENGTH = 1000;
inline int MAX_CUSTOM_EQ_NAME = 256;

static bool isOperator(char c);
static int getPrecedence(char op);
static double evaluateExpression(std::istringstream& iss);
static double evaluateExpression(std::string expression);
static std::string getFormattedEqForSample(std::string_view fmt, std::vector<CsvSignal*> const& signals, int i);

static bool isOperator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

static int getPrecedence(char op) {
    if (op == '+' || op == '-') {
        return 1;
    } else if (op == '*' || op == '/') {
        return 2;
    }
    return 0; // Default precedence for non-operators
}

static double applyOperator(double operand1, double operand2, char op) {
    switch (op) {
        case '+': return operand1 + operand2;
        case '-': return operand1 - operand2;
        case '*': return operand1 * operand2;
        case '/': return operand1 / operand2;
        default:
            throw std::runtime_error(std::format("Invalid operator: {}", op));
    }
}

static double evaluateExpression(std::istringstream& iss) {
    std::stack<double> operand_stack;
    std::stack<char> operator_stack;
    std::stack<char> full_stack;

    auto evaluateOperatorStack = [&]() {
        char top_operator = operator_stack.top();
        operator_stack.pop();

        if (operand_stack.size() < 2) {
            throw std::runtime_error("Invalid expression: too few operands");
        }
        double operand2 = operand_stack.top();
        operand_stack.pop();
        double operand1 = operand_stack.top();
        operand_stack.pop();

        double result = applyOperator(operand1, operand2, top_operator);
        operand_stack.push(result);
    };

    char current_char;
    while (iss.get(current_char)) {
        // Digit
        // Unary operator
        // Unary operator preceded by operator e.g. "1 + -2" or "1 / -(2)"
        if (isdigit(current_char)
            || (current_char == '-' && operand_stack.empty() && (isdigit(iss.peek()) || iss.peek() == '('))
            || (current_char == '-' && !full_stack.empty() && isOperator(full_stack.top()) && (isdigit(iss.peek()) || iss.peek() == '('))) {
            // Parse a number
            double operand;
            if (iss.peek() == '(') {
                iss.get(); // Remove opening parenthesis
                operand = -evaluateExpression(iss);
            } else {
                iss.putback(current_char);
                iss >> operand;
            }
            operand_stack.push(operand);
            // Don't care about the value in full stack but it has to be distinguishable from operator
            full_stack.push('0');
        }
        // sqrt
        else if (current_char == 's') {
            assert(iss.get() == 'q');
            assert(iss.get() == 'r');
            assert(iss.get() == 't');
            assert(iss.get() == '(');
            double operand = evaluateExpression(iss);
            operand_stack.push(sqrt(operand));
            full_stack.push('0');
        } else if (isOperator(current_char)) {
            // Token is an operator
            char current_operator = current_char;

            while (!operator_stack.empty() && getPrecedence(operator_stack.top()) >= getPrecedence(current_operator)) {
                // Apply higher or equal precedence operators on top of the operator stack
                evaluateOperatorStack();
            }

            // Push the current operator onto the stack
            operator_stack.push(current_operator);
            full_stack.push(current_operator);
        } else if (current_char == '(') {
            // Token is an opening parenthesis, evaluate the expression inside the parenthesis
            double result = evaluateExpression(iss);
            operand_stack.push(result);
            // Don't care about the value in full stack but it has to be distinguishable from operator
            full_stack.push('0');
        } else if (current_char == ')') {
            // Token is a closing parenthesis, evaluate the expression
            while (!operator_stack.empty()) {
                evaluateOperatorStack();
            }
            if (operand_stack.size() != 1) {
                throw std::runtime_error(std::format("Invalid number of operands"));
            }
            return operand_stack.top();
        } else {
            throw std::runtime_error(std::format("Invalid character in expression: {}", current_char));
            return 0.0; // Handle invalid characters gracefully
        }
    }

    // Process the remaining operators in the stack
    while (!operator_stack.empty()) {
        evaluateOperatorStack();
    }

    // The final result is on top of the operand stack
    if (operand_stack.size() == 1) {
        return operand_stack.top();
    } else {
        throw std::runtime_error(std::format("Invalid expression: too many operands"));
        return 0.0;
    }
}

static double evaluateExpression(std::string expression) {
    // Remove whitespace
    expression.erase(std::remove_if(expression.begin(), expression.end(), isspace), expression.end());
    std::istringstream iss(expression);
    return evaluateExpression(iss);
}

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
                c.samples.push_back(evaluateExpression(expr));
            }
            m_csv_data[0]->signals.push_back(c);

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
