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

inline void removeDeletedVectorComponents(PlotWindow::Subplot& subplot) {
    std::erase_if(subplot.vectors, [](VectorPair pair) {
        return !isLiveScalar(pair.x) || !isLiveScalar(pair.y);
    });
    if (subplot.reference_frame_vector && !contains(subplot.vectors, *subplot.reference_frame_vector)) {
        subplot.reference_frame_vector.reset();
    }
}

inline void removeDeletedSpectrumComponents(PlotWindow::Subplot& subplot) {
    std::erase_if(subplot.spectrums, [](auto const& spec) {
        return !isLiveScalar(spec.real) || (spec.imag && !isLiveScalar(spec.imag));
    });
}
