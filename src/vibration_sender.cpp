/*
 * Vibration Sender
 *
 * Sends vibration/rumble commands to Xbox controllers via UDP.
 * Usage: ./vibration_sender [device_id] [left_motor] [right_motor] [duration_ms] [host] [port]
 */

#include "xbox_udp_protocol.hpp"

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] 
                  << " <device_id> <left_motor> <right_motor> [duration_ms] [host] [port]" << std::endl;
        std::cerr << "  device_id: Controller index (usually 0)" << std::endl;
        std::cerr << "  left_motor: Left motor intensity (0-65535)" << std::endl;
        std::cerr << "  right_motor: Right motor intensity (0-65535)" << std::endl;
        std::cerr << "  duration_ms: Duration in milliseconds (optional, 0 = infinite)" << std::endl;
        std::cerr << "  host: Destination host (default: 127.0.0.1)" << std::endl;
        std::cerr << "  port: Destination port (default: " << (xbox_udp::DEFAULT_PORT + 1) << ")" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " 0 32767 32767 500    # Medium rumble for 500ms" << std::endl;
        std::cerr << "  " << argv[0] << " 0 65535 0            # Strong left motor only" << std::endl;
        std::cerr << "  " << argv[0] << " 0 0 0               # Stop vibration" << std::endl;
        return 1;
    }

    uint8_t device_id = static_cast<uint8_t>(std::stoul(argv[1]));
    uint16_t left_motor = static_cast<uint16_t>(std::stoul(argv[2]));
    uint16_t right_motor = static_cast<uint16_t>(std::stoul(argv[3]));
    uint32_t duration_ms = (argc >= 5) ? static_cast<uint32_t>(std::stoul(argv[4])) : 0;
    const char* host = (argc >= 6) ? argv[5] : "127.0.0.1";
    unsigned short port = (argc >= 7) ? static_cast<unsigned short>(std::stoul(argv[6])) 
                                     : (xbox_udp::DEFAULT_PORT + 1);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "socket: " << std::strerror(errno) << std::endl;
        return 1;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << host << std::endl;
        close(sock);
        return 1;
    }

    xbox_udp::VibrationPacket pkt;
    pkt.magic = xbox_udp::VIBRATION_MAGIC;
    pkt.device_id = device_id;
    pkt.left_motor = left_motor;
    pkt.right_motor = right_motor;
    pkt.duration_ms = duration_ms;

    ssize_t sent = sendto(sock, &pkt, sizeof(pkt), 0,
                          reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (sent != static_cast<ssize_t>(sizeof(pkt))) {
        std::cerr << "sendto: " << std::strerror(errno) << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "Sent vibration command to " << host << ":" << port << std::endl;
    std::cout << "  Controller: " << (int)device_id << std::endl;
    std::cout << "  Left motor: " << left_motor << std::endl;
    std::cout << "  Right motor: " << right_motor << std::endl;
    if (duration_ms > 0) {
        std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
    } else {
        std::cout << "  Duration: infinite (send 0 0 0 to stop)" << std::endl;
    }

    close(sock);
    return 0;
}
