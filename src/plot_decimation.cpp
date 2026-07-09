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

#include "plot_decimation.h"

#include "minmax.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

double transformX(double x, DecimationTransform const& transform) {
    return x * transform.x_scale + transform.x_offset;
}

std::pair<double, double> transformMinMax(double y_min, double y_max, DecimationTransform const& transform) {
    if (std::isnan(y_min) || std::isnan(y_max)) {
        double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan};
    }

    double transformed_min = y_min * transform.y_scale + transform.y_offset;
    double transformed_max = y_max * transform.y_scale + transform.y_offset;
    if (transformed_min <= transformed_max) {
        return {transformed_min, transformed_max};
    }
    return {transformed_max, transformed_min};
}

} // namespace

DecimatedValues decimateValues(std::span<double const> x,
                               std::span<double const> y,
                               int count,
                               DecimationTransform transform) {
    DecimatedValues decimated_values;
    if (x.empty() || y.empty()) {
        return decimated_values;
    }

    size_t sample_count = MIN(x.size(), y.size());
    if (count != ALL_SAMPLES) {
        count = std::clamp(count, MIN_PLOT_SAMPLE_COUNT, MAX_PLOT_SAMPLE_COUNT);
    }
    size_t bucket_count = MIN(sample_count, size_t(count));
    decimated_values.x.reserve(bucket_count);
    decimated_values.y_min.reserve(bucket_count);
    decimated_values.y_max.reserve(bucket_count);

    for (size_t bucket = 0; bucket < bucket_count; ++bucket) {
        size_t begin = bucket * sample_count / bucket_count;
        size_t end = (bucket + 1) * sample_count / bucket_count;
        if (end <= begin) {
            end = begin + 1;
        }

        double current_min = std::numeric_limits<double>::infinity();
        double current_max = -std::numeric_limits<double>::infinity();
        for (size_t i = begin; i < end; ++i) {
            current_min = MIN(y[i], current_min);
            current_max = MAX(y[i], current_max);
        }

        if (current_min > current_max) {
            current_min = std::numeric_limits<double>::quiet_NaN();
            current_max = std::numeric_limits<double>::quiet_NaN();
        }

        double x_center = 0.5 * (x[begin] + x[end - 1]);
        auto [y_min, y_max] = transformMinMax(current_min, current_max, transform);
        decimated_values.x.push_back(transformX(x_center, transform));
        decimated_values.y_min.push_back(y_min);
        decimated_values.y_max.push_back(y_max);
    }
    return decimated_values;
}

DecimatedValues decimateValues(std::span<double const> x,
                               std::span<double const> y,
                               size_t start_idx,
                               size_t end_idx,
                               int count,
                               DecimationTransform transform) {
    size_t sample_count = MIN(x.size(), y.size());
    start_idx = MIN(start_idx, sample_count);
    end_idx = MIN(end_idx, sample_count);
    if (end_idx <= start_idx) {
        return {};
    }
    return decimateValues(x.subspan(start_idx, end_idx - start_idx),
                          y.subspan(start_idx, end_idx - start_idx),
                          count,
                          transform);
}
