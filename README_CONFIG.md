# Controller Configuration System

This project now uses a YAML-based configuration system for controller definitions, making it extensible to any controller type.

## Configuration Files

Controller configurations are stored in the `config/` directory as YAML files. Each file defines:

- **Controller identification**: Vendor patterns to match device names
- **Button mappings**: evdev button codes → display names
- **Axis mappings**: evdev axis codes → display names, min/max bounds, deadzones
- **Normalization settings**: Output ranges and deadzone application

## Example: Xbox Controller Config

See `config/xbox_controller.yaml` for a complete example.

### Structure

```yaml
controller:
  name: "Xbox Controller"
  vendor_patterns:
    - "xbox"
    - "microsoft"
  exclude_patterns:
    - "consumer control"

buttons:
  - code: 304  # BTN_A
    name: "A"
  # ... more buttons

axes:
  - code: 0    # ABS_X
    name: "Left-X"
    min: -32768
    max: 32767
    deadzone: 7849
    normalize: true

normalization:
  output_min: -1.0
  output_max: 1.0
  apply_deadzone: true
```

## Adding a New Controller

1. Create a new YAML file in `config/` (e.g., `config/ps5_controller.yaml`)
2. Define vendor patterns to match your controller
3. Map all buttons and axes with their evdev codes
4. Set appropriate min/max bounds and deadzones
5. The system will auto-detect and load the config when a matching device is found

## Normalization

Axes can be normalized to a standard range (default: -1.0 to 1.0):

- **Sticks**: Normalized to -1.0 (full left/down) to 1.0 (full right/up)
- **Triggers**: Normalized to 0.0 (released) to 1.0 (fully pressed)
- **Deadzones**: Applied before normalization to eliminate drift

The receiver displays both raw and normalized values for normalized axes.

## Usage

The configuration system is automatically used by:

- **xbox_udp_publisher**: Auto-detects controller configs and uses them for device identification and event naming
- **udp_receiver_test**: Loads configs to display proper button/axis names and normalized values

Configs are loaded from:
1. `./config/` (current directory)
2. `/usr/share/xbox_control/config` (installed location)
