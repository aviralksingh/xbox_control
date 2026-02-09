/*
 * Controller Configuration
 * 
 * Loads and manages controller configurations from YAML files.
 * Supports button/axis mappings, normalization bounds, and deadzones.
 */

#ifndef CONTROLLER_CONFIG_HPP
#define CONTROLLER_CONFIG_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

struct ButtonMapping {
    unsigned code;
    std::string name;
};

struct DpadButtonMapping {
    unsigned axis_code;  // The axis code (e.g., ABS_HAT0X, ABS_HAT0Y)
    int32_t value;       // The axis value that triggers this button (-1, 0, or 1)
    std::string name;    // Button name (e.g., "Dpad-Left", "Dpad-Right")
};

struct AxisMapping {
    unsigned code;
    std::string name;
    int32_t min;
    int32_t max;
    int32_t deadzone;
    bool normalize;
    double output_min;  // Normalization output minimum (e.g., -1.0 for sticks, 0.0 for triggers)
    double output_max;  // Normalization output maximum (e.g., 1.0)
};

struct NormalizationSettings {
    double output_min;
    double output_max;
    bool apply_deadzone;
};

class ControllerConfig {
public:
    ControllerConfig();
    ~ControllerConfig();

    // Load configuration from YAML file
    bool loadFromFile(const std::string& config_path);
    
    // Check if a device name matches this controller
    bool matchesDevice(const std::string& device_name) const;
    
    // Get button name by code
    const std::string* getButtonName(unsigned code) const;
    
    // Get dpad button name by axis code and value (converts axis events to button names)
    const std::string* getDpadButtonName(unsigned axis_code, int32_t value) const;
    
    // Get axis mapping by code
    const AxisMapping* getAxisMapping(unsigned code) const;
    
    // Check if an axis code is a dpad axis (should be converted to buttons)
    bool isDpadAxis(unsigned code) const;
    
    // Normalize an axis value
    double normalizeAxis(unsigned code, int32_t raw_value) const;
    
    // Get all button mappings (for display purposes)
    const std::vector<ButtonMapping>& getButtonMappings() const { return buttons_; }
    
    // Get all dpad button mappings (for display purposes)
    const std::vector<DpadButtonMapping>& getDpadButtonMappings() const { return dpad_buttons_; }
    
    // Get all axis mappings (for display purposes)
    const std::vector<AxisMapping>& getAxisMappings() const { return axes_; }
    
    // Get controller name
    const std::string& getName() const { return name_; }
    
    // Get normalization settings
    const NormalizationSettings& getNormalizationSettings() const { return norm_settings_; }

private:
    std::string name_;
    std::vector<std::string> vendor_patterns_;
    std::vector<std::string> exclude_patterns_;
    std::vector<ButtonMapping> buttons_;
    std::vector<DpadButtonMapping> dpad_buttons_;
    std::vector<AxisMapping> axes_;
    NormalizationSettings norm_settings_;
    
    // Fast lookup maps
    std::unordered_map<unsigned, std::string> button_map_;
    std::unordered_map<unsigned, AxisMapping> axis_map_;
    std::unordered_map<unsigned, std::unordered_map<int32_t, std::string>> dpad_button_map_;  // axis_code -> (value -> name)
    std::unordered_set<unsigned> dpad_axis_codes_;  // Set of axis codes that are dpad axes
    
    void buildLookupMaps();
    bool matchesPattern(const std::string& text, const std::vector<std::string>& patterns) const;
};

// Global config manager
class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    // Load a controller config
    std::shared_ptr<ControllerConfig> loadConfig(const std::string& config_path);
    
    // Auto-detect and load config for a device
    std::shared_ptr<ControllerConfig> detectConfig(const std::string& device_name, 
                                                    const std::string& config_dir = "config");
    
    // Register a config
    void registerConfig(const std::string& name, std::shared_ptr<ControllerConfig> config);
    
    // Get config by name
    std::shared_ptr<ControllerConfig> getConfig(const std::string& name);

private:
    ConfigManager() = default;
    std::unordered_map<std::string, std::shared_ptr<ControllerConfig>> configs_;
};

#endif // CONTROLLER_CONFIG_HPP
