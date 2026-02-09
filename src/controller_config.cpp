/*
 * Controller Configuration Implementation
 */

#include "controller_config.hpp"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

ControllerConfig::ControllerConfig() 
    : norm_settings_{-1.0, 1.0, true} {
}

ControllerConfig::~ControllerConfig() = default;

bool ControllerConfig::loadFromFile(const std::string& config_path) {
    try {
        YAML::Node config = YAML::LoadFile(config_path);
        
        // Load controller info
        if (config["controller"]) {
            auto ctrl = config["controller"];
            if (ctrl["name"]) {
                name_ = ctrl["name"].as<std::string>();
            }
            if (ctrl["vendor_patterns"]) {
                vendor_patterns_ = ctrl["vendor_patterns"].as<std::vector<std::string>>();
            }
            if (ctrl["exclude_patterns"]) {
                exclude_patterns_ = ctrl["exclude_patterns"].as<std::vector<std::string>>();
            }
        }
        
        // Load button mappings
        if (config["buttons"]) {
            buttons_.clear();
            for (const auto& btn : config["buttons"]) {
                ButtonMapping mapping;
                mapping.code = btn["code"].as<unsigned>();
                mapping.name = btn["name"].as<std::string>();
                buttons_.push_back(mapping);
            }
        }
        
        // Load dpad button mappings (axis code + value -> button name)
        if (config["dpad_buttons"]) {
            dpad_buttons_.clear();
            for (const auto& dpad : config["dpad_buttons"]) {
                DpadButtonMapping mapping;
                mapping.axis_code = dpad["axis_code"].as<unsigned>();
                mapping.value = dpad["value"].as<int32_t>();
                mapping.name = dpad["name"].as<std::string>();
                dpad_buttons_.push_back(mapping);
                dpad_axis_codes_.insert(mapping.axis_code);
            }
        }
        
        // Load axis mappings
        if (config["axes"]) {
            axes_.clear();
            for (const auto& axis : config["axes"]) {
                AxisMapping mapping;
                mapping.code = axis["code"].as<unsigned>();
                mapping.name = axis["name"].as<std::string>();
                mapping.min = axis["min"].as<int32_t>();
                mapping.max = axis["max"].as<int32_t>();
                mapping.deadzone = axis["deadzone"].as<int32_t>(0);
                mapping.normalize = axis["normalize"].as<bool>(false);
                // Per-axis normalization range (defaults to global settings if not specified)
                mapping.output_min = axis["output_min"].as<double>(norm_settings_.output_min);
                mapping.output_max = axis["output_max"].as<double>(norm_settings_.output_max);
                axes_.push_back(mapping);
            }
        }
        
        // Load normalization settings
        if (config["normalization"]) {
            auto norm = config["normalization"];
            if (norm["output_min"]) {
                norm_settings_.output_min = norm["output_min"].as<double>();
            }
            if (norm["output_min"]) {
                norm_settings_.output_max = norm["output_max"].as<double>();
            }
            if (norm["apply_deadzone"]) {
                norm_settings_.apply_deadzone = norm["apply_deadzone"].as<bool>();
            }
        }
        
        buildLookupMaps();
        return true;
    } catch (const YAML::Exception& e) {
        std::cerr << "Error loading config file " << config_path << ": " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config file " << config_path << ": " << e.what() << std::endl;
        return false;
    }
}

bool ControllerConfig::matchesDevice(const std::string& device_name) const {
    std::string lower_name = device_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    // Check exclude patterns first
    if (matchesPattern(lower_name, exclude_patterns_)) {
        return false;
    }
    
    // Check vendor patterns
    return matchesPattern(lower_name, vendor_patterns_);
}

bool ControllerConfig::matchesPattern(const std::string& text, const std::vector<std::string>& patterns) const {
    for (const auto& pattern : patterns) {
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
        if (text.find(lower_pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const std::string* ControllerConfig::getButtonName(unsigned code) const {
    auto it = button_map_.find(code);
    if (it != button_map_.end()) {
        return &it->second;
    }
    return nullptr;
}

const std::string* ControllerConfig::getDpadButtonName(unsigned axis_code, int32_t value) const {
    auto axis_it = dpad_button_map_.find(axis_code);
    if (axis_it != dpad_button_map_.end()) {
        auto value_it = axis_it->second.find(value);
        if (value_it != axis_it->second.end()) {
            return &value_it->second;
        }
    }
    return nullptr;
}

bool ControllerConfig::isDpadAxis(unsigned code) const {
    return dpad_axis_codes_.count(code) > 0;
}

const AxisMapping* ControllerConfig::getAxisMapping(unsigned code) const {
    auto it = axis_map_.find(code);
    if (it != axis_map_.end()) {
        return &it->second;
    }
    return nullptr;
}

double ControllerConfig::normalizeAxis(unsigned code, int32_t raw_value) const {
    const AxisMapping* mapping = getAxisMapping(code);
    if (!mapping || !mapping->normalize) {
        return static_cast<double>(raw_value);
    }
    
    int32_t value = raw_value;
    bool is_symmetric = (mapping->output_min < 0.0);
    
    // Apply deadzone if enabled (e.g., for Left-X, Left-Y, Right-X, Right-Y)
    if (norm_settings_.apply_deadzone && mapping->deadzone > 0) {
        int32_t abs_value = std::abs(value);
        if (abs_value <= mapping->deadzone) {
            return 0.0;
        }
        // Scale to remove deadzone
        if (value > 0) {
            value = value - mapping->deadzone;
        } else {
            value = value + mapping->deadzone;
        }
    }
    
    // Clamp to bounds
    value = std::max(mapping->min, std::min(mapping->max, value));
    
    // Calculate effective range after deadzone removal
    // This is the range that values can actually reach after deadzone is applied
    int32_t effective_max_pos = mapping->max;
    int32_t effective_min_neg = mapping->min;
    
    if (norm_settings_.apply_deadzone && mapping->deadzone > 0) {
        effective_max_pos = mapping->max - mapping->deadzone;
        effective_min_neg = mapping->min + mapping->deadzone;
    }
    
    double normalized;
    double output_range = mapping->output_max - mapping->output_min;
    
    if (is_symmetric) {
        // For symmetric axes (e.g., -1.0 to 1.0): use maximum absolute value for normalization
        // This ensures the output reaches the full range [-1.0, 1.0]
        int32_t max_abs = std::max(std::abs(effective_max_pos), std::abs(effective_min_neg));
        if (max_abs == 0) {
            return 0.0;
        }
        normalized = static_cast<double>(value) / static_cast<double>(max_abs);
        // Clamp normalized to [-1.0, 1.0] and map to output range
        normalized = std::max(-1.0, std::min(1.0, normalized));
        return mapping->output_min + ((normalized + 1.0) / 2.0) * output_range;
    } else {
        // For asymmetric axes (e.g., 0.0 to 1.0): normalize using effective range
        double effective_range = static_cast<double>(effective_max_pos - effective_min_neg);
        if (effective_range == 0.0) {
            return mapping->output_min;
        }
        normalized = (static_cast<double>(value - effective_min_neg) / effective_range);
        return mapping->output_min + (normalized * output_range);
    }
}

void ControllerConfig::buildLookupMaps() {
    button_map_.clear();
    axis_map_.clear();
    dpad_button_map_.clear();
    dpad_axis_codes_.clear();
    
    for (const auto& btn : buttons_) {
        button_map_[btn.code] = btn.name;
    }
    
    for (const auto& dpad : dpad_buttons_) {
        dpad_button_map_[dpad.axis_code][dpad.value] = dpad.name;
        dpad_axis_codes_.insert(dpad.axis_code);
    }
    
    for (const auto& axis : axes_) {
        axis_map_[axis.code] = axis;
    }
}

// ConfigManager implementation
ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

std::shared_ptr<ControllerConfig> ConfigManager::loadConfig(const std::string& config_path) {
    auto config = std::make_shared<ControllerConfig>();
    if (config->loadFromFile(config_path)) {
        return config;
    }
    return nullptr;
}

std::shared_ptr<ControllerConfig> ConfigManager::detectConfig(const std::string& device_name, 
                                                                const std::string& config_dir) {
    // Try to find matching config file
    if (!fs::exists(config_dir) || !fs::is_directory(config_dir)) {
        std::cerr << "Config directory not found: " << config_dir << std::endl;
        return nullptr;
    }
    
    for (const auto& entry : fs::directory_iterator(config_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yaml") {
            auto config = loadConfig(entry.path().string());
            if (config && config->matchesDevice(device_name)) {
                std::string config_name = entry.path().stem().string();
                registerConfig(config_name, config);
                return config;
            }
        }
    }
    
    return nullptr;
}

void ConfigManager::registerConfig(const std::string& name, std::shared_ptr<ControllerConfig> config) {
    configs_[name] = config;
}

std::shared_ptr<ControllerConfig> ConfigManager::getConfig(const std::string& name) {
    auto it = configs_.find(name);
    if (it != configs_.end()) {
        return it->second;
    }
    return nullptr;
}
