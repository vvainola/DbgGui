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