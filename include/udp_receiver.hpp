/*
 * UDP Receiver
 * 
 * Receives controller input events and vibration commands over UDP.
 */

#ifndef UDP_RECEIVER_HPP
#define UDP_RECEIVER_HPP

#include "xbox_udp_protocol.hpp"
#include <functional>
#include <memory>

class UDPReceiver {
public:
    using EventCallback = std::function<void(const xbox_udp::InputEventPacket&)>;
    using VibrationCallback = std::function<void(const xbox_udp::VibrationPacket&)>;
    
    UDPReceiver(unsigned short event_port, unsigned short vibration_port);
    ~UDPReceiver();
    
    bool bind();
    void setEventCallback(EventCallback callback) { event_callback_ = callback; }
    void setVibrationCallback(VibrationCallback callback) { vibration_callback_ = callback; }
    
    // Poll for incoming packets (non-blocking)
    void poll(int timeout_ms = 0);
    
    bool isBound() const { return event_sock_ >= 0 && vib_sock_ >= 0; }

private:
    int event_sock_;
    int vib_sock_;
    unsigned short event_port_;
    unsigned short vibration_port_;
    EventCallback event_callback_;
    VibrationCallback vibration_callback_;
};

#endif // UDP_RECEIVER_HPP
