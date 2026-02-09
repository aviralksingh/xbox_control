/*
 * Xbox UDP protocol - binary packet format for controller input events.
 * Compatible with Linux input_event (evdev) types/codes.
 */
#ifndef XBOX_UDP_PROTOCOL_HPP
#define XBOX_UDP_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>

namespace xbox_udp {

// Magic bytes for packet validation
constexpr uint32_t PACKET_MAGIC = 0x31434258;  // "XBC1" in little-endian
constexpr uint32_t VIBRATION_MAGIC = 0x56425258;  // "XRBV" in little-endian (Xbox Rumble Vibration)

#pragma pack(push, 1)
struct InputEventPacket {
    uint32_t magic;      // PACKET_MAGIC
    uint8_t  device_id;  // Controller index (0, 1, ...)
    uint16_t type;       // EV_KEY, EV_ABS, EV_SYN, etc.
    uint16_t code;       // Button/axis code
    int32_t  value;      // Raw event value
    double   normalized; // Normalized value (-1.0 to 1.0, or 0.0 to 1.0 for triggers)
    uint32_t sec;        // Timestamp seconds
    uint32_t usec;       // Timestamp microseconds
};

struct VibrationPacket {
    uint32_t magic;      // VIBRATION_MAGIC
    uint8_t  device_id;  // Controller index (0, 1, ...)
    uint16_t left_motor; // Left motor intensity (0-65535)
    uint16_t right_motor; // Right motor intensity (0-65535)
    uint32_t duration_ms; // Duration in milliseconds (0 = infinite until stopped)
};
#pragma pack(pop)

constexpr size_t PACKET_SIZE = sizeof(InputEventPacket);
constexpr size_t VIBRATION_PACKET_SIZE = sizeof(VibrationPacket);

// Default UDP port for publisher (send) and receiver (bind)
constexpr unsigned short DEFAULT_PORT = 35555;

}  // namespace xbox_udp

#endif
