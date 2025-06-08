// MIT License
//
// Copyright (c) 2025 vvainola
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

#include "custom_signal.hpp"

#include <format>

std::string getFormattedEqForSample(std::string_view fmt, std::vector<double> const& samples) {
    switch (samples.size()) {
        case 0:
                    return std::vformat(fmt, std::make_format_args());
        case 1:
            return std::vformat(fmt, std::make_format_args(samples[0]));
        case 2:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1]));
        case 3:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2]));
        case 4:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3]));
        case 5:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3],
                                                      samples[4]));
        case 6:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3],
                                                      samples[4],
                                                      samples[5]));
        case 7:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3],
                                                      samples[4],
                                                      samples[5],
                                                      samples[6]));
        case 8:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3],
                                                      samples[4],
                                                      samples[5],
                                                      samples[6],
                                                      samples[7]));
        case 9:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3],
                                                      samples[4],
                                                      samples[5],
                                                      samples[6],
                                                      samples[7],
                                                      samples[8]));
        case 10:
            return std::vformat(fmt,
                                std::make_format_args(samples[0],
                                                      samples[1],
                                                      samples[2],
                                                      samples[3],
                                                      samples[4],
                                                      samples[5],
                                                      samples[6],
                                                      samples[7],
                                                      samples[8],
                                                      samples[9]));
        default:
            throw std::runtime_error("Too many selected signals");
            break;
    }
}
