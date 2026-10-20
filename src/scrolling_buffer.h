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
#include "plot_decimation.h"

// utility structure for realtime plot
class ScrollingBuffer {
  public:
    ScrollingBuffer(int32_t capacity)
        : m_capacity(std::max(1, capacity)) {}

    void setCapacity(int32_t capacity) {
        capacity = std::max(1, capacity);
        size_t retained = std::min(m_time.size(), size_t(capacity));
        auto resize = [&](std::vector<double>& buffer) {
            std::vector<double> newest;
            newest.reserve(retained);
            for (size_t i = m_time.size() - retained; i < m_time.size(); ++i) {
                newest.push_back(buffer[physicalIndex(i)]);
            }
            buffer.swap(newest);
        };
        for (auto& [scalar, buffer] : m_scalar_buffers) {
            resize(buffer);
        }
        resize(m_time);
        m_capacity = capacity;
        m_idx = 0;
    }

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
        // All channels share the same write position and grow only as samples arrive.
        for (auto& [scalar, buffer] : m_scalar_buffers) {
            append(buffer, m_scalar_buffers_temp.at(scalar));
        }
        m_idx = append(m_time, m_time_temp);
    }

    DecimatedValues getValuesInRange(Scalar* scalar, int32_t start_idx, int32_t end_idx, int32_t n_points, double scale = 1, double offset = 0) {
        DecimatedValues decimated_values;
        if (start_idx < 0 || end_idx < 0) {
            // Nothing sampled yet
            decimated_values.x.push_back(0);
            decimated_values.y_min.push_back(0);
            decimated_values.y_max.push_back(0);
            return decimated_values;
        }

        auto const& data = m_scalar_buffers.at(scalar);
        return decimateCircularValues(m_time,
                                      data,
                                      size_t(start_idx),
                                      size_t(end_idx) + 1,
                                      m_idx,
                                      n_points,
                                      {.y_scale = scale, .y_offset = offset});
    }

    DecimatedValues getValuesInRange(Scalar* scalar, std::pair<int32_t, int32_t> times, int32_t n_points, double scale = 1, double offset = 0) {
        return getValuesInRange(scalar, times.first, times.second, n_points, scale, offset);
    }

    // Sample export needs raw values, not plotting min/max decimation.
    std::vector<double> getTimeInRange(std::pair<int32_t, int32_t> times) {
        std::vector<double> time;
        if (times.first < 0 || times.second < 0 || times.second < times.first) {
            return time;
        }

        time.reserve(size_t(times.second - times.first + 1));
        for (int32_t i = times.first; i <= times.second; ++i) {
            time.push_back(m_time[physicalIndex(i)]);
        }
        return time;
    }

    std::vector<double> getSamplesInRange(Scalar* scalar, std::pair<int32_t, int32_t> times, double scale = 1, double offset = 0) {
        std::vector<double> samples;
        if (times.first < 0 || times.second < 0 || times.second < times.first || !m_scalar_buffers.contains(scalar)) {
            return samples;
        }

        auto const& data = m_scalar_buffers.at(scalar);
        samples.reserve(size_t(times.second - times.first + 1));
        for (int32_t i = times.first; i <= times.second; ++i) {
            samples.push_back(scale * data[physicalIndex(i)] + offset);
        }
        return samples;
    }

    void startSampling(Scalar* scalar) {
        if (m_scalar_buffers.contains(scalar)) {
            return;
        } else {
            // Initialize buffer with NAN so that the non-existing samples are not plotted
            m_scalar_buffers[scalar] = std::vector<double>(m_time.size(), NAN);
            m_scalar_buffers_temp[scalar] = std::vector<double>(m_time_temp.size(), NAN);
        }
    }

    void startSampling(VectorPair const& vector) {
        startSampling(vector.x);
        startSampling(vector.y);
    }

    void copySamples(Scalar& from, Scalar& to) {
        startSampling(&to);
        if (auto source = m_scalar_buffers.find(&from); source != m_scalar_buffers.end()) {
            m_scalar_buffers.at(&to) = source->second;
            m_scalar_buffers_temp.at(&to) = m_scalar_buffers_temp.at(&from);
        }
    }

    void copySamples(VectorPair const& from, VectorPair& to) {
        copySamples(*from.x, *to.x);
        copySamples(*from.y, *to.y);
    }

    void stopSampling(Scalar* scalar) {
        if (m_scalar_buffers.contains(scalar)) {
            m_scalar_buffers.erase(scalar);
            m_scalar_buffers_temp.erase(scalar);
        }
    }

    std::pair<int32_t, int32_t> getTimeIndices(double start_time, double end_time) {
        if (m_time.empty()) {
            return {-1, -1};
        }
        end_time = std::min(m_time[physicalIndex(m_time.size() - 1)], end_time);
        int32_t start_idx = binarySearch(start_time, 0, int32_t(m_time.size()) - 1);
        int32_t end_idx = binarySearch(end_time, start_idx, int32_t(m_time.size()) - 1);
        end_idx = std::max(end_idx, start_idx);
        return {start_idx, end_idx};
    }

    bool isScalarSampled(Scalar* scalar) {
        return m_scalar_buffers.contains(scalar);
    }

  private:
    int32_t append(std::vector<double>& buffer, std::vector<double>& pending) const {
        int32_t idx = m_idx;
        for (double value : pending) {
            if (buffer.size() < size_t(m_capacity)) {
                if (buffer.size() == buffer.capacity()) {
                    buffer.reserve(std::min(size_t(m_capacity), std::max(size_t(1), buffer.size() * 2)));
                }
                buffer.push_back(value);
            } else {
                buffer[idx] = value;
                idx = (idx + 1) % m_capacity;
            }
        }
        pending.clear();
        return idx;
    }

    // Public range indices are chronological, independent of the ring's write position.
    size_t physicalIndex(size_t index) const {
        return (m_idx + index) % m_time.size();
    }

    int32_t binarySearch(double t, int32_t start, int32_t end) {
        int32_t original_start = start;
        int32_t mid = std::midpoint(start, end);
        while (start <= end) {
            mid = std::midpoint(start, end);
            double val = m_time[physicalIndex(mid)];
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
    int32_t m_capacity;
    std::vector<double> m_time;
    std::unordered_map<Scalar*, std::vector<double>> m_scalar_buffers;
    std::vector<double> m_time_temp;
    std::unordered_map<Scalar*, std::vector<double>> m_scalar_buffers_temp;
};
