#pragma once

#include "control_grid.h"
#include "data_structures.h"

struct GridWindow : Window {
    GridWindow(std::string const& name, uint64_t id) : Window(name, id) {}
    explicit GridWindow(nlohmann::json const& data)
        : Window(data), grid(data.at("grid")) {}
    void updateJson(nlohmann::json& data) const {
        Window::updateJson(data);
        grid.updateJson(data["grid"]);
    }
    GridWindow duplicate(uint64_t new_id) const {
        nlohmann::json data;
        updateJson(data);
        data["id"] = new_id;
        data["name"] = name + " copy";
        data["initial_focus"] = false;
        return GridWindow(data);
    }

    Grid grid;
};
