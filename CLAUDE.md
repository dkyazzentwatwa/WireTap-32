# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based Bus Pirate implementation - a single-file Arduino sketch that creates a "mini Bus Pirate" with a serial terminal interface and optional OLED display. It provides protocol analysis capabilities for I2C, SPI, UART, GPIO, and **Bluetooth** operations.

**Current Version**: Bluetooth Experimental - includes full Bluetooth functionality with device scanning, pairing, HID operations, and traffic sniffing

## Build and Development

### Arduino IDE Setup
- **Board**: ESP32 Dev Module (use Arduino IDE board manager)
- **Compilation**: Open `ESP32_BusPirate_Stable.ino` in Arduino IDE and compile/upload directly
- **Library Dependencies**:
  - Arduino ESP32 core libraries
  - Adafruit GFX Library
  - Adafruit SSD1306 Library
  - **Bluetooth Libraries** (automatically included with ESP32 core):
    - NimBLE-Arduino (memory-efficient BLE stack)
    - BluetoothSerial (for Classic Bluetooth)

### No Traditional Build System
This project uses the Arduino IDE's build system. There are no separate build scripts, package managers, or configuration files.

## Architecture

### Core Components
- **Protocol Handlers**: Separate functions for I2C (`i2cBegin()`, `i2cScan()`, etc.), SPI (`spiBegin()`, `spiXfer()`), UART (`uartBegin()`, `uartTx()`), and GPIO operations
- **Advanced Protocol Features**:
  - **I2C Monitoring**: Real-time monitoring of I2C register values with change detection
  - **I2C Slave Mode**: ESP32 can act as I2C slave device with configurable address and responses
  - **UART Bridge Mode**: Transparent serial bridge between USB and target UART
  - **UART Spam Mode**: Automated periodic transmission for testing
  - **Macro System**: Advanced command sequences using bracket notation `[commands]`
- **Command Parser**: `handleCmd()` processes text commands from the serial interface with ANSI color support
- **Serial Interface**: Interactive terminal at 115200 baud via USB/Serial with colored output
- **Display System**: Optional SSD1306 OLED display with real-time status updates and protocol-specific info
- **Stability System**: `safeYield()` and `checkHeap()` prevent watchdog resets during long operations

### State Management
- **Mode System**: Global `mode` variable tracks current protocol (HIZ, GPIO_MODE, I2C_MODE, SPI_MODE, UART_MODE, **BLUETOOTH_MODE**)
- **Pin Configuration**: Global variables for pin assignments (PIN_I2C_SDA, PIN_SPI_MOSI, etc.) - configurable at runtime
- **UART Buffering**: Circular buffer system for UART RX data with overflow protection
- **Advanced State Tracking**:
  - I2C monitoring state with address, register, and interval settings
  - I2C slave mode with configurable address and TX values
  - UART bridge and spam mode active states
  - **Bluetooth state management**: scan results, sniff logs, HID server status, device connections
  - Smart defaults and command history (last I2C address, read length, UART baud)

### Key Design Patterns
- **Safe Operations**: All protocol operations include timeout handling and resource cleanup
- **Memory Management**: Fixed buffer sizes and yield calls prevent heap fragmentation and watchdog resets
- **Serial-Only Interface**: Single command system via USB/Serial terminal for maximum stability
- **Color-Coded Output**: ANSI color support for enhanced terminal experience (toggleable)
- **Macro Processing**: Advanced sequence parsing with support for hex data, read operations, and string literals

## Pin Assignments (Default)
- I2C: SDA=21, SCL=22
- SPI: MOSI=23, MISO=19, SCK=18, CS=5
- UART: RX=16, TX=17
- Display: Uses I2C (SDA=21, SCL=22) at address 0x3C

## Common Operations
- Set mode: `mode i2c|spi|uart|gpio|hiz|bluetooth`
- Configure pins: `pins set <name> <pin>`
- Protocol operations: `i2c scan`, `spi x <hex>`, `uart tx <data>`, `bt scan`
- System info: `status`, `help`
- Display control: `display on/off`
- Advanced features:
  - I2C monitoring: `i2c monitor <addr> <reg> [length] [interval]`
  - I2C slave mode: `i2c slave <addr> [tx_value]`
  - UART bridge: `uart bridge` (CTRL+] to exit)
  - UART spam: `uart spam <text> <period_ms>`
  - **Bluetooth operations**:
    - Device scanning: `bt scan [seconds]`
    - Device pairing: `bt pair <mac>`
    - MAC spoofing: `bt spoof <mac>`
    - Traffic sniffing: `bt sniff`
    - HID server: `bt server [name]`
    - Keyboard emulation: `bt keyboard [text]`
    - Mouse control: `bt mouse <x> <y>`, `bt mouse click`, `bt mouse jiggle [ms]`
  - Macro commands: `[0x50 0x00 r:8]` (I2C), `[0x9F r:3]` (SPI), `['Hello' r:10]` (UART)
  - Color toggle: `color on/off`
  - Status bar: `status bar on/off`

## Usage
- **Serial Monitor**: Use Arduino IDE Serial Monitor at 115200 baud
- **Terminal Programs**: PuTTY, screen, minicom, etc. at 115200 baud with ANSI color support
- **Interactive Prompts**: Shows current mode (HiZ>, I2C>, SPI>, UART>, GPIO>) with optional color coding
- **Command Help**: Type `help` for complete command reference
- **OLED Display**: Shows real-time system status, mode, heap, uptime, and protocol-specific information
- **Advanced Usage**:
  - Use macros for complex protocol sequences
  - Enable I2C monitoring for real-time register watching
  - Use UART bridge mode for transparent serial communication
  - Configure I2C slave mode for device emulation
  - Toggle features like colors and status bar for preferred experience

## Stability Considerations
- Watchdog management functions are commented out to prevent boot loops
- Includes `safeYield()` calls in loops to prevent blocking
- Limited buffer sizes and operation timeouts prevent resource exhaustion
- **Bluetooth Memory Management**:
  - Uses NimBLE library for reduced memory consumption (~100KB less than standard BLE)
  - Automatic heap monitoring with warnings when memory < 80KB
  - Clean shutdown and resource cleanup for all Bluetooth operations
  - Conditional compilation allows disabling Bluetooth to save memory (`#define ENABLE_BLUETOOTH 0`)
- **Memory Safety**: Bluetooth stack requires ~170KB RAM; remaining protocols may need to be disabled when BT is active
- Enhanced stability through proper resource management and error handling