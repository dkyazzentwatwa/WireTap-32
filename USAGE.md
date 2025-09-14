# ESP32 Bus Pirate Usage Guide

This guide explains how to safely use the ESP32 Bus Pirate to analyze and test various bus protocols.

## ⚠️ SAFETY FIRST

**READ THIS BEFORE CONNECTING ANYTHING**

### Voltage Limits
- **ESP32 GPIO pins are 3.3V ONLY** - applying 5V will damage your ESP32
- **Maximum current per pin**: 12mA (40mA absolute maximum)
- **Maximum total current**: 200mA across all pins
- Always check target device voltage levels before connecting

### Protection Measures
- Use a multimeter to verify target device voltage before connecting
- Consider using level shifters for 5V devices
- Never connect power supplies directly to GPIO pins
- Always start in Hi-Z mode to avoid accidental shorts

## Getting Started

### 1. Connect ESP32 to Computer
1. Connect ESP32 to computer via USB cable
2. Open Arduino IDE Serial Monitor (or terminal program)
3. Set baud rate to **115200**
4. You should see: `=== ESP32 Bus Pirate v3.0 (Serial-Only) ===`

### 2. Basic Commands
```
help          - Show all commands
status        - Show system status
mode <proto>  - Set protocol mode (hiz, gpio, i2c, spi, uart)
pins          - Show current pin assignments
pins set <name> <pin> - Change pin assignments
```

## Protocol Usage

### Hi-Z Mode (Default/Safe)
All pins are set to high impedance (input) - safest mode for connections.

```
mode hiz      # Enter Hi-Z mode (safe default)
```

### I2C Protocol

#### Default Pins
- **SDA**: GPIO 21
- **SCL**: GPIO 22

#### Connection Safety
1. Verify target device is 3.3V compatible
2. Connect grounds first: ESP32 GND ↔ Target GND
3. Connect data lines: ESP32 SDA ↔ Target SDA, ESP32 SCL ↔ Target SCL
4. **Important**: Many I2C devices need pull-up resistors (typically 4.7kΩ to 3.3V)

#### Commands
```
mode i2c           # Enter I2C mode
pullups on         # Enable internal pull-ups (weak, may not work for all devices)
freq i2c 100000    # Set frequency (10000-400000 Hz)
i2c scan           # Scan for devices (addresses 0x01-0x7F)
i2c r 0x50 8       # Read 8 bytes from device at 0x50
i2c w 0x50 0x00 0xFF  # Write 0x00, 0xFF to device at 0x50
```

#### Example: Reading EEPROM
```
mode i2c
i2c scan           # Look for EEPROM (common addresses: 0x50-0x57)
i2c w 0x50 0x00    # Set address to 0x00
i2c r 0x50 16      # Read 16 bytes starting from 0x00
```

### SPI Protocol

#### Default Pins
- **MOSI** (Master Out, Slave In): GPIO 23
- **MISO** (Master In, Slave Out): GPIO 19
- **SCK** (Clock): GPIO 18
- **CS** (Chip Select): GPIO 5

#### Connection Safety
1. Verify target device voltage (3.3V)
2. Connect grounds first
3. Connect data lines - **be careful with pin directions**:
   - ESP32 MOSI → Target MOSI (or DI/SDI)
   - ESP32 MISO ← Target MISO (or DO/SDO)
   - ESP32 SCK → Target SCK (or CLK)
   - ESP32 CS → Target CS (or SS/NSS)

#### Commands
```
mode spi              # Enter SPI mode
freq spi 1000000      # Set frequency (1000-10000000 Hz)
spi x 0x90 0x00       # Send 0x90, 0x00 and show response
```

#### Example: Reading SPI Flash ID
```
mode spi
spi x 0x9F            # JEDEC ID command - shows manufacturer/device ID
```

### UART Protocol

#### Default Pins
- **TX** (Transmit): GPIO 17
- **RX** (Receive): GPIO 16

#### Connection Safety
1. **Cross connections**: ESP32 TX → Target RX, ESP32 RX → Target TX
2. Connect grounds: ESP32 GND ↔ Target GND
3. Verify voltage levels (3.3V)

#### Commands
```
mode uart                    # Enter UART mode
uart baud 115200            # Set baud rate (1200-2000000)
uart tx "Hello World"       # Send text
uart tx 0x41 0x42 0x43      # Send hex bytes (ABC)
uart rx 10                  # Read up to 10 bytes (500ms timeout)
```

#### Example: Talking to Another Arduino
```
mode uart
uart baud 9600             # Match target baud rate
uart tx "AT"               # Send AT command
uart rx 10                 # Read response
```

### GPIO Mode

#### Safety Notes
- **Never exceed 3.3V input**
- **Maximum 12mA output current per pin**
- Avoid pins used by internal ESP32 functions (6-11)

#### Commands
```
mode gpio                  # Enter GPIO mode
gpio set 2 1              # Set GPIO 2 to HIGH (3.3V)
gpio set 2 0              # Set GPIO 2 to LOW (0V)
gpio get 4                # Read GPIO 4 state
```

## Pin Configuration

### Changing Pin Assignments
You can change pin assignments for flexibility:

```
pins                      # Show current assignments
pins set sda 25          # Change I2C SDA to GPIO 25
pins set mosi 32         # Change SPI MOSI to GPIO 32
mode i2c                 # Re-enter mode to apply changes
```

### Recommended Pins
**Good pins for general use**: 2, 4, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33

**Avoid these pins**:
- 0: Boot button (has pull-up)
- 1, 3: UART0 (Serial Monitor)
- 2: Built-in LED (some boards)
- 6-11: Connected to SPI flash
- 34-39: Input only (no pull-up/down)

## Common Connection Examples

### I2C Temperature Sensor (DS1621)
```
Connections:
ESP32 3.3V → DS1621 VDD
ESP32 GND → DS1621 GND
ESP32 GPIO21 (SDA) → DS1621 SDA
ESP32 GPIO22 (SCL) → DS1621 SCL
Add 4.7kΩ pull-ups on SDA and SCL to 3.3V

Commands:
mode i2c
i2c scan                 # Should find device at 0x48
i2c w 0x48 0xEE         # Start temperature conversion
i2c w 0x48 0xAA         # Read temperature command
i2c r 0x48 2            # Read 2 bytes of temperature data
```

### SPI EEPROM (25LC256)
```
Connections:
ESP32 3.3V → 25LC256 VCC
ESP32 GND → 25LC256 GND
ESP32 GPIO23 (MOSI) → 25LC256 SI
ESP32 GPIO19 (MISO) → 25LC256 SO
ESP32 GPIO18 (SCK) → 25LC256 SCK
ESP32 GPIO5 (CS) → 25LC256 CS

Commands:
mode spi
spi x 0x03 0x00 0x00    # Read from address 0x0000
spi x 0x06              # Write enable
spi x 0x02 0x00 0x00 0xAA 0xBB  # Write 0xAA, 0xBB to 0x0000
```

## Troubleshooting

### No Response from Target Device
1. Check power connections and voltage levels
2. Verify ground connections
3. Check pin assignments match your connections
4. For I2C: Ensure pull-up resistors are present
5. Use multimeter to verify signals are present

### ESP32 Crashes/Resets
1. Check for short circuits
2. Verify you're not exceeding current limits
3. Try `mode hiz` first before connecting devices
4. Check that target device is 3.3V compatible

### Communication Errors
1. Verify correct protocol mode
2. Check baud rates (UART) or frequencies (I2C/SPI)
3. Ensure proper wiring (especially TX/RX crossover for UART)
4. Try slower speeds first

## Advanced Tips

### Using Logic Analyzer Mode
Set up multiple pins and use `gpio get` to sample signals:
```
mode gpio
gpio get 2    # Sample pin 2
gpio get 4    # Sample pin 4
gpio get 5    # Sample pin 5
```

### Bit-banging Custom Protocols
Use GPIO mode to manually control timing:
```
mode gpio
gpio set 2 1    # Clock high
gpio set 4 0    # Data low
gpio set 2 0    # Clock low
gpio get 5      # Read response
```

### Power Supply Testing
Use `status` command to monitor ESP32 health:
```
status          # Shows heap usage, mode, buffer status
```

Remember: **When in doubt, start with Hi-Z mode and verify all connections with a multimeter before proceeding!**