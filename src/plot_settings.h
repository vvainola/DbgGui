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

namespace plot_settings {

template <typename Fn>
void forEachSignalId(nlohmann::json const& signals, Fn fn) {
    if (!signals.is_object()) {
        return;
    }
    for (auto const& signal : signals) {
        if (signal.is_number_unsigned() || signal.is_number_integer()) {
            fn(signal.get<uint64_t>());
        }
    }
}


template <typename Fn>
void forEachPair(nlohmann::json const& pairs, Fn fn) {
    if (!pairs.is_array()) {
        return;
    }
    for (auto const& pair : pairs) {
        if (pair.is_array() && pair.size() == 2
            && (pair[0].is_number_unsigned() || pair[0].is_number_integer())
            && (pair[1].is_number_unsigned() || pair[1].is_number_integer())) {
            fn(pair[0].get<uint64_t>(), pair[1].get<uint64_t>());
        }
    }
}

// Include deleted scalars: their saved placements must be removed, rather than
// preserved as unresolved after the scalar objects are destroyed.
inline bool isRegistered(uint64_t id, std::vector<std::unique_ptr<Scalar>> const& scalars) {
    return std::ranges::any_of(scalars, [id](auto const& scalar) { return scalar->id == id; });
}

namespace detail {
inline Scalar* findLive(uint64_t id, std::vector<std::unique_ptr<Scalar>> const& scalars) {
    for (auto const& scalar : scalars) {
        if (scalar->id == id && !scalar->deleted) {
            return scalar.get();
        }
    }
    return nullptr;
}
} // namespace detail

// Restore scalar placements at session load or for a newly registered ID.
inline std::vector<Scalar*> restoreScalars(PlotWindow::Subplot& subplot,
                                           nlohmann::json const& saved_subplot,
                                           std::vector<std::unique_ptr<Scalar>> const& scalars,
                                           std::optional<uint64_t> registered_id = std::nullopt) {
    std::vector<Scalar*> to_sample;
    if (!saved_subplot.contains("signals")) {
        return to_sample;
    }
    forEachSignalId(saved_subplot["signals"], [&](uint64_t id) {
        if (registered_id && *registered_id != id) {
            return;
        }
        if (Scalar* scalar = detail::findLive(id, scalars)) {
            subplot.addScalar(scalar);
            if (!contains(to_sample, scalar)) {
                to_sample.push_back(scalar);
            }
        }
    });
    return to_sample;
}

// Restoration functions return components to sample. Passing an ID limits
// restoration to placements involving a scalar that has just registered.
inline std::vector<Scalar*> restoreVectors(PlotWindow::Subplot& subplot,
                                           nlohmann::json const& saved_subplot,
                                           std::vector<std::unique_ptr<Scalar>> const& scalars,
                                           std::optional<uint64_t> registered_id = std::nullopt) {
    std::vector<Scalar*> to_sample;
    if (!saved_subplot.contains("vectors")) {
        return to_sample;
    }
    forEachPair(saved_subplot["vectors"], [&](uint64_t x_id, uint64_t y_id) {
        if (registered_id && *registered_id != x_id && *registered_id != y_id) {
            return;
        }
        Scalar* x = detail::findLive(x_id, scalars);
        Scalar* y = detail::findLive(y_id, scalars);
        if (!x || !y) {
            return;
        }
        subplot.addVector({x, y});
        if (saved_subplot.contains("reference_frame")
            && saved_subplot["reference_frame"] == nlohmann::json::array({x_id, y_id})) {
            subplot.reference_frame_vector = VectorPair{x, y};
        }
        for (Scalar* component : {x, y}) {
            if (!contains(to_sample, component)) {
                to_sample.push_back(component);
            }
        }
    });
    return to_sample;
}

inline std::vector<Scalar*> restoreSpectra(PlotWindow::Subplot& subplot,
                                           nlohmann::json const& saved_subplot,
                                           std::vector<std::unique_ptr<Scalar>> const& scalars,
                                           std::optional<uint64_t> registered_id = std::nullopt) {
    std::vector<Scalar*> to_sample;
    if (!saved_subplot.contains("spectrums")) {
        return to_sample;
    }
    forEachPair(saved_subplot["spectrums"], [&](uint64_t real_id, uint64_t imag_id) {
        if (registered_id && *registered_id != real_id && *registered_id != imag_id) {
            return;
        }
        Scalar* real = detail::findLive(real_id, scalars);
        // An imaginary ID of zero denotes a real-only spectrum.
        Scalar* imag = imag_id == 0 ? nullptr : detail::findLive(imag_id, scalars);
        if (!real || (imag_id != 0 && !imag)) {
            return;
        }
        subplot.addSpectrum(real, imag);
        for (Scalar* component : {real, imag}) {
            if (component && !contains(to_sample, component)) {
                to_sample.push_back(component);
            }
        }
    });
    return to_sample;
}

inline void reconcileScalars(PlotWindow::Subplot const& subplot, nlohmann::json& saved_subplot,
                              std::vector<std::unique_ptr<Scalar>> const& scalars) {
    auto& signals = saved_subplot["signals"];
    if (!signals.is_object()) {
        signals = nlohmann::json::object();
    }
    // Preserve unresolved names/IDs, but replace all registered placements.
    for (auto it = signals.begin(); it != signals.end();) {
        if ((!it->is_number_unsigned() && !it->is_number_integer())
            || isRegistered(it->get<uint64_t>(), scalars)) {
            it = signals.erase(it);
        } else {
            ++it;
        }
    }
    for (Scalar* scalar : subplot.scalars) {
        signals[scalar->name_and_group] = scalar->id;
    }
}

// Rebuild registered placements from the runtime subplot; keep unresolved
// entries so signals absent from this executable can restore later.
inline void reconcileVectors(PlotWindow::Subplot const& subplot,
                             nlohmann::json& saved_subplot,
                             std::vector<std::unique_ptr<Scalar>> const& scalars) {
    auto vectors = nlohmann::json::array();
    forEachPair(saved_subplot["vectors"], [&](uint64_t x_id, uint64_t y_id) {
        if (!isRegistered(x_id, scalars) || !isRegistered(y_id, scalars)) {
            vectors.push_back({x_id, y_id});
        }
    });
    for (VectorPair pair : subplot.vectors) {
        vectors.push_back({pair.x->id, pair.y->id});
    }
    saved_subplot["vectors"] = std::move(vectors);
    if (subplot.reference_frame_vector) {
        auto const& reference = *subplot.reference_frame_vector;
        saved_subplot["reference_frame"] = {reference.x->id, reference.y->id};
    } else if (saved_subplot.contains("reference_frame")) {
        bool unresolved = false;
        forEachPair(nlohmann::json::array({saved_subplot["reference_frame"]}), [&](uint64_t x_id, uint64_t y_id) {
            unresolved = !isRegistered(x_id, scalars) || !isRegistered(y_id, scalars);
        });
        if (!unresolved) {
            saved_subplot.erase("reference_frame");
        }
    }
}

inline void reconcileSpectra(PlotWindow::Subplot const& subplot,
                             nlohmann::json& saved_subplot,
                             std::vector<std::unique_ptr<Scalar>> const& scalars) {
    auto spectra = nlohmann::json::array();
    forEachPair(saved_subplot["spectrums"], [&](uint64_t real_id, uint64_t imag_id) {
        bool imag_registered = imag_id == 0 || isRegistered(imag_id, scalars);
        if (!isRegistered(real_id, scalars) || !imag_registered) {
            spectra.push_back({real_id, imag_id});
        }
    });
    for (auto const& spec : subplot.spectrums) {
        spectra.push_back({spec.real->id, spec.imag == nullptr ? 0 : spec.imag->id});
    }
    saved_subplot["spectrums"] = std::move(spectra);
}

// A group move changes the scalar ID. Update saved references too, including
// unresolved pairs that cannot be rebuilt from runtime placements when saving.
inline void remapScalarId(nlohmann::json& settings, uint64_t old_id,
                          std::string const& old_group, Scalar const& scalar) {
    uint64_t const new_id = scalar.id;
    std::string const old_name = scalar.name + " (" + old_group + ")";
    std::string const old_custom_key = old_group + " " + scalar.name;
    // Only placement fields contain signal IDs; window IDs and axis settings
    // must not be changed even if their numeric values happen to match.
    auto remap_ids = [&](auto&& self, nlohmann::json& ids) -> void {
        if (ids.is_array() || ids.is_object()) {
            for (auto& id : ids) {
                self(self, id);
            }
        } else if ((ids.is_number_unsigned() || ids.is_number_integer()) && ids.get<uint64_t>() == old_id) {
            ids = new_id;
        }
    };
    auto remap_signals = [&](nlohmann::json& owner, std::string const& old_key, std::string const& new_key) {
        if (!owner.contains("signals")) {
            return;
        }
        auto& signals = owner["signals"];
        remap_ids(remap_ids, signals);
        if (signals.is_object() && signals.contains(old_key)) {
            signals[new_key] = new_id;
            signals.erase(old_key);
        }
    };
    for (auto& plot : settings["scalar_plots"]) {
        if (!plot.contains("subplots")) {
            continue;
        }
        for (auto& subplot : plot["subplots"]) {
            remap_signals(subplot, old_name, scalar.name_and_group);
            for (auto key : {"vectors", "spectrums", "reference_frame"}) {
                if (subplot.contains(key)) {
                    remap_ids(remap_ids, subplot[key]);
                }
            }
        }
    }
    for (auto& window : settings["custom_windows"]) {
        remap_signals(window, old_custom_key, scalar.group + " " + scalar.name);
    }
    for (auto& window : settings["grid_windows"]) {
        remap_ids(remap_ids, window["signals"]);
    }
    for (auto key : {"scalars", "scalar_symbols"}) {
        auto& entries = settings[key];
        if (entries.contains(old_name)) {
            entries[scalar.name_and_group] = std::move(entries[old_name]);
            entries.erase(old_name);
        }
    }
    scalar.updateJson(settings["scalars"][scalar.name_and_group]);
    auto& symbol = settings["scalar_symbols"][scalar.name_and_group];
    symbol["name"] = scalar.name;
    symbol["group"] = scalar.group;
}

} // namespace plot_settings
