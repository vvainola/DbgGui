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

#include "data_structures.h"

// utility structure for realtime plot
class ScrollingBuffer {
  public:
    ScrollingBuffer(int32_t buffer_size = int32_t(1e6))
        : m_buffer_size(buffer_size),
          m_time(2 * buffer_size) {}

    void sample(double time) {
        m_time_temp.push_back(time);
        for (auto& [scalar, buffer] : m_scalar_buffers_temp) {
            buffer.push_back(scalar->getValue());
        }
    }

    void shiftTime(double time) {
        for (double& t : m_time) {
            t += time;
        }
        for (double& t : m_time_temp) {
            t += time;
        }
    }

    void emptyTempBuffers() {
        // Empty time buffer first and collect indices to which the time samples are added
        std::vector<int32_t> indices;
        indices.reserve(m_time_temp.size());
        for (size_t i = 0; i < m_time_temp.size(); ++i) {
            double time = m_time_temp[i];
            m_time[m_idx] = time;
            m_time[m_idx + m_buffer_size] = time;
            indices.push_back(m_idx);

            m_idx = (m_idx + 1) % m_buffer_size;
            if (m_idx == 0) {
                m_full_buffer_looped = true;
            }
        }
        m_time_temp.clear();

        // Empty temp scalar buffer to same indices
        for (auto& [scalar, buffer] : m_scalar_buffers) {
            auto& temp_buffer = m_scalar_buffers_temp[scalar];
            for (size_t i = 0; i < temp_buffer.size(); ++i) {
                size_t idx = indices[i];
                double value = temp_buffer[i];
                buffer[idx] = value;
                buffer[idx + m_buffer_size] = value;
            }
            temp_buffer.clear();
        }
    }

    struct DecimatedValues {
        std::vector<double> time;
        std::vector<double> y_min;
        std::vector<double> y_max;
    };
    DecimatedValues getValuesInRange(Scalar* scalar, int32_t start_idx, int32_t end_idx, int32_t n_points, double scale = 1, double offset = 0) {
        DecimatedValues decimated_values;
        if (start_idx < 0 || end_idx < 0) {
            // Nothing sampled yet
            decimated_values.time.push_back(0);
            decimated_values.y_min.push_back(0);
            decimated_values.y_max.push_back(0);
            return decimated_values;
        }

        int32_t decimation = static_cast<int32_t>(std::max(std::floor(double(end_idx - start_idx) / n_points) - 1, 0.0));

        // Add bit extra capacity for blanks at the end.
        decimated_values.time.reserve(std::min(end_idx - start_idx, n_points + 5));
        decimated_values.y_min.reserve(std::min(end_idx - start_idx, n_points + 5));
        decimated_values.y_max.reserve(std::min(end_idx - start_idx, n_points + 5));

        auto const& data = m_scalar_buffers[scalar];

        double current_min = INFINITY;
        double current_max = -INFINITY;
        int64_t counter = 0;
        for (int32_t i = start_idx; i <= end_idx; i++) {
            if (counter < 0) {
                decimated_values.time.push_back(m_time[i - 1]);
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
        decimated_values.time.push_back(m_time[end_idx]);
        decimated_values.y_min.push_back(scale * current_min + offset);
        decimated_values.y_max.push_back(scale * current_max + offset);
        return decimated_values;
    }

    DecimatedValues getValuesInRange(Scalar* scalar, std::pair<int32_t, int32_t> times, int32_t n_points, double scale = 1, double offset = 0) {
        return getValuesInRange(scalar, times.first, times.second, n_points, scale, offset);
    }

    void startSampling(Scalar* scalar) {
        if (m_scalar_buffers.contains(scalar)) {
            return;
        } else {
            // Initialize buffer with NAN so that the non-existing samples are not plotted
            m_scalar_buffers[scalar] = std::vector<double>(2 * m_buffer_size, NAN);
            m_scalar_buffers_temp[scalar] = {};
        }
    }
    void startSampling(Vector2D* vector) {
        startSampling(vector->x);
        startSampling(vector->y);
    }
    void stopSampling(Scalar* scalar) {
        if (m_scalar_buffers.contains(scalar)) {
            m_scalar_buffers.erase(scalar);
            m_scalar_buffers_temp.erase(scalar);
        }
    }

    std::pair<int32_t, int32_t> getTimeIndices(double start_time, double end_time) {
        end_time = std::min(m_time[m_idx - 1], end_time);
        int32_t start_idx = m_idx - 1;
        int32_t end_idx = m_idx - 1 + m_buffer_size;
        if (!m_full_buffer_looped) {
            // Nothing sampled yet
            if (m_idx == 0) {
                return {-1, -1};
            }
            start_idx = binarySearch(start_time, 0, m_idx - 1);
            end_idx = binarySearch(end_time, start_idx, m_idx - 1);
        } else {
            start_idx = binarySearch(start_time, start_idx, end_idx);
            end_idx = binarySearch(end_time, start_idx, end_idx);
        }
        end_idx = std::max(end_idx, start_idx);
        return {start_idx, end_idx};
    }

  private:
    int32_t binarySearch(double t, int32_t start, int32_t end) {
        int32_t original_start = start;
        int32_t mid = std::midpoint(start, end);
        while (start <= end) {
            mid = std::midpoint(start, end);
            double val = m_time[mid];
            if (val < t) {
                start = mid + 1;
            } else if (val > t) {
                end = mid - 1;
            } else {
                return mid;
            }
        }
        return std::max(original_start, end);
    }

    int32_t m_idx = 0;
    int32_t m_buffer_size = int32_t(1e6);
    std::vector<double> m_time;
    std::unordered_map<Scalar*, std::vector<double>> m_scalar_buffers;
    std::vector<double> m_time_temp;
    std::unordered_map<Scalar*, std::vector<double>> m_scalar_buffers_temp;
    bool m_full_buffer_looped = false;
};
