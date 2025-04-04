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
#include "dbg_gui.h"
#include "variant_symbol.h"

#include <regex>
#include <set>
#include <expected>

std::string currentDate() {
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(local_time, "%Y-%m-%d-%H-%M-%S");
    return oss.str();
}

const std::set<std::string> SPECIAL_OPERATIONS = {
  "pause",
  "save_csv",
};

std::expected<std::vector<VariantSymbol*>, std::string> getValueSymbols(std::string line, int line_number, DbgHelpSymbols const& symbols) {
    std::vector<VariantSymbol*> value_symbols;
    // Parse value for symbols that match {name} regex
    std::regex scalar_regex(R"(\{([^{}]+)\})");
    std::smatch match;
    while (std::regex_search(line, match, scalar_regex)) {
        std::string scalar_name = match[1].str();
        VariantSymbol* value_symbol = symbols.getSymbol(scalar_name);
        if (value_symbol
            && (value_symbol->getType() == VariantSymbol::Type::Arithmetic
                || value_symbol->getType() == VariantSymbol::Type::Enum)) {
            value_symbols.push_back(value_symbol);
            line = match.suffix().str();
            continue;
        } else if (scalar_name == "date") {
            line = match.suffix().str();
        } else {
            return std::unexpected(std::format("No matching symbol found for \"{}\" at line {}.\n{}", scalar_name, line_number, line));
        }
    }
    return value_symbols;
}

ScriptWindow::ScriptWindow(DbgGui* gui, std::string const& name_, uint64_t id_)
    : m_gui(gui) {
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

        Operation op;
        //--------------
        // Get time
        //--------------
        auto line_split = str::split(line, ';');
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
        //--------------
        std::string line_split_1 = str::trim(line_split[1]);
        if (SPECIAL_OPERATIONS.contains(line_split_1)) {
            std::expected<Operation, std::string> operation = parseSpecialOperation(line_split_1, line, i);
            if (!operation.has_value()) {
                return operation.error();
            }
            m_operations.push_back(operation.value());
            continue;
        }

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
        //--------------
        std::string value_orig = str::trim(line_split[2]);
        std::expected<std::vector<VariantSymbol*>, std::string> value_symbols = getValueSymbols(value_orig, i, m_gui->m_dbghelp_symbols);
        if (!value_symbols.has_value()) {
            return value_symbols.error();
        }

        // Try evaluating the expression that it is valid
        std::string value_replaced = value_orig;
        for (auto& s : value_symbols.value()) {
            value_replaced = str::replaceAll(value_replaced, std::format("{{{}}}", s->getFullName()), std::format("{}", s->read()));
        }
        std::expected<double, std::string> value = str::evaluateExpression(value_replaced);

        //--------------
        // Set action if value is valid
        //--------------
        if (value.has_value()) {
            op.action = [=](double timestamp) {
                (void)timestamp;
                // Replace value_str with the actual values of the scalars
                std::string value_replaced = value_orig;
                for (auto& s : value_symbols.value()) {
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

std::expected<ScriptWindow::Operation, std::string> ScriptWindow::parseSpecialOperation(std::string const& operation_name, std::string line, int line_number) {
    std::vector<std::string> line_split = str::split(line, ';');
    Operation op;
    try {
        bool use_prev_time = line_split[0].front() == '+';
        op.time = std::stod(line_split[0]);
        if (use_prev_time && m_operations.size() > 0) {
            op.time += m_operations.back().time;
        }
    } catch (std::exception e) {
        return std::unexpected(std::format("Error in time: {} at line {}", e.what(), line_number));
    }
    op.line = line_number;

    if (operation_name == "save_csv") {
        std::expected<std::vector<VariantSymbol*>, std::string> value_symbols = getValueSymbols(line_split[2], line_number, m_gui->m_dbghelp_symbols);
        if (!value_symbols.has_value()) {
            return std::unexpected(value_symbols.error());
        }

        op.action = [=](double timestamp) {


            (void)timestamp;
            std::string filename = line_split[2];
            str::trim(filename);
            for (auto& s : value_symbols.value()) {
                filename = str::replaceAll(filename, std::format("{{{}}}", s->getFullName()), std::format("{}", getSourceValue(s->getValueSource())));
            }
            // {date} is a special keyword that is replaced with the current time
            filename = str::replaceAll(filename, "{date}", currentDate());

            std::vector<Scalar*> scalars;
            for (auto const& scalar : m_gui->m_scalars) {
                scalars.push_back(scalar.get());
            }
            m_gui->saveScalarsAsCsv(filename, scalars, m_gui->m_linked_scalar_x_axis_limits);
        };
        return op;
    } else if (operation_name == "pause") {
        op.action = [=](double timestamp) {
            m_gui->m_paused = true;
        };
        return op;
    }
    return std::unexpected(std::format("Unknown special operation {} at line {}", operation_name, line_number));
}
