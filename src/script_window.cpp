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

#include "data_structures.h"
#include "str_helpers.h"
#include "dbghelp_symbols_lookup.h"
#include "variant_symbol.h"

#include <regex>

ScriptWindow::ScriptWindow(DbgHelpSymbols const& symbols, std::string const& name_, uint64_t id_)
    : m_symbols(symbols) {
    text[0] = '\0';
    name = name_;
    id = id_;
}

std::string ScriptWindow::startScript(double timestamp, std::vector<std::unique_ptr<Scalar>> const& scalars) {
    m_operations.clear();
    m_idx = -1;
    m_start_time = timestamp;

    std::vector<std::string> lines = str::split(text, '\n');
    for (int i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (str::trim(line).empty() || line[0] == '#') {
            continue;
        }

        auto line_split = str::split(line, ';');
        if (line_split.size() != 3) {
            return std::format("Line {} is invalid.\n{}. Each line must be splittable into 3 'time;symbol;value'. Example '1;test_symbol;3'", i, line);
        }

        Operation op;
        //--------------
        // Get time
        try {
            bool use_prev_time = line_split[0].front() == '+';
            op.time = std::stod(line_split[0]);
            if (use_prev_time && m_operations.size() > 0) {
                op.time += m_operations.back().time;
            }
        } catch (std::exception e) {
            return std::format("Error in time: {} at line {}", e.what(), i);
        }
        //--------------
        // Get scalar to write
        Scalar* scalar = nullptr;
        for (auto& s : scalars) {
            if (s->name == str::trim(line_split[1])) {
                scalar = s.get();
                break;
            }
        }
        if (scalar == nullptr) {
            return std::format("No matching signal found for \"{}\" at line {}.\n{}", line_split[1], i, line);
        }

        //--------------
        // Parse if the value refers to other symbols
        std::vector<VariantSymbol*> value_symbols;
        std::string value_orig = str::trim(line_split[2]);
        // Parse value for symbols that match {name} regex
        std::regex scalar_regex(R"(\{([^{}]+)\})");
        std::smatch match;
        std::string value_str = value_orig;
        while (std::regex_search(value_str, match, scalar_regex)) {
            std::string scalar_name = match[1].str();
            VariantSymbol* value_symbol = m_symbols.getSymbol(scalar_name);
            if (value_symbol
                && (value_symbol->getType() == VariantSymbol::Type::Arithmetic
                    || value_symbol->getType() == VariantSymbol::Type::Enum)) {
                value_symbols.push_back(value_symbol);
                value_str = match.suffix().str();
                continue;
            } else {
                return std::format("No matching symbol found for \"{}\" at line {}.\n{}", scalar_name, i, line);
            }
        }
        // Try evaluating the expression that it is valid
        std::string value_replaced = value_orig;
        for (auto& s : value_symbols) {
            value_replaced = str::replaceAll(value_replaced, std::format("{{{}}}", s->getFullName()), std::format("{}", s->read()));
        }
        std::expected<double, std::string> value = str::evaluateExpression(value_replaced);

        //--------------
        // Set action if value is valid
        if (value.has_value()) {
            op.action = [=](double timestamp) {
                (void)timestamp;
                // Replace value_str with the actual values of the scalars
                std::string value_replaced = value_orig;
                for (auto& s : value_symbols) {
                    value_replaced = str::replaceAll(value_replaced, std::format("{{{}}}", s->getFullName()), std::format("{}", s->read()));
                }
                scalar->setValue(*str::evaluateExpression(value_replaced));
            };
        } else {
            return std::format("Value error in line {}.\nOriginal: {}\nReplaced: {}\n{}", i, value_orig, value_replaced, value.error());
        }
        op.line = i;
        m_operations.push_back(op);
    }
    double prev_time = 0;
    for (auto& op : m_operations) {
        if (op.time < prev_time) {
            m_operations.clear();
            return "Time in operations has to be in ascending order";
        }
        prev_time = op.time;
    }
    m_idx = 0;

    return "";
}

void ScriptWindow::processScript(double timestamp) {
    if (m_idx >= 0 && m_idx < m_operations.size()) {
        Operation* op = &m_operations[m_idx];
        while (timestamp > m_start_time + op->time) {
            op->action(timestamp);
            m_idx++;
            if (m_idx >= m_operations.size()) {
                m_start_time = timestamp;
                m_idx = loop ? 0 : -1;
            } else {
                op = &m_operations[m_idx];
            }
        }
    }
}

void ScriptWindow::stopScript() {
    m_idx = -1;
    m_operations.clear();
}

int ScriptWindow::currentLine() {
    if (m_idx >= 0) {
        return m_operations[m_idx].line;
    }
    return 0;
}

bool ScriptWindow::running() {
    return m_idx >= 0;
}

double ScriptWindow::getTime(double timestamp) {
    return timestamp - m_start_time;
}
