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

std::unique_ptr<Vector2D> makeVector(Scalar* x, Scalar* y) {
    auto vector = std::make_unique<Vector2D>();
    vector->name = x->name;
    vector->group = x->group;
    vector->name_and_group = vector->name + " (" + vector->group + ")";
    vector->id = testSignalId(x->name + "+" + y->name, x->group);
    vector->x = x;
    vector->y = y;
    return vector;
}

void eraseDeletedVectors(std::vector<std::unique_ptr<Vector2D>>& vectors) {
    vectors.erase(std::remove_if(vectors.begin(), vectors.end(), [](auto const& vector) {
        return vector->deleted;
    }), vectors.end());
}

} // namespace

TEST_CASE("Deleted scalar vector cleanup allows recreating the vector") {
    std::string const group_name = "group 2";
    auto old_x = makeScalar("g::abc.a", group_name);
    auto old_y = makeScalar("g::abc.b", group_name);
    auto old_z = makeScalar("g::abc.c", group_name);

    std::vector<std::unique_ptr<Vector2D>> vectors;
    auto old_vector = makeVector(old_x.get(), old_y.get());
    Vector2D* old_vector_ptr = old_vector.get();
    vectors.push_back(std::move(old_vector));

    SignalGroup<Vector2D> vector_group;
    vector_group.name = group_name;
    vector_group.full_name = group_name;
    vector_group.signals.push_back(old_vector_ptr);

    VectorPlot vector_plot("Vector plot", 1);
    vector_plot.addVectorToPlot(old_vector_ptr);

    old_x->deleted = true;
    old_y->deleted = true;
    old_z->deleted = true;
    markVectorsDeletedByDeletedComponents(vectors);

    REQUIRE(old_vector_ptr->deleted);
    replaceDeletedVectorInPlot(vector_plot, old_vector_ptr);
    removeDeletedSignalsFromGroup(vector_group);
    eraseDeletedVectors(vectors);

    CHECK(vector_plot.vectors.empty());
    CHECK(vector_group.signals.empty());
    CHECK(vectors.empty());

    auto new_x = makeScalar("g::abc.a", group_name);
    auto new_y = makeScalar("g::abc.b", group_name);
    auto new_vector = makeVector(new_x.get(), new_y.get());
    Vector2D* new_vector_ptr = new_vector.get();
    vectors.push_back(std::move(new_vector));
    vector_group.signals.push_back(new_vector_ptr);
    vector_plot.addVectorToPlot(new_vector_ptr);

    REQUIRE(vector_group.signals.size() == 1);
    CHECK(vector_group.signals.front() == new_vector_ptr);
    REQUIRE(vector_plot.vectors.size() == 1);
    CHECK(vector_plot.vectors.front() == new_vector_ptr);
    CHECK(isLiveVector(new_vector_ptr));
}

TEST_CASE("Deleted vector cleanup does not keep a deleted replacement in plots") {
    std::string const group_name = "group 2";
    auto old_x = makeScalar("g::abc.a", group_name);
    auto old_y = makeScalar("g::abc.b", group_name);
    auto replacement_x = makeScalar("g::abc.a", group_name);
    auto replacement_y = makeScalar("g::abc.b", group_name);

    auto old_vector = makeVector(old_x.get(), old_y.get());
    auto replacement_vector = makeVector(replacement_x.get(), replacement_y.get());
    old_vector->deleted = true;
    old_vector->replacement = replacement_vector.get();
    replacement_x->deleted = true;

    VectorPlot vector_plot("Vector plot", 1);
    vector_plot.addVectorToPlot(old_vector.get());
    replaceDeletedVectorInPlot(vector_plot, old_vector.get());

    CHECK(vector_plot.vectors.empty());
}
