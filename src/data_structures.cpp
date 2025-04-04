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

#include "data_structures.h"
#include "fts_fuzzy_match.h"
#include "nfd.h"

template <>
bool SignalGroup<Scalar>::hasVisibleItems(std::string const& filter) {
    if (filter == m_filter_prev && !filter.empty()) {
        return m_has_visible_items;
    }
    m_filter_prev = filter;
    m_has_visible_items = false;

    if (!filter.empty()) {
        for (std::string_view const& g : str::splitSv(full_name, '|')) {
            m_has_visible_items |= fts::fuzzy_match_simple(filter.c_str(), g);
        }
    }
    for (Scalar* scalar : signals) {
        if (m_has_visible_items) {
            // No need to check further
            return m_has_visible_items;
        } else if (filter.empty()) {
            m_has_visible_items |= !scalar->hide_from_scalars_window;
        } else if (!scalar->hide_from_scalars_window) {
            m_has_visible_items |= fts::fuzzy_match_simple(filter.c_str(), scalar->alias.c_str());
        }
    }
    for (auto& subgroup : subgroups) {
        m_has_visible_items |= subgroup.second.hasVisibleItems(filter);
    }

    return m_has_visible_items;
}

template <>
bool SignalGroup<Vector2D>::hasVisibleItems(std::string const& filter) {
    if (filter == m_filter_prev && !filter.empty()) {
        return m_has_visible_items;
    }
    m_filter_prev = filter;
    m_has_visible_items = false;

    if (!filter.empty()) {
        for (std::string_view const& g : str::splitSv(full_name, '|')) {
            m_has_visible_items |= fts::fuzzy_match_simple(filter.c_str(), g);
        }
    }
    for (Vector2D* vector : signals) {
        if (m_has_visible_items) {
            // No need to check further
            return m_has_visible_items;
        }
        m_has_visible_items |= fts::fuzzy_match_simple(filter.c_str(), vector->name.c_str());
    }
    for (auto& subgroup : subgroups) {
        m_has_visible_items |= subgroup.second.hasVisibleItems(filter);
    }

    return m_has_visible_items;
}

std::string getFilenameToSave(std::string const& filter, std::string default_path) {
    nfdchar_t* out_path = NULL;
    if (default_path.empty()) {
        default_path = std::filesystem::current_path().string();
    }
#if _WIN32
    default_path = str::replaceAll(default_path, "/", "\\");
#endif
    nfdresult_t result = NFD_SaveDialog(filter.c_str(), default_path.c_str(), &out_path);
    if (result == NFD_OKAY) {
        std::string out(out_path);
        free(out_path);
        if (!out.ends_with("." + filter)) {
            out.append("." + filter);
        }
        return out;
    }
    return "";
}

std::string getFilenameToOpen(std::string const& filter, std::string default_path) {
    nfdchar_t* out_path = NULL;
    if (default_path.empty()) {
        default_path = std::filesystem::current_path().string();
    }
#if _WIN32
    default_path = str::replaceAll(default_path, "/", "\\");
#endif
    nfdresult_t result = NFD_OpenDialog(filter.c_str(), default_path.c_str(), &out_path);
    if (result == NFD_OKAY) {
        return out_path;
    }
    return "";
}
