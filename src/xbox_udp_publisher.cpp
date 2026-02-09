/*
 * Xbox UDP Publisher
 *
 * Finds Xbox (and compatible) controllers via evdev (USB + Bluetooth,
 * kernel xpad driver), reads all input events and publishes them over UDP.
 * Auto-detects new controllers by rescanning periodically.
 */

#include "xbox_udp_protocol.hpp"

#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <unordered_set>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace {

const char* INPUT_DEV_DIR = "/dev/input";
const unsigned RESCAN_INTERVAL_SEC = 5;

// Heuristic: device is a gamepad if name suggests Xbox/Microsoft or has gamepad-like capabilities.
// Exclude sub-devices like "Consumer Control" or "Keyboard" that share the same product name.
bool is_gamepad_device(struct libevdev* dev) {
    const char* name = libevdev_get_name(dev);
    if (!name) return false;

    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n.find("consumer control") != std::string::npos || n.find("keyboard") != std::string::npos)
        return false;

    if (n.find("xbox") != std::string::npos ||
        n.find("microsoft") != std::string::npos ||
        n.find("xpad") != std::string::npos ||
        n.find("gamepad") != std::string::npos ||
        n.find("joystick") != std::string::npos) {
        return true;
    }
    // Generic: has both keys and absolute axes (sticks/triggers)
    return libevdev_has_event_type(dev, EV_KEY) && libevdev_has_event_type(dev, EV_ABS);
}

struct Controller {
    int fd = -1;
    std::string path;
    std::string name;
    uint8_t device_id = 0;
    libevdev* dev = nullptr;
};

static const char* key_name(unsigned code) {
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
static const char* abs_name(unsigned code) {
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
static void print_event(uint8_t device_id, unsigned type, unsigned code, int value) {
    if (type == EV_SYN) return;
    std::cout << "[" << (int)device_id << "] ";
    if (type == EV_KEY) {
        const char* n = key_name(code);
        if (n) std::cout << n; else std::cout << "Btn-" << code;
        std::cout << " " << (value ? "pressed" : "released");
    } else if (type == EV_ABS) {
        const char* n = abs_name(code);
        if (n) std::cout << n; else std::cout << "axis-" << code;
        std::cout << " = " << value;
    } else {
        std::cout << "type=" << type << " code=" << code << " value=" << value;
    }
    std::cout << std::endl;
}

int create_udp_socket(const char* dest_addr, unsigned short port) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        std::cerr << "socket: " << std::strerror(errno) << std::endl;
        return -1;
    }
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, dest_addr, &addr.sin_addr) <= 0) {
        std::cerr << "inet_pton " << dest_addr << ": invalid address" << std::endl;
        close(s);
        return -1;
    }
    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect: " << std::strerror(errno) << std::endl;
        close(s);
        return -1;
    }
    return s;
}

std::vector<Controller> scan_controllers(const std::unordered_set<std::string>& exclude_paths) {
    std::vector<Controller> out;
    DIR* dir = opendir(INPUT_DEV_DIR);
    if (!dir) {
        std::cerr << "opendir " << INPUT_DEV_DIR << ": " << std::strerror(errno) << std::endl;
        return out;
    }

    std::vector<std::string> event_paths;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::string(ent->d_name).compare(0, 5, "event") != 0) continue;
        std::string path = std::string(INPUT_DEV_DIR) + "/" + ent->d_name;
        event_paths.push_back(path);
    }
    closedir(dir);

    std::sort(event_paths.begin(), event_paths.end());

    for (const auto& path : event_paths) {
        if (exclude_paths.count(path)) continue;

        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct libevdev* dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            close(fd);
            continue;
        }

        if (!is_gamepad_device(dev)) {
            libevdev_free(dev);
            close(fd);
            continue;
        }

        if (libevdev_grab(dev, LIBEVDEV_GRAB) != 0) {
            std::cerr << "Warning: could not grab " << path << " (another process may have it). Events may not appear." << std::endl;
        }

        Controller c;
        c.fd = fd;
        c.path = path;
        c.name = libevdev_get_name(dev) ? libevdev_get_name(dev) : path;
        c.dev = dev;
        c.device_id = static_cast<uint8_t>(out.size());
        out.push_back(std::move(c));
    }
    return out;
}

void close_controller(Controller& c) {
    if (c.dev) {
        libevdev_free(c.dev);
        c.dev = nullptr;
    }
    if (c.fd >= 0) {
        close(c.fd);
        c.fd = -1;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* dest = "127.0.0.1";
    unsigned short port = xbox_udp::DEFAULT_PORT;
    if (argc >= 2) dest = argv[1];
    if (argc >= 3) port = static_cast<unsigned short>(std::stoul(argv[2]));

    int udp_sock = create_udp_socket(dest, port);
    if (udp_sock < 0) return 1;

    std::cout << "Xbox UDP Publisher: sending to " << dest << ":" << port << std::endl;

    std::vector<Controller> controllers;
    std::unordered_set<std::string> open_paths;
    time_t last_rescan = time(nullptr);

    for (;;) {
        time_t now = time(nullptr);
        if (now - last_rescan >= static_cast<time_t>(RESCAN_INTERVAL_SEC)) {
            last_rescan = now;
            auto found = scan_controllers(open_paths);
            for (auto& c : found) {
                if (open_paths.count(c.path)) continue;
                open_paths.insert(c.path);
                c.device_id = static_cast<uint8_t>(controllers.size());
                controllers.push_back(std::move(c));
                std::cout << "Controller " << (int)controllers.back().device_id
                          << ": " << controllers.back().name << " (" << controllers.back().path << ")"
                          << std::endl;
            }
        }

        std::vector<pollfd> pfds;
        for (const auto& c : controllers) {
            if (c.fd >= 0) {
                pollfd p{};
                p.fd = c.fd;
                p.events = POLLIN;
                pfds.push_back(p);
            }
        }

        if (pfds.empty()) {
            sleep(1);
            continue;
        }

        int r = poll(pfds.data(), pfds.size(), 2000);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll: " << std::strerror(errno) << std::endl;
            break;
        }
        if (r == 0) continue;

        for (size_t i = 0; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            Controller& c = controllers[i];
            if (!c.dev) continue;

            struct input_event ev;
            int n_ev = 0;
            while (libevdev_next_event(c.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev) == 0) {
                std::cerr << "raw type=" << ev.type << " code=" << ev.code << " value=" << ev.value << std::endl;
                print_event(c.device_id, ev.type, ev.code, ev.value);
                ++n_ev;
                xbox_udp::InputEventPacket pkt{};
                pkt.magic = xbox_udp::PACKET_MAGIC;
                pkt.device_id = c.device_id;
                pkt.type = ev.type;
                pkt.code = ev.code;
                pkt.value = ev.value;
                pkt.sec = ev.time.tv_sec;
                pkt.usec = ev.time.tv_usec;

                ssize_t sent = send(udp_sock, &pkt, xbox_udp::PACKET_SIZE, 0);
                if (sent != static_cast<ssize_t>(xbox_udp::PACKET_SIZE)) {
                    std::cerr << "send: " << std::strerror(errno) << std::endl;
                }
            }
            if (n_ev > 0) {
                std::cerr << "(" << n_ev << " events)" << std::endl;
            }
        }

        open_paths.clear();
        for (const auto& c : controllers) open_paths.insert(c.path);
    }

    for (auto& c : controllers) close_controller(c);
    if (udp_sock >= 0) close(udp_sock);
    return 0;
}
