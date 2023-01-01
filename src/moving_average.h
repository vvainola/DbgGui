#pragma once

template <size_t N>
class MovingAverage {
  public:
    MovingAverage() {}

    void init(float value, float size) {
        if (size > N) {
            size = N;
        }
        m_size = size;
        m_size_inv = 1.0f / size;
        m_size_ceil = ceil(m_size);
        m_rolling_sum = value * size;
        m_cycle_sum = value * size;
        for (size_t i = 0; i < ceil(size); ++i) {
            m_samples[i] = value;
        }
    }

    void setLength(float size) {
        if (size > N) {
            size = N;
        }
        float scaling = size * m_size_inv;
        m_cycle_sum *= scaling;
        m_rolling_sum *= scaling;
        m_size = size;
        m_size_inv = 1.0f / size;
        m_size_ceil = ceil(size);
    }

    float step(float input) {
        // Remove oldest sample and add new
        m_rolling_sum -= m_samples[m_idx];
        m_rolling_sum += input;

        // Add old extra back and remove new extra
        float extra = (m_size_ceil - m_size) * input;
        m_rolling_sum += old_extra;
        m_rolling_sum -= extra;
        old_extra = extra;

        m_samples[m_idx] = input;
        m_cycle_sum += input;
        ++m_idx;

        // Reset rolling sum to cycle sum when full to remove accumulated errors
        if (m_idx >= m_size) {
            m_rolling_sum = m_cycle_sum - extra;
            m_cycle_sum = 0;
            m_idx = 0;
        }
        m_avg = m_rolling_sum * m_size_inv;
        return m_avg;
    }

  private:
    float m_samples[N]{};
    float m_cycle_sum = 0;
    float m_rolling_sum = 0;
    float m_avg = 0;
    float m_size = 1;
    float m_size_inv = 1;
    float m_size_ceil = 1;
    float old_extra = 0;

    int m_idx = 0;
};
