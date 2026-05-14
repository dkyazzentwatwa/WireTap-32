#include "PinSafety.h"

static bool isFlashPin(int pin) {
#if WIRETAP_CARDPUTER_ADV
    return false;
#else
    return pin >= 6 && pin <= 11;
#endif
}

static bool isInputOnlyPin(int pin) {
#if WIRETAP_CARDPUTER_ADV
    return pin == 4 || pin == 6 || pin == 13 || pin == 39;
#else
    return pin >= 34 && pin <= 39;
#endif
}

static bool isStrapPin(int pin) {
#if WIRETAP_CARDPUTER_ADV
    return pin == 3;
#else
    return pin == 0 || pin == 2 || pin == 4 || pin == 5 || pin == 12 || pin == 15;
#endif
}

static bool isValidPin(int pin) {
#if WIRETAP_CARDPUTER_ADV
    switch(pin) {
        case 4:
        case 5:
        case 6:
        case 8:
        case 9:
        case 13:
        case 14:
        case 15:
        case 39:
        case 40:
            return true;
        default:
            return false;
    }
#else
    return pin >= 0 && pin <= 39;
#endif
}

static bool isReservedPin(int pin) {
#if WIRETAP_CARDPUTER_ADV
    return pin == 3;
#else
    return false;
#endif
}

static String pinFlags(int pin, bool outputRole) {
    String flags = "";
    if(!isValidPin(pin)) flags += " INVALID";
    if(isReservedPin(pin)) flags += " RESERVED";
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
#if WIRETAP_CARDPUTER_ADV
    out += "Cardputer EXT 2.54-14P: 3.3V GPIO only. 5VIN/5VOUT are power rails, not signal pins.\n";
    out += "Reserved: G3 reset. Input-preferred: G4 INT, G6 BUSY, G13 UART_RX, G39 SPI_MISO.\n\n";
#else
    out += "Bare ESP32 limit: 3.3V GPIO only. No 5V tolerance or input protection.\n\n";
#endif

#if !WIRETAP_CARDPUTER_ADV
    appendRole(out, "DISP SDA", cfg.dispSda, false);
    appendRole(out, "DISP SCL", cfg.dispScl, false);
#endif
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
#if !WIRETAP_CARDPUTER_ADV
    appendConflict(out, "OLED SDA", cfg.dispSda, "SPI CS", cfg.spiCs);
    appendConflict(out, "OLED SDA", cfg.dispSda, "I2C SDA", cfg.i2cSda);
    appendConflict(out, "OLED SCL", cfg.dispScl, "I2C SCL", cfg.i2cScl);
#endif
    appendConflict(out, "UART TX", cfg.uartTx, "UART RX", cfg.uartRx);
    appendConflict(out, "SPI MOSI", cfg.spiMosi, "SPI MISO", cfg.spiMiso);
    appendConflict(out, "SPI SCK", cfg.spiSck, "SPI CS", cfg.spiCs);
    if(out.length() == before) out += "  None detected\n";

#if !WIRETAP_CARDPUTER_ADV
    if(cfg.dispSda == cfg.spiCs) {
        out += "\nRecommendation: OLED SDA currently shares GPIO";
        out += String(cfg.dispSda);
        out += " with SPI CS. If the OLED is installed, move SPI CS with 'pins set cs <pin>' before SPI work.\n";
    }

    out += "\nNotes: GPIO 6-11 are connected to onboard flash. GPIO 34-39 are input-only. Strap pins can affect boot if pulled wrong.\n";
#else
    out += "\nCardputer default roles: SPI SCK=G40 MOSI=G14 MISO=G39 CS=G5, I2C SDA=G8 SCL=G9, UART TX=G15 RX=G13.\n";
    out += "Use level shifting for 5V targets and keep target grounds common.\n";
#endif
    return out;
}
