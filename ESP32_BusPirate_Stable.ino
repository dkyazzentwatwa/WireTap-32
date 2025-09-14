// ESP32_MicroBusPirate_Web_UART.ino - STABLE VERSION
// Fixed watchdog and stability issues
// Single-file "mini Bus Pirate" with web console AND live UART terminal
// No external libs beyond Arduino core. Works on ESP32 Dev Module.

// WiFi and WebServer includes removed for serial-only version
#include <Wire.h>
#include <SPI.h>
#include <vector>
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
uint32_t I2C_FREQ = 100000;
uint32_t SPI_FREQ = 1000000;
bool I2C_PULLUPS = true;

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

// -------- SPI --------
void spiBegin() {
    SPI.end();
    safeYield();
    
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);
    pinMode(PIN_SPI_CS, OUTPUT);
    digitalWrite(PIN_SPI_CS, HIGH);
    println("SPI mode active - " + String(SPI_FREQ/1000) + "kHz");
    displayUpdate();
}

void spiXfer(const std::vector<uint8_t>& out) {
    if(out.empty() || out.size() > 256) {
        println("ERROR: Invalid transfer size (1-256 bytes)");
        return;
    }
    
    std::vector<uint8_t> in(out.size());
    
    digitalWrite(PIN_SPI_CS, LOW);
    SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
    
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

// -------- UART --------
void uartBegin() {
    TargetUART.end();
    safeYield();
    
    TargetUART.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
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

void serviceUARTRx() {
    int count = 0;
    while(TargetUART.available() && count < 64) { // Limit reads per call
        uart_push((uint8_t)TargetUART.read());
        count++;
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

void help() {
    showStatusBarLine();
    printHeader("=== ESP32 Bus Pirate Commands ===");
    println("");
    println("GENERAL:");
    println("  help, h, ?        - Show this help");
    println("  status, stat, s   - Show system status");
    println("  pins, p           - Show pin assignments");
    println("  pins set <name> <pin> - Set pin (sda,scl,mosi,miso,sck,cs,tx,rx)");
    println("  colors [on|off]   - Toggle/set color output");
    println("  statusbar [on|off] - Toggle/set status bar");
    println("  display [on|off]  - Toggle/set OLED display");
    println("  <Enter>           - Repeat last command");
    println("");
    println("MODES:");
    println("  mode <m>, m <m>   - Set mode: hiz|h, gpio|g, i2c|i, spi|s, uart|u");
    println("  Examples: 'm i' = I2C mode, 'm h' = Hi-Z safe mode");
    println("");
    println("I2C COMMANDS: (requires 'mode i2c' first)");
    println("  i2c scan, i2c s   - Scan for devices (0x01-0x7F) with progress");
    println("  i2c r [addr] [len] - Read bytes (uses last addr/len if omitted)");
    println("  i2c w <addr> <hex...> - Write bytes: 'i2c w 0x50 0x00 0xFF'");
    println("  pullups on|off    - Enable/disable I2C pullups");
    println("  freq i2c <hz>     - Set frequency (10000-400000Hz)");
    println("");
    println("SPI COMMANDS: (requires 'mode spi' first)");
    println("  spi x <hex...>    - Transfer bytes: 'spi x 0x90 0x00'");
    println("  freq spi <hz>     - Set frequency (1000-10000000Hz)");
    println("");
    println("UART COMMANDS: (requires 'mode uart' first)");
    println("  uart baud <rate>  - Set baud rate: 'uart baud 9600'");
    println("  uart tx <data>    - Send: 'uart tx \"Hello\"' or 'uart tx 0x41 0x42'");
    println("  uart rx <len>     - Read bytes: 'uart rx 10'");
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
        if(cmd=="read"||cmd=="r") {
            uint8_t addr = lastI2CAddr;  // Use last address as default
            int len = lastReadLen;       // Use last length as default

            if(v.size()>=3) addr = (uint8_t) strtoul(v[2].c_str(), nullptr, 0);
            if(v.size()>=4) len = constrain(v[3].toInt(), 1, 128);

            // Update defaults for next time
            lastI2CAddr = addr;
            lastReadLen = len;

            if(v.size()<3) printInfo("Using last address: 0x" + String(addr, HEX));
            if(v.size()<4) printInfo("Using last length: " + String(len));

            i2cRead(addr, len);
            return;
        }
        if((cmd=="write"||cmd=="w") && v.size()>=3) {
            uint8_t addr = lastI2CAddr;  // Use last address as default

            if(v.size()>=3) addr = (uint8_t) strtoul(v[2].c_str(), nullptr, 0);
            if(v.size()<4) {
                printError("ERROR: No data to write. Usage: i2c w <addr> <hex...>");
                return;
            }

            lastI2CAddr = addr;  // Update default

            auto bytes = parseHexBytes(v, 3);
            i2cWrite(addr, bytes);
            return;
        }
    }

    if(c=="spi" && v.size()>=2 && (v[1]=="x"||v[1]=="xfer")) {
        if(mode != SPI_MODE) {
            println("ERROR: Not in SPI mode (currently in " + String(mode == HIZ ? "HiZ" : mode == GPIO_MODE ? "GPIO" : mode == I2C_MODE ? "I2C" : "UART") + ")");
            println("Use 'mode spi' or 'm s' to switch to SPI mode first.");
            return;
        }
        auto bytes = parseHexBytes(v, 2);
        spiXfer(bytes);
        return;
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
    }
    
    // Handle serial input
    handleInputStream(USB);

    // Update display
    displayUpdate();

    // Periodic maintenance
    checkHeap();
    safeYield();
}
