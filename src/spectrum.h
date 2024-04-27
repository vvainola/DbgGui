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
#pragma once

#include <complex>
#include <vector>

struct Spectrum {
    std::vector<double> freq;
    std::vector<double> mag;
};

enum SpectrumWindow {
    None,
    Hann,
    Hamming,
    FlatTop
};

Spectrum calculateSpectrum(std::vector<std::complex<double>> samples,
                           double sampling_time,
                           SpectrumWindow window,
                           bool one_sided);

std::vector<std::complex<double>> collectFftSamples(std::vector<double> const& time,
                                                    std::vector<double> const& samples_x,
                                                    std::vector<double> const& samples_y,
                                                    double sampling_time);

int closestSpectralBin(std::vector<double> const& vec_x, std::vector<double> const& vec_y, double x, double y);
