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
            return std::format("Error in line {}. Each line must be splittable into 3 'time;symbol;value'. Example '1;test_symbol;3'", i);
        }

        Operation op;
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
        // Get scalar to write
        for (auto& scalar : scalars) {
            if (scalar->name == line_split[1]) {
                op.scalar = scalar.get();
                break;
            }
        }
        if (op.scalar == nullptr) {
            return std::format("No matching signal found for {} at line {}", line_split[1], i);
        }
        // Get value
        std::expected<double, std::string> value = str::evaluateExpression(line_split[2]);
        if (value.has_value()) {
            op.value = value.value();
        } else {
            return std::format("Value error in line {}: {}", i, value.error());
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
            op->scalar->setScaledValue(op->value);
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
