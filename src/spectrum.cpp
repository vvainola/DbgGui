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

#include "spectrum.h"
#include <array>
#include <kissfft/kissfft.hh>
#include <future>

constexpr double PI = 3.1415926535897;
constexpr double MAG_MIN_OF_MAX = 2e-3;
constexpr double APPROX_LIMIT = 1e-7;

std::vector<std::complex<double>> collectFftSamples(std::vector<double> const& time,
                                                    std::vector<double> const& samples_x,
                                                    std::vector<double> const& samples_y,
                                                    double sampling_time) {
    if (samples_x.size() != samples_y.size()) {
        return {};
    }
    size_t sample_cnt = time.size();
    std::vector<std::complex<double>> samples;
    samples.reserve(sample_cnt);
    double t_prev = 0;
    // Get first sample that is a multiple of the sampling time
    for (double t : time) {
        double t_multiple = std::round(t / sampling_time) * sampling_time;
        if (std::abs(t_multiple - t) < APPROX_LIMIT) {
            t_prev = t;
            break;
        }
    }
    // Collect samples that samples that are "sampling time" away from each other and leave out
    // samples in between in case of variable timestepping
    for (size_t i = 0; i < sample_cnt; ++i) {
        double t_current = time[i];
        double t_delta = t_current - t_prev;
        if (std::abs(t_delta - sampling_time) < APPROX_LIMIT) {
            t_prev = t_current;
            samples.push_back({samples_x[i], samples_y[i]});
        }
    }
    return samples;
}

int closestSpectralBin(std::vector<double> const& vec_x, std::vector<double> const& vec_y, double x, double y) {
    if (vec_x.size() == 0) {
        return -1;
    }
    auto const it_lower = std::lower_bound(vec_x.begin(), vec_x.end(), x - abs(x) * 0.03);
    auto it_upper = std::upper_bound(vec_x.begin(), vec_x.end(), x + abs(x) * 0.03);
    if (it_upper == vec_x.end()) {
        it_upper = vec_x.end() - 1;
    }

    int closest_idx = -1;
    double y_err_min = INFINITY;
    for (auto it = it_lower; it <= it_upper; ++it) {
        int idx = int(std::distance(vec_x.begin(), it));
        double y_err = std::abs(vec_y[idx] - y);
        if (y_err < y_err_min) {
            closest_idx = idx;
            y_err_min = y_err;
        }
    }
    return closest_idx;
}

/// <summary>
///  Reduce sample count to be a multiple of 2, 3 and 5 so that kf_bfly_generic does not need to be used since it is much slower
/// </summary>
/// <param name="n">Original sample count</param>
/// <returns>Largest sample count that can be expressed as 2^a * 3^b * 5^c where a, b and c are integers</returns>
size_t reduceSampleCountForFFT(size_t n) {
    if (n == 0) {
        n = 1;
    }

    size_t original_n = n;
    while (n % 2 == 0) {
        n = n / 2;
    }
    while (n % 3 == 0) {
        n = n / 3;
    }
    while (n % 5 == 0) {
        n = n / 5;
    }
    if (n == 1) {
        return original_n;
    } else {
        return reduceSampleCountForFFT(original_n - 1);
    }
}

Spectrum calculateSpectrum(std::vector<std::complex<double>> samples,
                           double sampling_time,
                           SpectrumWindow window,
                           bool one_sided) {
    // Push one zero if odd number of samples so that 1 second sampling time does not get
    // truncated down due to floating point inaccuracies when collecting samples (1 sample is missing)
    if (samples.size() % 2 == 1) {
        samples.push_back(0);
    }

    size_t sample_cnt = reduceSampleCountForFFT(samples.size());
    samples.resize(sample_cnt, 0);
    kissfft<double> fft(sample_cnt, false);
    std::vector<std::complex<double>> cplx_spec(sample_cnt, 0);

    // Apply window
    if (window == SpectrumWindow::Hann) {
        for (size_t n = 0; n < sample_cnt; ++n) {
            double amplitude_correction = 2.0;
            samples[n] *= amplitude_correction * (0.5 - 0.5 * cos(2 * PI * n / sample_cnt));
        }
    } else if (window == SpectrumWindow::Hamming) {
        for (size_t n = 0; n < sample_cnt; ++n) {
            double amplitude_correction = 1.8534;
            samples[n] *= amplitude_correction * (0.53836 - 0.46164 * cos(2 * PI * n / sample_cnt));
        }
    } else if (window == SpectrumWindow::FlatTop) {
        for (size_t n = 0; n < sample_cnt; ++n) {
            double a0 = 0.21557895;
            double a1 = 0.41663158;
            double a2 = 0.277263158;
            double a3 = 0.083578947;
            double a4 = 0.006947368;
            double amplitude_correction = 4.6432;
            samples[n] *= amplitude_correction
                        * (a0
                           - a1 * cos(2 * PI * n / (sample_cnt - 1))
                           + a2 * cos(4 * PI * n / (sample_cnt - 1))
                           - a3 * cos(6 * PI * n / (sample_cnt - 1))
                           + a4 * cos(8 * PI * n / (sample_cnt - 1)));
        }
    }
    fft.transform(samples.data(), cplx_spec.data());

    // Calculate magnitude spectrum with Hz on x-axis
    Spectrum spec;
    double abs_max = 0;
    double amplitude_inv = 1.0 / sample_cnt;
    // Very small bins are left out from FFT result because it breaks the autozoom with
    // double click since there are zero or very small amplitude bins that get included
    // into the plot and the plot always gets always zoomed -sampling_freq/2 to sampling_freq/2
    for (std::complex<double> x : cplx_spec) {
        abs_max = std::max(abs_max, amplitude_inv * std::abs(x));
    }
    double mag_min = abs_max * MAG_MIN_OF_MAX;

    int mid = int(cplx_spec.size() / 2);
    double resolution = 1.0 / (sampling_time * cplx_spec.size());
    double mag_coeff = one_sided ? 2 : 1;
    if (!one_sided) {
        // Negative side
        for (int i = 0; i < mid; ++i) {
            double mag = mag_coeff * std::abs(cplx_spec[mid + i]) * amplitude_inv;
            if (mag > mag_min) {
                spec.freq.push_back((-mid + i) * resolution);
                spec.mag.push_back(mag);
            }
        }
    }
    // DC
    spec.freq.push_back(0);
    spec.mag.push_back(std::abs(cplx_spec[0]) * amplitude_inv);

    // Positive side
    for (int i = 1; i < mid; ++i) {
        double mag = mag_coeff * std::abs(cplx_spec[i]) * amplitude_inv;
        if (mag > mag_min) {
            spec.freq.push_back(i * resolution);
            spec.mag.push_back(mag);
        }
    }

    return spec;
}
