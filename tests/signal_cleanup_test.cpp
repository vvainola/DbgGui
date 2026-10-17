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

TEST_CASE("Moving vector components to an earlier group replaces every custom window scalar") {
    auto old_x = makeScalar("x", "z");
    auto old_y = makeScalar("y", "z");
    auto new_x = makeScalar("x", "a");
    auto new_y = makeScalar("y", "a");
    auto unchanged = makeScalar("other", "m");
    double value = 12;
    new_x->src = &value;
    new_y->src = &value;
    unchanged->src = &value;
    old_x->deleted = true;
    old_y->deleted = true;
    old_x->replacement = new_x.get();
    old_y->replacement = new_y.get();

    CustomWindow window("Signals", 1);
    window.addScalar(old_x.get());
    window.addScalar(old_y.get());
    window.addScalar(unchanged.get());
    replaceDeletedScalarsInCustomWindow(window);
    REQUIRE(window.scalars == std::vector<Scalar*>{new_x.get(), new_y.get(), unchanged.get()});

    // End-of-frame cleanup frees the moved scalars before the next render.
    old_x.reset();
    old_y.reset();
    for (Scalar* scalar : window.scalars) {
        CHECK(scalar->getValue() == value);
    }
    replaceDeletedScalarsInCustomWindow(window);
    CHECK(window.scalars.size() == 3);
}

TEST_CASE("Custom window cleanup deduplicates replacements and removes deleted replacements") {
    auto old_x = makeScalar("x", "a");
    auto old_y = makeScalar("y", "a");
    auto new_x = makeScalar("x", "z");
    auto new_y = makeScalar("y", "z");
    old_x->deleted = true;
    old_y->deleted = true;
    new_y->deleted = true;
    old_x->replacement = new_x.get();
    old_y->replacement = new_y.get();

    CustomWindow window("Signals", 1);
    window.addScalar(old_x.get());
    window.addScalar(old_y.get());
    window.addScalar(new_x.get());
    replaceDeletedScalarsInCustomWindow(window);
    REQUIRE(window.scalars == std::vector<Scalar*>{new_x.get()});
}

TEST_CASE("Vector subplot cleanup replaces components and deduplicates pairs") {
    auto old_x = makeScalar("old x", "group");
    auto x = makeScalar("x", "group");
    auto y = makeScalar("y", "group");
    PlotWindow::Subplot subplot;
    subplot.addVector({old_x.get(), y.get()});
    subplot.addVector({x.get(), y.get()});
    subplot.reference_frame_vector = VectorPair{old_x.get(), y.get()};
    old_x->deleted = true;
    old_x->replacement = x.get();

    replaceDeletedVectorComponents(subplot);

    REQUIRE(subplot.vectors.size() == 1);
    REQUIRE(subplot.vectors[0].x == x.get());
    REQUIRE(subplot.vectors[0].y == y.get());
    REQUIRE(subplot.reference_frame_vector == subplot.vectors[0]);

    SECTION("Deleting a component removes the pair and reference") {
        y->deleted = true;
    }
    SECTION("A deleted replacement cannot remain in the subplot") {
        y->deleted = true;
        y->replacement = old_x.get();
    }
    replaceDeletedVectorComponents(subplot);
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
