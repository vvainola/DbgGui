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

#include <cstddef>
#include <span>
#include <vector>

struct DecimatedValues {
    std::vector<double> x;
    std::vector<double> y_min;
    std::vector<double> y_max;
};

struct DecimationTransform {
    double x_scale = 1;
    double x_offset = 0;
    double y_scale = 1;
    double y_offset = 0;
};

inline constexpr int MIN_PLOT_SAMPLE_COUNT = 128;
inline constexpr int MAX_PLOT_SAMPLE_COUNT = 10'000;
inline constexpr int ALL_SAMPLES = -1;

DecimatedValues decimateValues(std::span<double const> x,
                               std::span<double const> y,
                               int count,
                               DecimationTransform transform = {});
DecimatedValues decimateValues(std::span<double const> x,
                               std::span<double const> y,
                               size_t start_idx,
                               size_t end_idx,
                               int count,
                               DecimationTransform transform = {});
