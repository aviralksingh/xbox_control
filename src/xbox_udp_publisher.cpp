/*
 * Xbox UDP Publisher
 *
 * Finds Xbox (and compatible) controllers via evdev (USB + Bluetooth,
 * kernel xpad driver), reads all input events and publishes them over UDP.
 * Auto-detects new controllers by rescanning periodically.
 */

#include "xbox_udp_protocol.hpp"
#include "controller_config.hpp"

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
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <filesystem>

namespace {

const char* INPUT_DEV_DIR = "/dev/input";
const unsigned RESCAN_INTERVAL_SEC = 5;

// Check if device matches a known controller config
std::shared_ptr<ControllerConfig> detect_controller_config(struct libevdev* dev) {
    const char* name = libevdev_get_name(dev);
    if (!name) return nullptr;
    
    // Try to auto-detect using config manager
    std::string config_dir = "config";
    // Try current directory first, then installed location
    if (!std::filesystem::exists(config_dir)) {
        config_dir = "/usr/share/xbox_control/config";
    }
    
    return ConfigManager::getInstance().detectConfig(name, config_dir);
}

// Fallback: check if device is a gamepad (has keys and axes)
bool is_generic_gamepad(struct libevdev* dev) {
    return libevdev_has_event_type(dev, EV_KEY) && libevdev_has_event_type(dev, EV_ABS);
}

struct Controller {
    int fd = -1;
    std::string path;
    std::string name;
    uint8_t device_id = 0;
    libevdev* dev = nullptr;
    std::shared_ptr<ControllerConfig> config;
};

static void print_event(uint8_t device_id, unsigned type, unsigned code, int value, 
                        const Controller& ctrl) {
    if (type == EV_SYN) return;
    std::cout << "[" << (int)device_id << "] ";
    if (type == EV_KEY) {
        const std::string* name = nullptr;
        if (ctrl.config) {
            name = ctrl.config->getButtonName(code);
        }
        if (name) {
            std::cout << *name;
        } else {
            std::cout << "Btn-" << code;
        }
        std::cout << " " << (value ? "pressed" : "released");
    } else if (type == EV_ABS) {
        const AxisMapping* mapping = nullptr;
        if (ctrl.config) {
            mapping = ctrl.config->getAxisMapping(code);
        }
        if (mapping) {
            std::cout << mapping->name;
            if (mapping->normalize) {
                double normalized = ctrl.config->normalizeAxis(code, value);
                std::cout << " = " << value << " (norm: " << normalized << ")";
            } else {
                std::cout << " = " << value;
            }
        } else {
            std::cout << "axis-" << code << " = " << value;
        }
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

        // Open with read-write access to support force feedback (vibration)
        int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        struct libevdev* dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            close(fd);
            continue;
        }

        // Try to detect controller config
        auto config = detect_controller_config(dev);
        
        // If no config found, check if it's a generic gamepad
        if (!config && !is_generic_gamepad(dev)) {
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
        c.config = config;
        c.device_id = static_cast<uint8_t>(out.size());
        out.push_back(std::move(c));
        
        if (config) {
            std::cout << "  Using config: " << config->getName() << std::endl;
        }
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

// Helper macro for testing bits
#define BITS_PER_LONG (sizeof(long) * 8)
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define test_bit(nr, addr) (((1UL << ((nr) % BITS_PER_LONG)) & ((addr)[BIT_WORD(nr)])) != 0)

bool send_vibration(Controller& c, uint16_t left_motor, uint16_t right_motor) {
    if (c.fd < 0) return false;
    
    // Check if device supports force feedback
    unsigned long features[4] = {0};
    if (ioctl(c.fd, EVIOCGBIT(EV_FF, sizeof(features)), features) < 0) {
        return false;
    }
    
    // Check if rumble is supported
    if (!test_bit(FF_RUMBLE, features)) {
        return false;
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
    if (ioctl(c.fd, EVIOCSFF, &effect) < 0) {
        return false;
    }
    
    // Play effect
    struct input_event play;
    memset(&play, 0, sizeof(play));
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;  // Start
    
    if (write(c.fd, &play, sizeof(play)) < 0) {
        return false;
    }
    
    return true;
}

void stop_vibration(Controller& c) {
    if (c.fd < 0) return;
    
    // Stop all effects by uploading an empty effect
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.u.rumble.strong_magnitude = 0;
    effect.u.rumble.weak_magnitude = 0;
    
    if (ioctl(c.fd, EVIOCSFF, &effect) >= 0) {
        struct input_event stop;
        memset(&stop, 0, sizeof(stop));
        stop.type = EV_FF;
        stop.code = effect.id;
        stop.value = 0;  // Stop
        write(c.fd, &stop, sizeof(stop));
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

    // Create a UDP socket to receive vibration commands
    int vib_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (vib_sock < 0) {
        std::cerr << "socket (vibration): " << std::strerror(errno) << std::endl;
        close(udp_sock);
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(vib_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt SO_REUSEADDR: " << std::strerror(errno) << std::endl;
        close(vib_sock);
        close(udp_sock);
        return 1;
    }
    
    struct sockaddr_in vib_addr;
    std::memset(&vib_addr, 0, sizeof(vib_addr));
    vib_addr.sin_family = AF_INET;
    vib_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    vib_addr.sin_port = htons(port + 1);  // Use port+1 for vibration commands
    
    if (bind(vib_sock, reinterpret_cast<struct sockaddr*>(&vib_addr), sizeof(vib_addr)) < 0) {
        std::cerr << "bind vibration socket :" << (port + 1) << ": " << std::strerror(errno) << std::endl;
        close(vib_sock);
        close(udp_sock);
        return 1;
    }

    std::cout << "Xbox UDP Publisher: sending to " << dest << ":" << port << std::endl;
    std::cout << "Listening for vibration commands on port " << (port + 1) << std::endl;

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
        // Add vibration socket to poll
        pollfd vib_pfd{};
        vib_pfd.fd = vib_sock;
        vib_pfd.events = POLLIN;
        pfds.push_back(vib_pfd);
        
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

        // Check vibration socket first (index 0)
        if (pfds[0].revents & POLLIN) {
            xbox_udp::VibrationPacket vib_pkt;
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            ssize_t vib_n = recvfrom(vib_sock, &vib_pkt, sizeof(vib_pkt), 0,
                                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
            if (vib_n == static_cast<ssize_t>(sizeof(vib_pkt)) && 
                vib_pkt.magic == xbox_udp::VIBRATION_MAGIC) {
                if (vib_pkt.device_id < controllers.size()) {
                    Controller& c = controllers[vib_pkt.device_id];
                    if (vib_pkt.left_motor == 0 && vib_pkt.right_motor == 0) {
                        stop_vibration(c);
                        std::cout << "Stopped vibration on controller " << (int)vib_pkt.device_id << std::endl;
                    } else {
                        if (send_vibration(c, vib_pkt.left_motor, vib_pkt.right_motor)) {
                            std::cout << "Vibration on controller " << (int)vib_pkt.device_id 
                                      << ": L=" << vib_pkt.left_motor << " R=" << vib_pkt.right_motor << std::endl;
                        } else {
                            std::cerr << "Failed to send vibration to controller " << (int)vib_pkt.device_id << std::endl;
                        }
                    }
                }
            }
        }

        // Process controller events (skip index 0 which is vibration socket)
        for (size_t i = 1; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            size_t ctrl_idx = i - 1;  // Adjust for vibration socket
            if (ctrl_idx >= controllers.size()) continue;
            Controller& c = controllers[ctrl_idx];
            if (!c.dev) continue;

            struct input_event ev;
            int n_ev = 0;
            while (libevdev_next_event(c.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev) == 0) {
                std::cerr << "raw type=" << ev.type << " code=" << ev.code << " value=" << ev.value << std::endl;
                print_event(c.device_id, ev.type, ev.code, ev.value, c);
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

    for (auto& c : controllers) {
        stop_vibration(c);
        close_controller(c);
    }
    if (vib_sock >= 0) close(vib_sock);
    if (udp_sock >= 0) close(udp_sock);
    return 0;
}
