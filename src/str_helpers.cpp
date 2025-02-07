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

#include "str_helpers.h"
#include <windows.h>
#include <format>
#include <sstream>
#include <iomanip>
#include <stack>

namespace str {

std::expected<std::string, std::string> str::readFile(const std::string& filename) {
    HANDLE file_handle = CreateFileA(
      filename.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

    if (file_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(std::format("Error opening file: {}", filename));
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle, &file_size)) {
        CloseHandle(file_handle);
        return std::unexpected(std::format("Error getting file size: {}", filename));
    }

    HANDLE file_mapping = CreateFileMapping(file_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (file_mapping == nullptr) {
        CloseHandle(file_handle);
        return std::unexpected(std::format("Error creating file mapping: {}", filename));
    }

    char* file_contents = static_cast<char*>(MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, file_size.QuadPart));
    if (file_contents == nullptr) {
        CloseHandle(file_mapping);
        CloseHandle(file_handle);
        return std::unexpected(std::format("Error mapping file to memory: {}", filename));
    }

    std::string result(file_contents, file_size.QuadPart);

    if (!UnmapViewOfFile(file_contents)) {
        return std::unexpected(std::format("Error unmapping file: {}", filename));
    }

    CloseHandle(file_mapping);
    CloseHandle(file_handle);

    return result;
}

std::vector<std::string_view> str::splitSv(const std::string& s, char delim, int expected_column_count) {
    std::vector<std::string_view> elems;
    elems.reserve(expected_column_count);
    int32_t pos_start = 0;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == delim) {
            elems.push_back(std::string_view(&s[pos_start], &s[i]));
            pos_start = i + 1;
        }
    }
    // Add the last value if there is no trailing delimiter
    if (s.back() != delim) {
        elems.push_back(std::string_view(&s[pos_start]));
    }
    return elems;
}

std::vector<std::string> str::split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::string str::replaceAll(
  const std::string& str,    // where to work
  const std::string& find,   // substitute 'find'
  const std::string& replace // by 'replace'
) {
    using namespace std;
    string result;
    size_t find_len = find.size();
    size_t pos, from = 0;
    while (string::npos != (pos = str.find(find, from))) {
        result.append(str, from, pos - from);
        result.append(replace);
        from = pos + find_len;
    }
    result.append(str, from, string::npos);
    return result;
}

std::string removeWhitespace(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        if (!std::isspace(c)) {
            result.push_back(c);
        }
    }
    return result;
}

std::string& str::ltrim(std::string& str) {
    auto it2 = std::find_if(str.begin(), str.end(), [](char ch) { return !std::isspace<char>(ch, std::locale::classic()); });
    str.erase(str.begin(), it2);
    return str;
}

std::string& str::rtrim(std::string& str) {
    auto it1 = std::find_if(str.rbegin(), str.rend(), [](char ch) { return !std::isspace<char>(ch, std::locale::classic()); });
    str.erase(it1.base(), str.end());
    return str;
}

std::string& str::trim(std::string& str) {
    return ltrim(rtrim(str));
}

static bool isOperator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '^';
}

static int getPrecedence(char op) {
    if (op == '+' || op == '-') {
        return 1;
    } else if (op == '*' || op == '/') {
        return 2;
    } else if (op == '^') {
        return 3;
    }
    return 0; // Default precedence for non-operators
}

static double applyOperator(double operand1, double operand2, char op) {
    switch (op) {
        case '+': return operand1 + operand2;
        case '-': return operand1 - operand2;
        case '*': return operand1 * operand2;
        case '/': return operand1 / operand2;
        case '^': return pow(operand1, operand2);
        default:
            throw std::runtime_error(std::format("Invalid operator {}", op));
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
            throw std::runtime_error(std::format("Unexpected operand stack size {} when evaluating operators", operand_stack.size()));
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
            if (iss.get() != 'q' || iss.get() != 'r' || iss.get() != 't' || iss.get() != '(') {
                throw std::runtime_error(std::format("sqrt is the only supported special operation"));
            }
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
                throw std::runtime_error(std::format("Unexpected operand stack size {} after evaluating operators", operand_stack.size()));
            }
            return operand_stack.top();
        } else {
            throw std::runtime_error(std::format("Invalid character: {}", current_char));
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
        throw std::runtime_error("Invalid expression : Too many operands");
    }
}

std::expected<double, std::string> str::evaluateExpression(std::string expression) {
    // Remove whitespace
    expression.erase(std::remove_if(expression.begin(), expression.end(), isspace), expression.end());
    std::istringstream iss(expression);
    try {
        return evaluateExpression(iss);
    } catch (std::runtime_error e) {
        return std::unexpected(e.what());
    }
}

} // namespace str
