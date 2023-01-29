// MIT License
//
// Copyright (c) 2022 vvainola
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

#include <numeric>
#include <vector>
#include <cmath>

// utility structure for realtime plot
struct ScrollingBuffer {
    int32_t current_idx = 0;
    int32_t buffer_size;

    std::vector<double> time;
    std::vector<double> data;
    struct DecimatedValues {
        std::vector<double> time;
        std::vector<double> y_min;
        std::vector<double> y_max;
    };
    bool full_buffer_looped = false;

    ScrollingBuffer(int32_t max_size)
        : buffer_size(max_size),
          time(buffer_size * 2),
          data(buffer_size * 2) {
    }

    void addPoint(double x, double y) {
        // y += 0.1 * (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5);
        time[current_idx] = x;
        time[current_idx + buffer_size] = x;
        data[current_idx] = y;
        data[current_idx + buffer_size] = y;
        current_idx = (current_idx + 1) % buffer_size;
        if (current_idx == 0) {
            full_buffer_looped = true;
        }
    }

    int32_t binarySearch(double t, int32_t start, int32_t end) {
        int32_t mid = std::midpoint(start, end);
        while (start <= end) {
            mid = std::midpoint(start, end);
            double val = time[mid];
            if (val < t) {
                start = mid + 1;
            } else if (val > t) {
                end = mid - 1;
            } else {
                return mid;
            }
        }
        return mid;
    }

    DecimatedValues getValuesInRange(double x_min,
                                     double x_max,
                                     int32_t n_points,
                                     double scale = 1,
                                     double offset = 0) {
        DecimatedValues decimated_values;
        x_max = std::min(time[current_idx - 1], x_max);
        int32_t start_idx = current_idx - 1;
        int32_t end_idx = current_idx - 1 + buffer_size;
        if (!full_buffer_looped) {
            // Nothing sampled yet
            if (current_idx == 0) {
                decimated_values.time.push_back(0);
                decimated_values.y_min.push_back(0);
                decimated_values.y_max.push_back(0);
                return decimated_values;
            }
            x_min = std::max(0.0, x_min);
            start_idx = binarySearch(x_min, 0, current_idx - 1);
            end_idx = binarySearch(x_max, start_idx, current_idx - 1);
        } else {
            start_idx = binarySearch(x_min, start_idx, end_idx);
            end_idx = binarySearch(x_max, start_idx, end_idx);
        }
        end_idx = std::max(end_idx, start_idx + 2);

        int32_t decimation = static_cast<int32_t>(std::max(std::floor(double(end_idx - start_idx) / n_points) - 1, 0.0));

        // Add bit extra capacity for blanks at the end.
        decimated_values.time.reserve(std::min(end_idx - start_idx, n_points + 5));
        decimated_values.y_min.reserve(std::min(end_idx - start_idx, n_points + 5));
        decimated_values.y_max.reserve(std::min(end_idx - start_idx, n_points + 5));

        double current_min = INFINITY;
        double current_max = -INFINITY;
        int64_t counter = 0;
        for (int32_t i = start_idx; i <= end_idx; i++) {
            if (counter < 0) {
                decimated_values.time.push_back(time[i]);
                decimated_values.y_min.push_back(scale * current_min + offset);
                decimated_values.y_max.push_back(scale * current_max + offset);

                current_min = INFINITY;
                current_max = -INFINITY;
                counter = decimation;
            }
            current_min = std::min(data[i], current_min);
            current_max = std::max(data[i], current_max);
            counter--;
        }
        // Update leftover so that blanks are not left at the end
        decimated_values.time.push_back(time[end_idx]);
        decimated_values.y_min.push_back(scale * current_min + offset);
        decimated_values.y_max.push_back(scale * current_max + offset);
        return decimated_values;
    }
};
