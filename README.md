# xbox_control

Publishes Xbox controller input (USB and Bluetooth) over UDP. Uses the Linux evdev interface and the kernel **xpad** driver; no extra userspace driver needed.

## Requirements

- Linux with kernel xpad support (Xbox/Xbox 360/Xbox One controllers)
- **libevdev** (and headers)
- CMake 3.14+
- C++17

Install dependencies (Debian/Ubuntu):

```bash
sudo apt install build-essential cmake pkg-config libevdev-dev
```

Run the publisher with **read access** to `/dev/input/event*` (e.g. add your user to the `input` group, or run as root for testing):

```bash
sudo usermod -aG input $USER
# then log out and back in
```

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

### 1. Receiver (test)

Start the UDP receiver to print all incoming controller events:

```bash
./udp_receiver_test [port]
```

Default port: **35555**. It binds to all interfaces.

### 2. Publisher

Start the publisher so it sends controller input to the receiver:

```bash
./xbox_udp_publisher [destination] [port]
```

Examples:

```bash
# Send to localhost (receiver on same machine)
./xbox_udp_publisher 127.0.0.1

# Send to another host
./xbox_udp_publisher 192.168.1.10 35555
```

- Connects to the first Xbox (or compatible) controller found in `/dev/input/event*`.
- Auto-detects **USB** and **Bluetooth** controllers (rescan every 5 seconds).
- Sends one UDP packet per input event (buttons, sticks, triggers, d-pad).

### Test flow

Terminal 1:

```bash
./udp_receiver_test
```

Terminal 2:

```bash
./xbox_udp_publisher 127.0.0.1
```

Move sticks and press buttons; the receiver prints events like:

```
[0] ABS Left-X = 0
[0] ABS Left-Y = 0
[0] KEY A pressed
[0] KEY A released
```

## Protocol

See `include/xbox_udp_protocol.hpp`. Each packet is a fixed-size struct: magic, device id, evdev type/code/value, and timestamp. Same semantics as Linux `input_event` (evdev).

## License

Apache-2.0 (see LICENSE).
