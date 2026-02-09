/*
 * UDP Receiver Test
 *
 * Binds to the Xbox UDP port, receives input event packets and displays
 * a constant status display that updates in place showing all button and axis states.
 * Uses controller configuration for button/axis names and normalization.
 */

#include "xbox_udp_protocol.hpp"
#include "controller_config.hpp"

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
#include <map>
#include <unordered_map>
#include <set>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <cmath>

namespace {

struct ControllerState {
    std::map<unsigned, bool> buttons;  // button code -> pressed state
    std::map<std::string, bool> dpad_buttons;  // dpad button name -> pressed state (e.g., "Dpad-Left")
    std::map<unsigned, int32_t> axes;   // axis code -> raw value
    std::map<unsigned, double> normalized_axes;  // axis code -> normalized value
    std::shared_ptr<ControllerConfig> config;
};

std::unordered_map<uint8_t, ControllerState> controller_states;

void update_state(const xbox_udp::InputEventPacket& pkt) {
    if (pkt.magic != xbox_udp::PACKET_MAGIC) return;
    if (pkt.type == EV_SYN) return;

    ControllerState& state = controller_states[pkt.device_id];
    
    // Auto-detect config on first event if not set
    if (!state.config) {
        std::string config_dir = "config";
        if (!std::filesystem::exists(config_dir)) {
            config_dir = "/usr/share/xbox_control/config";
        }
        // Try to load a default config (xbox_controller.yaml)
        std::string default_config = config_dir + "/xbox_controller.yaml";
        if (std::filesystem::exists(default_config)) {
            state.config = ConfigManager::getInstance().loadConfig(default_config);
        }
    }

    if (pkt.type == EV_KEY) {
        state.buttons[pkt.code] = (pkt.value != 0);
    } else if (pkt.type == EV_ABS) {
        // Check if this is a dpad axis that should be converted to buttons
        if (state.config && state.config->isDpadAxis(pkt.code)) {
            // Convert dpad axis to button states
            const std::string* button_name = state.config->getDpadButtonName(pkt.code, pkt.value);
            if (button_name) {
                // Set this dpad button to pressed
                state.dpad_buttons[*button_name] = true;
                // Set other dpad buttons on the same axis to released
                for (const auto& dpad : state.config->getDpadButtonMappings()) {
                    if (dpad.axis_code == pkt.code && dpad.value != pkt.value) {
                        state.dpad_buttons[dpad.name] = false;
                    }
                }
            } else if (pkt.value == 0) {
                // Center position - release all dpad buttons on this axis
                for (const auto& dpad : state.config->getDpadButtonMappings()) {
                    if (dpad.axis_code == pkt.code) {
                        state.dpad_buttons[dpad.name] = false;
                    }
                }
            }
        }
        state.axes[pkt.code] = pkt.value;
        state.normalized_axes[pkt.code] = pkt.normalized;
    }
}

void print_status() {
    // Clear screen and move cursor to top
    std::cout << "\033[2J\033[H";
    
    std::cout << "=== Xbox Controller Status ===" << std::endl << std::endl;

    for (const auto& [device_id, state] : controller_states) {
        std::cout << "Controller [" << (int)device_id << "]" << std::endl;
        std::cout << "----------------------------" << std::endl;
        
        // Print buttons using config
        std::cout << "Buttons:" << std::endl;
        if (state.config) {
            // Use config-defined button order
            for (const auto& btn : state.config->getButtonMappings()) {
                bool pressed = state.buttons.count(btn.code) ? state.buttons.at(btn.code) : false;
                std::cout << "  " << std::setw(10) << std::left << btn.name 
                          << ": " << (pressed ? "[PRESSED ]" : "[        ]") << std::endl;
            }
            
            // Print dpad buttons
            for (const auto& dpad : state.config->getDpadButtonMappings()) {
                bool pressed = state.dpad_buttons.count(dpad.name) ? state.dpad_buttons.at(dpad.name) : false;
                std::cout << "  " << std::setw(10) << std::left << dpad.name 
                          << ": " << (pressed ? "[PRESSED ]" : "[        ]") << std::endl;
            }
        }
        
        // Print any other buttons not in config
        for (const auto& [code, pressed] : state.buttons) {
            if (!state.config || !state.config->getButtonName(code)) {
                std::cout << "  " << std::setw(10) << std::left << ("Btn-" + std::to_string(code))
                          << ": " << (pressed ? "[PRESSED ]" : "[        ]") << std::endl;
            }
        }
        
        std::cout << std::endl;
        std::cout << "Axes:" << std::endl;
        
        // Print axes using config (including dpad axes for redundancy)
        if (state.config) {
            std::set<unsigned> processed_axes;  // Track which axes we've already displayed
            
            for (const auto& axis : state.config->getAxisMappings()) {
                if (processed_axes.count(axis.code)) continue;
                
                int32_t raw_value = state.axes.count(axis.code) ? state.axes.at(axis.code) : 0;
                double normalized_value = state.normalized_axes.count(axis.code) ? 
                                         state.normalized_axes.at(axis.code) : 0.0;
                
                // Check if this is part of a stick pair (Left-X/Y or Right-X/Y)
                bool is_paired = false;
                const AxisMapping* paired_axis = nullptr;
                if (axis.name.find("Left-X") != std::string::npos || 
                    axis.name.find("Right-X") != std::string::npos) {
                    // Find the corresponding Y axis
                    for (const auto& other : state.config->getAxisMappings()) {
                        if (other.code != axis.code && 
                            ((axis.name.find("Left") != std::string::npos && other.name.find("Left-Y") != std::string::npos) ||
                             (axis.name.find("Right") != std::string::npos && other.name.find("Right-Y") != std::string::npos))) {
                            paired_axis = &other;
                            is_paired = true;
                            break;
                        }
                    }
                }
                
                if (is_paired && paired_axis) {
                    // Display as combined stick (X,Y)
                    int32_t raw_y = state.axes.count(paired_axis->code) ? state.axes.at(paired_axis->code) : 0;
                    double norm_y = state.normalized_axes.count(paired_axis->code) ? 
                                   state.normalized_axes.at(paired_axis->code) : 0.0;
                    
                    // Extract stick name (Left or Right)
                    std::string stick_name = axis.name.substr(0, axis.name.find("-X"));
                    
                    std::cout << "  " << std::setw(10) << std::left << stick_name;
                    std::cout << ": (X: " << std::setw(8) << std::right << raw_value 
                              << ", Y: " << std::setw(8) << std::right << raw_y << ")";
                    
                    if (axis.normalize && paired_axis->normalize) {
                        // Show normalized as direction components
                        std::string x_dir = normalized_value > 0 ? "Right" : (normalized_value < 0 ? "Left" : "Center");
                        std::string y_dir = norm_y > 0 ? "Up" : (norm_y < 0 ? "Down" : "Center");
                        std::cout << " (norm: " << x_dir << " " << std::fixed << std::setprecision(3) 
                                  << std::abs(normalized_value) << ", " << y_dir << " " 
                                  << std::abs(norm_y) << ")";
                    }
                    std::cout << std::endl;
                    
                    processed_axes.insert(axis.code);
                    processed_axes.insert(paired_axis->code);
                } else {
                    // Display as individual axis
                    std::cout << "  " << std::setw(10) << std::left << axis.name;
                    
                    // Special handling for dpad axes to show directional text
                    if (state.config->isDpadAxis(axis.code)) {
                        std::string direction;
                        if (axis.name.find("Dpad-X") != std::string::npos) {
                            if (raw_value == -1) direction = "Left";
                            else if (raw_value == 1) direction = "Right";
                            else direction = "Center";
                        } else if (axis.name.find("Dpad-Y") != std::string::npos) {
                            if (raw_value == -1) direction = "Up";
                            else if (raw_value == 1) direction = "Down";
                            else direction = "Center";
                        }
                        std::cout << ": " << std::setw(8) << std::right << raw_value 
                                  << " (" << direction << ")";
                    } else if (axis.normalize) {
                        std::cout << ": " << std::setw(8) << std::right << raw_value 
                                  << " (norm: " << std::fixed << std::setprecision(3) << normalized_value << ")";
                    } else {
                        std::cout << ": " << std::setw(8) << std::right << raw_value;
                    }
                    std::cout << std::endl;
                    
                    processed_axes.insert(axis.code);
                }
            }
        }
        
        // Print any other axes not in config
        for (const auto& [code, value] : state.axes) {
            if (!state.config || !state.config->getAxisMapping(code)) {
                std::cout << "  " << std::setw(10) << std::left << ("Axis-" + std::to_string(code))
                          << ": " << std::setw(8) << std::right << value << std::endl;
            }
        }
        
        std::cout << std::endl;
    }
    
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
    std::cout << "(Start the receiver first, then the publisher.)" << std::endl;
    std::cout << "Press Ctrl+C to exit." << std::endl << std::endl;
    
    // Initial display
    print_status();

    xbox_udp::InputEventPacket pkt;
    struct pollfd pfd = { sock, POLLIN, 0 };

    for (;;) {
        int r = poll(&pfd, 1, 100);  // Shorter timeout for more responsive updates
        if (r < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll: " << std::strerror(errno) << std::endl;
            break;
        }
        if (r == 0) {
            // No data, but continue to allow periodic updates
            continue;
        }

        ssize_t n = recv(sock, &pkt, sizeof(pkt), MSG_DONTWAIT);
        if (n == static_cast<ssize_t>(sizeof(pkt))) {
            if (pkt.magic != xbox_udp::PACKET_MAGIC) {
                // Bad packet, ignore
                continue;
            }
            update_state(pkt);
            print_status();
        } else if (n > 0) {
            // Short read, ignore
            continue;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            std::cerr << "recv: " << std::strerror(errno) << std::endl;
        }
    }

    close(sock);
    return 0;
}
