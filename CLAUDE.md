# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based Bus Pirate implementation - a single-file Arduino sketch that creates a "mini Bus Pirate" with a serial terminal interface and optional OLED display. It provides protocol analysis capabilities for I2C, SPI, UART, and GPIO operations.

**Current Version**: Serial-only with SSD1306 OLED display support (WiFi/web interface removed for stability)

## Build and Development

### Arduino IDE Setup
- **Board**: ESP32 Dev Module (use Arduino IDE board manager)
- **Compilation**: Open `ESP32_BusPirate_Stable.ino` in Arduino IDE and compile/upload directly
- **Library Dependencies**:
  - Arduino ESP32 core libraries
  - Adafruit GFX Library
  - Adafruit SSD1306 Library

### No Traditional Build System
This project uses the Arduino IDE's build system. There are no separate build scripts, package managers, or configuration files.

## Architecture

### Core Components
- **Protocol Handlers**: Separate functions for I2C (`i2cBegin()`, `i2cScan()`, etc.), SPI (`spiBegin()`, `spiXfer()`), UART (`uartBegin()`, `uartTx()`), and GPIO operations
- **Command Parser**: `handleCmd()` processes text commands from the serial interface
- **Serial Interface**: Interactive terminal at 115200 baud via USB/Serial
- **Display System**: Optional SSD1306 OLED display with real-time status updates
- **Stability System**: `safeYield()` and `checkHeap()` prevent watchdog resets during long operations

### State Management
- **Mode System**: Global `mode` variable tracks current protocol (HIZ, GPIO_MODE, I2C_MODE, SPI_MODE, UART_MODE)
- **Pin Configuration**: Global variables for pin assignments (PIN_I2C_SDA, PIN_SPI_MOSI, etc.) - configurable at runtime
- **UART Buffering**: Circular buffer system for UART RX data with overflow protection

### Key Design Patterns
- **Safe Operations**: All protocol operations include timeout handling and resource cleanup
- **Memory Management**: Fixed buffer sizes and yield calls prevent heap fragmentation and watchdog resets
- **Serial-Only Interface**: Single command system via USB/Serial terminal for maximum stability

## Pin Assignments (Default)
- I2C: SDA=21, SCL=22
- SPI: MOSI=23, MISO=19, SCK=18, CS=5
- UART: RX=16, TX=17
- Display: Uses I2C (SDA=21, SCL=22) at address 0x3C

## Common Operations
- Set mode: `mode i2c|spi|uart|gpio|hiz`
- Configure pins: `pins set <name> <pin>`
- Protocol operations: `i2c scan`, `spi x <hex>`, `uart tx <data>`
- System info: `status`, `help`
- Display control: `display on/off`

## Usage
- **Serial Monitor**: Use Arduino IDE Serial Monitor at 115200 baud
- **Terminal Programs**: PuTTY, screen, minicom, etc. at 115200 baud
- **Interactive Prompts**: Shows current mode (HiZ>, I2C>, SPI>, UART>, GPIO>)
- **Command Help**: Type `help` for complete command reference
- **OLED Display**: Shows real-time system status, mode, heap, uptime, and protocol info

## Stability Considerations
- Watchdog management functions are commented out to prevent boot loops
- Includes `safeYield()` calls in loops to prevent blocking
- Limited buffer sizes and operation timeouts prevent resource exhaustion
- Serial-only design eliminates WiFi-related crashes and complexity