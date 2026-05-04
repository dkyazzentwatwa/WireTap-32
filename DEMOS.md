# WireTap-32 — Hands-On Demo Guide

Real things to try with real gear. Every demo below uses only WireTap-32 commands you can type right now.

> **Voltage reminder**: ESP32 GPIO is 3.3V only. Use a level shifter or 10kΩ resistors in series for 5V targets. When in doubt, measure first.

---

## Quick-start wiring cheat-sheet

| Protocol | WireTap-32 pin | Connect to |
|---|---|---|
| I2C SDA | GPIO 21 | Target SDA |
| I2C SCL | GPIO 22 | Target SCL |
| SPI MOSI | GPIO 23 | Target MOSI/DI |
| SPI MISO | GPIO 19 | Target MISO/DO |
| SPI CLK  | GPIO 18 | Target CLK/SCK |
| SPI CS   | GPIO 5  | Target CS/CE |
| UART TX  | GPIO 17 | Target RX |
| UART RX  | GPIO 16 | Target TX |
| GND      | GND     | Target GND (always shared) |

Start every session in Hi-Z so nothing drives an unknown bus until you're ready:

```
mode hiz
```

---

## Arduino demos

### 1. Scan an Arduino's I2C bus

An Arduino running any sensor library (BME280, OLED, MPU-6050…) already has I2C wired up. Tap in with WireTap-32 as a second master and see what's on the bus.

**Wiring**: WireTap-32 GPIO 21 → Arduino A4 (SDA), GPIO 22 → Arduino A5 (SCL), GND → GND.  
(Uno uses A4/A5. Mega uses 20/21. Nano uses A4/A5.)

```
mode i2c
i2c scan
```

You'll see every device the Arduino has connected — OLED at 0x3C, BMP280 at 0x76, RTC at 0x68, etc. — with a best-guess device name for each address.

---

### 2. Read a sensor the Arduino is already using

Once you know an address from the scan, you can read its registers directly without touching the Arduino code.

```
i2c identify 0x76        -- guess the device type
i2c dump 0x76            -- dump first 32 registers
i2c read 0x76 0xD0 1     -- read chip ID register (0x60 = BME280, 0x58 = BMP280)
```

---

### 3. Make an Arduino talk to WireTap-32 over UART

Upload this tiny sketch to any Arduino:

```cpp
void setup() { Serial.begin(9600); }
void loop()  { Serial.println("Hello from Arduino!"); delay(1000); }
```

Wire: Arduino TX → WireTap-32 GPIO 16 (RX), GND → GND. (Don't connect TX if Arduino is 5V — use a voltage divider.)

```
mode uart
uart baud 9600
uart read
```

You'll see the Arduino's messages in real time. To send a reply back:

```
uart tx "OK"
```

---

### 4. Use WireTap-32 as a USB-to-serial adapter

Forget FTDI adapters. Wire any serial device to WireTap-32 and use bridge mode:

```
mode uart
uart baud 115200
uart bridge
```

Everything you type goes to the target device's UART; everything the target sends appears on your screen. Press `Ctrl+]` to exit.

---

### 5. Write to an Arduino EEPROM (24C32 / DS1307 module)

Many Arduino RTC modules include a small 24Cxx EEPROM on the same I2C bus at 0x50–0x57.

```
mode i2c
i2c eeprom 0x50           -- open interactive EEPROM shell
```

Inside the shell you can read pages, write bytes, and verify. Type `help` at the EEPROM prompt for commands.

---

### 6. Stress-test an I2C slave on an Arduino

Upload an `I2C_Slave` sketch to the Arduino (Arduino IDE → Examples → Wire → slave_sender). Then:

```
mode i2c
i2c flood 0x08 100        -- fire 100 random writes at address 0x08
i2c monitor 0x08 0x00 1 500  -- watch register 0x00 every 500ms
```

This tests whether your slave handles unexpected data gracefully.

---

## Raspberry Pi demos

The Raspberry Pi's GPIO runs at 3.3V — fully compatible with WireTap-32, no level shifting needed.

### 7. Tap the Raspberry Pi I2C bus

Enable I2C on the Pi (`raspi-config → Interface Options → I2C`), attach any sensor. Then connect WireTap-32 in parallel:

**Wiring**: GPIO 21 → Pi pin 3 (SDA1), GPIO 22 → Pi pin 5 (SCL1), GND → Pi pin 6 (GND).

```
mode i2c
i2c scan
```

See every sensor the Pi sees. You can read registers at the same time the Pi is running its Python scripts — non-destructive observation.

---

### 8. Emulate a sensor for a Raspberry Pi

Write a Pi script that tries to read a temperature sensor at 0x40. Instead of wiring a real sensor, point the I2C lines to WireTap-32 running in slave mode:

```
mode i2c
i2c slave 0x40 200ms
```

WireTap-32 answers every read request from the Pi with `0x00` (configurable) and logs what the Pi wrote. Great for testing your Pi script's error handling when data is missing or wrong.

---

### 9. Tap the Raspberry Pi UART console

The Pi exposes a Linux serial console on GPIO 14 (TX) / 15 (RX) at 115200 baud by default (enabled in `raspi-config`).

**Wiring**: Pi GPIO 14 (TX) → WireTap-32 GPIO 16 (RX). GND → GND. (Just RX — don't drive Pi's RX unless you know what you're sending.)

```
mode uart
uart baud 115200
uart read
```

Watch the Pi's boot log or Linux shell output scroll by. Full bidirectional bridge:

```
uart bridge
```

---

### 10. Check Raspberry Pi sensor wiring without a debugger

You just soldered a sensor to a Pi and the Python script says "no device found." Before reflowing solder joints, confirm whether the device is actually on the bus:

```
mode i2c
i2c scan
i2c ping 0x76    -- should print ACK if BMP280 is wired correctly
i2c recover      -- if bus is stuck (SDA held low), send 16 clocks to free it
```

---

## Other ESP32 / microcontroller demos

### 11. Debug two ESP32 boards talking over I2C

One ESP32 runs as I2C master reading a sensor; another runs your custom firmware as I2C slave. Wire WireTap-32 on the same bus:

```
mode i2c
i2c monitor 0x08 0x00 4 200    -- watch 4 bytes at address 0x08 every 200ms
```

Every time the slave's register value changes you'll see a line printed. Useful for confirming your master is reading the right address and your slave is updating registers correctly.

---

### 12. Find the baud rate of an unknown serial device

You found an old ESP8266 module, a GPS receiver, or a mystery board — and you don't know its baud rate:

```
mode uart
uart scan
```

WireTap-32 tests common baud rates (1200 through 921600) and tells you which ones produce valid-looking output. Then:

```
uart baud 9600    -- switch to the detected rate
uart bridge       -- talk to it interactively
```

---

### 13. Send AT commands to a GSM or WiFi module

Classic modules like SIM800L, HC-05 Bluetooth, and ESP-01 speak AT commands over UART.

```
mode uart
uart baud 9600
uart at
```

This opens an interactive AT shell. Type `AT` and you should get `OK`. Then try `AT+GMR` for firmware version, `AT+CPIN?` for SIM status, etc.

---

### 14. Read JEDEC ID from a SPI flash chip

Any W25Qxx (W25Q32, W25Q64, W25Q128) flash chip — commonly found on ESP8266/ESP32 boards, cheap programmer dongles, or stripped from dead devices — exposes its ID via SPI command 0x9F.

**Wiring**: WireTap-32 GPIO 23→DI, 19→DO, 18→CLK, 5→CS. Chip needs 3.3V VCC.

```
mode spi
[0x9F r:3]    -- SPI macro: send 0x9F (JEDEC ID cmd), read 3 bytes
```

Output looks like: `EF 40 16` = Winbond W25Q32. First byte is manufacturer (EF = Winbond, C8 = GigaDevice, 20 = Micron).

---

## Household items with accessible chips

Many everyday objects have I2C or UART buried inside them. You don't need a soldering iron for most of these — just poke exposed test points.

### 15. Old hard drives — SATA diagnostic UART

Most laptop and desktop hard drives have a 3.3V UART debug port on the PCB. It's usually a 4-pin header or exposed pads near the SATA connector labeled UART, TX, RX, GND, or 1–4.

```
mode uart
uart scan           -- let WireTap-32 find the baud rate (usually 38400)
uart bridge         -- see the drive's diagnostic output
```

Do not power a hard drive from the ESP32. Use a proper 12V/5V supply and share only GND.

---

### 16. PC DDR3/DDR4 RAM — SPD EEPROM

Every DDR memory stick has a small EEPROM on the SMBus (a subset of I2C) that stores its speed and timing data. You can read it live from a running PC motherboard or from the stick alone.

**WARNING**: Motherboard SMBus is live while the PC is on. Only probe if you're comfortable. Alternatively, read the stick on a bench with a 3.3V supply.

Addresses are 0x50–0x57 (data) and 0x30–0x37 (temperature sensor on DDR4).

```
mode i2c
i2c scan              -- should show 0x50 (first DIMM) and maybe 0x30 (thermal)
i2c dump 0x50 128     -- first 128 bytes of SPD data (timing profile)
i2c read 0x30 0x05 1  -- DDR4 thermal sensor temperature register
```

The first byte at 0x50 tells you the DRAM type: `0x0B` = DDR3, `0x0C` = DDR4, `0x12` = DDR5.

---

### 17. Cheap LCD displays (PCF8574 backpack)

The ubiquitous 16x2 and 20x4 LCD displays with a small I2C adapter board use a PCF8574 IO expander at 0x27 or 0x3F.

```
mode i2c
i2c scan              -- should show 0x27 or 0x3F
i2c ping 0x27         -- confirm ACK
i2c write 0x27 0x00   -- write a nibble to the expander
i2c dump 0x27 1       -- read current state of the 8 output pins
```

You can control the LCD backlight, enable line, and RS pin directly from WireTap-32 without any Arduino in the loop.

---

### 18. USB hubs and charger chips

Many powered USB hubs and some USB-C chargers use I2C-addressable chips for power negotiation. Common ones:

- `0x50` — EEPROM containing hub descriptor
- `0x51` — alternate EEPROM address
- `0x18` — audio or power chips on some hubs

Expose the I2C test pads (look for TP_SDA, TP_SCL near a small 6-pin or 8-pin SOT chip) and:

```
mode i2c
i2c scan
i2c dump 0x50 64
```

---

### 19. Smart LED strips (addressable via SPI or UART)

WS2812B LEDs use a proprietary 1-wire protocol, but APA102/SK9822 strips use SPI with separate clock and data lines.

**Wiring**: WireTap-32 GPIO 23→Data, GPIO 18→Clock, GND→GND. Strip needs its own 5V power.

```
mode spi
spi x 0x00 0x00 0x00 0x00   -- start frame (4 zero bytes)
spi x 0xFF 0x00 0x00 0xFF   -- LED 1: full brightness, blue (R G B)
spi x 0xFF 0xFF 0x00 0x00   -- LED 2: full brightness, red
spi x 0xFF 0xFF 0x00 0xFF   -- LED 3: full brightness, magenta
spi x 0xFF 0xFF 0xFF 0xFF   -- end frame
```

Change the RGB bytes to any color you want. You can script full sequences with the macro syntax.

---

### 20. Cheap BMP280 / AHT20 weather sensor modules

These are sold on AliExpress for under $2 and are a perfect first demo.

**BMP280** (usually at 0x76 or 0x77):

```
mode i2c
i2c scan
i2c identify 0x76
i2c read 0x76 0xD0 1       -- chip ID: 0x60 = BME280, 0x58 = BMP280
i2c read 0x76 0xF3 1       -- status register (bit 3 = measuring)
i2c read 0x76 0xF7 6       -- raw pressure + temperature (3 bytes each)
```

**AHT20** (at 0x38):

```
i2c scan
i2c ping 0x38
i2c write 0x38 0xBE 0x08 0x00   -- initialize AHT20
i2c write 0x38 0xAC 0x33 0x00   -- trigger measurement
i2c dump 0x38 6                  -- read 6 bytes: status + humidity + temperature
```

---

### 21. Old toy or gadget with a mystery chip

Found a broken toy, a dead smart bulb, an old fitness tracker? Look for any small IC with 6–16 pins near exposed pads. If it's an SO-8 or SOT-23-6 package:

1. Identify the chip marking (magnifying glass or phone camera)
2. Look up the datasheet to find the protocol (usually I2C or SPI)
3. Wire WireTap-32 to the bus pads

```
mode i2c
i2c scan              -- see if anything responds
i2c dump 0xXX 32     -- dump registers of whatever you found
```

Even if you can't identify the chip, the scan result and register dump often tell you a lot about what the device is doing.

---

### 22. Car OBD-II diagnostic port (ELM327 adapter)

If you have a cheap ELM327 Bluetooth/USB OBD adapter, it has a UART interface. Wire its TX/RX to WireTap-32.

```
mode uart
uart baud 38400
uart at
```

Type `ATZ` to reset the ELM327, `ATI` for version, `ATRV` for battery voltage, `0100` to query supported PIDs.

---

### 23. MicroSD / SD card over SPI

SD cards speak SPI at 3.3V. Most breakout boards have a 3.3V regulator onboard.

**Wiring**: MOSI, MISO, CLK, CS as normal SPI. Supply 3.3V to VCC.

```
mode spi
spi x 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF  -- 10 clocks with CS high (card init)
spi x 0x40 0x00 0x00 0x00 0x00 0x95   -- CMD0 (reset) with CRC 0x95
```

Reading raw card responses lets you verify the card is alive and responding before troubleshooting higher-level filesystem issues.

---

## Measurement demos (no external hardware needed)

### 24. Measure your own power supply voltage

Use `signal adc` on any ADC pin to measure a voltage divider. To measure 5V safely with two 10kΩ resistors in series (5V→R1→pin→R2→GND):

```
signal adc 34 64
```

Output includes min/avg/max raw ADC and voltage estimates. Since you divided by 2, multiply the reported voltage by 2. Calibrate the divider ratio for your actual resistor values.

---

### 25. Measure a PWM duty cycle from a servo controller

Connect the servo signal wire (usually orange/yellow) to any input pin:

```
gpio pulse 34     -- measure the HIGH pulse width (should be 1000–2000 us for servos)
signal freq 34    -- measure frequency/duty (should be ~50 Hz for standard servos)
```

Standard servo neutral = ~1500 µs. Full range = 1000–2000 µs. This instantly tells you whether a servo controller is sending correct signals without an oscilloscope.

---

### 26. Capture a button press or IR signal

```
signal scope 34 80 1000    -- 80 samples, 1ms apart (80ms total window)
```

Press a button or trigger an IR remote while scope is sampling. The output:

```
____----____----____----____----____----____----____----____---
```

Shows you the timing pattern of the signal. Run a few times to catch the event.

---

### 27. PWM loopback self-test

Jumper GPIO25 to GPIO26, generate a PWM signal, and measure it:

```
pins check
signal pwmout 25 1000 50
signal freq 26 1000
signal edges 26 500
signal scope 26 80 100
```

This confirms the signal generator and simple analyzer path without external gear. Keep the jumper short and stay on 3.3V GPIO.

---

## Tips for household exploration

- **Look for 4-pin headers on PCBs** — they're often UART debug ports (Vcc, GND, TX, RX)
- **Small 8-pin ICs near a crystal** — likely an I2C sensor or EEPROM
- **Two traces going everywhere** — that's the I2C bus (SDA + SCL run to every chip)
- **Four traces with a CS per chip** — that's SPI
- **Always start with `mode hiz`** and measure VCC with a multimeter before connecting
- **GND first, always** — share ground before touching any data lines
- **If i2c scan shows nothing**, try `i2c recover` — a stuck bus won't respond to scans
- **If a device responds but gives garbage data**, check whether it's a 5V device needing a level shifter

---

*All commands shown here work in WireTap-32 v3.0+. Type `help` at any prompt for the full command reference.*
