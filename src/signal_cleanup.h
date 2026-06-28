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

#pragma once

#include "data_structures.h"

#include <memory>
#include <vector>

inline bool isLiveScalar(Scalar const* scalar) {
    return scalar != nullptr && !scalar->deleted;
}

inline bool isLiveVector(Vector2D const* vector) {
    return vector != nullptr
        && !vector->deleted
        && isLiveScalar(vector->x)
        && isLiveScalar(vector->y);
}

inline void markVectorsDeletedByDeletedComponents(std::vector<std::unique_ptr<Vector2D>>& vectors) {
    for (auto& vector : vectors) {
        if (vector->x->deleted || vector->y->deleted) {
            vector->deleted = true;
        }
        if (vector->deleted) {
            if (vector->x->hide_from_scalars_window) {
                vector->x->deleted = true;
            }
            if (vector->y->hide_from_scalars_window) {
                vector->y->deleted = true;
            }
        }
    }
}

template <typename T>
inline void removeDeletedSignalsFromGroup(SignalGroup<T>& group) {
    for (int i = int(group.signals.size() - 1); i >= 0; --i) {
        if (group.signals[i]->deleted) {
            remove(group.signals, group.signals[i]);
        }
    }
    for (auto& subgroup : group.subgroups) {
        removeDeletedSignalsFromGroup(subgroup.second);
    }
}

inline void replaceDeletedVectorInPlot(VectorPlot& vector_plot, Vector2D* vector) {
    if (vector->deleted && isLiveVector(vector->replacement)) {
        vector_plot.addVectorToPlot(vector->replacement);
    }
    remove(vector_plot.vectors, vector);
}
