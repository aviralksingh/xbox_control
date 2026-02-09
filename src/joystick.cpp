/*
 * Joystick Controller Manager
 *
 * Main application that:
 * - Scans for controllers and loads appropriate configs
 * - Creates controller instances using factory pattern
 * - Publishes input events over UDP
 * - Receives vibration commands over UDP
 */

#include "controller_base.hpp"
#include "controller_config.hpp"
#include "udp_publisher.hpp"
#include "udp_receiver.hpp"
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
#include <unistd.h>
#include <filesystem>
#include <memory>

namespace {

const char* INPUT_DEV_DIR = "/dev/input";
const unsigned RESCAN_INTERVAL_SEC = 5;

// Fallback: check if device is a gamepad (has keys and axes)
bool is_generic_gamepad(struct libevdev* dev) {
    return libevdev_has_event_type(dev, EV_KEY) && libevdev_has_event_type(dev, EV_ABS);
}

std::shared_ptr<ControllerConfig> detect_controller_config(struct libevdev* dev) {
    const char* name = libevdev_get_name(dev);
    if (!name) return nullptr;
    
    std::string config_dir = "config";
    if (!std::filesystem::exists(config_dir)) {
        config_dir = "/usr/share/xbox_control/config";
    }
    
    return ConfigManager::getInstance().detectConfig(name, config_dir);
}

struct ControllerInfo {
    ControllerHandle handle;
    std::unique_ptr<ControllerBase> controller;
    uint8_t device_id;
};

std::vector<ControllerInfo> scan_controllers(const std::unordered_set<std::string>& exclude_paths,
                                             uint8_t& next_device_id) {
    std::vector<ControllerInfo> out;
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

        // Open with read-write access to support force feedback
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

        ControllerHandle handle;
        handle.fd = fd;
        handle.path = path;
        handle.name = libevdev_get_name(dev) ? libevdev_get_name(dev) : path;
        handle.dev = dev;
        handle.config = config;
        
        // Create controller using factory
        auto controller = ControllerBase::create(handle);
        if (!controller) {
            libevdev_free(dev);
            close(fd);
            continue;
        }
        
        ControllerInfo info;
        info.handle = std::move(handle);
        info.controller = std::move(controller);
        info.device_id = next_device_id++;
        info.controller->setDeviceId(info.device_id);
        
        out.push_back(std::move(info));
        
        std::cout << "Controller " << (int)info.device_id
                  << ": " << info.handle.name << " (" << info.handle.path << ")";
        if (info.handle.config) {
            std::cout << " [Config: " << info.handle.config->getName() << "]";
        }
        std::cout << std::endl;
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* dest = "127.0.0.1";
    unsigned short port = xbox_udp::DEFAULT_PORT;
    if (argc >= 2) dest = argv[1];
    if (argc >= 3) port = static_cast<unsigned short>(std::stoul(argv[2]));

    // Create UDP publisher
    UDPPublisher publisher(dest, port);
    if (!publisher.isConnected()) {
        std::cerr << "Failed to create UDP publisher" << std::endl;
        return 1;
    }

    // Create UDP receiver for vibration commands
    UDPReceiver receiver(port, port + 1);
    if (!receiver.bind()) {
        std::cerr << "Failed to bind UDP receiver" << std::endl;
        return 1;
    }

    std::cout << "Joystick Controller Manager" << std::endl;
    std::cout << "  Publishing events to: " << dest << ":" << port << std::endl;
    std::cout << "  Listening for vibration on: 0.0.0.0:" << (port + 1) << std::endl;

    std::vector<ControllerInfo> controllers;
    std::unordered_set<std::string> open_paths;
    time_t last_rescan = time(nullptr);
    uint8_t next_device_id = 0;

    // Set up vibration callback
    receiver.setVibrationCallback([&controllers](const xbox_udp::VibrationPacket& pkt) {
        if (pkt.device_id < controllers.size()) {
            ControllerInfo& info = controllers[pkt.device_id];
            if (pkt.left_motor == 0 && pkt.right_motor == 0) {
                info.controller->stopVibration();
                std::cout << "Stopped vibration on controller " << (int)pkt.device_id << std::endl;
            } else {
                if (info.controller->sendVibration(pkt.left_motor, pkt.right_motor)) {
                    std::cout << "Vibration on controller " << (int)pkt.device_id 
                              << ": L=" << pkt.left_motor << " R=" << pkt.right_motor << std::endl;
                } else {
                    std::cerr << "Failed to send vibration to controller " << (int)pkt.device_id << std::endl;
                }
            }
        }
    });

    for (;;) {
        time_t now = time(nullptr);
        if (now - last_rescan >= static_cast<time_t>(RESCAN_INTERVAL_SEC)) {
            last_rescan = now;
            auto found = scan_controllers(open_paths, next_device_id);
            for (auto& info : found) {
                if (open_paths.count(info.handle.path)) continue;
                open_paths.insert(info.handle.path);
                controllers.push_back(std::move(info));
            }
        }

        // Poll for vibration commands
        receiver.poll(0);

        std::vector<pollfd> pfds;
        for (const auto& info : controllers) {
            if (info.handle.fd >= 0) {
                pollfd p{};
                p.fd = info.handle.fd;
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
            if (i >= controllers.size()) continue;
            
            ControllerInfo& info = controllers[i];
            if (!info.handle.dev) continue;

            struct input_event ev;
            int n_ev = 0;
            while (libevdev_next_event(info.handle.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev) == 0) {
                if (ev.type == EV_SYN) continue;
                
                xbox_udp::InputEventPacket pkt;
                if (info.controller->processEvent(ev, pkt)) {
                    publisher.sendEvent(pkt);
                    ++n_ev;
                }
            }
        }

        open_paths.clear();
        for (const auto& info : controllers) {
            open_paths.insert(info.handle.path);
        }
    }

    // Cleanup
    for (auto& info : controllers) {
        info.controller->stopVibration();
        if (info.handle.dev) {
            libevdev_free(info.handle.dev);
        }
        if (info.handle.fd >= 0) {
            close(info.handle.fd);
        }
    }

    return 0;
}
