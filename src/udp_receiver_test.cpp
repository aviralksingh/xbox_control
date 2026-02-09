/*
 * UDP Receiver Test
 *
 * Binds to the Xbox UDP port, receives input event packets and prints
 * them in human-readable form (button names, axis names, values).
 */

#include "xbox_udp_protocol.hpp"

#include <linux/input-event-codes.h>
#include <linux/input.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>

namespace {

const char* ev_type_name(unsigned type) {
    switch (type) {
        case EV_SYN: return "SYN";
        case EV_KEY: return "KEY";
        case EV_ABS: return "ABS";
        case EV_MSC: return "MSC";
        case EV_REL: return "REL";
        default: return "?";
    }
}

const char* ev_key_name(unsigned code) {
    switch (code) {
        case BTN_A: return "A";
        case BTN_B: return "B";
        case BTN_X: return "X";
        case BTN_Y: return "Y";
        case BTN_TL: return "LB";
        case BTN_TR: return "RB";
        case BTN_SELECT: return "Back";
        case BTN_START: return "Start";
        case BTN_MODE: return "Guide";
        case BTN_THUMBL: return "L3";
        case BTN_THUMBR: return "R3";
        case BTN_DPAD_UP: return "Dpad-Up";
        case BTN_DPAD_DOWN: return "Dpad-Down";
        case BTN_DPAD_LEFT: return "Dpad-Left";
        case BTN_DPAD_RIGHT: return "Dpad-Right";
        default: return nullptr;
    }
}

const char* ev_abs_name(unsigned code) {
    switch (code) {
        case ABS_X: return "Left-X";
        case ABS_Y: return "Left-Y";
        case ABS_RX: return "Right-X";
        case ABS_RY: return "Right-Y";
        case ABS_Z: return "LT";
        case ABS_RZ: return "RT";
        case ABS_HAT0X: return "Dpad-X";
        case ABS_HAT0Y: return "Dpad-Y";
        default: return nullptr;
    }
}

void print_packet(const xbox_udp::InputEventPacket& pkt) {
    if (pkt.magic != xbox_udp::PACKET_MAGIC) return;
    if (pkt.type == EV_SYN) return;

    std::cout << "[" << (int)pkt.device_id << "] "
              << ev_type_name(pkt.type) << " ";

    if (pkt.type == EV_KEY) {
        const char* name = ev_key_name(pkt.code);
        if (name)
            std::cout << name << " " << (pkt.value ? "pressed" : "released");
        else
            std::cout << "Btn-" << pkt.code << " " << (pkt.value ? "pressed" : "released");
    } else if (pkt.type == EV_ABS) {
        const char* name = ev_abs_name(pkt.code);
        if (name)
            std::cout << name << " = " << pkt.value;
        else
            std::cout << "axis-" << pkt.code << " = " << pkt.value;
    } else {
        std::cout << "type=" << pkt.type << " code=" << pkt.code << " value=" << pkt.value;
    }

    std::cout << std::endl;
    std::cout.flush();
}

}  // namespace

int main(int argc, char* argv[]) {
    unsigned short port = xbox_udp::DEFAULT_PORT;
    if (argc >= 2) port = static_cast<unsigned short>(std::stoul(argv[1]));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "socket: " << std::strerror(errno) << std::endl;
        return 1;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt SO_REUSEADDR: " << std::strerror(errno) << std::endl;
        close(sock);
        return 1;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind :" << port << ": " << std::strerror(errno) << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "UDP Receiver Test: listening on 0.0.0.0:" << port << std::endl;
    std::cout << "In another terminal run: ./xbox_udp_publisher 127.0.0.1 " << port << std::endl;
    std::cout << "(Start the receiver first, then the publisher.)" << std::endl << std::endl;

    xbox_udp::InputEventPacket pkt;
    struct pollfd pfd = { sock, POLLIN, 0 };

    for (;;) {
        int r = poll(&pfd, 1, 5000);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll: " << std::strerror(errno) << std::endl;
            break;
        }
        if (r == 0) {
            std::cout << "(no packets in 5s - is publisher running? ./xbox_udp_publisher 127.0.0.1 " << port << ")" << std::endl;
            continue;
        }

        ssize_t n = recv(sock, &pkt, sizeof(pkt), 0);
        if (n == static_cast<ssize_t>(sizeof(pkt))) {
            if (pkt.magic != xbox_udp::PACKET_MAGIC) {
                std::cout << "[?] Bad magic 0x" << std::hex << pkt.magic << std::dec
                          << " type=" << pkt.type << " code=" << pkt.code << " value=" << pkt.value << std::endl;
            } else {
                print_packet(pkt);
            }
        } else if (n > 0) {
            std::cerr << "short read: " << n << " bytes (expected " << sizeof(pkt) << ")" << std::endl;
        } else if (errno != EAGAIN && errno != EINTR) {
            std::cerr << "recv: " << std::strerror(errno) << std::endl;
        }
    }

    close(sock);
    return 0;
}
