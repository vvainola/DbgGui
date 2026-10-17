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

#include <catch2/catch_test_macros.hpp>

#include "signal_cleanup.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

uint64_t testSignalId(std::string const& name, std::string const& group) {
    return std::hash<std::string>{}(name + " (" + group + ")");
}

std::unique_ptr<Scalar> makeScalar(std::string const& name, std::string const& group) {
    auto scalar = std::make_unique<Scalar>();
    scalar->name = name;
    scalar->group = group;
    scalar->alias = name;
    scalar->updateDisplayNames();
    scalar->id = testSignalId(name, group);
    return scalar;
}

} // namespace

TEST_CASE("Vector subplot cleanup removes pairs with either deleted component") {
    auto x = makeScalar("x", "group");
    auto y = makeScalar("y", "group");
    PlotWindow::Subplot subplot;
    subplot.addVector({x.get(), y.get()});
    subplot.reference_frame_vector = subplot.vectors[0];
    removeDeletedVectorComponents(subplot);
    REQUIRE(subplot.vectors.size() == 1);
    REQUIRE(subplot.reference_frame_vector == subplot.vectors[0]);
    SECTION("X is deleted") {
        x->deleted = true;
    }
    SECTION("Y is deleted") {
        y->deleted = true;
    }
    removeDeletedVectorComponents(subplot);
    REQUIRE(subplot.vectors.empty());
    REQUIRE_FALSE(subplot.reference_frame_vector.has_value());
}

TEST_CASE("Shrinking a subplot grid preserves vector pairs without changing the reference frame") {
    auto x = makeScalar("x", "group");
    auto y = makeScalar("y", "group");
    VectorPair xy{x.get(), y.get()};
    VectorPair yx{y.get(), x.get()};
    PlotWindow plot("test", 1);
    plot.setSubplotGrid(2, 2);
    plot.subplots[0].addVector(xy);
    plot.subplots[3].addVector(xy);
    plot.subplots[3].addVector(yx);
    plot.subplots[3].reference_frame_vector = yx;

    SECTION("Do not adopt the removed cell's reference frame") {
        REQUIRE_FALSE(plot.subplots[0].reference_frame_vector.has_value());
    }
    SECTION("Keep an existing reference frame") {
        plot.subplots[0].reference_frame_vector = xy;
    }
    auto expected_reference = plot.subplots[0].reference_frame_vector;
    plot.setSubplotGrid(1, 1);

    REQUIRE(plot.subplots[0].vectors.size() == 2);
    REQUIRE(contains(plot.subplots[0].vectors, xy));
    REQUIRE(contains(plot.subplots[0].vectors, yx));
    REQUIRE(plot.subplots[0].reference_frame_vector == expected_reference);
}

TEST_CASE("Spectrum subplot cleanup removes spectra with deleted components") {
    auto old_real = makeScalar("old real", "group");
    auto real = makeScalar("real", "group");
    auto imag = makeScalar("imag", "group");
    PlotWindow::Subplot subplot;
    subplot.addSpectrum(old_real.get(), imag.get());
    subplot.addSpectrum(real.get(), imag.get());
    subplot.addSpectrum(real.get());
    old_real->deleted = true;
    removeDeletedSpectrumComponents(subplot);
    REQUIRE(subplot.spectrums.size() == 2);
    REQUIRE(subplot.spectrums[0].real == real.get());
    REQUIRE(subplot.spectrums[0].imag == imag.get());
    imag->deleted = true;
    removeDeletedSpectrumComponents(subplot);
    REQUIRE(subplot.spectrums.size() == 1);
    REQUIRE(subplot.spectrums[0].real == real.get());
    REQUIRE(subplot.spectrums[0].imag == nullptr);
}

TEST_CASE("Spectrum subplot settings round trip and grid shrinking preserves spectra") {
    auto real = makeScalar("real", "group");
    auto imag = makeScalar("imag", "group");
    PlotWindow plot("spectra", 1);
    plot.setSubplotGrid(2, 2);
    plot.subplots[3].type = PlotType::Spectrum;
    plot.spectrum.logarithmic_y_axis = true;
    plot.subplots[3].spectrum_x_axis = {10, 100};
    plot.spectrum.time_range = 0.5;
    plot.spectrum.window = SpectrumWindow::Hann;
    plot.subplots[3].addSpectrum(real.get(), imag.get());
    nlohmann::json saved;
    plot.updateJson(saved);
    PlotWindow restored(saved);
    REQUIRE(restored.subplots[3].type == PlotType::Spectrum);
    REQUIRE(restored.spectrum.logarithmic_y_axis);
    REQUIRE(restored.subplots[3].spectrum_x_axis.min == 10);
    REQUIRE(restored.spectrum.time_range == 0.5);
    REQUIRE(restored.spectrum.window == SpectrumWindow::Hann);
    plot.setSubplotGrid(1, 1);
    REQUIRE(plot.subplots[0].spectrums.size() == 1);
    plot.subplots[0].addSpectrum(real.get());
    plot.subplots[0].removeSpectrum(real.get(), imag.get());
    REQUIRE(plot.subplots[0].spectrums.size() == 1);
    REQUIRE(plot.subplots[0].spectrums[0].imag == nullptr);
    plot.clearSignals();
    REQUIRE(plot.subplots[0].spectrums.empty());
}
