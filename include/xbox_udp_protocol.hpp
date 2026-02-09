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

#pragma pack(push, 1)
struct InputEventPacket {
    uint32_t magic;      // PACKET_MAGIC
    uint8_t  device_id;  // Controller index (0, 1, ...)
    uint16_t type;       // EV_KEY, EV_ABS, EV_SYN, etc.
    uint16_t code;       // Button/axis code
    int32_t  value;      // Event value
    uint32_t sec;        // Timestamp seconds
    uint32_t usec;       // Timestamp microseconds
};
#pragma pack(pop)

constexpr size_t PACKET_SIZE = sizeof(InputEventPacket);

// Default UDP port for publisher (send) and receiver (bind)
constexpr unsigned short DEFAULT_PORT = 35555;

}  // namespace xbox_udp

#endif
