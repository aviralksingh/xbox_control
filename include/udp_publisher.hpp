/*
 * UDP Publisher
 * 
 * Sends controller input events over UDP.
 */

#ifndef UDP_PUBLISHER_HPP
#define UDP_PUBLISHER_HPP

#include "xbox_udp_protocol.hpp"
#include <string>
#include <memory>

class UDPPublisher {
public:
    UDPPublisher(const std::string& dest_addr, unsigned short port);
    ~UDPPublisher();
    
    bool sendEvent(const xbox_udp::InputEventPacket& pkt);
    bool isConnected() const { return sock_ >= 0; }

private:
    int sock_;
    std::string dest_addr_;
    unsigned short port_;
};

#endif // UDP_PUBLISHER_HPP
