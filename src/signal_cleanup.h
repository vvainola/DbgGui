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

inline void replaceDeletedVectorComponents(PlotWindow::Subplot& subplot) {
    auto resolve_pair = [](VectorPair pair) -> std::optional<VectorPair> {
        if (pair.x && pair.x->deleted) {
            pair.x = pair.x->replacement;
        }
        if (pair.y && pair.y->deleted) {
            pair.y = pair.y->replacement;
        }
        if (!isLiveScalar(pair.x) || !isLiveScalar(pair.y)) {
            return std::nullopt;
        }
        return pair;
    };
    bool const needs_cleanup = std::ranges::any_of(subplot.vectors, [](VectorPair pair) {
        return !isLiveScalar(pair.x) || !isLiveScalar(pair.y);
    });
    if (needs_cleanup) {
        auto old_vectors = std::move(subplot.vectors);
        subplot.vectors.clear();
        for (VectorPair pair : old_vectors) {
            if (auto replacement = resolve_pair(pair)) {
                subplot.addVector(*replacement);
            }
        }
    }
    if (subplot.reference_frame_vector) {
        subplot.reference_frame_vector = resolve_pair(*subplot.reference_frame_vector);
        if (subplot.reference_frame_vector && !contains(subplot.vectors, *subplot.reference_frame_vector)) {
            subplot.reference_frame_vector.reset();
        }
    }
}

inline void replaceDeletedSpectrumComponents(PlotWindow::Subplot& subplot) {
    bool needs_cleanup = std::ranges::any_of(subplot.spectrums, [](auto const& spec) {
        return !isLiveScalar(spec.real) || (spec.imag && !isLiveScalar(spec.imag));
    });
    if (!needs_cleanup) {
        return;
    }
    auto old_spectrums = std::move(subplot.spectrums);
    subplot.spectrums.clear();
    for (auto& spec : old_spectrums) {
        Scalar* real = spec.real->deleted ? spec.real->replacement : spec.real;
        Scalar* imag = spec.imag && spec.imag->deleted ? spec.imag->replacement : spec.imag;
        if (!isLiveScalar(real) || (spec.imag && !isLiveScalar(imag))) {
            continue;
        }
        bool exists = std::ranges::any_of(subplot.spectrums, [=](auto const& current) {
            return current.real == real && current.imag == imag;
        });
        if (exists) {
            continue;
        }
        if (real == spec.real && imag == spec.imag) {
            subplot.spectrums.push_back(std::move(spec));
        } else {
            subplot.addSpectrum(real, imag);
        }
    }
}

inline void replaceDeletedScalarsInSubplot(PlotWindow::Subplot& subplot) {
    bool needs_cleanup = std::ranges::any_of(subplot.scalars, [](Scalar* scalar) { return scalar->deleted; });
    if (!needs_cleanup) {
        return;
    }
    auto old_scalars = std::move(subplot.scalars);
    subplot.scalars.clear();
    for (Scalar* scalar : old_scalars) {
        Scalar* replacement = scalar->deleted ? scalar->replacement : scalar;
        if (isLiveScalar(replacement)) {
            subplot.addScalar(replacement);
        }
    }
}

inline void replaceDeletedScalarsInCustomWindow(CustomWindow& window) {
    std::vector<Scalar*> replacements;
    for (Scalar* scalar : window.scalars) {
        if (scalar->deleted && isLiveScalar(scalar->replacement)) {
            replacements.push_back(scalar->replacement);
        }
    }
    std::erase_if(window.scalars, [](Scalar* scalar) { return scalar->deleted; });
    // addScalar sorts the list, so add replacements only after removing every old scalar.
    for (Scalar* replacement : replacements) {
        window.addScalar(replacement);
    }
}
