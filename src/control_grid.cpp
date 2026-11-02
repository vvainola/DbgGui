#include "control_grid.h"

ControlCell ControlCell::fromJson(nlohmann::json const& data) {
    ControlCell cell;
    cell.type = data.value("type", ControlCellType::Value);
    switch (cell.type) {
        case ControlCellType::Button:
            cell.button.label = data.value("label", cell.button.label);
            cell.button.script = data.value("script", cell.button.script);
            break;
        case ControlCellType::Checkbox:
            cell.checkbox.label = data.value("label", cell.checkbox.label);
            cell.checkbox.on_enable.script = data.value("on_enable", std::string{});
            cell.checkbox.on_disable.script = data.value("on_disable", std::string{});
            break;
        case ControlCellType::Value:
        case ControlCellType::Slider: {
            auto& value = cell.type == ControlCellType::Value ? cell.value : cell.slider;
            if (data.contains("scalar_id") && !data["scalar_id"].is_null()) {
                value.scalar_id = data["scalar_id"].get<uint64_t>();
            }
            cell.slider.minimum = data.value("minimum", 0.0);
            cell.slider.maximum = data.value("maximum", 1.0);
            break;
        }
        case ControlCellType::Led: {
            cell.led.label = data.value("label", cell.led.label);
            cell.led.condition = data.value("script", cell.led.condition);
            auto const color = data.value("color", std::vector<float>{0.2f, 0.9f, 0.3f, 1});
            if (color.size() == 4) {
                cell.led.color = {color[0], color[1], color[2], color[3]};
            }
            break;
        }
    }
    return cell;
}

void ControlCell::updateJson(nlohmann::json& data) const {
    data = {{"type", type}};
    switch (type) {
        case ControlCellType::Button:
            data["label"] = button.label;
            data["script"] = button.script;
            break;
        case ControlCellType::Checkbox:
            data["label"] = checkbox.label;
            data["on_enable"] = checkbox.on_enable.script;
            data["on_disable"] = checkbox.on_disable.script;
            break;
        case ControlCellType::Value:
        case ControlCellType::Slider: {
            auto const& settings = type == ControlCellType::Value ? value : slider;
            if (settings.scalar_id) {
                data["scalar_id"] = *settings.scalar_id;
            }
            if (type == ControlCellType::Slider) {
                data["minimum"] = slider.minimum;
                data["maximum"] = slider.maximum;
            }
            break;
        }
        case ControlCellType::Led:
            data["label"] = led.label;
            data["script"] = led.condition;
            data["color"] = {led.color.x, led.color.y, led.color.z, led.color.w};
            break;
    }
}

void ControlCell::remapScalarId(uint64_t old_id, uint64_t new_id) {
    for (auto* binding : {&value.scalar_id, &slider.scalar_id}) {
        if (*binding == old_id) {
            *binding = new_id;
        }
    }
}
