/*
 * UDP Publisher Implementation
 */

#include "udp_publisher.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>

UDPPublisher::UDPPublisher(const std::string& dest_addr, unsigned short port)
    : dest_addr_(dest_addr), port_(port), sock_(-1) {
    
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        std::cerr << "socket: " << std::strerror(errno) << std::endl;
        return;
    }
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, dest_addr.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "inet_pton " << dest_addr << ": invalid address" << std::endl;
        close(sock_);
        sock_ = -1;
        return;
    }
    
    if (connect(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect: " << std::strerror(errno) << std::endl;
        close(sock_);
        sock_ = -1;
        return;
    }
}

UDPPublisher::~UDPPublisher() {
    if (sock_ >= 0) {
        close(sock_);
    }
}

bool UDPPublisher::sendEvent(const xbox_udp::InputEventPacket& pkt) {
    if (sock_ < 0) return false;
    
    ssize_t sent = send(sock_, &pkt, sizeof(pkt), 0);
    if (sent != static_cast<ssize_t>(sizeof(pkt))) {
        std::cerr << "send: " << std::strerror(errno) << std::endl;
        return false;
    }
    return true;
}
