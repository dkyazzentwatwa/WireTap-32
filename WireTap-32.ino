// ESP32_MicroBusPirate_Web_UART.ino - STABLE VERSION
// Fixed watchdog and stability issues
// Single-file "mini Bus Pirate" with web console AND live UART terminal
// No external libs beyond Arduino core. Works on ESP32 Dev Module.

// WiFi and WebServer includes removed for serial-only version
#ifndef WIRETAP_CARDPUTER_ADV
#define WIRETAP_CARDPUTER_ADV 0
#endif

#include <Wire.h>
#include <SPI.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <esp_system.h>
#include <Preferences.h>
#if WIRETAP_CARDPUTER_ADV
#include <M5Cardputer.h>
#include <CypherPuterReturn.h>
namespace {
constexpr uint8_t HID_USAGE_DOWN_ARROW = 0x51;
constexpr uint8_t HID_USAGE_UP_ARROW = 0x52;
constexpr uint8_t HID_USAGE_LEFT_ARROW = 0x50;
constexpr uint8_t HID_USAGE_RIGHT_ARROW = 0x4F;
constexpr uint8_t ARDUINO_KEY_DOWN_ARROW = 0xD9;
constexpr uint8_t ARDUINO_KEY_UP_ARROW = 0xDA;
constexpr uint8_t ARDUINO_KEY_LEFT_ARROW = 0xD8;
constexpr uint8_t ARDUINO_KEY_RIGHT_ARROW = 0xD7;

constexpr uint16_t WT_BG = 0x0000;
constexpr uint16_t WT_PANEL = 0x1082;
constexpr uint16_t WT_PANEL_2 = 0x2104;
constexpr uint16_t WT_TEXT = 0xFFFF;
constexpr uint16_t WT_MUTED = 0xA514;
constexpr uint16_t WT_ACCENT = 0x07FF;
constexpr uint16_t WT_GOOD = 0x07E0;
constexpr uint16_t WT_WARN = 0xFD20;

bool cardputerHidContains(const Keyboard_Class::KeysState& keys, uint8_t hidUsage, uint8_t arduinoKey) {
    for(uint8_t key : keys.hid_keys) {
        if(key == hidUsage || key == arduinoKey) return true;
    }
    return false;
}
}
#else
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif
#include "src/SignalTools.h"
#include "src/PinSafety.h"

static void wireTapReturnToLauncher(uint32_t delayMs) {
#if WIRETAP_CARDPUTER_ADV
    cypherPuterReturnToLauncher(delayMs);
#else
    delay(delayMs);
    ESP.restart();
#endif
}

// WiFi AP variables removed for serial-only version

// -------- Display --------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#if WIRETAP_CARDPUTER_ADV
#ifndef SSD1306_SWITCHCAPVCC
#define SSD1306_SWITCHCAPVCC 0
#endif
#ifndef SSD1306_WHITE
#define SSD1306_WHITE 0xFFFF
#endif
#ifndef SSD1306_BLACK
#define SSD1306_BLACK 0x0000
#endif

class WireTapCardputerDisplay : public Print {
  public:
    bool begin(uint8_t, uint8_t) {
        auto cfg = M5.config();
        cfg.fallback_board = m5::board_t::board_M5CardputerADV;
        M5Cardputer.begin(cfg, true);
        M5Cardputer.Display.setRotation(1);
        M5Cardputer.Display.setBrightness(170);
        M5Cardputer.Display.setTextWrap(false);
        M5Cardputer.Display.fillScreen(SSD1306_BLACK);
        return true;
    }

    size_t write(uint8_t c) override {
        return M5Cardputer.Display.write(c);
    }

    void clearDisplay() {
        M5Cardputer.Display.fillScreen(SSD1306_BLACK);
    }

    void display() {}

    void setTextSize(uint8_t size) {
        M5Cardputer.Display.setTextSize(size);
    }

    void setTextColor(uint16_t color) {
        M5Cardputer.Display.setTextColor(color, SSD1306_BLACK);
    }

    void setTextColor(uint16_t color, uint16_t bg) {
        M5Cardputer.Display.setTextColor(color, bg);
    }

    void setCursor(int16_t x, int16_t y) {
        M5Cardputer.Display.setCursor(x, y);
    }
};

WireTapCardputerDisplay display;
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif
bool displayEnabled = false;
unsigned long lastDisplayUpdate = 0;
TwoWire TargetWire(1);

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

// -------- Display and target bus pins --------
#if WIRETAP_CARDPUTER_ADV
int PIN_DISP_SDA = -1, PIN_DISP_SCL = -1;
int PIN_I2C_SDA = 8, PIN_I2C_SCL = 9;
int PIN_SPI_MOSI = 14, PIN_SPI_MISO = 39, PIN_SPI_SCK = 40, PIN_SPI_CS = 5;
int PIN_UART_TX = 15, PIN_UART_RX = 13;
#else
int PIN_DISP_SDA = 5, PIN_DISP_SCL = 4;
int PIN_I2C_SDA = 21, PIN_I2C_SCL = 22;
int PIN_SPI_MOSI = 23, PIN_SPI_MISO = 19, PIN_SPI_SCK = 18, PIN_SPI_CS = 5;
int PIN_UART_TX = 17, PIN_UART_RX = 16;
#endif
uint32_t UART_BAUD = 115200;
uint32_t UART_CONFIG = SERIAL_8N1;
uint32_t I2C_FREQ = 100000;
uint32_t SPI_FREQ = 1000000;
uint8_t SPI_MODE_CFG = SPI_MODE0;
uint8_t SPI_BIT_ORDER = MSBFIRST;
bool I2C_PULLUPS = true;

// -------- Command history --------
#define CMD_HISTORY_SIZE 10
String cmdHistory[CMD_HISTORY_SIZE];
uint8_t cmdHistoryHead  = 0;
uint8_t cmdHistoryCount = 0;
int8_t  cmdHistoryPos   = -1;   // -1 = not browsing history

// -------- OLED Button Menu --------
#if WIRETAP_CARDPUTER_ADV
static const int8_t BTN_LEFT_PIN = -1;
static const int8_t BTN_CENTER_PIN = -1;
static const int8_t BTN_RIGHT_PIN = -1;
#else
static const uint8_t BTN_LEFT_PIN = 34;
static const uint8_t BTN_CENTER_PIN = 36;
static const uint8_t BTN_RIGHT_PIN = 39;
#endif
static const unsigned long BUTTON_DEBOUNCE_MS = 50;
static const unsigned long UI_REFRESH_MS = 500;

enum UiScreen {
    UI_MAIN_MENU,
    UI_GPIO_MENU,
    UI_STATUS_VIEW,
};

enum MainMenuItem {
    MENU_GPIO = 0,
    MENU_STATUS = 1,
    MENU_RETURN = 2
};

#if WIRETAP_CARDPUTER_ADV
static const uint8_t GPIO_MENU_PINS[] = {
    4, 6, 8, 9, 13, 14, 15, 39, 40, 5
};
#else
static const uint8_t GPIO_MENU_PINS[] = {
    0, 2, 12, 13, 14, 15, 16, 17, 18,
    19, 21, 22, 23, 25, 26, 32, 33
};
#endif
static const uint8_t GPIO_MENU_PIN_COUNT = sizeof(GPIO_MENU_PINS) / sizeof(GPIO_MENU_PINS[0]);
static const uint8_t GPIO_MENU_EXIT_INDEX = GPIO_MENU_PIN_COUNT;

struct Button {
    int8_t pin;
    bool lastRaw;
    bool stable;
    unsigned long lastChangeTime;
    bool pressEvent;
};

Button buttons[3] = {
    {BTN_LEFT_PIN,   true, true, 0, false},
    {BTN_CENTER_PIN, true, true, 0, false},
    {BTN_RIGHT_PIN,  true, true, 0, false}
};

const char* mainMenuItems[] = {
    "GPIO",
    "Status",
    "Launcher"
};
static const uint8_t MAIN_MENU_COUNT = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);

UiScreen uiScreen = UI_MAIN_MENU;
uint8_t mainMenuIndex = 0;
uint8_t gpioMenuIndex = 0;
bool gpioMenuDriven[GPIO_MENU_PIN_COUNT] = {false};
bool gpioMenuState[GPIO_MENU_PIN_COUNT] = {false};
bool screenDirty = true;
#if WIRETAP_CARDPUTER_ADV
bool uiBackEvent = false;
#endif

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
void initButtons();
void updateButtons();
void handleButtonPresses();
void initGpioMenuPins();
void stopActivePeripherals();
void setAllBusPinsInput();
void returnToLauncherFromWireTap(uint32_t delayMs = 250);
void serviceUARTRx();
void serviceI2CSlave();
void i2cMonitorStop(bool silent = false);
void i2cSlaveStop(bool silent = false);
std::vector<String> parseMacroTokens(const String& raw);
bool readLineBlocking(String promptText, String& out, unsigned long timeoutMs = 0);
std::vector<String> tok(const String& s);
std::vector<uint8_t> parseHexBytes(const std::vector<String>& v, size_t start);
bool parseHexByteToken(const String& token, uint8_t& value);
size_t uart_avail();
void flushUartBuffer();
size_t uart_popN(uint8_t* out, size_t maxN);
void setScreenDirty();
void renderCurrentScreen();
void renderMainMenu();
void renderGpioMenu();
void renderStatusView();
void toggleSelectedGpio();
void showPins();
void gpioAdc(int pin);
void gpioPwm(int pin, uint32_t freq, uint8_t dutyPct);
void gpioFreq(int pin);
void gpioPulse(int pin);
void gpioScope(int pin, uint16_t samples, uint32_t intervalUs);
void pinsCheck();
void signalFreqCmd(int pin, uint32_t windowMs);
void signalEdgesCmd(int pin, uint32_t windowMs);
void signalScopeCmd(int pin, uint16_t samples, uint32_t intervalUs);
void signalAdcCmd(int pin, uint16_t samples);
void signalPwmOutCmd(int pin, uint32_t freq, uint8_t dutyPct);
void configSave();
void configLoad();
void configReset();
void historyPush(const String& cmd);
String historyGet(int8_t age);

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

const char* getModeName(uint8_t m) {
    switch(m) {
        case HIZ: return "Hi-Z";
        case GPIO_MODE: return "GPIO";
        case I2C_MODE: return "I2C";
        case SPI_MODE: return "SPI";
        case UART_MODE: return "UART";
        default: return "Unknown";
    }
}

void showStatusBarLine() {
    if(!showStatusBar) return;

    String modeStr = String(getModeName(mode));
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

void setScreenDirty() {
    screenDirty = true;
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
#if WIRETAP_CARDPUTER_ADV
        display.println("WireTap-32");
        display.println("Cardputer EXT");
#else
        display.println("ESP32 Bus Pirate");
        display.println("v3.0 Buttons");
#endif
        display.println("Starting...");
        display.display();
#if WIRETAP_CARDPUTER_ADV
        printSuccess("Cardputer display initialized");
#else
        printSuccess("SSD1306 display initialized");
#endif
    } else {
        displayEnabled = false;
        printWarning("SSD1306 display not found");
    }
}

void renderMainMenu() {
#if WIRETAP_CARDPUTER_ADV
    auto& d = M5Cardputer.Display;
    d.fillScreen(WT_BG);
    d.fillRect(0, 0, d.width(), 24, 0x0186);
    d.setTextSize(1);
    d.setTextColor(WT_TEXT, 0x0186);
    d.setCursor(8, 5);
    d.print("WireTap-32");
    d.setTextColor(WT_ACCENT, 0x0186);
    d.setCursor(178, 5);
    d.print("EXT BENCH");

    d.setTextColor(WT_MUTED, WT_BG);
    d.setCursor(8, 28);
    d.print("3.3V only  ");
    d.print(getModeName(mode));

    for(uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
        int16_t y = 42 + i * 26;
        bool selected = i == mainMenuIndex;
        uint16_t bg = selected ? WT_ACCENT : WT_PANEL;
        uint16_t fg = selected ? WT_BG : WT_TEXT;
        d.fillRoundRect(7, y, d.width() - 14, 22, 4, bg);
        d.setTextColor(fg, bg);
        d.setCursor(15, y + 7);
        d.print(selected ? "> " : "  ");
        d.print(mainMenuItems[i]);
        d.setTextColor(selected ? WT_BG : WT_MUTED, bg);
        d.setCursor(130, y + 7);
        if(i == MENU_GPIO) d.print("browse/toggle pins");
        else if(i == MENU_STATUS) d.print("mode + buffers");
        else d.print("boot launcher");
    }

    d.fillRect(0, d.height() - 14, d.width(), 14, 0x1082);
    d.setTextColor(WT_MUTED, 0x1082);
    d.setCursor(8, d.height() - 11);
    d.print("arrows/WASD move  Enter select  Del back");
#else
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("== MAIN MENU ==");
    display.println();
    for(uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
        display.print(i == mainMenuIndex ? "> " : "  ");
        display.println(mainMenuItems[i]);
    }
    display.println();
    display.println("L/R:nav  C:select");
#endif
}

const char* gpioMenuLabel(uint8_t index) {
    if(index >= GPIO_MENU_PIN_COUNT) return "EXIT";
    uint8_t pin = GPIO_MENU_PINS[index];
    switch(pin) {
        case 0: return "IO0";
        case 2: return "IO2";
        case 4: return "IO4";
        case 5: return "IO5";
        case 6: return "IO6";
        case 8: return "IO8";
        case 9: return "IO9";
        case 12: return "IO12";
        case 13: return "IO13";
        case 14: return "IO14";
        case 15: return "IO15";
        case 16: return "IO16";
        case 17: return "IO17";
        case 18: return "IO18";
        case 19: return "IO19";
        case 21: return "IO21";
        case 22: return "IO22";
        case 23: return "IO23";
        case 25: return "IO25";
        case 26: return "IO26";
        case 32: return "IO32";
        case 33: return "IO33";
        case 39: return "IO39";
        case 40: return "IO40";
        default: return "IO?";
    }
}

String gpioMenuStateLabel(uint8_t index) {
    if(index >= GPIO_MENU_PIN_COUNT) return "EXIT";
    return digitalRead(GPIO_MENU_PINS[index]) == HIGH ? "HIGH" : "LOW";
}

const char* gpioMenuDriveLabel(uint8_t index) {
    if(index >= GPIO_MENU_PIN_COUNT) return "";
    return gpioMenuDriven[index] ? "OUTPUT" : "INPUT";
}

String gpioMenuHeader(uint8_t pin) {
#if WIRETAP_CARDPUTER_ADV
    switch(pin) {
        case 4: return "EXT INT";
        case 6: return "EXT BUSY";
        case 8: return "EXT I2C SDA";
        case 9: return "EXT I2C SCL";
        case 13: return "EXT UART RX";
        case 14: return "EXT SPI MOSI";
        case 15: return "EXT UART TX";
        case 39: return "EXT SPI MISO";
        case 40: return "EXT SPI SCK";
        case 5: return "EXT SPI CS";
        default: return "EXT";
    }
#else
    if(pin == 23 || pin == 19 || pin == 18 || pin == 25 || pin == 26) return "H1";
    if(pin == 13 || pin == 12 || pin == 14 || pin == 15 || pin == 21 || pin == 22 || pin == 17 || pin == 16) return "H2";
    if(pin == 33 || pin == 32 || pin == 2 || pin == 0) return "H3";
    return "??";
#endif
}

void renderGpioMenu() {
#if WIRETAP_CARDPUTER_ADV
    auto& d = M5Cardputer.Display;
    d.fillScreen(WT_BG);
    d.fillRect(0, 0, d.width(), 24, 0x0186);
    d.setTextSize(1);
    d.setTextColor(WT_TEXT, 0x0186);
    d.setCursor(8, 5);
    d.print("EXT GPIO");
    d.setTextColor(WT_ACCENT, 0x0186);
    d.setCursor(176, 5);
    d.printf("%u/%u", min<uint8_t>(gpioMenuIndex + 1, GPIO_MENU_PIN_COUNT), GPIO_MENU_PIN_COUNT);

    if(gpioMenuIndex >= GPIO_MENU_EXIT_INDEX) {
        d.fillRoundRect(8, 39, d.width() - 16, 54, 6, WT_PANEL);
        d.setTextSize(2);
        d.setTextColor(WT_ACCENT, WT_PANEL);
        d.setCursor(18, 51);
        d.print("Back to Menu");
        d.setTextSize(1);
        d.setTextColor(WT_MUTED, WT_PANEL);
        d.setCursor(18, 76);
        d.print("Enter returns to the WireTap menu");
    } else {
        uint8_t pin = GPIO_MENU_PINS[gpioMenuIndex];
        d.fillRoundRect(8, 32, 86, 70, 6, WT_PANEL);
        d.setTextColor(WT_MUTED, WT_PANEL);
        d.setCursor(18, 40);
        d.print(gpioMenuHeader(pin));
        d.setTextSize(3);
        d.setTextColor(WT_ACCENT, WT_PANEL);
        d.setCursor(18, 55);
        d.print("G");
        d.print(pin);
        d.setTextSize(1);
        d.setTextColor(WT_TEXT, WT_PANEL);
        d.setCursor(18, 88);
        d.print(gpioMenuStateLabel(gpioMenuIndex));
        d.print(" ");
        d.print(gpioMenuDriveLabel(gpioMenuIndex));

        d.fillRoundRect(102, 32, d.width() - 110, 70, 6, WT_PANEL_2);
        d.setTextColor(WT_TEXT, WT_PANEL_2);
        d.setCursor(112, 42);
        d.print(signalIsOutputCapablePin(pin) ? "Output capable" : "Input/read only");
        d.setTextColor(WT_MUTED, WT_PANEL_2);
        d.setCursor(112, 58);
        if(pin == PIN_I2C_SDA || pin == PIN_I2C_SCL) d.print("I2C default line");
        else if(pin == PIN_SPI_MOSI || pin == PIN_SPI_MISO || pin == PIN_SPI_SCK || pin == PIN_SPI_CS) d.print("SPI default line");
        else if(pin == PIN_UART_TX || pin == PIN_UART_RX) d.print("UART default line");
        else d.print("General EXT signal");
        d.setCursor(112, 76);
        d.print("Enter toggles outputs");
        d.setCursor(112, 88);
        d.print("Pins check in serial");
    }

    d.fillRect(0, 106, d.width(), 15, WT_BG);
    uint8_t first = gpioMenuIndex > 2 ? gpioMenuIndex - 2 : 0;
    if(first + 5 > GPIO_MENU_PIN_COUNT + 1) first = max<int16_t>(0, GPIO_MENU_PIN_COUNT + 1 - 5);
    for(uint8_t i = 0; i < 5 && first + i <= GPIO_MENU_EXIT_INDEX; i++) {
        uint8_t idx = first + i;
        int16_t x = 8 + i * 45;
        bool selected = idx == gpioMenuIndex;
        d.fillRoundRect(x, 107, 38, 14, 3, selected ? WT_ACCENT : WT_PANEL);
        d.setTextColor(selected ? WT_BG : WT_MUTED, selected ? WT_ACCENT : WT_PANEL);
        d.setCursor(x + 4, 110);
        d.print(gpioMenuLabel(idx));
    }

    d.fillRect(0, d.height() - 14, d.width(), 14, 0x1082);
    d.setTextColor(WT_MUTED, 0x1082);
    d.setCursor(8, d.height() - 11);
    d.print("arrows/WASD browse  Enter toggle  Del menu");
#else
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("== GPIO ==");
    display.println();
    if(gpioMenuIndex >= GPIO_MENU_EXIT_INDEX) {
        display.println("> EXIT");
        display.println();
        display.println("C: main menu");
        return;
    }

    uint8_t pin = GPIO_MENU_PINS[gpioMenuIndex];
    display.print("Pin ");
    display.println(gpioMenuLabel(gpioMenuIndex));
    display.println();
    display.print("State: ");
    display.println(gpioMenuStateLabel(gpioMenuIndex));
    display.print("Drive: ");
    display.println(gpioMenuDriveLabel(gpioMenuIndex));
    display.print("Header: ");
    display.println(gpioMenuHeader(pin));
    display.print("Item: ");
    display.print(gpioMenuIndex + 1);
    display.print("/");
    display.println(GPIO_MENU_PIN_COUNT);
    display.println();
    display.println("L/R: browse");
    display.println("C: toggle");
#endif
}

void renderCurrentScreen() {
    display.clearDisplay();
    display.setCursor(0, 0);

    switch(uiScreen) {
        case UI_MAIN_MENU: renderMainMenu(); break;
        case UI_GPIO_MENU: renderGpioMenu(); break;
        case UI_STATUS_VIEW: renderStatusView(); break;
    }
}

void displayUpdate() {
    if(!displayEnabled) return;

    bool dynamicScreen = (uiScreen == UI_GPIO_MENU || uiScreen == UI_STATUS_VIEW);
    if(!screenDirty) {
        if(!dynamicScreen) return;
        if(millis() - lastDisplayUpdate < UI_REFRESH_MS) return;
    }

    lastDisplayUpdate = millis();
    screenDirty = false;
    renderCurrentScreen();
    display.display();
}

void renderStatusView() {
#if WIRETAP_CARDPUTER_ADV
    auto& d = M5Cardputer.Display;
    d.fillScreen(WT_BG);
    d.fillRect(0, 0, d.width(), 24, 0x0186);
    d.setTextSize(1);
    d.setTextColor(WT_TEXT, 0x0186);
    d.setCursor(8, 5);
    d.print("WireTap Status");
    d.setTextColor(WT_ACCENT, 0x0186);
    d.setCursor(176, 5);
    d.print(getModeName(mode));

    auto card = [&](int16_t x, int16_t y, int16_t w, const char* label, const String& value, uint16_t color) {
        d.fillRoundRect(x, y, w, 32, 5, WT_PANEL);
        d.setTextColor(WT_MUTED, WT_PANEL);
        d.setCursor(x + 7, y + 6);
        d.print(label);
        d.setTextColor(color, WT_PANEL);
        d.setCursor(x + 7, y + 19);
        d.print(value);
    };

    card(8, 34, 70, "HEAP", String(ESP.getFreeHeap() / 1024) + " KB", WT_GOOD);
    card(86, 34, 70, "UPTIME", String(millis() / 1000) + " s", WT_TEXT);
    card(164, 34, 68, "UART RX", String(uart_avail()) + " B", uart_avail() ? WT_WARN : WT_MUTED);
    card(8, 74, 108, "I2C", "G" + String(PIN_I2C_SDA) + "/G" + String(PIN_I2C_SCL) + " " + String(I2C_FREQ / 1000) + "k", mode == I2C_MODE ? WT_ACCENT : WT_TEXT);
    card(124, 74, 108, "SPI", "G" + String(PIN_SPI_SCK) + "/G" + String(PIN_SPI_MOSI) + "/G" + String(PIN_SPI_MISO), mode == SPI_MODE ? WT_ACCENT : WT_TEXT);

    d.fillRect(0, d.height() - 14, d.width(), 14, 0x1082);
    d.setTextColor(WT_MUTED, 0x1082);
    d.setCursor(8, d.height() - 11);
    d.print("any key returns to menu  Q/Tab launcher");
#else
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("== STATUS ==");
    display.println();
    display.print("Mode: ");
    display.println(getModeName(mode));
    display.print("Heap: ");
    display.print(ESP.getFreeHeap() / 1024);
    display.println("KB");
    display.print("Uptime: ");
    display.print(millis() / 1000);
    display.println("s");
    display.print("UART RX: ");
    display.print(uart_avail());
    display.println("B");
    display.setCursor(0, 56);
    display.println("Any key: back");
#endif
}

void initGpioMenuPins() {
    for(uint8_t i = 0; i < GPIO_MENU_PIN_COUNT; i++) {
        pinMode(GPIO_MENU_PINS[i], INPUT);
        gpioMenuDriven[i] = false;
        gpioMenuState[i] = false;
    }
}

void toggleSelectedGpio() {
    if(gpioMenuIndex >= GPIO_MENU_EXIT_INDEX) {
        uiScreen = UI_MAIN_MENU;
        setScreenDirty();
        return;
    }

    uint8_t pin = GPIO_MENU_PINS[gpioMenuIndex];
    if(!signalIsOutputCapablePin(pin)) {
        printWarning("GPIO" + String(pin) + " is input-only or reserved on this profile");
        setScreenDirty();
        return;
    }
    bool nextState = !gpioMenuState[gpioMenuIndex];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, nextState ? HIGH : LOW);
    gpioMenuDriven[gpioMenuIndex] = true;
    gpioMenuState[gpioMenuIndex] = nextState;
    setScreenDirty();
}

void handleButtonPresses() {
    bool left = buttons[0].pressEvent;
    bool center = buttons[1].pressEvent;
    bool right = buttons[2].pressEvent;
#if WIRETAP_CARDPUTER_ADV
    bool back = uiBackEvent;
    uiBackEvent = false;
#else
    bool back = false;
#endif

    if(!left && !center && !right && !back) return;

    if(uiScreen == UI_MAIN_MENU) {
        if(back) {
            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("Returning to");
            display.println("Cypher Putter OS");
            display.display();
            returnToLauncherFromWireTap(500);
            return;
        }
        if(left) {
            mainMenuIndex = (mainMenuIndex + MAIN_MENU_COUNT - 1) % MAIN_MENU_COUNT;
            setScreenDirty();
        }
        if(right) {
            mainMenuIndex = (mainMenuIndex + 1) % MAIN_MENU_COUNT;
            setScreenDirty();
        }
        if(center) {
            if(mainMenuIndex == MENU_GPIO) {
                uiScreen = UI_GPIO_MENU;
                gpioMenuIndex = 0;
            } else if(mainMenuIndex == MENU_STATUS) {
                uiScreen = UI_STATUS_VIEW;
            } else {
                display.clearDisplay();
                display.setCursor(0, 0);
                display.println("Returning to");
                display.println("Cypher Putter OS");
                display.display();
                returnToLauncherFromWireTap(500);
            }
            setScreenDirty();
        }
        displayUpdate();
        return;
    }

    if(uiScreen == UI_GPIO_MENU) {
        if(back) {
            uiScreen = UI_MAIN_MENU;
            mainMenuIndex = MENU_GPIO;
            setScreenDirty();
            displayUpdate();
            return;
        }
        if(left) {
            gpioMenuIndex = (gpioMenuIndex + GPIO_MENU_PIN_COUNT) % (GPIO_MENU_PIN_COUNT + 1);
            setScreenDirty();
        }
        if(right) {
            gpioMenuIndex = (gpioMenuIndex + 1) % (GPIO_MENU_PIN_COUNT + 1);
            setScreenDirty();
        }
        if(center) {
            toggleSelectedGpio();
        }
        displayUpdate();
        return;
    }

    if(uiScreen == UI_STATUS_VIEW) {
        if(back && uiScreen == UI_STATUS_VIEW) {
            returnToLauncherFromWireTap(250);
            return;
        }
        uiScreen = UI_MAIN_MENU;
        mainMenuIndex = MENU_GPIO;
        setScreenDirty();
        displayUpdate();
    }
}

void stopActivePeripherals() {
    i2cMonitorStop(true);
    if(i2cSlaveMode) i2cSlaveStop(true);
    TargetWire.end();
    SPI.end();
    TargetUART.end();
}

void setAllBusPinsInput() {
    pinMode(PIN_I2C_SDA, INPUT);
    pinMode(PIN_I2C_SCL, INPUT);
    pinMode(PIN_SPI_MOSI, INPUT);
    pinMode(PIN_SPI_MISO, INPUT);
    pinMode(PIN_SPI_SCK, INPUT);
    pinMode(PIN_SPI_CS, INPUT);
    pinMode(PIN_UART_TX, INPUT);
    pinMode(PIN_UART_RX, INPUT);
}

void returnToLauncherFromWireTap(uint32_t delayMs) {
    stopActivePeripherals();
    setAllBusPinsInput();
    wireTapReturnToLauncher(delayMs);
}

void setHiZ() {
    printInfo("Setting Hi-Z mode...");

    stopActivePeripherals();
    setAllBusPinsInput();

    mode = HIZ;
    printSuccess("Hi-Z mode active - All pins safe");
    setScreenDirty();
    displayUpdate();
}

void enterGpioMode() {
    printInfo("Setting GPIO mode...");
    stopActivePeripherals();
    setAllBusPinsInput();

    mode = GPIO_MODE;
    printSuccess("GPIO mode active - use gpio set/get");
    setScreenDirty();
    displayUpdate();
}

// -------- I2C --------
void i2cBegin() {
    TargetWire.end();
    safeYield();
    
    if(I2C_PULLUPS) {
        pinMode(PIN_I2C_SDA, INPUT_PULLUP);
        pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    }
    
    if (!TargetWire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ)) {
        printError("ERROR: I2C begin failed!");
        return;
    }
    TargetWire.setTimeout(500); // Shorter timeout to prevent hanging
    mode = I2C_MODE;
    printSuccess("I2C mode active - " + String(I2C_FREQ/1000) + "kHz, pullups " + String(I2C_PULLUPS ? "ON" : "OFF"));
    setScreenDirty();
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

        TargetWire.beginTransmission(addr);
        uint8_t error = TargetWire.endTransmission();
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
    
    TargetWire.beginTransmission(addr);
    size_t written = TargetWire.write(bytes.data(), bytes.size());
    uint8_t rc = TargetWire.endTransmission();
    
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
    
    uint8_t received = TargetWire.requestFrom((int)addr, (int)n);
    std::vector<uint8_t> buf;
    buf.reserve(n);
    
    unsigned long timeout = millis() + 100;
    while(TargetWire.available() && buf.size() < n && millis() < timeout) {
        buf.push_back(TargetWire.read());
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
    TargetWire.beginTransmission(addr);
    return TargetWire.endTransmission() == 0;
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
    // Display controllers
    if(addr == 0x3C || addr == 0x3D) return "SSD1306/SH1106/SSD1309 OLED display";
    if(addr == 0x27 || addr == 0x3F) return "PCF8574 LCD backpack (HD44780)";
    // EEPROMs
    if(addr >= 0x50 && addr <= 0x57) return "24xx series EEPROM (24C02..24C512)";
    // RTC / IMU
    if(addr == 0x68) return "DS3231/DS1307 RTC or MPU-6050/MPU-6500 IMU";
    if(addr == 0x69) return "MPU-6050/MPU-9250 IMU (AD0=HIGH)";
    if(addr == 0x1C || addr == 0x1D) return "ADXL345 or MMA8452 accelerometer";
    if(addr == 0x1E) return "HMC5883L / QMC5883 magnetometer";
    if(addr == 0x53) return "ADXL345 accelerometer (ALT addr)";
    if(addr == 0x19) return "LSM303 accelerometer or LIS3DH";
    if(addr == 0x1F) return "LSM303 magnetometer";
    if(addr == 0x6A || addr == 0x6B) return "LSM6DS3/LSM9DS1 IMU";
    // Environmental sensors
    if(addr == 0x76 || addr == 0x77) return "BME280/BMP280/BMP388 pressure/temp/humidity";
    if(addr == 0x44 || addr == 0x45) return "SHT31/SHT35 temp+humidity sensor";
    if(addr == 0x38) return "AHT10/AHT20/AHT21 temp+humidity sensor";
    if(addr == 0x40) return "INA219 power monitor or Si7021 humidity sensor";
    if(addr == 0x41) return "INA219 power monitor (A0=HIGH)";
    if(addr == 0x59) return "SGP30 / ENS160 air quality sensor";
    if(addr == 0x52) return "ENS160 air quality sensor";
    if(addr == 0x29) return "VL53L0X / VL53L1X ToF distance sensor or TCS34725 color";
    if(addr == 0x39) return "APDS-9960 gesture/proximity/color sensor";
    if(addr == 0x23 || addr == 0x5C) return "BH1750 ambient light sensor";
    if(addr == 0x36) return "MAX17048/MAX17049 LiPo fuel gauge";
    if(addr == 0x48 || addr == 0x49 || addr == 0x4A || addr == 0x4B) return "ADS1115/ADS1015 ADC or TMP102 temp sensor";
    // IO expanders
    if(addr == 0x20 || addr == 0x21) return "MCP23017 16-bit IO expander or PCF8574";
    if(addr == 0x22 || addr == 0x23) return "PCF8574 IO expander";
    if(addr == 0x24 || addr == 0x25 || addr == 0x26) return "PCF8574 IO expander";
    // DAC / other
    if(addr == 0x60) return "MCP4725 12-bit DAC or Si5351 clock gen";
    if(addr == 0x62 || addr == 0x63) return "Si5351 clock generator";
    if(addr == 0x70) return "TCA9548A I2C multiplexer (all channels)";
    if(addr >= 0x71 && addr <= 0x77) return "TCA9548A I2C multiplexer";
    if(addr == 0x61) return "MCP4725 DAC (A0=HIGH)";
    // Motor drivers
    if(addr == 0x60 || addr == 0x61) return "PCA9685 16-ch PWM or MCP4725 DAC";
    if(addr == 0x40 || addr == 0x41 || addr == 0x42 || addr == 0x43) return "PCA9685 PWM driver or INA21x monitor";
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
    TargetWire.beginTransmission(addr);
    TargetWire.write(0x00);
    if(TargetWire.endTransmission(false) == 0) {
        uint8_t got = TargetWire.requestFrom((int)addr, 2);
        if(got > 0) {
            uint8_t a = TargetWire.read();
            uint8_t b = (got > 1) ? TargetWire.read() : 0xFF;
            printData("  Peek: 0x" + String(a, HEX) + " 0x" + String(b, HEX));
        }
    }
}

bool i2cReadRegister(uint8_t addr, uint8_t reg, size_t len, std::vector<uint8_t>& out, bool verbose = true) {
    out.clear();
    TargetWire.beginTransmission(addr);
    TargetWire.write(reg);
    if(TargetWire.endTransmission(false) != 0) {
        if(verbose) printError("I2C READ REG failed to write register 0x" + String(reg, HEX));
        return false;
    }

    uint8_t received = TargetWire.requestFrom((int)addr, (int)len);
    unsigned long timeout = millis() + 200;
    while(TargetWire.available() && out.size() < len && millis() < timeout) {
        out.push_back(TargetWire.read());
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
    uint8_t got = TargetWire.requestFrom((int)addr, (int)len);
    unsigned long timeout = millis() + 200;
    while(TargetWire.available() && out.size() < len && millis() < timeout) {
        out.push_back(TargetWire.read());
    }
    if(out.empty()) {
        printWarning("I2C READ <- 0x" + String(addr, HEX) + " [no data]");
        return false;
    }
    printData("I2C READ <- " + toHex(out.data(), out.size()));
    return true;
}

void i2cWriteRegister(uint8_t addr, uint8_t reg, const std::vector<uint8_t>& values) {
    TargetWire.beginTransmission(addr);
    uint8_t written = TargetWire.write(reg);
    if(!values.empty()) {
        written += TargetWire.write(values.data(), values.size());
    }
    uint8_t rc = TargetWire.endTransmission();

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
        TargetWire.beginTransmission(addr);
        TargetWire.write((uint8_t)(offset & 0xFF));
        if(TargetWire.endTransmission(false) != 0) {
            printError("  Failed to set address pointer at 0x" + String(offset, HEX));
            break;
        }

        uint8_t got = TargetWire.requestFrom((int)addr, (int)chunk);
        std::vector<uint8_t> data;
        while(got-- && TargetWire.available()) data.push_back(TargetWire.read());

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
        TargetWire.beginTransmission(addr);
        TargetWire.write((uint8_t)(i & 0xFF));
        TargetWire.write(value);
        uint8_t rc = TargetWire.endTransmission();
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

    TargetWire.end();
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
    TargetWire.end();
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
            TargetWire.beginTransmission(addr);
            TargetWire.write((uint8_t)(offset >> 8));
            TargetWire.write((uint8_t)(offset & 0xFF));
            if(TargetWire.endTransmission(false) != 0) {
                printError("Failed to set offset");
                continue;
            }
            uint8_t got = TargetWire.requestFrom((int)addr, (int)len);
            std::vector<uint8_t> data;
            while(got-- && TargetWire.available()) data.push_back(TargetWire.read());
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
            TargetWire.beginTransmission(addr);
            TargetWire.write((uint8_t)(offset >> 8));
            TargetWire.write((uint8_t)(offset & 0xFF));
            TargetWire.write(bytes.data(), bytes.size());
            uint8_t rc = TargetWire.endTransmission();
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
            TargetWire.beginTransmission(addr);
            TargetWire.write((uint8_t)(offset >> 8));
            TargetWire.write((uint8_t)(offset & 0xFF));
            TargetWire.write(bytes.data(), bytes.size());
            uint8_t rc = TargetWire.endTransmission();
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
    while(len-- > 0 && TargetWire.available()) {
        uint8_t b = TargetWire.read();
        data.push_back(b);
    }
    if(!data.empty()) line += toHex(data.data(), data.size());
    else line += "(none)";
    if(i2cSlaveLog.size() > 16) i2cSlaveLog.pop_front();
    i2cSlaveLog.push_back(line);
}

void i2cSlaveRequest() {
    TargetWire.write(i2cSlaveTxValue);
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
    TargetWire.end();
    delay(5);
    TargetWire.begin(addr, PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
    TargetWire.onReceive(i2cSlaveReceive);
    TargetWire.onRequest(i2cSlaveRequest);
    i2cSlaveMode = true;
    i2cSlaveAddress = addr;
    i2cSlaveEnd = durationMs ? millis() + durationMs : 0;
    i2cSlaveLog.clear();
    printInfo("I2C slave monitor enabled at 0x" + String(addr, HEX));
    printInfo("Press ENTER to stop");
}

void i2cSlaveStop(bool silent) {
    if(!i2cSlaveMode) return;
    TargetWire.onReceive(nullptr);
    TargetWire.onRequest(nullptr);
    TargetWire.end();
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
    mode = SPI_MODE;
    println("SPI mode active - " + String(SPI_FREQ/1000) + "kHz mode " + String(SPI_MODE_CFG) + (SPI_BIT_ORDER == MSBFIRST ? " MSB" : " LSB"));
    setScreenDirty();
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
    printWarning("Write commands modify the target chip. Start with read/status when probing unknown parts.");
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
    printWarning("Read-only helper: identify and dump before making changes with external tools.");
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
    printWarning("SPI sniff is not supported on a bare ESP32 dev board.");
    printInfo("Use 'signal scope' or 'signal edges' for simple line checks, or an external logic analyzer for passive SPI decode.");
}

void spiSlaveMonitor() {
    printWarning("SPI slave monitor is not supported on this bare ESP32 build.");
    printInfo("WireTap-32 can actively transfer with 'spi x', but passive multi-line SPI capture needs dedicated analyzer hardware.");
}

void spiSdcardShell() {
    printWarning("SPI SD card helper is not implemented for the dev-board-only build.");
    printInfo("Use 'spi x' for manual transfers or 'spi flash'/'spi eeprom' for supported chip helpers.");
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

    mode = UART_MODE;
    println("UART mode active - " + String(UART_BAUD) + " baud");
    setScreenDirty();
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

void initButtons() {
#if WIRETAP_CARDPUTER_ADV
    for(uint8_t i = 0; i < 3; i++) {
        buttons[i].pressEvent = false;
    }
#else
    for(uint8_t i = 0; i < 3; i++) {
        pinMode(buttons[i].pin, INPUT);
        buttons[i].lastRaw = digitalRead(buttons[i].pin);
        buttons[i].stable = buttons[i].lastRaw;
        buttons[i].lastChangeTime = millis();
        buttons[i].pressEvent = false;
    }
#endif
}

void updateButtons() {
#if WIRETAP_CARDPUTER_ADV
    for(uint8_t i = 0; i < 3; i++) {
        buttons[i].pressEvent = false;
    }
    uiBackEvent = false;

    M5Cardputer.update();
    bool left = false;
    bool center = M5Cardputer.BtnA.wasClicked();
    bool right = false;
    bool back = false;

    if(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
        center = center || keys.enter || keys.space;
        back = back || keys.del || keys.tab;
        left = left || cardputerHidContains(keys, HID_USAGE_UP_ARROW, ARDUINO_KEY_UP_ARROW);
        left = left || cardputerHidContains(keys, HID_USAGE_LEFT_ARROW, ARDUINO_KEY_LEFT_ARROW);
        right = right || cardputerHidContains(keys, HID_USAGE_DOWN_ARROW, ARDUINO_KEY_DOWN_ARROW);
        right = right || cardputerHidContains(keys, HID_USAGE_RIGHT_ARROW, ARDUINO_KEY_RIGHT_ARROW);
        for(auto c : keys.word) {
            if(c == ',' || c == ';' || c == 'w' || c == 'W' || c == 'k' || c == 'K' ||
               c == 'a' || c == 'A' || c == 'h' || c == 'H') left = true;
            if(c == '.' || c == '/' || c == 's' || c == 'S' || c == 'j' || c == 'J' ||
               c == 'd' || c == 'D' || c == 'l' || c == 'L') right = true;
            if(c == ' ' || c == 'e' || c == 'E') center = true;
            if(c == '`' || c == 'q' || c == 'Q' || c == 'b' || c == 'B') back = true;
        }

        for(const auto& key : M5Cardputer.Keyboard.keyList()) {
            if(key.y == 3 && key.x == 10) left = true;
            if(key.y == 3 && key.x == 12) right = true;
            if(key.y == 2 && key.x == 11) left = true;
            if(key.y == 3 && key.x == 11) right = true;
        }
    }

    buttons[0].pressEvent = left;
    buttons[1].pressEvent = center;
    buttons[2].pressEvent = right;
    uiBackEvent = back;
#else
    unsigned long now = millis();

    for(uint8_t i = 0; i < 3; i++) {
        bool raw = digitalRead(buttons[i].pin);
        buttons[i].pressEvent = false;

        if(raw != buttons[i].lastRaw) {
            buttons[i].lastChangeTime = now;
            buttons[i].lastRaw = raw;
        }

        if((now - buttons[i].lastChangeTime) <= BUTTON_DEBOUNCE_MS) {
            continue;
        }

        if(raw != buttons[i].stable) {
            if(raw == LOW && buttons[i].stable == HIGH) {
                buttons[i].pressEvent = true;
            }
            buttons[i].stable = raw;
        }
    }
#endif
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

// -------- GPIO extended --------
void gpioAdc(int pin) {
    signalAdcCmd(pin, 1);
}

void gpioPwm(int pin, uint32_t freq, uint8_t dutyPct) {
    signalPwmOutCmd(pin, freq, dutyPct);
}

void gpioFreq(int pin) {
    signalFreqCmd(pin, 1000);
}

void gpioPulse(int pin) {
    if(pin < 0 || pin > 39) { printError("Invalid pin"); return; }
    pinMode(pin, INPUT);
    unsigned long w = pulseIn(pin, HIGH, 100000UL);
    if(w == 0) {
        printWarning("No HIGH pulse on pin " + String(pin) + " within 100 ms");
        return;
    }
    printData("Pin " + String(pin) + ": pulse width = " + String(w) + " us");
}

void gpioScope(int pin, uint16_t samples, uint32_t intervalUs) {
    signalScopeCmd(pin, samples, intervalUs);
}

// -------- NVS Config persistence --------
void configSave() {
    Preferences prefs;
    prefs.begin("wiretap32", false);
    prefs.putInt("disp_sda",  PIN_DISP_SDA);
    prefs.putInt("disp_scl",  PIN_DISP_SCL);
    prefs.putInt("i2c_sda",   PIN_I2C_SDA);
    prefs.putInt("i2c_scl",   PIN_I2C_SCL);
    prefs.putInt("spi_mosi",  PIN_SPI_MOSI);
    prefs.putInt("spi_miso",  PIN_SPI_MISO);
    prefs.putInt("spi_sck",   PIN_SPI_SCK);
    prefs.putInt("spi_cs",    PIN_SPI_CS);
    prefs.putInt("uart_tx",   PIN_UART_TX);
    prefs.putInt("uart_rx",   PIN_UART_RX);
    prefs.putUInt("uart_baud", UART_BAUD);
    prefs.putUInt("i2c_freq",  I2C_FREQ);
    prefs.putUInt("spi_freq",  SPI_FREQ);
    prefs.putBool("colors",    useColors);
    prefs.putBool("statusbar", showStatusBar);
    prefs.end();
    printSuccess("Config saved to flash (NVS)");
}

void configLoad() {
    Preferences prefs;
    prefs.begin("wiretap32", true);
    if(!prefs.isKey("i2c_sda")) {
        prefs.end();
        printWarning("No saved config found — use 'config save' first");
        return;
    }
    PIN_DISP_SDA  = prefs.getInt("disp_sda",   PIN_DISP_SDA);
    PIN_DISP_SCL  = prefs.getInt("disp_scl",   PIN_DISP_SCL);
    PIN_I2C_SDA   = prefs.getInt("i2c_sda",    PIN_I2C_SDA);
    PIN_I2C_SCL   = prefs.getInt("i2c_scl",    PIN_I2C_SCL);
    PIN_SPI_MOSI  = prefs.getInt("spi_mosi",   PIN_SPI_MOSI);
    PIN_SPI_MISO  = prefs.getInt("spi_miso",   PIN_SPI_MISO);
    PIN_SPI_SCK   = prefs.getInt("spi_sck",    PIN_SPI_SCK);
    PIN_SPI_CS    = prefs.getInt("spi_cs",     PIN_SPI_CS);
    PIN_UART_TX   = prefs.getInt("uart_tx",    PIN_UART_TX);
    PIN_UART_RX   = prefs.getInt("uart_rx",    PIN_UART_RX);
    UART_BAUD     = prefs.getUInt("uart_baud", UART_BAUD);
    I2C_FREQ      = prefs.getUInt("i2c_freq",  I2C_FREQ);
    SPI_FREQ      = prefs.getUInt("spi_freq",  SPI_FREQ);
    useColors     = prefs.getBool("colors",    useColors);
    showStatusBar = prefs.getBool("statusbar", showStatusBar);
    prefs.end();
    printSuccess("Config loaded from flash (NVS)");
    showPins();
}

void configReset() {
    Preferences prefs;
    prefs.begin("wiretap32", false);
    prefs.clear();
    prefs.end();
    printSuccess("Saved config cleared from flash");
}

// -------- Command history helpers --------
void historyPush(const String& cmd) {
    if(cmd.length() == 0) return;
    if(cmdHistoryCount > 0) {
        uint8_t lastIdx = (cmdHistoryHead + CMD_HISTORY_SIZE - 1) % CMD_HISTORY_SIZE;
        if(cmdHistory[lastIdx] == cmd) return;  // skip duplicate of last entry
    }
    cmdHistory[cmdHistoryHead] = cmd;
    cmdHistoryHead = (cmdHistoryHead + 1) % CMD_HISTORY_SIZE;
    if(cmdHistoryCount < CMD_HISTORY_SIZE) cmdHistoryCount++;
}

String historyGet(int8_t age) {
    if(age < 0 || age >= (int8_t)cmdHistoryCount) return "";
    uint8_t idx = (cmdHistoryHead + CMD_HISTORY_SIZE - 1 - (uint8_t)age) % CMD_HISTORY_SIZE;
    return cmdHistory[idx];
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
    println("  pins check        - Audit unsafe pins, input-only pins, and conflicts");
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
    println("SIGNAL COMMANDS: (3.3V only, simple ESP32 sampler)");
    println("  signal freq <pin> [ms]          - Frequency/duty estimate");
    println("  signal edges <pin> [ms]         - Count rising/falling edges");
    println("  signal scope <pin> [samples] [us] - ASCII waveform capture");
    println("  signal adc <pin> [samples]      - ADC min/avg/max voltage");
    println("  signal pwmout <pin> <hz> <duty%> - Generate PWM test signal");
    println("");
    println("UART COMMANDS: (requires 'mode uart' first)");
    println("  uart baud|scan|ping|read|write|bridge|spam|at");
    println("  uart tx <data>    - Send: 'uart tx \"Hello\"' or hex bytes");
    println("  uart rx <len>     - Read bytes once");
    println("  uart config baud|format|pins ...");
    println("  ['Hello' r:64]    - UART macro syntax");
    println("");
    println("GPIO COMMANDS: (requires 'mode gpio' first)");
    println("  gpio set <pin> <val>              - Set output HIGH/LOW: 'gpio set 2 1'");
    println("  gpio get <pin>                    - Read digital input: 'gpio get 4'");
    println("  gpio adc <pin>                    - Read analog voltage (ADC pins only)");
    println("  gpio pwm <pin> <freq> <duty%>     - PWM output: 'gpio pwm 2 1000 50'");
    println("  gpio freq <pin>                   - Measure signal frequency (1s window)");
    println("  gpio pulse <pin>                  - Measure HIGH pulse width (us)");
    println("  gpio scope <pin> [samples] [us]   - ASCII waveform capture");
    println("");
    println("CONFIG COMMANDS:");
    println("  config save   - Persist pins/baud/colors to flash (NVS)");
    println("  config load   - Restore saved config from flash");
    println("  config reset  - Erase saved config from flash");
    println("");
    println("TIPS:");
#if WIRETAP_CARDPUTER_ADV
    println("  Cardputer nav: ,/; or W/K = left, ./ or S/J = right, Enter/BtnA = select");
    println("  Launcher menu item, launcher, or return goes back to Cypher Putter OS");
    println("  EXT pins: SPI 40/14/39/5, I2C 8/9, UART TX15 RX13. 3.3V logic only.");
#endif
    println("  Backspace works  |  Up/Down arrows browse command history");
    println("  <Enter> repeats last command  |  3.3V max, 12mA max per pin");
}

void showPins() {
    println("=== Pin Assignments ===");
#if WIRETAP_CARDPUTER_ADV
    println("Display: built-in M5Stack Cardputer ST7789 + TCA8418 keyboard");
    println("EXT: reset=G3 reserved, INT=G4 input, BUSY=G6 input, 5VIN/5VOUT/GND are not GPIO");
#else
    println("DISP: SDA=" + String(PIN_DISP_SDA) + "  SCL=" + String(PIN_DISP_SCL) + "  (OLED Wire bus)");
#endif
    println("I2C:  SDA=" + String(PIN_I2C_SDA)  + "  SCL=" + String(PIN_I2C_SCL)  + "  Freq=" + String(I2C_FREQ/1000) + "kHz  Pullups=" + String(I2C_PULLUPS ? "ON" : "OFF"));
    println("SPI:  MOSI=" + String(PIN_SPI_MOSI) + " MISO=" + String(PIN_SPI_MISO) + " SCK=" + String(PIN_SPI_SCK) + " CS=" + String(PIN_SPI_CS) + "  Freq=" + String(SPI_FREQ/1000) + "kHz");
    println("UART: RX=" + String(PIN_UART_RX) + "    TX=" + String(PIN_UART_TX) + "    Baud=" + String(UART_BAUD));
#if WIRETAP_CARDPUTER_ADV
    println("Use 'pins set <name> <pin>' — names: sda,scl,mosi,miso,sck,cs,tx,rx");
#else
    println("Use 'pins set <name> <pin>' — names: sda,scl,mosi,miso,sck,cs,tx,rx,disp-sda,disp-scl");
#endif
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

void pinsCheck() {
    WireTapPinConfig cfg = {
        PIN_DISP_SDA,
        PIN_DISP_SCL,
        PIN_I2C_SDA,
        PIN_I2C_SCL,
        PIN_SPI_MOSI,
        PIN_SPI_MISO,
        PIN_SPI_SCK,
        PIN_SPI_CS,
        PIN_UART_TX,
        PIN_UART_RX,
        getModeName(mode)
    };
    print(buildPinSafetyReport(cfg));
}

void signalFreqCmd(int pin, uint32_t windowMs) {
    if(!signalIsSafeDigitalPin(pin)) {
        printError("Invalid or unsafe GPIO for signal measurement");
        printInfo("Avoid GPIO 6-11. Use 3.3V logic only.");
        return;
    }
    SignalFrequencyResult result = signalMeasureFrequency(pin, windowMs);
    if(!result.valid) {
        printError("Signal frequency measurement failed");
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "GPIO%d freq=%.2fHz duty=%.1f%% edges R:%lu F:%lu avg HIGH:%luus LOW:%luus window:%lums",
             pin,
             result.frequencyHz,
             result.dutyPct,
             (unsigned long)result.risingEdges,
             (unsigned long)result.fallingEdges,
             (unsigned long)result.avgHighUs,
             (unsigned long)result.avgLowUs,
             (unsigned long)(result.durationUs / 1000));
    printData(String(buf));
    if(result.risingEdges == 0 && result.fallingEdges == 0) {
        printWarning("No edges detected; signal may be stuck or outside this simple sampler's range.");
    }
}

void signalEdgesCmd(int pin, uint32_t windowMs) {
    if(!signalIsSafeDigitalPin(pin)) {
        printError("Invalid or unsafe GPIO for edge count");
        return;
    }
    SignalEdgeResult result = signalCountEdges(pin, windowMs);
    if(!result.valid) {
        printError("Signal edge count failed");
        return;
    }
    char buf[104];
    snprintf(buf, sizeof(buf), "GPIO%d edges R:%lu F:%lu start:%s end:%s window:%lums",
             pin,
             (unsigned long)result.risingEdges,
             (unsigned long)result.fallingEdges,
             result.startedHigh ? "HIGH" : "LOW",
             result.endedHigh ? "HIGH" : "LOW",
             (unsigned long)(result.durationUs / 1000));
    printData(String(buf));
    if(result.risingEdges == 0 && result.fallingEdges == 0) {
        printWarning("No transitions detected.");
    }
}

void signalScopeCmd(int pin, uint16_t samples, uint32_t intervalUs) {
    String wave;
    String ruler;
    if(!signalBuildScope(pin, samples, intervalUs, wave, ruler)) {
        printError("Invalid or unsafe GPIO for scope capture");
        return;
    }
    printInfo("Sampling GPIO" + String(pin) + " x" + String(samples) + " @ " + String(intervalUs) + "us");
    printData(wave);
    printInfo(ruler);
}

void signalAdcCmd(int pin, uint16_t samples) {
    if(!signalIsAdcCapablePin(pin)) {
        printError("GPIO" + String(pin) + " is not ADC-capable on ESP32");
        printInfo("ADC pins: 32,33,34,35,36,39 (input-only) or 0,2,4,12-15,25-27");
        return;
    }
    SignalAdcStats result = signalMeasureAdc(pin, samples);
    if(!result.valid) {
        printError("ADC read failed");
        return;
    }
    char buf[144];
    snprintf(buf, sizeof(buf), "GPIO%d ADC samples=%u raw min/avg/max=%d/%.1f/%d voltage min/avg/max=%.3f/%.3f/%.3fV",
             pin,
             result.samples,
             result.minRaw,
             result.avgRaw,
             result.maxRaw,
             result.minVoltage,
             result.avgVoltage,
             result.maxVoltage);
    printData(String(buf));
}

void signalPwmOutCmd(int pin, uint32_t freq, uint8_t dutyPct) {
    String error;
    if(!signalStartPwm(pin, freq, dutyPct, error)) {
        printError("PWM output failed: " + error);
        return;
    }
    printSuccess("PWM GPIO" + String(pin) + ": " + String(freq) + "Hz duty=" + String(dutyPct) + "%");
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
    if(c=="launcher"||c=="return") {
        printInfo("Returning to Cypher Putter OS");
        returnToLauncherFromWireTap(250);
        return;
    }

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
        else if(m=="gpio"||m=="g") { enterGpioMode(); }
        else if(m=="i2c"||m=="i") { i2cBegin(); }
        else if(m=="spi"||m=="s") { spiBegin(); }
        else if(m=="uart"||m=="u") { uartBegin(); }
        else { println("ERROR: Invalid mode. Use: hiz|h, gpio|g, i2c|i, spi|s, uart|u"); }
        return;
    }
    
    if(c=="pins"||c=="p") {
        if(v.size()==1) { showPins(); return; }
        if(v.size()==2 && v[1]=="check") { pinsCheck(); return; }
        if(v.size()==4 && (v[1]=="set"||v[1]=="s")) {
            int p=v[3].toInt();
            String name = v[2];
            name.toLowerCase();
#if WIRETAP_CARDPUTER_ADV
            if(name == "disp-sda" || name == "disp-scl") {
                println("ERROR: Cardputer display is built in; display pins are not configurable.");
                return;
            }
            if(!signalIsValidGpio(p)) {
                println("ERROR: GPIO" + String(p) + " is not on the Cardputer EXT header.");
                println("EXT GPIOs: 4,5,6,8,9,13,14,15,39,40. G3 reset is reserved.");
                return;
            }
#endif
            if(name=="sda") PIN_I2C_SDA=p;
            else if(name=="scl") PIN_I2C_SCL=p;
            else if(name=="mosi") PIN_SPI_MOSI=p;
            else if(name=="miso") PIN_SPI_MISO=p;
            else if(name=="sck") PIN_SPI_SCK=p;
            else if(name=="cs") PIN_SPI_CS=p;
            else if(name=="tx") PIN_UART_TX=p;
            else if(name=="rx") PIN_UART_RX=p;
            else if(name=="disp-sda") PIN_DISP_SDA=p;
            else if(name=="disp-scl") PIN_DISP_SCL=p;
            else {
                println("ERROR: Invalid pin name '" + v[2] + "'");
                println("Valid pins: sda, scl, mosi, miso, sck, cs, tx, rx, disp-sda, disp-scl");
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

    if(c=="signal" && v.size()>=2) {
        String cmd = v[1];
        cmd.toLowerCase();
        if(cmd=="freq" && v.size()>=3) {
            uint32_t windowMs = (v.size()>=4) ? (uint32_t)strtoul(v[3].c_str(), nullptr, 0) : 1000;
            signalFreqCmd(v[2].toInt(), windowMs);
            return;
        }
        if(cmd=="edges" && v.size()>=3) {
            uint32_t windowMs = (v.size()>=4) ? (uint32_t)strtoul(v[3].c_str(), nullptr, 0) : 1000;
            signalEdgesCmd(v[2].toInt(), windowMs);
            return;
        }
        if(cmd=="scope" && v.size()>=3) {
            uint16_t samples = (v.size()>=4) ? (uint16_t)v[3].toInt() : 64;
            uint32_t intervalUs = (v.size()>=5) ? (uint32_t)v[4].toInt() : 100;
            signalScopeCmd(v[2].toInt(), samples, intervalUs);
            return;
        }
        if(cmd=="adc" && v.size()>=3) {
            uint16_t samples = (v.size()>=4) ? (uint16_t)v[3].toInt() : 32;
            signalAdcCmd(v[2].toInt(), samples);
            return;
        }
        if(cmd=="pwmout" && v.size()>=5) {
            signalPwmOutCmd(v[2].toInt(), (uint32_t)strtoul(v[3].c_str(), nullptr, 0), (uint8_t)constrain(v[4].toInt(),0,100));
            return;
        }
        printError("Unknown signal subcommand: " + cmd);
        printInfo("signal freq|edges|scope|adc|pwmout");
        return;
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
            if(val >= 0) println("Pin "+v[2]+" = "+String(val));
            return;
        }
        if(cmd=="adc" && v.size()>=3) {
            gpioAdc(v[2].toInt());
            return;
        }
        if(cmd=="pwm" && v.size()>=5) {
            gpioPwm(v[2].toInt(), (uint32_t)v[3].toInt(), (uint8_t)constrain(v[4].toInt(),0,100));
            return;
        }
        if(cmd=="freq" && v.size()>=3) {
            gpioFreq(v[2].toInt());
            return;
        }
        if(cmd=="pulse" && v.size()>=3) {
            gpioPulse(v[2].toInt());
            return;
        }
        if(cmd=="scope" && v.size()>=3) {
            uint16_t samples   = (v.size()>=4) ? (uint16_t)v[3].toInt() : 64;
            uint32_t intervalUs = (v.size()>=5) ? (uint32_t)v[4].toInt() : 100;
            gpioScope(v[2].toInt(), samples, intervalUs);
            return;
        }
        printError("Unknown gpio subcommand: " + cmd);
        printInfo("gpio set|get|adc|pwm|freq|pulse|scope");
        return;
    }

    if(c=="config") {
        if(v.size()>=2) {
            String sub = v[1];
            sub.toLowerCase();
            if(sub=="save")  { configSave();  return; }
            if(sub=="load")  { configLoad();  return; }
            if(sub=="reset") { configReset(); return; }
        }
        println("Usage: config save|load|reset");
        return;
    }

    println("Unknown command: '" + c + "'");
    println("Type 'help' or 'h' for available commands.");

    // Suggest similar commands
    if(c.startsWith("i2")) println("Did you mean: i2c scan|read|write ?");
    else if(c.startsWith("sp")) println("Did you mean: spi x <hex> ?");
    else if(c.startsWith("ua")) println("Did you mean: uart tx|rx|baud ?");
    else if(c.startsWith("gp")) println("Did you mean: gpio set|get ?");
    else if(c.startsWith("si")) println("Did you mean: signal freq|edges|scope|adc|pwmout ?");
    else if(c.startsWith("mo")) println("Did you mean: mode hiz|gpio|i2c|spi|uart ?");
    else if(c.startsWith("pi")) println("Did you mean: pins [set <name> <pin>] ?");
}

void handleInputStream(Stream& s) {
    static uint8_t escState = 0;  // 0=normal, 1=got ESC, 2=got ESC[

    while(s.available()) {
        char ch = s.read();

        // ---- ANSI escape sequence handling ----
        if(escState == 1) {
            escState = (ch == '[') ? 2 : 0;
            continue;
        }
        if(escState == 2) {
            escState = 0;
            auto replaceInput = [&](const String& newBuf) {
                // Erase current input with backspaces then print new buf
                for(size_t i = 0; i < inbuf.length(); i++) { USB.print('\b'); USB.print(' '); USB.print('\b'); }
                inbuf = newBuf;
                USB.print(inbuf);
            };
            if(ch == 'A') {  // Up arrow — older history
                if(cmdHistoryPos < (int8_t)cmdHistoryCount - 1) cmdHistoryPos++;
                String h = historyGet(cmdHistoryPos);
                if(h.length() > 0) replaceInput(h);
            } else if(ch == 'B') {  // Down arrow — newer history
                cmdHistoryPos--;
                replaceInput(cmdHistoryPos >= 0 ? historyGet(cmdHistoryPos) : "");
            }
            continue;
        }
        if(ch == 27) { escState = 1; continue; }  // ESC

        // ---- Normal character handling ----
        if(ch == '\r') continue;

        if(ch == '\n') {
            String cmd = inbuf;
            inbuf = "";
            cmdHistoryPos = -1;
            USB.print('\n');
            if(cmd.length() > 0) {
                historyPush(cmd);
                handleCmd(cmd);
            }
            prompt();
        } else if(ch == 127 || ch == '\b') {  // Backspace / DEL
            if(inbuf.length() > 0) {
                inbuf.remove(inbuf.length() - 1);
                USB.print('\b'); USB.print(' '); USB.print('\b');
            }
        } else if(inbuf.length() < 256) {
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
#if !WIRETAP_CARDPUTER_ADV
    Wire.begin(PIN_DISP_SDA, PIN_DISP_SCL);
#endif
    USB.begin(115200);
    delay(2000); // Longer delay for stability
    
#if WIRETAP_CARDPUTER_ADV
    USB.println("\n=== WireTap-32 Cardputer EXT Bench ===");
#else
    USB.println("\n=== ESP32 Bus Pirate v3.0 (Serial-Only) ===");
#endif
    USB.println("Free heap: " + String(ESP.getFreeHeap()));

    // Initialize in safe order
    setHiZ();

    // Initialize display after I2C is set up
    displayInit();
    initButtons();
    initGpioMenuPins();
    mainMenuIndex = MENU_GPIO;
    gpioMenuIndex = 0;
    uiScreen = UI_MAIN_MENU;
    setScreenDirty();

    USB.println("Ready! Type 'help' for commands.");

    // Re-enable watchdog with longer timeout (commented out to avoid WDT errors)
    // enableCore0WDT();

    // Show initial status bar
    showStatusBarLine();
    displayUpdate();

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

    updateButtons();
    handleButtonPresses();
    
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
