/*
 * Base Controller Implementation
 */

#include "controller_base.hpp"
#include <sys/ioctl.h>
#include <linux/input.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <iostream>

// Helper macro for testing bits
#define BITS_PER_LONG (sizeof(long) * 8)
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define test_bit(nr, addr) (((1UL << ((nr) % BITS_PER_LONG)) & ((addr)[BIT_WORD(nr)])) != 0)

ControllerBase::ControllerBase(ControllerHandle handle) 
    : handle_(std::move(handle)), device_id_(0) {
}

ControllerBase::~ControllerBase() = default;

std::unique_ptr<ControllerBase> ControllerBase::create(ControllerHandle handle) {
    if (!handle.config) {
        return nullptr;
    }
    
    // For now, all controllers use XboxController implementation
    // In the future, we can check config type and create different implementations
    std::string config_name = handle.config->getName();
    std::transform(config_name.begin(), config_name.end(), config_name.begin(), ::tolower);
    
    if (config_name.find("xbox") != std::string::npos) {
        return std::make_unique<XboxController>(std::move(handle));
    }
    
    // Default to XboxController for generic gamepads
    return std::make_unique<XboxController>(std::move(handle));
}

double ControllerBase::normalizeAxisValue(unsigned code, int32_t raw_value) const {
    if (!handle_.config) {
        return static_cast<double>(raw_value);
    }
    return handle_.config->normalizeAxis(code, raw_value);
}

// XboxController implementation
XboxController::XboxController(ControllerHandle handle)
    : ControllerBase(std::move(handle)), current_effect_id_(-1) {
}

XboxController::~XboxController() {
    stopVibration();
}

bool XboxController::processEvent(const struct input_event& ev, xbox_udp::InputEventPacket& pkt) {
    pkt.magic = xbox_udp::PACKET_MAGIC;
    pkt.device_id = device_id_;
    pkt.type = ev.type;
    pkt.code = ev.code;
    pkt.value = ev.value;
    pkt.sec = ev.time.tv_sec;
    pkt.usec = ev.time.tv_usec;
    
    // Normalize axis values
    if (ev.type == EV_ABS) {
        pkt.normalized = normalizeAxisValue(ev.code, ev.value);
    } else {
        pkt.normalized = static_cast<double>(ev.value);
    }
    
    return true;
}

bool XboxController::sendVibration(uint16_t left_motor, uint16_t right_motor) {
    if (handle_.fd < 0) return false;
    
    // Check if device supports force feedback
    unsigned long features[4] = {0};
    if (ioctl(handle_.fd, EVIOCGBIT(EV_FF, sizeof(features)), features) < 0) {
        return false;
    }
    
    // Check if rumble is supported
    if (!test_bit(FF_RUMBLE, features)) {
        return false;
    }
    
    // Stop previous effect if any
    if (current_effect_id_ >= 0) {
        struct input_event stop;
        memset(&stop, 0, sizeof(stop));
        stop.type = EV_FF;
        stop.code = current_effect_id_;
        stop.value = 0;
        write(handle_.fd, &stop, sizeof(stop));
    }
    
    // Create rumble effect
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = -1;  // Let kernel assign ID
    effect.u.rumble.strong_magnitude = left_motor;
    effect.u.rumble.weak_magnitude = right_motor;
    effect.replay.length = 0;  // Infinite
    effect.replay.delay = 0;
    
    // Upload effect
    if (ioctl(handle_.fd, EVIOCSFF, &effect) < 0) {
        return false;
    }
    
    current_effect_id_ = effect.id;
    
    // Play effect
    struct input_event play;
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;  // Start
    
    if (write(handle_.fd, &play, sizeof(play)) < 0) {
        return false;
    }
    
    return true;
}

void XboxController::stopVibration() {
    if (handle_.fd < 0 || current_effect_id_ < 0) return;
    
    struct input_event stop;
    memset(&stop, 0, sizeof(stop));
    stop.type = EV_FF;
    stop.code = current_effect_id_;
    stop.value = 0;  // Stop
    write(handle_.fd, &stop, sizeof(stop));
    
    current_effect_id_ = -1;
}
