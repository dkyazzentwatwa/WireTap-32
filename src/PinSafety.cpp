#include "PinSafety.h"

static bool isFlashPin(int pin) {
    return pin >= 6 && pin <= 11;
}

static bool isInputOnlyPin(int pin) {
    return pin >= 34 && pin <= 39;
}

static bool isStrapPin(int pin) {
    return pin == 0 || pin == 2 || pin == 4 || pin == 5 || pin == 12 || pin == 15;
}

static bool isValidPin(int pin) {
    return pin >= 0 && pin <= 39;
}

static String pinFlags(int pin, bool outputRole) {
    String flags = "";
    if(!isValidPin(pin)) flags += " INVALID";
    if(isFlashPin(pin)) flags += " FLASH";
    if(isInputOnlyPin(pin)) flags += outputRole ? " INPUT_ONLY_CONFLICT" : " INPUT_ONLY";
    if(isStrapPin(pin)) flags += " STRAP";
    if(flags.length() == 0) flags = " OK";
    return flags;
}

static void appendRole(String& out, const char* role, int pin, bool outputRole) {
    out += "  ";
    out += role;
    out += " GPIO";
    out += String(pin);
    out += ":";
    out += pinFlags(pin, outputRole);
    out += "\n";
}

static void appendConflict(String& out, const char* a, int pinA, const char* b, int pinB) {
    if(pinA != pinB) return;
    out += "  CONFLICT: ";
    out += a;
    out += " and ";
    out += b;
    out += " both use GPIO";
    out += String(pinA);
    out += "\n";
}

String buildPinSafetyReport(const WireTapPinConfig& cfg) {
    String out = "=== Pin Safety Check ===\n";
    out += "Mode: ";
    out += cfg.modeName ? cfg.modeName : "Unknown";
    out += "\n";
    out += "Bare ESP32 limit: 3.3V GPIO only. No 5V tolerance or input protection.\n\n";

    appendRole(out, "DISP SDA", cfg.dispSda, false);
    appendRole(out, "DISP SCL", cfg.dispScl, false);
    appendRole(out, "I2C SDA ", cfg.i2cSda, true);
    appendRole(out, "I2C SCL ", cfg.i2cScl, true);
    appendRole(out, "SPI MOSI", cfg.spiMosi, true);
    appendRole(out, "SPI MISO", cfg.spiMiso, false);
    appendRole(out, "SPI SCK ", cfg.spiSck, true);
    appendRole(out, "SPI CS  ", cfg.spiCs, true);
    appendRole(out, "UART TX ", cfg.uartTx, true);
    appendRole(out, "UART RX ", cfg.uartRx, false);

    out += "\nConflicts:\n";
    size_t before = out.length();
    appendConflict(out, "OLED SDA", cfg.dispSda, "SPI CS", cfg.spiCs);
    appendConflict(out, "OLED SDA", cfg.dispSda, "I2C SDA", cfg.i2cSda);
    appendConflict(out, "OLED SCL", cfg.dispScl, "I2C SCL", cfg.i2cScl);
    appendConflict(out, "UART TX", cfg.uartTx, "UART RX", cfg.uartRx);
    appendConflict(out, "SPI MOSI", cfg.spiMosi, "SPI MISO", cfg.spiMiso);
    appendConflict(out, "SPI SCK", cfg.spiSck, "SPI CS", cfg.spiCs);
    if(out.length() == before) out += "  None detected\n";

    if(cfg.dispSda == cfg.spiCs) {
        out += "\nRecommendation: OLED SDA currently shares GPIO";
        out += String(cfg.dispSda);
        out += " with SPI CS. If the OLED is installed, move SPI CS with 'pins set cs <pin>' before SPI work.\n";
    }

    out += "\nNotes: GPIO 6-11 are connected to onboard flash. GPIO 34-39 are input-only. Strap pins can affect boot if pulled wrong.\n";
    return out;
}
