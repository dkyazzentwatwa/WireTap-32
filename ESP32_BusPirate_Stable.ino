// ESP32_MicroBusPirate_Web_UART.ino - STABLE VERSION
// Fixed watchdog and stability issues
// Single-file "mini Bus Pirate" with web console AND live UART terminal
// No external libs beyond Arduino core. Works on ESP32 Dev Module.

// WiFi and WebServer includes removed for serial-only version
#include <Wire.h>
#include <SPI.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <esp_system.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi AP variables removed for serial-only version

// -------- SSD1306 Display --------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool displayEnabled = false;
unsigned long lastDisplayUpdate = 0;

// -------- CLI IO --------
HardwareSerial& USB = Serial;
String inbuf = "";
String outbuf = "";
bool CAPTURE = false;

// -------- ANSI Colors --------
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"  // Errors
#define COLOR_GREEN   "\033[32m"  // Success
#define COLOR_YELLOW  "\033[33m"  // Warnings
#define COLOR_BLUE    "\033[34m"  // Info
#define COLOR_CYAN    "\033[36m"  // Data
#define COLOR_MAGENTA "\033[35m"  // Prompts
#define COLOR_WHITE   "\033[37m"  // Headers

bool useColors = true; // Can be toggled by user

// -------- Smart Defaults --------
String lastCommand = "";
uint8_t lastI2CAddr = 0x50;  // Common EEPROM address
uint8_t lastReadLen = 8;     // Default read length
uint32_t lastUARTBaud = 115200;
bool showStatusBar = true;  // Can be toggled

void resetCapture() {
    outbuf = "";
    CAPTURE = true;
}

String flushCapture() {
    CAPTURE = false;
    return outbuf;
}

enum Mode {HIZ, GPIO_MODE, I2C_MODE, SPI_MODE, UART_MODE};
Mode mode = HIZ;

// -------- Default pins --------
int PIN_I2C_SDA = 21, PIN_I2C_SCL = 22;
int PIN_SPI_MOSI = 23, PIN_SPI_MISO = 19, PIN_SPI_SCK = 18, PIN_SPI_CS = 5;
int PIN_UART_TX = 17, PIN_UART_RX = 16;
uint32_t UART_BAUD = 115200;
uint32_t UART_CONFIG = SERIAL_8N1;
uint32_t I2C_FREQ = 100000;
uint32_t SPI_FREQ = 1000000;
uint8_t SPI_MODE_CFG = SPI_MODE0;
uint8_t SPI_BIT_ORDER = MSBFIRST;
bool I2C_PULLUPS = true;

// -------- Mode state helpers --------
bool uartBridgeActive = false;
bool uartSpamActive = false;
unsigned long uartSpamNext = 0;
String uartSpamPayload = "";
uint32_t uartSpamPeriod = 0;

bool i2cMonitorActive = false;
uint8_t i2cMonitorAddr = 0;
uint8_t i2cMonitorReg = 0;
uint32_t i2cMonitorInterval = 500;
unsigned long i2cMonitorNext = 0;
std::vector<uint8_t> i2cMonitorCache;

std::deque<String> i2cSlaveLog;
bool i2cSlaveMode = false;
unsigned long i2cSlaveEnd = 0;
uint8_t i2cSlaveAddress = 0x00;
uint8_t i2cSlaveTxValue = 0x00;

// Forward declarations
void serviceUARTRx();
void serviceI2CSlave();
void i2cMonitorStop(bool silent = false);
void i2cSlaveStop(bool silent = false);
std::vector<String> parseMacroTokens(const String& raw);
bool readLineBlocking(String promptText, String& out, unsigned long timeoutMs = 0);
std::vector<String> tok(const String& s);
std::vector<uint8_t> parseHexBytes(const std::vector<String>& v, size_t start);
bool parseHexByteToken(const String& token, uint8_t& value);

// -------- UART target --------
HardwareSerial TargetUART(2);

// -------- Stability fixes --------
unsigned long lastYield = 0;
unsigned long lastHeapCheck = 0;

void safeYield() {
    if (millis() - lastYield > 50) { // Yield every 50ms minimum
        yield();
        delay(1);
        lastYield = millis();
    }
}

void checkHeap() {
    if (millis() - lastHeapCheck > 10000) { // Check every 10s
        if (ESP.getFreeHeap() < 10000) {
            USB.printf("WARNING: Low heap: %d bytes\n", ESP.getFreeHeap());
        }
        lastHeapCheck = millis();
    }
}

// -------- Utils --------
static inline bool isHex(char c) {
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}

String toHex(const uint8_t* d, size_t n) {
    if (n == 0) return "";
    static const char* hexd="0123456789ABCDEF";
    String out;
    out.reserve(n * 3);
    for(size_t i=0; i<n && i<64; i++) { // Limit to prevent memory issues
        out += hexd[d[i]>>4];
        out += hexd[d[i]&0xF];
        if(i+1<n) out+=' ';
        if (i > 0 && i % 16 == 0) safeYield(); // Yield during long operations
    }
    return out;
}

void _out_raw(const String& s) {
    USB.print(s);
    if(CAPTURE && outbuf.length() < 2048) { // Limit capture buffer size
        outbuf += s;
    }
}

void print(const String& s) { _out_raw(s); }
void println(const String& s) { _out_raw(s + "\n"); }

// Color helper functions
void printSuccess(const String& s) {
    if(useColors) _out_raw(String(COLOR_GREEN) + s + String(COLOR_RESET) + "\n");
    else _out_raw(s + "\n");
}
void printError(const String& s) {
    if(useColors) _out_raw(String(COLOR_RED) + s + String(COLOR_RESET) + "\n");
    else _out_raw(s + "\n");
}
void printWarning(const String& s) {
    if(useColors) _out_raw(String(COLOR_YELLOW) + s + String(COLOR_RESET) + "\n");
    else _out_raw(s + "\n");
}
void printInfo(const String& s) {
    if(useColors) _out_raw(String(COLOR_BLUE) + s + String(COLOR_RESET) + "\n");
    else _out_raw(s + "\n");
}
void printData(const String& s) {
    if(useColors) _out_raw(String(COLOR_CYAN) + s + String(COLOR_RESET) + "\n");
    else _out_raw(s + "\n");
}
void printHeader(const String& s) {
    if(useColors) _out_raw(String(COLOR_WHITE) + String(COLOR_BOLD) + s + String(COLOR_RESET) + "\n");
    else _out_raw(s + "\n");
}

void showStatusBarLine() {
    if(!showStatusBar) return;

    String modeStr = (mode == HIZ ? "HiZ" : mode == GPIO_MODE ? "GPIO" : mode == I2C_MODE ? "I2C" : mode == SPI_MODE ? "SPI" : "UART");
    String heapStr = String(ESP.getFreeHeap()/1024) + "KB";
    String uptimeStr = String(millis()/1000) + "s";

    // Create status bar string
    String statusBar = "ESP32-BP │ Mode: " + modeStr + " │ Heap: " + heapStr + " │ Uptime: " + uptimeStr;

    // Add protocol-specific info
    if(mode == I2C_MODE) {
        statusBar += " │ I2C: " + String(I2C_FREQ/1000) + "kHz";
    } else if(mode == SPI_MODE) {
        statusBar += " │ SPI: " + String(SPI_FREQ/1000) + "kHz";
    } else if(mode == UART_MODE) {
        statusBar += " │ UART: " + String(UART_BAUD);
    }

    if(useColors) {
        _out_raw(String(COLOR_BLUE) + statusBar + String(COLOR_RESET) + "\n");
        String line = "";
        for(int i = 0; i < min((int)statusBar.length(), 80); i++) line += "─";
        _out_raw(String(COLOR_BLUE) + line + String(COLOR_RESET) + "\n");
    } else {
        _out_raw(statusBar + "\n");
        String line = "";
        for(int i = 0; i < min((int)statusBar.length(), 80); i++) line += "-";
        _out_raw(line + "\n");
    }
}

void prompt() {
    if(useColors) {
        switch(mode) {
            case HIZ: print(COLOR_GREEN "HiZ> " COLOR_RESET); break;
            case GPIO_MODE: print(COLOR_YELLOW "GPIO> " COLOR_RESET); break;
            case I2C_MODE: print(COLOR_CYAN "I2C> " COLOR_RESET); break;
            case SPI_MODE: print(COLOR_BLUE "SPI> " COLOR_RESET); break;
            case UART_MODE: print(COLOR_MAGENTA "UART> " COLOR_RESET); break;
        }
    } else {
        switch(mode) {
            case HIZ: print("HiZ> "); break;
            case GPIO_MODE: print("GPIO> "); break;
            case I2C_MODE: print("I2C> "); break;
            case SPI_MODE: print("SPI> "); break;
            case UART_MODE: print("UART> "); break;
        }
    }
}

// -------- Display Functions --------
void displayInit() {
    // Try to initialize display
    if(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        displayEnabled = true;
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("ESP32 Bus Pirate");
        display.println("v3.0 Serial");
        display.println("Starting...");
        display.display();
        printSuccess("SSD1306 display initialized");
    } else {
        displayEnabled = false;
        printWarning("SSD1306 display not found");
    }
}

void displayUpdate() {
    if(!displayEnabled) return;

    // Only update every 500ms to reduce I2C traffic
    if(millis() - lastDisplayUpdate < 500) return;
    lastDisplayUpdate = millis();

    display.clearDisplay();
    display.setCursor(0, 0);

    // Title
    display.setTextSize(1);
    display.println("ESP32 Bus Pirate");

    // Mode
    String modeStr = (mode == HIZ ? "HiZ" : mode == GPIO_MODE ? "GPIO" :
                     mode == I2C_MODE ? "I2C" : mode == SPI_MODE ? "SPI" : "UART");
    display.print("Mode: ");
    display.println(modeStr);

    // Heap
    display.print("Heap: ");
    display.print(ESP.getFreeHeap()/1024);
    display.println("KB");

    // Uptime
    display.print("Up: ");
    display.print(millis()/1000);
    display.println("s");

    // Protocol specific info
    if(mode == I2C_MODE) {
        display.print("I2C: ");
        display.print(I2C_FREQ/1000);
        display.println("kHz");
        display.print("Pull: ");
        display.println(I2C_PULLUPS ? "ON" : "OFF");
    } else if(mode == SPI_MODE) {
        display.print("SPI: ");
        display.print(SPI_FREQ/1000);
        display.println("kHz");
    } else if(mode == UART_MODE) {
        display.print("UART: ");
        display.println(UART_BAUD);
        if(uart_avail() > 0) {
            display.print("RX: ");
            display.print(uart_avail());
            display.println("B");
        }
    }

    display.display();
}

void setHiZ() {
    printInfo("Setting Hi-Z mode...");

    // End peripherals safely
    i2cMonitorStop(true);
    if(i2cSlaveMode) i2cSlaveStop(true);
    Wire.end();
    SPI.end();
    TargetUART.end();

    // Set pins to safe state
    pinMode(PIN_I2C_SDA, INPUT);
    pinMode(PIN_I2C_SCL, INPUT);
    pinMode(PIN_SPI_MOSI, INPUT);
    pinMode(PIN_SPI_MISO, INPUT);
    pinMode(PIN_SPI_SCK, INPUT);
    pinMode(PIN_SPI_CS, INPUT);
    pinMode(PIN_UART_TX, INPUT);
    pinMode(PIN_UART_RX, INPUT);

    mode = HIZ;
    printSuccess("Hi-Z mode active - All pins safe");
    displayUpdate();
}

// -------- I2C --------
void i2cBegin() {
    Wire.end();
    safeYield();
    
    if(I2C_PULLUPS) {
        pinMode(PIN_I2C_SDA, INPUT_PULLUP);
        pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    }
    
    if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ)) {
        printError("ERROR: I2C begin failed!");
        return;
    }
    Wire.setTimeout(500); // Shorter timeout to prevent hanging
    printSuccess("I2C mode active - " + String(I2C_FREQ/1000) + "kHz, pullups " + String(I2C_PULLUPS ? "ON" : "OFF"));
    displayUpdate();
}

void i2cScan() {
    printInfo("Scanning I2C bus (0x01-0x7E)...");
    uint8_t count=0;

    for(uint8_t addr=1; addr<127; addr++) {
        // Progress indicator every 16 addresses
        if (addr % 16 == 1) {
            int progress = (addr * 10) / 126; // 0-10 scale
            print("Progress: [");
            for(int i=0; i<10; i++) {
                if(useColors) {
                    print(i < progress ? String(COLOR_GREEN) + "#" + String(COLOR_RESET) : "-");
                } else {
                    print(i < progress ? "#" : "-");
                }
            }
            print("] " + String(addr*100/126) + "%\r");
        }

        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();
        if(error == 0) {
            print("                                        \r"); // Clear progress line
            printSuccess("Found device at 0x" + String(addr, HEX));
            count++;
        }
        if (addr % 16 == 0) safeYield(); // Yield periodically
    }
    print("                                        \r"); // Clear progress line
    if(count > 0) {
        printInfo("Scan complete: " + String(count) + " device" + (count==1?"":"s") + " found");
    } else {
        printWarning("Scan complete: No devices found");
    }
}

void i2cWrite(uint8_t addr, const std::vector<uint8_t>& bytes) {
    if(bytes.empty()) {
        printError("ERROR: No bytes to write");
        return;
    }
    if(bytes.size() > 128) {
        printError("ERROR: Too many bytes (max 128)");
        return;
    }
    
    Wire.beginTransmission(addr);
    size_t written = Wire.write(bytes.data(), bytes.size());
    uint8_t rc = Wire.endTransmission();
    
    printInfo("I2C WRITE -> 0x" + String(addr, HEX) + " [" + String(written) + "/" + String(bytes.size()) + " bytes]");
    printData("  Hex: " + toHex(bytes.data(), bytes.size()));
    if(rc == 0) printSuccess("  Result: SUCCESS");
    else printError("  Result: ERROR " + String(rc) + " " +
        (rc == 1 ? "(too long)" : rc == 2 ? "(NACK addr)" : rc == 3 ? "(NACK data)" : "(other)"));
}

void i2cRead(uint8_t addr, size_t n) {
    if(n == 0 || n > 128) {
        printError("ERROR: Invalid length (1-128)");
        return;
    }
    
    uint8_t received = Wire.requestFrom((int)addr, (int)n);
    std::vector<uint8_t> buf;
    buf.reserve(n);
    
    unsigned long timeout = millis() + 100;
    while(Wire.available() && buf.size() < n && millis() < timeout) {
        buf.push_back(Wire.read());
        safeYield();
    }
    
    if(buf.size() > 0) {
        printInfo("I2C READ <- 0x" + String(addr, HEX) + " [" + String(buf.size()) + "/" + String(n) + " bytes]");
        printData("  Hex: " + toHex(buf.data(), buf.size()));
        String decStr = "  Dec: ";
        for(size_t i = 0; i < buf.size(); i++) {
            decStr += String(buf[i]);
            if(i < buf.size()-1) decStr += " ";
        }
        printData(decStr);
    } else {
        printWarning("I2C READ <- 0x" + String(addr, HEX) + " [TIMEOUT - no response]");
    }
}

bool i2cDevicePresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void i2cPing(uint8_t addr) {
    bool ok = i2cDevicePresent(addr);
    if(ok) {
        printSuccess("I2C PING 0x" + String(addr, HEX) + " -> ACK");
    } else {
        printError("I2C PING 0x" + String(addr, HEX) + " -> NACK");
    }
}

String i2cGuessDevice(uint8_t addr) {
    if(addr == 0x3C || addr == 0x3D) return "Likely SSD1306/SH1106 OLED";
    if(addr >= 0x50 && addr <= 0x57) return "Likely 24xx EEPROM";
    if(addr == 0x68) return "Likely DS3231 RTC or MPU-6050";
    if(addr == 0x76 || addr == 0x77) return "Likely BME280/BMP280 sensor";
    if(addr == 0x20 || addr == 0x21) return "Likely MCP23017 IO expander";
    if(addr == 0x40) return "Likely INA219/Si7021 sensor";
    return "Unknown device";
}

void i2cIdentify(uint8_t addr) {
    if(!i2cDevicePresent(addr)) {
        printError("I2C IDENTIFY: Address 0x" + String(addr, HEX) + " not responding");
        return;
    }

    printSuccess("I2C IDENTIFY 0x" + String(addr, HEX) + "");
    printInfo("  Guess: " + i2cGuessDevice(addr));

    // Attempt to read two bytes for signature if possible
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    if(Wire.endTransmission(false) == 0) {
        uint8_t got = Wire.requestFrom((int)addr, 2);
        if(got > 0) {
            uint8_t a = Wire.read();
            uint8_t b = (got > 1) ? Wire.read() : 0xFF;
            printData("  Peek: 0x" + String(a, HEX) + " 0x" + String(b, HEX));
        }
    }
}

bool i2cReadRegister(uint8_t addr, uint8_t reg, size_t len, std::vector<uint8_t>& out, bool verbose = true) {
    out.clear();
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if(Wire.endTransmission(false) != 0) {
        if(verbose) printError("I2C READ REG failed to write register 0x" + String(reg, HEX));
        return false;
    }

    uint8_t received = Wire.requestFrom((int)addr, (int)len);
    unsigned long timeout = millis() + 200;
    while(Wire.available() && out.size() < len && millis() < timeout) {
        out.push_back(Wire.read());
    }

    if(out.empty()) {
        if(verbose) printWarning("I2C READ REG 0x" + String(addr, HEX) + " -> no data");
        return false;
    }

    if(verbose) {
        printSuccess("I2C READ REG 0x" + String(addr, HEX) + "[0x" + String(reg, HEX) + "] " + String(out.size()) + "B");
        printData("  Hex: " + toHex(out.data(), out.size()));
    }
    return true;
}

bool i2cRequest(uint8_t addr, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    uint8_t got = Wire.requestFrom((int)addr, (int)len);
    unsigned long timeout = millis() + 200;
    while(Wire.available() && out.size() < len && millis() < timeout) {
        out.push_back(Wire.read());
    }
    if(out.empty()) {
        printWarning("I2C READ <- 0x" + String(addr, HEX) + " [no data]");
        return false;
    }
    printData("I2C READ <- " + toHex(out.data(), out.size()));
    return true;
}

void i2cWriteRegister(uint8_t addr, uint8_t reg, const std::vector<uint8_t>& values) {
    Wire.beginTransmission(addr);
    uint8_t written = Wire.write(reg);
    if(!values.empty()) {
        written += Wire.write(values.data(), values.size());
    }
    uint8_t rc = Wire.endTransmission();

    String hexValues = values.empty() ? "" : toHex(values.data(), values.size());
    printInfo("I2C WRITE REG 0x" + String(addr, HEX) + "[0x" + String(reg, HEX) + "] " + hexValues);
    if(rc == 0 && written == values.size() + 1) printSuccess("  Result: SUCCESS");
    else printError("  Result: ERROR " + String(rc));
}

void i2cDump(uint8_t addr, size_t len) {
    if(len == 0 || len > 512) len = 256;
    printInfo("I2C DUMP 0x" + String(addr, HEX) + " len=" + String(len));
    for(size_t offset = 0; offset < len; offset += 16) {
        size_t chunk = min((size_t)16, len - offset);
        Wire.beginTransmission(addr);
        Wire.write((uint8_t)(offset & 0xFF));
        if(Wire.endTransmission(false) != 0) {
            printError("  Failed to set address pointer at 0x" + String(offset, HEX));
            break;
        }

        uint8_t got = Wire.requestFrom((int)addr, (int)chunk);
        std::vector<uint8_t> data;
        while(got-- && Wire.available()) data.push_back(Wire.read());

        if(data.empty()) {
            printWarning("  No data at 0x" + String(offset, HEX));
            break;
        }

        String line = "  0x" + String(offset, HEX) + ": " + toHex(data.data(), data.size());
        printData(line);
        safeYield();
    }
}

void i2cFlood(uint8_t addr, size_t count) {
    if(count == 0) count = 32;
    printWarning("Starting I2C flood on 0x" + String(addr, HEX) + " for " + String(count) + " iterations");
    for(size_t i = 0; i < count; i++) {
        uint8_t value = (uint8_t)esp_random();
        Wire.beginTransmission(addr);
        Wire.write((uint8_t)(i & 0xFF));
        Wire.write(value);
        uint8_t rc = Wire.endTransmission();
        if(rc != 0) {
            printError("  Flood stopped at iteration " + String(i) + " rc=" + String(rc));
            break;
        }
        safeYield();
    }
    printSuccess("I2C flood complete");
}

void i2cInjectGlitches(uint8_t addr, size_t count) {
    if(count == 0) count = 8;
    printWarning("Injecting " + String(count) + " glitch pulses targeting 0x" + String(addr, HEX));

    Wire.end();
    delay(2);

    pinMode(PIN_I2C_SDA, OUTPUT);
    pinMode(PIN_I2C_SCL, OUTPUT);
    digitalWrite(PIN_I2C_SDA, HIGH);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);

    for(size_t i = 0; i < count; i++) {
        digitalWrite(PIN_I2C_SDA, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_I2C_SCL, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_I2C_SCL, HIGH);
        delayMicroseconds(2);
        digitalWrite(PIN_I2C_SDA, HIGH);
        delayMicroseconds(2);
    }

    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delay(2);

    i2cBegin();
}

void i2cRecoverBus() {
    printWarning("Attempting I2C bus recovery (16 clock pulses + STOP)");
    Wire.end();
    delay(5);

    pinMode(PIN_I2C_SCL, OUTPUT);
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delay(1);

    for(int i = 0; i < 16; i++) {
        digitalWrite(PIN_I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(PIN_I2C_SCL, HIGH);
        delayMicroseconds(5);
    }

    // Create a STOP condition manually
    pinMode(PIN_I2C_SDA, OUTPUT);
    digitalWrite(PIN_I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SDA, HIGH);
    delayMicroseconds(5);

    pinMode(PIN_I2C_SDA, INPUT);
    pinMode(PIN_I2C_SCL, INPUT);

    i2cBegin();
}

void i2cMonitorStart(uint8_t addr, uint8_t reg, size_t watchLen, uint32_t interval) {
    i2cMonitorActive = true;
    i2cMonitorAddr = addr;
    i2cMonitorReg = reg;
    i2cMonitorInterval = std::max<uint32_t>(interval, 50);
    i2cMonitorCache.assign(std::max<size_t>(1, watchLen), 0);
    i2cMonitorNext = 0;
    printInfo("I2C monitor started for 0x" + String(addr, HEX) + " reg 0x" + String(reg, HEX) + " every " + String(i2cMonitorInterval) + "ms");
}

void i2cMonitorStop(bool silent) {
    if(i2cMonitorActive) {
        i2cMonitorActive = false;
        if(!silent) printWarning("I2C monitor stopped");
    }
}

void serviceI2CMonitor() {
    if(!i2cMonitorActive) return;
    unsigned long now = millis();
    if(now < i2cMonitorNext) return;
    i2cMonitorNext = now + i2cMonitorInterval;

    std::vector<uint8_t> buf;
    if(!i2cReadRegister(i2cMonitorAddr, i2cMonitorReg, std::max<size_t>(1, i2cMonitorCache.size()), buf, false)) {
        return;
    }

    if(i2cMonitorCache != buf) {
        printHeader("I2C monitor change @" + String((double)millis()/1000.0, 3) + "s");
        printData("  New: " + toHex(buf.data(), buf.size()));
        i2cMonitorCache = buf;
    }
}

void i2cConfigCmd(const std::vector<String>& v) {
    if(v.size() < 3) {
        println("Usage: i2c config <option> <value>");
        println("Options: freq <hz>, pullups on|off, pins <sda> <scl>");
        return;
    }

    String opt = v[2];
    opt.toLowerCase();
    if(opt == "freq" && v.size() >= 4) {
        uint32_t hz = constrain((uint32_t)strtoul(v[3].c_str(), nullptr, 10), 10000, 4000000);
        I2C_FREQ = hz;
        printSuccess("I2C frequency set to " + String(I2C_FREQ) + "Hz");
        if(mode == I2C_MODE) i2cBegin();
        return;
    }
    if(opt == "pullups" && v.size() >= 4) {
        I2C_PULLUPS = (v[3] == "on" || v[3] == "1" || v[3] == "true");
        printSuccess(String("I2C pullups ") + (I2C_PULLUPS ? "enabled" : "disabled"));
        if(mode == I2C_MODE) i2cBegin();
        return;
    }
    if(opt == "pins" && v.size() >= 5) {
        PIN_I2C_SDA = v[3].toInt();
        PIN_I2C_SCL = v[4].toInt();
        printSuccess("I2C pins set SDA=" + String(PIN_I2C_SDA) + " SCL=" + String(PIN_I2C_SCL));
        if(mode == I2C_MODE) i2cBegin();
        return;
    }

    printError("Unknown i2c config option");
}

void i2cExecuteMacro(const String& command) {
    if(command.length() < 2) {
        printError("Invalid macro syntax");
        return;
    }

    String body = command.substring(1, command.length() - 1);
    auto tokens = parseMacroTokens(body);
    if(tokens.empty()) {
        printError("Empty macro");
        return;
    }

    uint8_t addr = 0;
    if(!parseHexByteToken(tokens[0], addr)) {
        printError("Macro must start with I2C address");
        return;
    }

    std::vector<uint8_t> pending;
    for(size_t i = 1; i < tokens.size(); i++) {
        String tok = tokens[i];
        if(tok.startsWith("r:") || tok.startsWith("R:")) {
            int n = tok.substring(2).toInt();
            if(n <= 0 || n > 128) {
                printError("Invalid read length in macro");
                return;
            }
            if(!pending.empty()) {
                i2cWrite(addr, pending);
                pending.clear();
            }
            std::vector<uint8_t> buf;
            i2cRequest(addr, n, buf);
        } else {
            uint8_t val;
            if(!parseHexByteToken(tok, val)) {
                printWarning("Skipping unknown macro token: " + tok);
                continue;
            }
            pending.push_back(val);
        }
    }

    if(!pending.empty()) {
        i2cWrite(addr, pending);
    }
}

void i2cEepromShell(uint8_t addr) {
    printHeader("I2C EEPROM shell @0x" + String(addr, HEX));
    println("Commands: read <offset> [len], write <offset> <hex..>, fill <offset> <hex> <len>, exit");
    println("Offsets are 16-bit, values are hex bytes");

    while(true) {
        String line;
        if(!readLineBlocking("eeprom> ", line)) {
            println("Shell cancelled");
            break;
        }
        auto parts = tok(line);
        if(parts.empty()) continue;
        String cmd = parts[0];
        cmd.toLowerCase();
        if(cmd == "exit" || cmd == "quit") break;
        if(cmd == "read" && parts.size() >= 2) {
            uint16_t offset = (uint16_t)strtoul(parts[1].c_str(), nullptr, 0);
            size_t len = (parts.size() >= 3) ? constrain((int)strtoul(parts[2].c_str(), nullptr, 0), 1, 64) : 16;
            Wire.beginTransmission(addr);
            Wire.write((uint8_t)(offset >> 8));
            Wire.write((uint8_t)(offset & 0xFF));
            if(Wire.endTransmission(false) != 0) {
                printError("Failed to set offset");
                continue;
            }
            uint8_t got = Wire.requestFrom((int)addr, (int)len);
            std::vector<uint8_t> data;
            while(got-- && Wire.available()) data.push_back(Wire.read());
            if(data.empty()) {
                printWarning("No data");
            } else {
                printData("  " + toHex(data.data(), data.size()));
            }
            continue;
        }
        if(cmd == "write" && parts.size() >= 3) {
            uint16_t offset = (uint16_t)strtoul(parts[1].c_str(), nullptr, 0);
            auto bytes = parseHexBytes(parts, 2);
            if(bytes.empty()) {
                printError("No bytes to write");
                continue;
            }
            Wire.beginTransmission(addr);
            Wire.write((uint8_t)(offset >> 8));
            Wire.write((uint8_t)(offset & 0xFF));
            Wire.write(bytes.data(), bytes.size());
            uint8_t rc = Wire.endTransmission();
            if(rc == 0) {
                printSuccess("Write queued; waiting for EEPROM cycle");
                delay(10);
            } else {
                printError("Write failed rc=" + String(rc));
            }
            continue;
        }
        if(cmd == "fill" && parts.size() >= 4) {
            uint16_t offset = (uint16_t)strtoul(parts[1].c_str(), nullptr, 0);
            uint8_t value;
            if(!parseHexByteToken(parts[2], value)) {
                printError("Invalid fill value");
                continue;
            }
            size_t count = constrain((int)strtoul(parts[3].c_str(), nullptr, 0), 1, 64);
            std::vector<uint8_t> bytes(count, value);
            Wire.beginTransmission(addr);
            Wire.write((uint8_t)(offset >> 8));
            Wire.write((uint8_t)(offset & 0xFF));
            Wire.write(bytes.data(), bytes.size());
            uint8_t rc = Wire.endTransmission();
            if(rc == 0) {
                printSuccess("Fill queued; waiting");
                delay(10);
            } else {
                printError("Fill failed rc=" + String(rc));
            }
            continue;
        }

        printWarning("Unknown shell command");
    }

    println("Leaving EEPROM shell");
}

void i2cSlaveReceive(int len) {
    String line = "I2C slave RX <- ";
    std::vector<uint8_t> data;
    while(len-- > 0 && Wire.available()) {
        uint8_t b = Wire.read();
        data.push_back(b);
    }
    if(!data.empty()) line += toHex(data.data(), data.size());
    else line += "(none)";
    if(i2cSlaveLog.size() > 16) i2cSlaveLog.pop_front();
    i2cSlaveLog.push_back(line);
}

void i2cSlaveRequest() {
    Wire.write(i2cSlaveTxValue);
    String line = "I2C slave TX -> 0x" + String(i2cSlaveTxValue, HEX);
    if(i2cSlaveLog.size() > 16) i2cSlaveLog.pop_front();
    i2cSlaveLog.push_back(line);
}

void i2cSlaveStart(uint8_t addr, uint32_t durationMs) {
    if(i2cSlaveMode) {
        printWarning("I2C slave monitor already active");
        return;
    }
    i2cMonitorStop(true);
    Wire.end();
    delay(5);
    Wire.begin(addr, PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
    Wire.onReceive(i2cSlaveReceive);
    Wire.onRequest(i2cSlaveRequest);
    i2cSlaveMode = true;
    i2cSlaveAddress = addr;
    i2cSlaveEnd = durationMs ? millis() + durationMs : 0;
    i2cSlaveLog.clear();
    printInfo("I2C slave monitor enabled at 0x" + String(addr, HEX));
    printInfo("Press ENTER to stop");
}

void i2cSlaveStop(bool silent) {
    if(!i2cSlaveMode) return;
    Wire.onReceive(nullptr);
    Wire.onRequest(nullptr);
    Wire.end();
    delay(5);
    i2cSlaveMode = false;
    i2cSlaveLog.clear();
    if(mode == I2C_MODE) i2cBegin();
    if(!silent) printWarning("I2C slave monitor disabled");
}

void serviceI2CSlave() {
    if(!i2cSlaveMode) return;
    if(i2cSlaveEnd && millis() > i2cSlaveEnd) {
        i2cSlaveStop();
        return;
    }
    while(!i2cSlaveLog.empty()) {
        println(i2cSlaveLog.front());
        i2cSlaveLog.pop_front();
    }
}

// -------- SPI --------
void spiBegin() {
    SPI.end();
    safeYield();
    
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);
    pinMode(PIN_SPI_CS, OUTPUT);
    digitalWrite(PIN_SPI_CS, HIGH);
    println("SPI mode active - " + String(SPI_FREQ/1000) + "kHz mode " + String(SPI_MODE_CFG) + (SPI_BIT_ORDER == MSBFIRST ? " MSB" : " LSB"));
    displayUpdate();
}

void spiXfer(const std::vector<uint8_t>& out) {
    if(out.empty() || out.size() > 256) {
        println("ERROR: Invalid transfer size (1-256 bytes)");
        return;
    }

    std::vector<uint8_t> in(out.size());
    
    digitalWrite(PIN_SPI_CS, LOW);
    SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
    
    for(size_t i = 0; i < out.size(); i++) {
        in[i] = SPI.transfer(out[i]);
        if (i > 0 && i % 32 == 0) safeYield(); // Yield during long transfers
    }
    
    SPI.endTransaction();
    digitalWrite(PIN_SPI_CS, HIGH);
    
    println("SPI TRANSFER [" + String(out.size()) + " bytes]");
    println("  TX -> " + toHex(out.data(), out.size()));
    println("  RX <- " + toHex(in.data(), in.size()));
}

void spiConfigCmd(const std::vector<String>& v) {
    if(v.size() < 3) {
        println("Usage: spi config <option> <value>");
        println("Options: freq <hz>, mode <0-3>, order msb|lsb, pins <mosi> <miso> <sck> [cs]");
        return;
    }
    String opt = v[2];
    opt.toLowerCase();
    if(opt == "freq" && v.size() >= 4) {
        SPI_FREQ = std::max<uint32_t>(1000, (uint32_t)strtoul(v[3].c_str(), nullptr, 10));
        printSuccess("SPI frequency set to " + String(SPI_FREQ) + " Hz");
        if(mode == SPI_MODE) spiBegin();
        return;
    }
    if(opt == "mode" && v.size() >= 4) {
        int m = constrain(v[3].toInt(), 0, 3);
        SPI_MODE_CFG = m;
        printSuccess("SPI mode set to " + String(m));
        if(mode == SPI_MODE) spiBegin();
        return;
    }
    if(opt == "order" && v.size() >= 4) {
        String order = v[3];
        order.toLowerCase();
        if(order == "msb") SPI_BIT_ORDER = MSBFIRST;
        else if(order == "lsb") SPI_BIT_ORDER = LSBFIRST;
        else {
            printError("Unknown bit order");
            return;
        }
        printSuccess("SPI bit order set to " + order);
        if(mode == SPI_MODE) spiBegin();
        return;
    }
    if(opt == "pins" && v.size() >= 6) {
        PIN_SPI_MOSI = v[3].toInt();
        PIN_SPI_MISO = v[4].toInt();
        PIN_SPI_SCK = v[5].toInt();
        if(v.size() >= 7) PIN_SPI_CS = v[6].toInt();
        printSuccess("SPI pins updated");
        if(mode == SPI_MODE) spiBegin();
        return;
    }
    printError("Unknown spi config option");
}

void spiExecuteMacro(const String& command) {
    if(command.length() < 2) {
        printError("Invalid SPI macro");
        return;
    }
    auto tokens = parseMacroTokens(command.substring(1, command.length()-1));
    if(tokens.empty()) {
        printError("Empty SPI macro");
        return;
    }
    std::vector<uint8_t> txLog;
    std::vector<uint8_t> rxLog;

    digitalWrite(PIN_SPI_CS, LOW);
    SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));

    for(const auto& tok : tokens) {
        if(tok.startsWith("r:") || tok.startsWith("R:")) {
            int n = tok.substring(2).toInt();
            if(n <= 0 || n > 256) {
                printError("Bad SPI read length");
                continue;
            }
            for(int i = 0; i < n; i++) {
                uint8_t rx = SPI.transfer(0x00);
                txLog.push_back(0x00);
                rxLog.push_back(rx);
            }
        } else {
            uint8_t val;
            if(!parseHexByteToken(tok, val)) {
                printWarning("Skipping token: " + tok);
                continue;
            }
            uint8_t rx = SPI.transfer(val);
            txLog.push_back(val);
            rxLog.push_back(rx);
        }
    }

    SPI.endTransaction();
    digitalWrite(PIN_SPI_CS, HIGH);

    if(!txLog.empty()) {
        printData("SPI TX -> " + toHex(txLog.data(), txLog.size()));
        printData("SPI RX <- " + toHex(rxLog.data(), rxLog.size()));
    }
}

void spiEepromShell() {
    printHeader("SPI EEPROM shell (25xx)");
    println("Commands: read <addr> <len>, write <addr> <hex..>, status, exit");
    while(true) {
        String line;
        if(!readLineBlocking("spi-eeprom> ", line)) break;
        auto parts = tok(line);
        if(parts.empty()) continue;
        String cmd = parts[0];
        cmd.toLowerCase();
        if(cmd == "exit" || cmd == "quit") break;
        if(cmd == "status") {
            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x05);
            uint8_t status = SPI.transfer(0x00);
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            printData("Status: 0x" + String(status, HEX));
            continue;
        }
        if(cmd == "read" && parts.size() >= 3) {
            uint32_t addr = strtoul(parts[1].c_str(), nullptr, 0);
            size_t len = (parts.size() >= 3) ? constrain((int)strtoul(parts[2].c_str(), nullptr, 0), 1, 128) : 16;
            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x03);
            SPI.transfer((addr >> 8) & 0xFF);
            SPI.transfer(addr & 0xFF);
            std::vector<uint8_t> data;
            for(size_t i = 0; i < len; i++) {
                data.push_back(SPI.transfer(0x00));
            }
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            printData("  " + toHex(data.data(), data.size()));
            continue;
        }
        if(cmd == "write" && parts.size() >= 3) {
            uint32_t addr = strtoul(parts[1].c_str(), nullptr, 0);
            auto bytes = parseHexBytes(parts, 2);
            if(bytes.empty()) {
                printError("No data");
                continue;
            }
            // Write enable
            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x06);
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            delayMicroseconds(10);

            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x02);
            SPI.transfer((addr >> 8) & 0xFF);
            SPI.transfer(addr & 0xFF);
            for(uint8_t b : bytes) SPI.transfer(b);
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            delay(5);
            printSuccess("Write complete");
            continue;
        }
        printWarning("Unknown EEPROM command");
    }
    println("Leaving SPI EEPROM shell");
}

void spiFlashShell() {
    printHeader("SPI flash shell");
    println("Commands: id, read <addr> <len>, status, exit");
    while(true) {
        String line;
        if(!readLineBlocking("spi-flash> ", line)) break;
        auto parts = tok(line);
        if(parts.empty()) continue;
        String cmd = parts[0];
        cmd.toLowerCase();
        if(cmd == "exit" || cmd == "quit") break;
        if(cmd == "id") {
            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x9F);
            uint8_t m = SPI.transfer(0x00);
            uint8_t t = SPI.transfer(0x00);
            uint8_t c = SPI.transfer(0x00);
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            printData("JEDEC: 0x" + String(m, HEX) + " 0x" + String(t, HEX) + " 0x" + String(c, HEX));
            continue;
        }
        if(cmd == "status") {
            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x05);
            uint8_t status = SPI.transfer(0x00);
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            printData("Status: 0x" + String(status, HEX));
            continue;
        }
        if(cmd == "read" && parts.size() >= 3) {
            uint32_t addr = strtoul(parts[1].c_str(), nullptr, 0);
            size_t len = constrain((int)strtoul(parts[2].c_str(), nullptr, 0), 1, 256);
            digitalWrite(PIN_SPI_CS, LOW);
            SPI.beginTransaction(SPISettings(SPI_FREQ, SPI_BIT_ORDER, SPI_MODE_CFG));
            SPI.transfer(0x03);
            SPI.transfer((addr >> 16) & 0xFF);
            SPI.transfer((addr >> 8) & 0xFF);
            SPI.transfer(addr & 0xFF);
            std::vector<uint8_t> data;
            for(size_t i = 0; i < len; i++) data.push_back(SPI.transfer(0x00));
            SPI.endTransaction();
            digitalWrite(PIN_SPI_CS, HIGH);
            printData("  " + toHex(data.data(), data.size()));
            continue;
        }
        printWarning("Unknown flash command");
    }
    println("Leaving SPI flash shell");
}

void spiSniff() {
    printWarning("SPI sniffing not supported on this build (requires logic analyzer hardware)");
}

void spiSlaveMonitor() {
    printWarning("SPI slave monitor not implemented");
}

void spiSdcardShell() {
    printWarning("SD card helper not implemented");
}

// -------- UART --------
void uartBegin() {
    TargetUART.end();
    safeYield();

    TargetUART.begin(UART_BAUD, UART_CONFIG, PIN_UART_RX, PIN_UART_TX);
    // Note: ESP32 HardwareSerial.begin() returns void, not bool
    
    delay(50);
    TargetUART.flush();
    while(TargetUART.available()) TargetUART.read();

    println("UART mode active - " + String(UART_BAUD) + " baud");
    displayUpdate();
}

void uartTx(const std::vector<uint8_t>& bytes) {
    if(bytes.empty() || bytes.size() > 1024) {
        println("ERROR: Invalid data size (1-1024 bytes)");
        return;
    }
    
    size_t written = TargetUART.write(bytes.data(), bytes.size());
    TargetUART.flush();
    println("UART tx -> "+String(written)+"/"+String(bytes.size())+" bytes");
}

void uartChangeBaud(uint32_t baud) {
    UART_BAUD = baud;
    TargetUART.updateBaudRate(baud);
    println("UART baud=" + String(baud));
}

void uartScan() {
    std::vector<uint32_t> bauds = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    printHeader("UART auto-baud scan");
    uint32_t original = UART_BAUD;
    String bestLog = "No activity";
    for(auto b : bauds) {
        TargetUART.updateBaudRate(b);
        flushUartBuffer();
        unsigned long start = millis();
        while(millis() - start < 150) {
            if(TargetUART.available()) {
                bestLog = "Detected traffic at " + String(b) + " baud";
                uartChangeBaud(b);
                TargetUART.write((uint8_t)'?');
                TargetUART.flush();
                delay(5);
                printSuccess(bestLog);
                goto SCAN_DONE;
            }
            delay(5);
        }
        printInfo("  " + String(b) + " baud -> silent");
    }
SCAN_DONE:
    if(bestLog == "No activity") {
        TargetUART.updateBaudRate(original);
        UART_BAUD = original;
        printWarning("No UART activity detected");
    }
}

void uartPing(const String& probe, uint32_t waitMs) {
    println("UART ping -> " + probe);
    TargetUART.write((const uint8_t*)probe.c_str(), probe.length());
    TargetUART.flush();
    unsigned long end = millis() + waitMs;
    String resp;
    while(millis() < end) {
        if(TargetUART.available()) {
            resp += (char)TargetUART.read();
        }
        delay(1);
        safeYield();
    }
    if(resp.length()) {
        printSuccess("UART ping response: " + resp);
    } else {
        printWarning("UART ping: no response");
    }
}

void uartContinuousRead() {
    printInfo("UART read mode: press ENTER to stop");
    while(true) {
        serviceUARTRx();
        uint8_t tmp[64];
        size_t got = uart_popN(tmp, sizeof(tmp));
        if(got) {
            USB.write(tmp, got);
        }
        if(USB.available()) {
            char c = USB.read();
            if(c == '\n' || c == '\r' || c == 0x03) break;
        }
        delay(2);
        safeYield();
    }
    println("UART read stopped");
}

void uartBridge() {
    if(uartBridgeActive) {
        printWarning("Bridge already active");
        return;
    }
    uartBridgeActive = true;
    printInfo("UART bridge running. Press CTRL+] to exit.");
    while(uartBridgeActive) {
        serviceUARTRx();
        uint8_t tmp[128];
        size_t got = uart_popN(tmp, sizeof(tmp));
        if(got) {
            USB.write(tmp, got);
        }
        while(USB.available()) {
            char c = USB.read();
            if(c == 0x1D) { // CTRL+]
                uartBridgeActive = false;
                break;
            }
            TargetUART.write((uint8_t)c);
        }
        safeYield();
    }
    println("UART bridge closed");
}

void uartSpamStart(const String& text, uint32_t periodMs) {
    uartSpamPayload = text;
    uartSpamPeriod = std::max<uint32_t>(periodMs, 10);
    uartSpamNext = 0;
    uartSpamActive = true;
    printInfo("UART spam started every " + String(uartSpamPeriod) + "ms");
}

void uartAtShell() {
    printHeader("UART AT helper");
    println("Type command body (without AT). 'exit' to leave.");
    while(true) {
        String line;
        if(!readLineBlocking("AT> ", line)) break;
        line.trim();
        if(line.equalsIgnoreCase("exit") || line.equalsIgnoreCase("quit")) break;
        String cmd = line.startsWith("AT") ? line : "AT" + line;
        if(!cmd.endsWith("\r")) cmd += "\r";
        TargetUART.write((const uint8_t*)cmd.c_str(), cmd.length());
        TargetUART.flush();
        unsigned long end = millis() + 500;
        String response;
        while(millis() < end) {
            if(TargetUART.available()) {
                response += (char)TargetUART.read();
            }
        }
        if(response.length()) println(response);
    }
    println("Leaving AT helper");
}

void uartSendBreak(uint16_t holdMs) {
    TargetUART.flush();
    TargetUART.end();
    pinMode(PIN_UART_TX, OUTPUT);
    digitalWrite(PIN_UART_TX, LOW);
    delay(holdMs);
    digitalWrite(PIN_UART_TX, HIGH);
    delay(1);
    uartBegin();
}

void uartGlitch(uint16_t pulses, uint16_t holdUs) {
    printWarning("Sending UART glitch pulses");
    TargetUART.flush();
    pinMode(PIN_UART_TX, OUTPUT);
    for(uint16_t i = 0; i < pulses; i++) {
        digitalWrite(PIN_UART_TX, LOW);
        delayMicroseconds(holdUs);
        digitalWrite(PIN_UART_TX, HIGH);
        delayMicroseconds(holdUs);
    }
    uartBegin();
}

void uartConfigCmd(const std::vector<String>& v) {
    if(v.size() < 3) {
        println("Usage: uart config <option> <value>");
        println("Options: baud <rate>, format <7E1|8N1|...>, pins <rx> <tx>");
        return;
    }
    String opt = v[2];
    opt.toLowerCase();
    if(opt == "baud" && v.size() >= 4) {
        uint32_t baud = std::max<uint32_t>(300, (uint32_t)strtoul(v[3].c_str(), nullptr, 10));
        uartChangeBaud(baud);
        return;
    }
    if(opt == "format" && v.size() >= 4) {
        String fmt = v[3];
        fmt.toUpperCase();
        if(fmt == "8N1") UART_CONFIG = SERIAL_8N1;
        else if(fmt == "8E1") UART_CONFIG = SERIAL_8E1;
        else if(fmt == "8O1") UART_CONFIG = SERIAL_8O1;
        else if(fmt == "7E1") UART_CONFIG = SERIAL_7E1;
        else if(fmt == "7O1") UART_CONFIG = SERIAL_7O1;
        else {
            printError("Unsupported format");
            return;
        }
        uartBegin();
        return;
    }
    if(opt == "pins" && v.size() >= 5) {
        PIN_UART_RX = v[3].toInt();
        PIN_UART_TX = v[4].toInt();
        uartBegin();
        return;
    }
    printError("Unknown UART config option");
}

// -------- UART RX buffer - FIXED --------
static const size_t UART_BUF_SZ = 512; // Smaller buffer for stability
uint8_t uart_buf[UART_BUF_SZ];
size_t uart_head = 0, uart_tail = 0; // Remove volatile - not in ISR

size_t uart_avail() {
    return (uart_head >= uart_tail) ? (uart_head - uart_tail) : (UART_BUF_SZ - uart_tail + uart_head);
}

void uart_push(uint8_t c) {
    size_t next_head = (uart_head + 1) % UART_BUF_SZ;
    if (next_head != uart_tail) {
        uart_buf[uart_head] = c;
        uart_head = next_head;
    }
    // Drop data if buffer full - prevents overflow
}

size_t uart_popN(uint8_t* out, size_t maxN) {
    size_t n = 0;
    while(uart_tail != uart_head && n < maxN && n < UART_BUF_SZ) {
        out[n++] = uart_buf[uart_tail];
        uart_tail = (uart_tail + 1) % UART_BUF_SZ;
    }
    return n;
}

void flushUartBuffer() {
    uart_head = uart_tail = 0;
    while(TargetUART.available()) TargetUART.read();
}

void serviceUARTRx() {
    int count = 0;
    while(TargetUART.available() && count < 64) { // Limit reads per call
        uart_push((uint8_t)TargetUART.read());
        count++;
    }
}

void stopUartSpam() {
    if(uartSpamActive) {
        uartSpamActive = false;
        printWarning("UART spam stopped");
    }
}

void serviceUartSpam() {
    if(!uartSpamActive) return;
    unsigned long now = millis();
    if(now < uartSpamNext) return;
    uartSpamNext = now + uartSpamPeriod;
    TargetUART.write((const uint8_t*)uartSpamPayload.c_str(), uartSpamPayload.length());
    TargetUART.flush();
}

void uartExecuteMacro(const String& command) {
    if(command.length() < 2) return;
    String body = command.substring(1, command.length() - 1);
    auto tokens = parseMacroTokens(body);
    if(tokens.empty()) return;
    for(const auto& tok : tokens) {
        if(tok.startsWith("r:") || tok.startsWith("R:")) {
            int want = tok.substring(2).toInt();
            if(want <= 0 || want > 256) {
                printError("Invalid UART read length");
                return;
            }
            unsigned long end = millis() + 500;
            std::vector<uint8_t> buf;
            while(buf.size() < (size_t)want && millis() < end) {
                serviceUARTRx();
                while(uart_avail() && buf.size() < (size_t)want) {
                    buf.push_back(uart_buf[uart_tail]);
                    uart_tail = (uart_tail + 1) % UART_BUF_SZ;
                }
            }
            if(buf.empty()) printWarning("UART macro read timeout");
            else printData("UART <- " + toHex(buf.data(), buf.size()));
        } else if(tok.length() >= 2 && (tok[0] == '\'' || tok[0] == '"')) {
            String text = tok.substring(1, tok.length() - 1);
            TargetUART.write((const uint8_t*)text.c_str(), text.length());
            TargetUART.flush();
        } else {
            uint8_t val;
            if(!parseHexByteToken(tok, val)) {
                printWarning("Skipping token: " + tok);
                continue;
            }
            TargetUART.write(val);
            TargetUART.flush();
        }
    }
}

// -------- GPIO --------
void gpioSet(int pin, int val) {
    // Avoid problematic pins
    if(pin < 0 || pin > 39 || pin == 6 || pin == 7 || pin == 8 || pin == 9 || pin == 10 || pin == 11) {
        println("ERROR: Invalid or unsafe pin number");
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, val ? HIGH : LOW);
    println("GPIO"+String(pin)+" set to "+String(val));
}

int gpioGet(int pin) {
    if(pin < 0 || pin > 39) {
        println("ERROR: Invalid pin number");
        return -1;
    }
    pinMode(pin, INPUT);
    return digitalRead(pin);
}

// -------- Parser helpers --------
std::vector<String> tok(const String& s) {
    std::vector<String> v;
    String cur;
    for(size_t i=0; i<s.length() && i<512; i++) { // Limit input length
        char c=s[i];
        if(c==' '||c=='\t') {
            if(cur.length()) {
                v.push_back(cur);
                cur="";
            }
        } else {
            cur+=c;
        }
        if (i % 64 == 0) safeYield();
    }
    if(cur.length()) v.push_back(cur);
    return v;
}

std::vector<uint8_t> parseHexBytes(const std::vector<String>& v, size_t start) {
    std::vector<uint8_t> out;
    for(size_t i=start; i<v.size() && i<start+128; i++) { // Limit bytes
        String t=v[i];
        t.replace("0x","");
        t.replace("0X","");
        if(t.length()==0) continue;
        if(t.length()==1) t="0"+t;
        if(t.length()!=2 || !isHex(t[0]) || !isHex(t[1])) {
            println("WARNING: Skipping invalid hex: "+v[i]);
            continue;
        }
        out.push_back((uint8_t)strtoul(t.c_str(), nullptr, 16));
    }
    return out;
}

bool parseHexByteToken(const String& token, uint8_t& value) {
    String t = token;
    t.replace("0x","");
    t.replace("0X","");
    if(t.length() == 0 || t.length() > 2) return false;
    while(t.length() < 2) t = "0" + t;
    if(!isHex(t[0]) || !isHex(t[1])) return false;
    value = (uint8_t)strtoul(t.c_str(), nullptr, 16);
    return true;
}

std::vector<String> parseMacroTokens(const String& raw) {
    std::vector<String> tokens;
    String current;
    bool inString = false;
    char stringQuote = '\0';

    for(size_t i = 0; i < raw.length() && i < 512; i++) {
        char c = raw[i];
        if(inString) {
            current += c;
            if(c == stringQuote) {
                tokens.push_back(current);
                current = "";
                inString = false;
                stringQuote = '\0';
            }
            continue;
        }

        if(c == '\'' || c == '"') {
            if(current.length()) {
                tokens.push_back(current);
                current = "";
            }
            current += c;
            inString = true;
            stringQuote = c;
            continue;
        }

        if(c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if(current.length()) {
                tokens.push_back(current);
                current = "";
            }
        } else {
            current += c;
        }
    }

    if(current.length()) tokens.push_back(current);
    return tokens;
}

bool readLineBlocking(String promptText, String& out, unsigned long timeoutMs) {
    out = "";
    if(promptText.length()) {
        print(promptText);
    }
    unsigned long start = millis();
    while(true) {
        while(USB.available()) {
            char c = USB.read();
            if(c == '\r') continue;
            if(c == '\n') {
                print("\n");
                return true;
            }
            out += c;
        }
        if(timeoutMs && millis() - start > timeoutMs) return false;
        if(i2cSlaveMode) serviceI2CSlave();
        if(mode == UART_MODE) serviceUARTRx();
        safeYield();
    }
}

void help() {
    showStatusBarLine();
    printHeader("=== WireTap-32 Commands ===");
    println("");
    println("GENERAL:");
    println("  help, h, ?        - Show this help");
    println("  status, stat, s   - Show system status");
    println("  pins, p           - Show pin assignments");
    println("  pins set <name> <pin> - Set pin (sda,scl,mosi,miso,sck,cs,tx,rx)");
    println("  colors [on|off]   - Toggle/set color output");
    println("  statusbar [on|off] - Toggle/set status bar");
    println("  display [on|off]  - Toggle/set OLED display");
    println("  pullups on|off    - Toggle I2C pull-ups");
    println("  freq i2c|spi <hz> - Quick frequency change");
    println("  <Enter>           - Repeat last command");
    println("");
    println("MODES:");
    println("  mode <m>, m <m>   - Set mode: hiz|h, gpio|g, i2c|i, spi|s, uart|u");
    println("  Examples: 'm i' = I2C mode, 'm h' = Hi-Z safe mode");
    println("");
    println("I2C COMMANDS: (requires 'mode i2c' first)");
    println("  i2c scan          - Scan for devices");
    println("  i2c ping <addr>   - Probe address for ACK");
    println("  i2c identify <addr> - Guess device based on common addresses");
    println("  i2c read <addr> <reg> [len]  - Read register");
    println("  i2c write <addr> <reg> <hex...> - Write register");
    println("  i2c dump <addr> [len]        - Dump sequential bytes");
    println("  i2c slave <addr> [ms]        - Emulate slave/logger");
    println("  i2c flood/glitch/monitor/eeprom/recover/config");
    println("  [0x13 0x4B r:8]    - Macro syntax for advanced sequences");
    println("");
    println("SPI COMMANDS: (requires 'mode spi' first)");
    println("  spi x <hex...>    - Transfer bytes: 'spi x 0x90 0x00'");
    println("  spi sniff|slave|sdcard|eeprom|flash - Mode helpers");
    println("  spi config freq|mode|order|pins ...");
    println("  [0x9F r:3]        - SPI macro (JEDEC ID example)");
    println("");
    println("UART COMMANDS: (requires 'mode uart' first)");
    println("  uart baud|scan|ping|read|write|bridge|spam|at");
    println("  uart tx <data>    - Send: 'uart tx \"Hello\"' or hex bytes");
    println("  uart rx <len>     - Read bytes once");
    println("  uart config baud|format|pins ...");
    println("  ['Hello' r:64]    - UART macro syntax");
    println("");
    println("GPIO COMMANDS: (requires 'mode gpio' first)");
    println("  gpio set <pin> <val> - Set output: 'gpio set 2 1' (HIGH)");
    println("  gpio get <pin>    - Read input: 'gpio get 4'");
    println("");
    println("Note: Serial-only version (3.3V max, 12mA max per pin)");
}

void showPins() {
    println("=== Pin Assignments ===");
    println("I2C:  SDA=" + String(PIN_I2C_SDA) + "  SCL=" + String(PIN_I2C_SCL) + "  Freq=" + String(I2C_FREQ/1000) + "kHz  Pullups=" + String(I2C_PULLUPS ? "ON" : "OFF"));
    println("SPI:  MOSI=" + String(PIN_SPI_MOSI) + " MISO=" + String(PIN_SPI_MISO) + " SCK=" + String(PIN_SPI_SCK) + " CS=" + String(PIN_SPI_CS) + "  Freq=" + String(SPI_FREQ/1000) + "kHz");
    println("UART: RX=" + String(PIN_UART_RX) + "    TX=" + String(PIN_UART_TX) + "    Baud=" + String(UART_BAUD));
}

void showStatus() {
    showStatusBarLine();
    printHeader("=== System Status ===");
    String modeStr = (mode == HIZ ? "Hi-Z (Safe)" : mode == GPIO_MODE ? "GPIO" : mode == I2C_MODE ? "I2C" : mode == SPI_MODE ? "SPI" : "UART");
    println("Mode:        " + modeStr);
    println("Free Heap:   " + String(ESP.getFreeHeap()/1024) + "KB (" + String(ESP.getFreeHeap()) + " bytes)");
    println("UART Buffer: " + String(uart_avail()) + "/" + String(UART_BUF_SZ) + " bytes" + (uart_avail() > 0 ? " [DATA WAITING]" : ""));
    println("Uptime:      " + String(millis()/1000) + " seconds");
}

void handleCmd(const String& line) {
    if (line.length() == 0) return;

    // Store command for repeat functionality (but not if it's a repeat)
    if(line != lastCommand) {
        lastCommand = line;
    }

    auto v = tok(line);
    if(v.empty()) return;
    
    String c=v[0];
    c.toLowerCase();

    String trimmed = line;
    trimmed.trim();
    if(trimmed.startsWith("[") && trimmed.endsWith("]")) {
        if(mode == I2C_MODE) {
            i2cExecuteMacro(trimmed);
        } else if(mode == SPI_MODE) {
            spiExecuteMacro(trimmed);
        } else if(mode == UART_MODE) {
            uartExecuteMacro(trimmed);
        } else {
            printError("Macros require protocol mode");
        }
        return;
    }

    if(c=="help"||c=="?"||c=="h") { help(); return; }
    if(c=="status"||c=="stat"||c=="s") { showStatus(); return; }

    // Special commands
    if(c=="colors"||c=="color") {
        if(v.size()>=2) {
            useColors = (v[1]=="on"||v[1]=="1"||v[1]=="true");
        } else {
            useColors = !useColors; // Toggle
        }
        if(useColors) printSuccess("Colors enabled");
        else println("Colors disabled");
        return;
    }

    if(c=="statusbar"||c=="bar") {
        if(v.size()>=2) {
            showStatusBar = (v[1]=="on"||v[1]=="1"||v[1]=="true");
        } else {
            showStatusBar = !showStatusBar; // Toggle
        }
        if(showStatusBar) {
            printSuccess("Status bar enabled");
            showStatusBarLine();
        } else {
            println("Status bar disabled");
        }
        return;
    }

    if(c=="display") {
        if(v.size()>=2) {
            bool newState = (v[1]=="on"||v[1]=="1"||v[1]=="true");
            if(newState && !displayEnabled) {
                displayInit(); // Try to initialize if not already done
            }
            if(displayEnabled) {
                displayEnabled = newState;
                if(displayEnabled) {
                    printSuccess("Display enabled");
                    displayUpdate(); // Force immediate update
                } else {
                    display.clearDisplay();
                    display.display();
                    println("Display disabled");
                }
            } else {
                printError("Display not found - check I2C connection at 0x3C");
            }
        } else {
            if(displayEnabled) {
                displayEnabled = false;
                display.clearDisplay();
                display.display();
                println("Display disabled");
            } else {
                displayInit(); // Try to initialize
            }
        }
        return;
    }

    // Repeat last command on empty input
    if(line.length() == 0 && lastCommand.length() > 0) {
        printInfo("Repeating: " + lastCommand);
        handleCmd(lastCommand);
        return;
    }
    
    if((c=="mode"||c=="m") && v.size()>=2) {
        String m=v[1];
        m.toLowerCase();
        if(m=="hiz"||m=="h") { setHiZ(); }
        else if(m=="gpio"||m=="g") { mode=GPIO_MODE; println("GPIO mode active"); }
        else if(m=="i2c"||m=="i") { i2cBegin(); mode=I2C_MODE; }
        else if(m=="spi"||m=="s") { spiBegin(); mode=SPI_MODE; }
        else if(m=="uart"||m=="u") { uartBegin(); mode=UART_MODE; }
        else { println("ERROR: Invalid mode. Use: hiz|h, gpio|g, i2c|i, spi|s, uart|u"); }
        return;
    }
    
    if(c=="pins"||c=="p") {
        if(v.size()==1) { showPins(); return; }
        if(v.size()==4 && (v[1]=="set"||v[1]=="s")) {
            int p=v[3].toInt();
            String name = v[2];
            name.toLowerCase();
            if(name=="sda") PIN_I2C_SDA=p;
            else if(name=="scl") PIN_I2C_SCL=p;
            else if(name=="mosi") PIN_SPI_MOSI=p;
            else if(name=="miso") PIN_SPI_MISO=p;
            else if(name=="sck") PIN_SPI_SCK=p;
            else if(name=="cs") PIN_SPI_CS=p;
            else if(name=="tx") PIN_UART_TX=p;
            else if(name=="rx") PIN_UART_RX=p;
            else {
                println("ERROR: Invalid pin name '" + v[2] + "'");
                println("Valid pins: sda, scl, mosi, miso, sck, cs, tx, rx");
                return;
            }
            println("Pin updated. Re-enter mode to apply.");
            return;
        }
    }
    
    if(c=="pullups" && v.size()>=2) {
        I2C_PULLUPS = (v[1]=="on");
        println("I2C pullups "+String(I2C_PULLUPS?"ON":"OFF"));
        return;
    }
    
    if(c=="freq" && v.size()>=3) {
        String bus = v[1];
        bus.toLowerCase();
        uint32_t freq = (uint32_t)strtoul(v[2].c_str(), nullptr, 10);
        if(bus=="i2c") {
            I2C_FREQ = constrain(freq, 10000, 400000);
            println("I2C freq set to "+String(I2C_FREQ)+"Hz");
        } else if(bus=="spi") {
            SPI_FREQ = constrain(freq, 1000, 10000000);
            println("SPI freq set to "+String(SPI_FREQ)+"Hz");
        } else {
            println("ERROR: Invalid bus type '" + bus + "'");
            println("Usage: freq i2c <10000-400000> | freq spi <1000-10000000>");
        }
        return;
    }

    // Protocol-specific commands with mode checks
    if(c=="i2c" && v.size()>=2) {
        if(mode != I2C_MODE) {
            println("ERROR: Not in I2C mode (currently in " + String(mode == HIZ ? "HiZ" : mode == GPIO_MODE ? "GPIO" : mode == SPI_MODE ? "SPI" : "UART") + ")");
            println("Use 'mode i2c' or 'm i' to switch to I2C mode first.");
            return;
        }
        String cmd = v[1];
        cmd.toLowerCase();
        if(cmd=="scan"||cmd=="s") { i2cScan(); return; }
        if(cmd=="ping" && v.size()>=3) {
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            i2cPing(addr);
            return;
        }
        if(cmd=="identify" && v.size()>=3) {
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            i2cIdentify(addr);
            return;
        }
        if(cmd=="sniff") {
            printWarning("Passive I2C sniffing not supported on this hardware");
            return;
        }
        if(cmd=="read"||cmd=="r") {
            if(v.size()<4) {
                printError("Usage: i2c read <addr> <reg> [len]");
                return;
            }
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            uint8_t reg = (uint8_t)strtoul(v[3].c_str(), nullptr, 0);
            size_t len = (v.size()>=5) ? constrain(v[4].toInt(), 1, 128) : 1;
            std::vector<uint8_t> buf;
            i2cReadRegister(addr, reg, len, buf);
            return;
        }
        if((cmd=="write"||cmd=="w") && v.size()>=3) {
            if(v.size()<5) {
                printError("Usage: i2c write <addr> <reg> <hex...>");
                return;
            }
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            uint8_t reg = (uint8_t)strtoul(v[3].c_str(), nullptr, 0);
            auto bytes = parseHexBytes(v, 4);
            if(bytes.empty()) {
                printError("No bytes provided");
                return;
            }
            i2cWriteRegister(addr, reg, bytes);
            return;
        }
        if(cmd=="dump" && v.size()>=3) {
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            size_t len = (v.size()>=4) ? constrain(v[3].toInt(), 1, 512) : 256;
            i2cDump(addr, len);
            return;
        }
        if(cmd=="slave" && v.size()>=3) {
            if(v[2]=="stop") {
                i2cSlaveStop();
                return;
            }
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            uint32_t duration = (v.size()>=4) ? (uint32_t)strtoul(v[3].c_str(), nullptr, 0) : 0;
            i2cSlaveStart(addr, duration);
            return;
        }
        if(cmd=="flood" && v.size()>=3) {
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            size_t count = (v.size()>=4) ? (size_t)std::max<long>(1, v[3].toInt()) : 32;
            i2cFlood(addr, count);
            return;
        }
        if(cmd=="glitch" && v.size()>=3) {
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            size_t pulses = (v.size()>=4) ? (size_t)std::max<long>(1, v[3].toInt()) : 8;
            i2cInjectGlitches(addr, pulses);
            return;
        }
        if(cmd=="monitor") {
            if(v.size()==3 && v[2]=="stop") {
                i2cMonitorStop();
                return;
            }
            if(v.size()>=3) {
                uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
                uint8_t reg = (v.size()>=4) ? (uint8_t)strtoul(v[3].c_str(), nullptr, 0) : 0;
                size_t len = (v.size()>=5) ? (size_t)std::max<long>(1, v[4].toInt()) : 1;
                uint32_t ms = (v.size()>=6) ? (uint32_t)strtoul(v[5].c_str(), nullptr, 0) : 500;
                i2cMonitorStart(addr, reg, len, ms);
            } else {
                i2cMonitorStop();
            }
            return;
        }
        if(cmd=="eeprom" && v.size()>=3) {
            uint8_t addr = (uint8_t)strtoul(v[2].c_str(), nullptr, 0);
            i2cEepromShell(addr);
            return;
        }
        if(cmd=="recover") {
            i2cRecoverBus();
            return;
        }
        if(cmd=="config") {
            i2cConfigCmd(v);
            return;
        }
    }

    if(c=="spi" && v.size()>=2) {
        String cmd = v[1];
        cmd.toLowerCase();
        bool needsMode = (cmd != "config");
        if(needsMode && mode != SPI_MODE) {
            println("ERROR: Not in SPI mode (currently in " + String(mode == HIZ ? "HiZ" : mode == GPIO_MODE ? "GPIO" : mode == I2C_MODE ? "I2C" : "UART") + ")");
            println("Use 'mode spi' or 'm s' to switch to SPI mode first.");
            return;
        }
        if(cmd=="x"||cmd=="xfer") {
            auto bytes = parseHexBytes(v, 2);
            spiXfer(bytes);
            return;
        }
        if(cmd=="sniff") { spiSniff(); return; }
        if(cmd=="slave") { spiSlaveMonitor(); return; }
        if(cmd=="sdcard") { spiSdcardShell(); return; }
        if(cmd=="eeprom") { spiEepromShell(); return; }
        if(cmd=="flash") { spiFlashShell(); return; }
        if(cmd=="config") { spiConfigCmd(v); return; }
    }

    if(c=="uart" && v.size()>=2) {
        String cmd = v[1];
        cmd.toLowerCase();
        if(cmd=="baud" && v.size()>=3) {
            UART_BAUD = constrain((uint32_t)strtoul(v[2].c_str(), nullptr, 10), 1200, 2000000);
            if(mode == UART_MODE) uartBegin();
            println("UART baud="+String(UART_BAUD));
            return;
        }
        if(cmd=="scan") {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            uartScan();
            return;
        }
        if(cmd=="ping") {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            String probe = (v.size() >= 3) ? line.substring(line.indexOf(v[2])) : String("PING\r\n");
            uartPing(probe, 200);
            return;
        }
        if(cmd=="read") {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            uartContinuousRead();
            return;
        }
        if(cmd=="tx" && v.size()>=3) {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            std::vector<uint8_t> bytes;
            if(v[2].startsWith("\"") && line.lastIndexOf('"')>2) {
                int a=line.indexOf('"');
                int b=line.lastIndexOf('"');
                String s=line.substring(a+1,b);
                for(size_t i=0; i<s.length() && i<1024; i++) {
                    bytes.push_back((uint8_t)s[i]);
                }
            } else {
                bytes = parseHexBytes(v, 2);
            }
            uartTx(bytes);
            return;
        }
        if(cmd=="write" && v.size()>=3) {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            std::vector<uint8_t> bytes;
            if(v[2].startsWith("\"") && line.lastIndexOf('"')>2) {
                int a=line.indexOf('"');
                int b=line.lastIndexOf('"');
                String s=line.substring(a+1,b);
                for(size_t i=0; i<s.length() && i<1024; i++) bytes.push_back((uint8_t)s[i]);
            } else if(v[2].startsWith("'" ) && line.lastIndexOf('\'')>2) {
                int a=line.indexOf('\'');
                int b=line.lastIndexOf('\'');
                String s=line.substring(a+1,b);
                for(size_t i=0; i<s.length() && i<1024; i++) bytes.push_back((uint8_t)s[i]);
            } else {
                bytes = parseHexBytes(v, 2);
            }
            uartTx(bytes);
            return;
        }
        if(cmd=="rx" && v.size()>=3) {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            int want = constrain(v[2].toInt(), 1, 512);
            std::vector<uint8_t> tmp;
            tmp.resize(want);
            uint32_t t0=millis();
            size_t got=0;
            while(got<(size_t)want && (millis()-t0)<500) {
                serviceUARTRx();
                while(uart_avail() && got<(size_t)want) {
                    tmp[got++] = uart_buf[uart_tail];
                    uart_tail = (uart_tail+1)%UART_BUF_SZ;
                }
                safeYield();
            }
            println(got ? ("UART rx <- "+toHex(tmp.data(), got)) : "UART rx: (timeout)");
            return;
        }
        if(cmd=="bridge") {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            uartBridge();
            return;
        }
        if(cmd=="spam" && v.size()>=3 && v[2]=="stop") {
            stopUartSpam();
            return;
        }
        if(cmd=="spam" && v.size()>=4) {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            int start = line.indexOf(v[2]);
            int periodPos = line.lastIndexOf(v.back());
            String message = line.substring(start, periodPos);
            message.trim();
            uint32_t period = std::max<uint32_t>(10, (uint32_t)strtoul(v.back().c_str(), nullptr, 10));
            if(message.startsWith("\"") && message.endsWith("\"")) {
                message = message.substring(1, message.length()-1);
            }
            if(message.startsWith("'") && message.endsWith("'")) {
                message = message.substring(1, message.length()-1);
            }
            uartSpamStart(message, period);
            return;
        }
        if(cmd=="at") {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            uartAtShell();
            return;
        }
        if(cmd=="glitch") {
            if(mode != UART_MODE) {
                println("ERROR: Not in UART mode. Use 'mode uart' first.");
                return;
            }
            uint16_t pulses = (v.size()>=3) ? (uint16_t)strtoul(v[2].c_str(), nullptr, 0) : 8;
            uint16_t hold = (v.size()>=4) ? (uint16_t)strtoul(v[3].c_str(), nullptr, 0) : 1;
            uartGlitch(pulses, hold);
            return;
        }
        if(cmd=="xmodem") {
            printWarning("XMODEM transfer not implemented in this build");
            return;
        }
        if(cmd=="config") {
            uartConfigCmd(v);
            return;
        }
    }

    if(c=="gpio" && v.size()>=2) {
        String cmd = v[1];
        cmd.toLowerCase();
        if(cmd=="set" && v.size()>=4) {
            gpioSet(v[2].toInt(), v[3].toInt());
            return;
        }
        if(cmd=="get" && v.size()>=3) {
            int val=gpioGet(v[2].toInt());
            if(val >= 0) {
                println("Pin "+v[2]+" = "+String(val));
            }
            return;
        }
    }

    println("Unknown command: '" + c + "'");
    println("Type 'help' or 'h' for available commands.");

    // Suggest similar commands
    if(c.startsWith("i2")) println("Did you mean: i2c scan|read|write ?");
    else if(c.startsWith("sp")) println("Did you mean: spi x <hex> ?");
    else if(c.startsWith("ua")) println("Did you mean: uart tx|rx|baud ?");
    else if(c.startsWith("gp")) println("Did you mean: gpio set|get ?");
    else if(c.startsWith("mo")) println("Did you mean: mode hiz|gpio|i2c|spi|uart ?");
    else if(c.startsWith("pi")) println("Did you mean: pins [set <name> <pin>] ?");
}

void handleInputStream(Stream& s) {
    while(s.available()) {
        char ch=s.read();
        if(ch=='\r') continue;
        if(ch=='\n') {
            String cmd=inbuf;
            inbuf="";
            if(cmd.length() > 0) {
                handleCmd(cmd);
            }
            prompt();
        } else if (inbuf.length() < 256) { // Limit input buffer
            inbuf += ch;
        }
        safeYield();
    }
}

// Web interface code removed for serial-only version

// -------- Main Arduino Functions --------
void setup() {
    // Disable watchdog during setup (commented out to avoid WDT errors)
    // disableCore0WDT();
    Wire.begin(21, 22);
    USB.begin(115200);
    delay(2000); // Longer delay for stability
    
    USB.println("\n=== ESP32 Bus Pirate v3.0 (Serial-Only) ===");
    USB.println("Free heap: " + String(ESP.getFreeHeap()));

    // Initialize in safe order
    setHiZ();

    // Initialize display after I2C is set up
    displayInit();

    USB.println("Ready! Type 'help' for commands.");

    // Re-enable watchdog with longer timeout (commented out to avoid WDT errors)
    // enableCore0WDT();

    // Show initial status bar
    showStatusBarLine();

    prompt();
}

void loop() {
    static unsigned long lastLoop = 0;
    unsigned long now = millis();
    
    // Limit loop frequency to prevent watchdog issues
    if (now - lastLoop < 10) {
        delay(1);
        return;
    }
    lastLoop = now;
    
    // Service UART if in UART mode
    if (mode == UART_MODE) {
        serviceUARTRx();
        serviceUartSpam();
    } else {
        if(uartSpamActive) stopUartSpam();
    }
    
    // Handle serial input
    handleInputStream(USB);

    // Update display
    displayUpdate();

    if(mode == I2C_MODE) {
        serviceI2CMonitor();
    }
    serviceI2CSlave();

    // Periodic maintenance
    checkHeap();
    safeYield();
}
