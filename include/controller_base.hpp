/*
 * Base Controller Interface
 * 
 * Abstract base class for different controller types.
 * Follows factory pattern for extensibility.
 */

#ifndef CONTROLLER_BASE_HPP
#define CONTROLLER_BASE_HPP

#include "controller_config.hpp"
#include "xbox_udp_protocol.hpp"
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <memory>
#include <string>

struct ControllerHandle {
    int fd = -1;
    std::string path;
    std::string name;
    libevdev* dev = nullptr;
    std::shared_ptr<ControllerConfig> config;
};

class ControllerBase {
public:
    ControllerBase(ControllerHandle handle);
    virtual ~ControllerBase();

    // Factory method - creates appropriate controller type
    static std::unique_ptr<ControllerBase> create(ControllerHandle handle);

    // Process input event and create UDP packet
    virtual bool processEvent(const struct input_event& ev, xbox_udp::InputEventPacket& pkt) = 0;
    
    // Send vibration command
    virtual bool sendVibration(uint16_t left_motor, uint16_t right_motor) = 0;
    virtual void stopVibration() = 0;
    
    // Getters
    uint8_t getDeviceId() const { return device_id_; }
    void setDeviceId(uint8_t id) { device_id_ = id; }
    const std::string& getName() const { return handle_.name; }
    const std::string& getPath() const { return handle_.path; }
    int getFd() const { return handle_.fd; }
    libevdev* getDevice() const { return handle_.dev; }
    std::shared_ptr<ControllerConfig> getConfig() const { return handle_.config; }

protected:
    ControllerHandle handle_;
    uint8_t device_id_;
    
    // Helper: normalize axis value using config
    double normalizeAxisValue(unsigned code, int32_t raw_value) const;
};

// Xbox Controller implementation
class XboxController : public ControllerBase {
public:
    XboxController(ControllerHandle handle);
    ~XboxController() override;

    bool processEvent(const struct input_event& ev, xbox_udp::InputEventPacket& pkt) override;
    bool sendVibration(uint16_t left_motor, uint16_t right_motor) override;
    void stopVibration() override;

private:
    int current_effect_id_ = -1;
};

#endif // CONTROLLER_BASE_HPP
