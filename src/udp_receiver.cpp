/*
 * UDP Receiver Implementation
 */

#include "udp_receiver.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <iostream>

UDPReceiver::UDPReceiver(unsigned short event_port, unsigned short vibration_port)
    : event_port_(event_port), vibration_port_(vibration_port),
      event_sock_(-1), vib_sock_(-1) {
}

UDPReceiver::~UDPReceiver() {
    if (event_sock_ >= 0) close(event_sock_);
    if (vib_sock_ >= 0) close(vib_sock_);
}

bool UDPReceiver::bind() {
    // Create event socket
    event_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (event_sock_ < 0) {
        std::cerr << "socket (event): " << std::strerror(errno) << std::endl;
        return false;
    }
    
    int opt = 1;
    if (setsockopt(event_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt SO_REUSEADDR: " << std::strerror(errno) << std::endl;
        close(event_sock_);
        event_sock_ = -1;
        return false;
    }
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(event_port_);
    
    if (::bind(event_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind event socket :" << event_port_ << ": " << std::strerror(errno) << std::endl;
        close(event_sock_);
        event_sock_ = -1;
        return false;
    }
    
    // Create vibration socket
    vib_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (vib_sock_ < 0) {
        std::cerr << "socket (vibration): " << std::strerror(errno) << std::endl;
        close(event_sock_);
        event_sock_ = -1;
        return false;
    }
    
    if (setsockopt(vib_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt SO_REUSEADDR: " << std::strerror(errno) << std::endl;
        close(vib_sock_);
        close(event_sock_);
        vib_sock_ = -1;
        event_sock_ = -1;
        return false;
    }
    
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(vibration_port_);
    
    if (::bind(vib_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind vibration socket :" << vibration_port_ << ": " << std::strerror(errno) << std::endl;
        close(vib_sock_);
        close(event_sock_);
        vib_sock_ = -1;
        event_sock_ = -1;
        return false;
    }
    
    return true;
}

void UDPReceiver::poll(int timeout_ms) {
    struct pollfd pfds[2];
    pfds[0].fd = event_sock_;
    pfds[0].events = POLLIN;
    pfds[1].fd = vib_sock_;
    pfds[1].events = POLLIN;
    
    int r = ::poll(pfds, 2, timeout_ms);
    if (r < 0) {
        if (errno != EINTR) {
            std::cerr << "poll: " << std::strerror(errno) << std::endl;
        }
        return;
    }
    if (r == 0) return;
    
    // Check event socket
    if (pfds[0].revents & POLLIN) {
        xbox_udp::InputEventPacket pkt;
        ssize_t n = recv(event_sock_, &pkt, sizeof(pkt), MSG_DONTWAIT);
        if (n == static_cast<ssize_t>(sizeof(pkt)) && pkt.magic == xbox_udp::PACKET_MAGIC) {
            if (event_callback_) {
                event_callback_(pkt);
            }
        }
    }
    
    // Check vibration socket
    if (pfds[1].revents & POLLIN) {
        xbox_udp::VibrationPacket pkt;
        ssize_t n = recv(vib_sock_, &pkt, sizeof(pkt), MSG_DONTWAIT);
        if (n == static_cast<ssize_t>(sizeof(pkt)) && pkt.magic == xbox_udp::VIBRATION_MAGIC) {
            if (vibration_callback_) {
                vibration_callback_(pkt);
            }
        }
    }
}
