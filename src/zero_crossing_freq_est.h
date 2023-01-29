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

#include <cstdlib>

struct ZeroCrossingFreqEst {
    float dead_time;
    float sampling_period;
    float time_since_last_zero_crossing;

    float last_sample;

    float out_estimated_freq;
};

void estimateFreq(ZeroCrossingFreqEst* freq_est, float sample) {
    if (freq_est->last_sample * sample < 0
        && freq_est->time_since_last_zero_crossing > freq_est->dead_time) {
        // Interpolate sample
        float interpolated_time = fabs(freq_est->last_sample) / (fabs(freq_est->last_sample) + fabs(sample));
        freq_est->out_estimated_freq = 0.5f / (freq_est->time_since_last_zero_crossing + interpolated_time * freq_est->sampling_period);
        freq_est->time_since_last_zero_crossing = (1.0f - interpolated_time) * freq_est->sampling_period;
    } else {
        freq_est->time_since_last_zero_crossing += freq_est->sampling_period;
    }
    freq_est->last_sample = sample;
}
